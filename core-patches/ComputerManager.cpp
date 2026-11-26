/*
 * ComputerManager.cpp - maintains and provides a computer object list
 *
 * Copyright (c) 2017-2026 Tobias Junghans <tobydox@veyon.io>
 *
 * This file is part of Veyon - https://veyon.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QMessageBox>
#include <QTime>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QThreadPool>
#include <QSet>
#include <QNetworkInterface>
#include <QJsonArray>
#include <QJsonObject>

#include "BuiltinFeatures.h"
#include "ComputerManager.h"
#include "VeyonConfiguration.h"
#include "MonitoringMode.h"
#include "NetworkObject.h"
#include "NetworkObjectDirectory.h"
#include "NetworkObjectDirectoryManager.h"
#include "NetworkObjectFilterProxyModel.h"
#include "NetworkObjectOverlayDataModel.h"
#include "NetworkObjectTreeModel.h"
#include "PlatformFilesystemFunctions.h"
#include "UserConfig.h"
#include "ObjectManager.h"


ComputerManager::ComputerManager( UserConfig& config, QObject* parent ) :
	QObject( parent ),
	m_config( config ),
	m_networkObjectDirectory( VeyonCore::networkObjectDirectoryManager().configuredDirectory() ),
	m_networkObjectModel( new NetworkObjectTreeModel( m_networkObjectDirectory, this ) ),
	m_networkObjectOverlayDataModel(new NetworkObjectOverlayDataModel({tr("User"), tr("Logged in since")}, this)),
	m_computerTreeModel( new CheckableItemProxyModel( NetworkObjectModel::UidRole, this ) ),
	m_networkObjectFilterProxyModel( new NetworkObjectFilterProxyModel( this ) ),
	m_localHostNames( QHostInfo::localHostName().toLower() ),
	m_localHostAddresses(QHostInfo::fromName(QHostInfo::localHostName()).addresses()),
	m_guestUserProperty(VeyonCore::config().guestUserProperty()),
	m_guestUserValue(VeyonCore::config().guestUserValue())
{
	if( m_networkObjectDirectory == nullptr )
	{
		QMessageBox::critical(nullptr,
							  tr("Missing network object directory plugin"),
							  tr("No default network object directory plugin was found. "
								 "Please check your installation or configure a different "
								 "network object directory backend via DigiAgent Configurator."));
		qFatal( "ComputerManager: missing network object directory plugin!" );
	}

	if( QHostInfo::localDomainName().isEmpty() == false )
	{
		m_localHostNames.append( QHostInfo::localHostName().toLower() +
								 QStringLiteral( "." ) +
								 QHostInfo::localDomainName().toLower() );
	}

	initNetworkObjectLayer();
	initLocations();
	initComputerTreeModel();
	initAutoDiscovery();
}



ComputerManager::~ComputerManager()
{
	m_config.setCheckedNetworkObjects( m_computerTreeModel->saveStates() );
}



void ComputerManager::addLocation( const QString& location )
{
	m_locationFilterList.append( location );

	updateLocationFilterList();
}



void ComputerManager::removeLocation( const QString& location )
{
	if( m_currentLocations.contains( location ) == false )
	{
		m_locationFilterList.removeAll( location );

		updateLocationFilterList();
	}
}



bool ComputerManager::saveComputerAndUsersList( const QString& fileName )
{
	QStringList lines( tr( "Computer name;Hostname;User" ) );

	const auto computers = selectedComputers( QModelIndex() );

	for( const auto& computer : computers )
	{
		const auto networkObjectIndex = findNetworkObject( computer.networkObjectUid() );
		if( networkObjectIndex.isValid() )
		{
			// fetch user
			const auto user = m_networkObjectOverlayDataModel->data( mapToUserNameModelIndex( networkObjectIndex ) ).toString();
			// create new line with computer and user
			lines += computer.displayName() + QLatin1Char(';') + computer.hostName() + QLatin1Char(';') + user; // clazy:exclude=reserve-candidates
		}
	}

	// append empty string to generate final newline at end of file
	lines += QString();

	QFile outputFile( fileName );
	if( VeyonCore::platform().filesystemFunctions().openFileSafely(
			&outputFile,
			QFile::WriteOnly | QFile::Truncate,
			QFile::ReadOwner | QFile::WriteOwner ) == false )
	{
		return false;
	}

	outputFile.write( lines.join( QStringLiteral("\r\n") ).toUtf8() );

	return true;
}



void ComputerManager::updateUser(const ComputerControlInterface::Pointer& controlInterface) const
{
	const auto networkObjectIndex = findNetworkObject( controlInterface->computer().networkObjectUid() );

	if( networkObjectIndex.isValid() )
	{
		auto user = controlInterface->userFullName();
		if( user.isEmpty() )
		{
			user = VeyonCore::stripDomain(controlInterface->userLoginName());
		}
		m_networkObjectOverlayDataModel->setData(mapToUserNameModelIndex(networkObjectIndex), user);

		if (controlInterface->computerNameSource() == Computer::NameSource::UserFullName ||
			controlInterface->computerNameSource() == Computer::NameSource::UserLoginName)
		{
			const auto computerName = controlInterface->computerName();
			if (computerName.isEmpty() == false)
			{
				m_networkObjectOverlayDataModel->setData(m_networkObjectOverlayDataModel->mapFromSource(networkObjectIndex),
														 computerName);
			}
		}

		if ((m_guestUserProperty == PlatformUserFunctions::UserProperty::LoginName &&
			 controlInterface->userLoginName() == m_guestUserValue)
			||
			(m_guestUserProperty == PlatformUserFunctions::UserProperty::FullName &&
						 controlInterface->userFullName() == m_guestUserValue))
		{
			VeyonCore::builtinFeatures().monitoringMode().identifyUser({controlInterface});
		}
	}
}



void ComputerManager::updateSessionInfo(const ComputerControlInterface::Pointer& controlInterface) const
{
	const auto networkObjectIndex = findNetworkObject(controlInterface->computer().networkObjectUid());

	if (networkObjectIndex.isValid())
	{
		const auto uptime24h = controlInterface->sessionInfo().uptime % (60*60*24);
		const auto uptimeDays = controlInterface->sessionInfo().uptime / (60*60*24);
		const QString uptimeString = (uptimeDays > 0 ?
										  ((uptimeDays > 1 ? tr("%1 days").arg(uptimeDays) : tr("1 day")) + QStringLiteral(", ")) :
										  QString()) +
									 QTime::fromMSecsSinceStartOfDay(uptime24h * 1000).toString(QStringLiteral("hh:mm:ss"));
		m_networkObjectOverlayDataModel->setData(mapToSessionUptimeModelIndex(networkObjectIndex),
												  uptimeString,
												  Qt::DisplayRole);

		switch (controlInterface->computerNameSource())
		{
		case Computer::NameSource::HostAddress:
		case Computer::NameSource::SessionClientName:
		case Computer::NameSource::SessionClientAddress:
		case Computer::NameSource::SessionHostName:
		case Computer::NameSource::SessionMetaData:
		{
			const auto computerName = controlInterface->computerName();
			if (computerName.isEmpty() == false)
			{
				m_networkObjectOverlayDataModel->setData(m_networkObjectOverlayDataModel->mapFromSource(networkObjectIndex),
														 computerName);
			}
			break;
		}
		default:
			break;
		}
	}
}



void ComputerManager::clearOverlayModelData(const ComputerControlInterface::Pointer& controlInterface) const
{
	const auto networkObjectIndex = findNetworkObject(controlInterface->computer().networkObjectUid());

	if (networkObjectIndex.isValid())
	{
		m_networkObjectOverlayDataModel->setData(mapToUserNameModelIndex(networkObjectIndex), {});
		m_networkObjectOverlayDataModel->setData(mapToSessionUptimeModelIndex(networkObjectIndex), {});
		m_networkObjectOverlayDataModel->setData(m_networkObjectOverlayDataModel->mapFromSource(networkObjectIndex), {});
	}
}



void ComputerManager::checkChangedData( const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles )
{
	Q_UNUSED(topLeft);
	Q_UNUSED(bottomRight);

	if( roles.contains( Qt::CheckStateRole ) )
	{
		Q_EMIT computerSelectionChanged();
	}
}



void ComputerManager::initLocations()
{
	for( const auto& hostName : std::as_const( m_localHostNames ) )
	{
		vDebug() << "initializing locations for hostname" << hostName;
	}

	for( const auto& address : std::as_const( m_localHostAddresses ) )
	{
		vDebug() << "initializing locations for host address" << address.toString();
	}

	const auto currentLocation = findLocationOfComputer(m_localHostNames, m_localHostAddresses, {});
	if (currentLocation.isEmpty() == false)
	{
		m_currentLocations.append(currentLocation);
	}

	vDebug() << "found locations" << m_currentLocations;

	if( VeyonCore::config().showCurrentLocationOnly() )
	{
		if( m_currentLocations.isEmpty() )
		{
			QMessageBox::warning(nullptr,
								 tr("Location detection failed"),
								 tr("The location of this computer could not be determined. "
									"This indicates a problem with the system configuration. "
									"No locations and computers will be displayed in the \"Locations & computers\" panel."));
			vWarning() << "location detection failed";
			m_currentLocations = QStringList{tr("Unknown location")};
		}

		m_locationFilterList = m_currentLocations;
		updateLocationFilterList();
	}
}



void ComputerManager::initNetworkObjectLayer()
{
	m_networkObjectDirectory->update();
	m_networkObjectDirectory->setUpdateInterval( VeyonCore::config().networkObjectDirectoryUpdateInterval() );
	m_networkObjectOverlayDataModel->setSourceModel( m_networkObjectModel );
	m_networkObjectFilterProxyModel->setSourceModel( m_networkObjectOverlayDataModel );
	m_computerTreeModel->setException( NetworkObjectModel::TypeRole, QVariant::fromValue( NetworkObject::Type::Label ) );
	m_computerTreeModel->setSourceModel( m_networkObjectFilterProxyModel );

	const auto hideOwnSession = VeyonCore::config().hideOwnSession();

	if( hideOwnSession )
	{
		QStringList localHostNames( {
										QStringLiteral("localhost"),
										QHostAddress( QHostAddress::LocalHost ).toString(),
										QHostAddress( QHostAddress::LocalHostIPv6 ).toString()
									} );

		localHostNames.append( m_localHostNames );

		for( const auto& address : std::as_const( m_localHostAddresses ) )
		{
			localHostNames.append( address.toString() ); // clazy:exclude=reserve-candidates
		}

		QStringList ownSessionNames;
		ownSessionNames.reserve( localHostNames.size() );

		const auto sessionServerPort = QString::number( VeyonCore::config().veyonServerPort() +
														 VeyonCore::sessionId() );

		for( const auto& localHostName : std::as_const(localHostNames) )
		{
			ownSessionNames.append( QStringLiteral("%1:%2").arg( localHostName, sessionServerPort ) );
		}

		m_networkObjectFilterProxyModel->setComputerExcludeFilter( ownSessionNames );
	}

	m_networkObjectFilterProxyModel->setEmptyGroupsExcluded( VeyonCore::config().hideEmptyLocations() );
}



void ComputerManager::initComputerTreeModel()
{
	QJsonArray checkedNetworkObjects;
	if( VeyonCore::config().autoSelectCurrentLocation() )
	{
		for( const auto& location : std::as_const( m_currentLocations ) )
		{
			const auto computersAtLocation = getComputersAtLocation( location );
			for( const auto& computer : computersAtLocation )
			{
				checkedNetworkObjects += computer.networkObjectUid().toString();
			}
		}
	}
	else
	{
		checkedNetworkObjects = m_config.checkedNetworkObjects();
	}

	m_computerTreeModel->loadStates( checkedNetworkObjects );

	connect( computerTreeModel(), &QAbstractItemModel::modelReset,
			 this, &ComputerManager::computerSelectionReset );
	connect( computerTreeModel(), &QAbstractItemModel::layoutChanged,
			 this, &ComputerManager::computerSelectionReset );

	connect( computerTreeModel(), &QAbstractItemModel::dataChanged,
			 this, &ComputerManager::checkChangedData );
	connect( computerTreeModel(), &QAbstractItemModel::rowsInserted,
			 this, &ComputerManager::computerSelectionChanged );
	connect( computerTreeModel(), &QAbstractItemModel::rowsRemoved,
			 this, &ComputerManager::computerSelectionChanged );
}



void ComputerManager::updateLocationFilterList()
{
	if( VeyonCore::config().showCurrentLocationOnly() )
	{
		m_networkObjectFilterProxyModel->setGroupFilter( m_locationFilterList );
	}
}



QString ComputerManager::findLocationOfComputer(const QStringList& hostNames, const QList<QHostAddress>& hostAddresses,
												const QModelIndex& parent) const
{
	QAbstractItemModel* model = networkObjectModel();

	int rows = model->rowCount( parent );

	for( int i = 0; i < rows; ++i )
	{
		const auto entryIndex = model->index(i, 0, parent);
		const auto objectType = NetworkObject::Type(model->data(entryIndex, NetworkObjectModel::TypeRole).toInt());

		if( NetworkObject::isContainer(objectType) )
		{
			if (model->canFetchMore(entryIndex))
			{
				model->fetchMore(entryIndex);
			}

			const auto location = findLocationOfComputer( hostNames, hostAddresses, entryIndex );
			if( location.isEmpty() == false )
			{
				return location;
			}
		}
		else if( objectType == NetworkObject::Type::Host )
		{
			QString currentHost = model->data( entryIndex, NetworkObjectModel::HostAddressRole ).toString().toLower();
			QHostAddress currentHostAddress;

			if( hostNames.contains( currentHost ) ||
					( currentHostAddress.setAddress( currentHost ) && hostAddresses.contains( currentHostAddress ) ) )
			{
				return model->data( parent, NetworkObjectModel::NameRole ).toString();
			}
		}
	}

	return {};
}



ComputerList ComputerManager::getComputersAtLocation(const QString& locationName, const QModelIndex& parent, bool parentMatches) const
{
	QAbstractItemModel* model = computerTreeModel();

	int rows = model->rowCount( parent );

	ComputerList computers;
	computers.reserve( rows );

	for( int i = 0; i < rows; ++i )
	{
		const auto entryIndex = model->index(i, 0, parent);
		const auto objectType = NetworkObject::Type(model->data(entryIndex, NetworkObjectModel::TypeRole).toInt());
		const auto objectName = model->data(entryIndex, NetworkObjectModel::NameRole).toString();
		if( NetworkObject::isContainer(objectType) )
		{
			const auto currentLocationMatches = objectName == locationName;
			if (parentMatches || currentLocationMatches || hasSubLocations(entryIndex))
			{
				computers += getComputersAtLocation(locationName, entryIndex, parentMatches || currentLocationMatches);
			}
		}
		else if( objectType == NetworkObject::Type::Host )
		{
			if (parentMatches)
			{
				computers += Computer(model->data(entryIndex, NetworkObjectModel::UidRole).toUuid(),
									  objectName,
									  model->data(entryIndex, NetworkObjectModel::HostAddressRole).toString(),
									  model->data(entryIndex, NetworkObjectModel::MacAddressRole).toString());
			}
		}
	}

	return computers;
}



bool ComputerManager::hasSubLocations(const QModelIndex& index) const
{
	const auto model = computerTreeModel();
	const auto rows = model->rowCount(index);

	for (int i = 0; i < rows; ++i)
	{
		const auto objectType = NetworkObject::Type(model->data(model->index(i, 0, index),
																NetworkObjectModel::TypeRole).toInt());

		if (objectType == NetworkObject::Type::Location || objectType == NetworkObject::Type::DesktopGroup)
		{
			return true;
		}
	}

	return false;
}



ComputerList ComputerManager::selectedComputers(const QModelIndex& parent) const
{
	QAbstractItemModel* model = computerTreeModel();

	int rows = model->rowCount( parent );

	ComputerList computers;

	for( int i = 0; i < rows; ++i )
	{
		QModelIndex entryIndex = model->index( i, 0, parent );

		if( model->data( entryIndex, NetworkObjectModel::CheckStateRole ).value<Qt::CheckState>() == Qt::Unchecked )
		{
			continue;
		}

		const auto objectType = NetworkObject::Type( model->data(entryIndex, NetworkObjectModel::TypeRole).toInt() );

		if( NetworkObject::isContainer(objectType) )
		{
			computers += selectedComputers( entryIndex );
		}
		else if( objectType == NetworkObject::Type::Host )
		{
			computers += Computer( model->data( entryIndex, NetworkObjectModel::UidRole ).toUuid(),
								   model->data( entryIndex, NetworkObjectModel::NameRole ).toString(),
								   model->data( entryIndex, NetworkObjectModel::HostAddressRole ).toString(),
								   model->data( entryIndex, NetworkObjectModel::MacAddressRole ).toString(),
								   model->data( parent, NetworkObjectModel::NameRole ).toString() );
		}
	}

	return computers;
}



QModelIndex ComputerManager::findNetworkObject(NetworkObject::Uid networkObjectUid, const QModelIndex& parent) const
{
	QAbstractItemModel* model = networkObjectModel();

	int rows = model->rowCount( parent );

	for( int i = 0; i < rows; ++i )
	{
		QModelIndex entryIndex = model->index( i, 0, parent );

		const auto objectType = NetworkObject::Type( model->data(entryIndex, NetworkObjectModel::TypeRole).toInt() );

		if( NetworkObject::isContainer(objectType) )
		{
			QModelIndex index = findNetworkObject( networkObjectUid, entryIndex );
			if( index.isValid() )
			{
				return index;
			}
		}
		else if( objectType == NetworkObject::Type::Host )
		{
			if( model->data( entryIndex, NetworkObjectModel::UidRole ).toUuid() == networkObjectUid )
			{
				return entryIndex;
			}
		}
	}

	return {};
}



QModelIndex ComputerManager::mapToUserNameModelIndex(const QModelIndex& networkObjectIndex) const
{
	// map arbitrary index from m_networkObjectModel to username column in m_networkObjectOverlayDataModel
	const auto parent = m_networkObjectOverlayDataModel->mapFromSource( networkObjectIndex.parent() );

	return m_networkObjectOverlayDataModel->index( networkObjectIndex.row(), OverlayDataUsernameColumn, parent );
}



QModelIndex ComputerManager::mapToSessionUptimeModelIndex(const QModelIndex& networkObjectIndex) const
{
	// map arbitrary index from m_networkObjectModel to username column in m_networkObjectOverlayDataModel
	const auto parent = m_networkObjectOverlayDataModel->mapFromSource(networkObjectIndex.parent());

	return m_networkObjectOverlayDataModel->index(networkObjectIndex.row(), OverlayDataSessionUptimeColumn, parent);
}



void ComputerManager::initAutoDiscovery()
{
	m_discoverySocket = new QUdpSocket( this );
	m_discoverySocket->bind( QHostAddress::AnyIPv4, 11400, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint );
	connect( m_discoverySocket, &QUdpSocket::readyRead, this, &ComputerManager::processDiscoveryDatagrams );

	m_discoveryProbeTimer = new QTimer( this );
	connect( m_discoveryProbeTimer, &QTimer::timeout, this, &ComputerManager::sendDiscoveryProbe );
	m_discoveryProbeTimer->start( 15000 ); // probe every 15 seconds

	// Auto-register local admin machine in directory and monitoring
	const QString localName = QHostInfo::localHostName();
	QString localIp;
	for( const auto& addr : std::as_const( m_localHostAddresses ) )
	{
		if( addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback() && !addr.toString().startsWith( QStringLiteral("169.254.") ) )
		{
			localIp = addr.toString();
			break;
		}
	}
	if( localIp.isEmpty() )
	{
		localIp = QStringLiteral("127.0.0.1");
	}
	registerDiscoveredComputer( localName, localIp, QString{} );

	QTimer::singleShot( 1000, this, &ComputerManager::sendDiscoveryProbe );
}



void ComputerManager::sendDiscoveryProbe()
{
	if( !m_discoverySocket )
	{
		return;
	}

	const QByteArray probe = QByteArrayLiteral( "SYNAPSE_DISCOVER" );
	m_discoverySocket->writeDatagram( probe, QHostAddress::Broadcast, 11400 );

	const auto interfaces = QNetworkInterface::allInterfaces();
	for( const auto& iface : interfaces )
	{
		if( !( iface.flags() & QNetworkInterface::IsLoopBack ) && ( iface.flags() & QNetworkInterface::IsUp ) )
		{
			for( const auto& entry : iface.addressEntries() )
			{
				if( !entry.broadcast().isNull() )
				{
					m_discoverySocket->writeDatagram( probe, entry.broadcast(), 11400 );
				}

				if( entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.ip().isLoopback() )
				{
					const quint32 ip = entry.ip().toIPv4Address();
					const quint32 mask = entry.netmask().toIPv4Address();
					if( ( mask & 0xFFFFFF00 ) == 0xFFFFFF00 )
					{
						const quint32 base = ip & mask;
						for( quint32 host = 1; host < 255; ++host )
						{
							const quint32 targetIp = base | host;
							if( targetIp != ip )
							{
								m_discoverySocket->writeDatagram( probe, QHostAddress( targetIp ), 11400 );
							}
						}
					}
				}
			}
		}
	}

	// Also actively sweep subnet for TCP port 11100 (Synapse / Veyon agent service)
	scanSubnetForNewClients();
}



void ComputerManager::scanSubnetForNewClients()
{
	if( m_isScanningSubnet )
	{
		return;
	}
	m_isScanningSubnet = true;

	const auto rawObjects = VeyonCore::config().value( QStringLiteral( "NetworkObjects" ), QStringLiteral( "BuiltinDirectory" ), QJsonArray() ).toJsonArray();
	QSet<QString> knownIps;
	for( const auto& val : rawObjects )
	{
		const NetworkObject obj( val.toObject() );
		if( obj.type() == NetworkObject::Type::Host && !obj.hostAddress().trimmed().isEmpty() )
		{
			knownIps.insert( obj.hostAddress().trimmed() );
		}
	}

	QString baseSubnet = QStringLiteral( "192.168.1." );
	for( const auto& addr : std::as_const( m_localHostAddresses ) )
	{
		if( addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback() )
		{
			const QString s = addr.toString();
			const int lastDot = s.lastIndexOf( QLatin1Char( '.' ) );
			if( lastDot > 0 )
			{
				baseSubnet = s.left( lastDot + 1 );
				break;
			}
		}
	}

	const int startHost = 2;
	const int endHost = 254;
	const int numTasks = 4;
	const int chunkSize = ( endHost - startHost + 1 ) / numTasks;

	auto* activeTasksCount = new QAtomicInt( numTasks );

	for( int t = 0; t < numTasks; ++t )
	{
		const int chunkStart = startHost + t * chunkSize;
		const int chunkEnd = ( t == numTasks - 1 ) ? endHost : ( chunkStart + chunkSize - 1 );

		QThreadPool::globalInstance()->start( [this, knownIps, baseSubnet, chunkStart, chunkEnd, activeTasksCount]() {
			for( int h = chunkStart; h <= chunkEnd; ++h )
			{
				const QString targetIp = baseSubnet + QString::number( h );
				if( knownIps.contains( targetIp ) )
				{
					continue;
				}

				QTcpSocket sock;
				sock.connectToHost( targetIp, 11100 );
				if( sock.waitForConnected( 60 ) )
				{
					sock.disconnectFromHost();

					QString hostName = QHostInfo::fromName( targetIp ).hostName();
					if( hostName.isEmpty() || hostName == targetIp )
					{
						hostName = QStringLiteral( "BYOD-%1" ).arg( h );
					}

					QMetaObject::invokeMethod( this, [this, hostName, targetIp]() {
						registerDiscoveredComputer( hostName, targetIp, QString{} );
					}, Qt::QueuedConnection );
				}
			}

			if( activeTasksCount->deref() == 0 )
			{
				delete activeTasksCount;
				QMetaObject::invokeMethod( this, [this]() {
					m_isScanningSubnet = false;
				}, Qt::QueuedConnection );
			}
		} );
	}
}



void ComputerManager::processDiscoveryDatagrams()
{
	while( m_discoverySocket && m_discoverySocket->hasPendingDatagrams() )
	{
		QByteArray datagram;
		datagram.resize( m_discoverySocket->pendingDatagramSize() );
		QHostAddress sender;
		quint16 senderPort = 0;
		m_discoverySocket->readDatagram( datagram.data(), datagram.size(), &sender, &senderPort );

		if( datagram.startsWith( "SYNAPSE_BEACON|" ) )
		{
			const auto parts = QString::fromUtf8( datagram ).split( QLatin1Char( '|' ) );
			if( parts.size() >= 5 )
			{
				const QString hostName = parts.value( 2 );
				QString ipAddress = parts.value( 3 );
				if( ipAddress.isEmpty() || ipAddress == QStringLiteral( "127.0.0.1" ) || ipAddress.startsWith( QStringLiteral( "169.254." ) ) )
				{
					if( sender.protocol() == QAbstractSocket::IPv6Protocol )
					{
						bool ok = false;
						const auto v4 = sender.toIPv4Address( &ok );
						if( ok )
						{
							ipAddress = QHostAddress( v4 ).toString();
						}
						else
						{
							ipAddress = sender.toString();
						}
					}
					else
					{
						ipAddress = sender.toString();
					}
				}
				if( ipAddress.startsWith( QStringLiteral( "::ffff:" ) ) )
				{
					ipAddress.remove( 0, 7 );
				}
				const QString macAddress = parts.value( 4 );

				registerDiscoveredComputer( hostName, ipAddress, macAddress );
			}
		}
	}
}



void ComputerManager::registerDiscoveredComputer( const QString& hostName, const QString& ipAddress, const QString& macAddress )
{
	if( ipAddress.isEmpty() )
	{
		return;
	}

	const auto rawObjects = VeyonCore::config().value( QStringLiteral( "NetworkObjects" ), QStringLiteral( "BuiltinDirectory" ), QJsonArray() ).toJsonArray();
	ObjectManager<NetworkObject> mgr( rawObjects );

	bool isLabIp = false;
	const QStringList ipParts = ipAddress.split( QLatin1Char( '.' ) );
	if( ipParts.size() == 4 )
	{
		const int octet = ipParts.at( 3 ).toInt();
		if( octet >= 101 && octet <= 140 )
		{
			isLabIp = true;
		}
	}

	const QString targetLocationName = isLabIp ? QStringLiteral( "DIGIHUB" ) : QStringLiteral( "BYOD" );
	NetworkObject::Uid targetLocationUid;
	NetworkObject::Uid fallbackLocationUid;
	NetworkObject::Uid existingHostUid;
	bool alreadyRegistered = false;

	for( const auto& val : mgr.objects() )
	{
		const NetworkObject obj( val.toObject() );
		if( obj.type() == NetworkObject::Type::Location )
		{
			if( obj.name().compare( targetLocationName, Qt::CaseInsensitive ) == 0 )
			{
				targetLocationUid = obj.uid();
			}
			if( fallbackLocationUid.isNull() )
			{
				fallbackLocationUid = obj.uid();
			}
		}
		else if( obj.type() == NetworkObject::Type::Host )
		{
			if( obj.hostAddress().trimmed().compare( ipAddress.trimmed(), Qt::CaseInsensitive ) == 0 ||
				( !hostName.isEmpty() && obj.name().trimmed().compare( hostName.trimmed(), Qt::CaseInsensitive ) == 0 ) )
			{
				alreadyRegistered = true;
				existingHostUid = obj.uid();
				break;
			}
		}
	}

	if( alreadyRegistered )
	{
		// Already registered: never call loadStates() here, as it resets the tree model
		// and forces ComputerControlListModel::reload() on all active VNC connections.
		return;
	}

	if( targetLocationUid.isNull() )
	{
		if( !fallbackLocationUid.isNull() && !isLabIp )
		{
			const NetworkObject newLoc( NetworkObject::Type::Location, targetLocationName, {}, {}, {}, QUuid::createUuid(), QUuid() );
			targetLocationUid = newLoc.uid();
			mgr.add( newLoc );
		}
		else if( !fallbackLocationUid.isNull() )
		{
			targetLocationUid = fallbackLocationUid;
		}
		else
		{
			const NetworkObject newLoc( NetworkObject::Type::Location, targetLocationName, {}, {}, {}, QUuid::createUuid(), QUuid() );
			targetLocationUid = newLoc.uid();
			mgr.add( newLoc );
		}
	}

	const NetworkObject newHost( NetworkObject::Type::Host,
								 hostName.isEmpty() ? ipAddress : hostName,
								 ipAddress,
								 macAddress,
								 targetLocationName,
								 QUuid::createUuid(),
								 targetLocationUid );
	mgr.add( newHost );

	VeyonCore::config().setValue( QStringLiteral( "NetworkObjects" ), mgr.objects(), QStringLiteral( "BuiltinDirectory" ) );
	VeyonCore::config().flushStore();

	vWarning() << "Synapse Auto-Discovery: Dynamically registered new client" << hostName << ipAddress << "in location" << targetLocationName;

	m_networkObjectDirectory->update();

	// Check the newly added host incrementally without calling loadStates() (avoids full grid reload)
	const auto matches = m_computerTreeModel->match(
		m_computerTreeModel->index( 0, 0 ),
		NetworkObjectModel::UidRole,
		newHost.uid(),
		1,
		Qt::MatchExactly | Qt::MatchRecursive );
	if( !matches.isEmpty() )
	{
		m_computerTreeModel->setData( matches.first(), Qt::Checked, Qt::CheckStateRole );
	}

	Q_EMIT computerSelectionChanged();
}
