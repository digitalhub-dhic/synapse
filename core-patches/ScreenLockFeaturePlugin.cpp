/*
 * ScreenLockFeaturePlugin.cpp - implementation of ScreenLockFeaturePlugin class
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
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>

#include "ScreenLockFeaturePlugin.h"
#include "ComputerControlInterface.h"
#include "FeatureWorkerManager.h"
#include "LockWidget.h"
#include "PlatformCoreFunctions.h"
#include "PlatformFilesystemFunctions.h"
#include "PlatformInputDeviceFunctions.h"
#include "PlatformSessionFunctions.h"
#include "VeyonMasterInterface.h"
#include "VeyonServerInterface.h"

namespace
{

QStringList persistentLockFilePaths()
{
	QStringList paths;
	const auto dir = VeyonCore::platform().filesystemFunctions().globalAppDataPath();
	if( !dir.isEmpty() )
	{
		QDir().mkpath( dir );
		paths.append( dir + QStringLiteral("/persistent_lock.json") );
	}
#ifdef Q_OS_WIN
	paths.append( QStringLiteral("C:/ProgramData/DigiAgent/persistent_lock.json") );
	paths.append( QStringLiteral("C:/ProgramData/Veyon/persistent_lock.json") );
#endif
	paths.removeDuplicates();
	return paths;
}

void savePersistentLockToRegistry( bool locked, const Feature::Uid& uid, const QString& text )
{
#ifdef Q_OS_WIN
	for( const auto& company : { QStringLiteral("Veyon Solutions"), QStringLiteral("DigiAgent") } )
	{
		QSettings settings( QSettings::Registry64Format, QSettings::SystemScope,
							company, QStringLiteral("Veyon") );
		settings.beginGroup( QStringLiteral("PersistentLock") );
		settings.setValue( QStringLiteral("IsLocked"), locked );
		if( locked )
		{
			settings.setValue( QStringLiteral("FeatureUid"), uid.toString() );
			settings.setValue( QStringLiteral("LockText"), text );
			settings.setValue( QStringLiteral("Timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate) );
		}
		else
		{
			settings.remove( QStringLiteral("FeatureUid") );
			settings.remove( QStringLiteral("LockText") );
			settings.remove( QStringLiteral("Timestamp") );
		}
		settings.endGroup();
		settings.sync();
	}
#else
	Q_UNUSED(locked);
	Q_UNUSED(uid);
	Q_UNUSED(text);
#endif
}

struct PersistentLockInfo
{
	bool isLocked{ false };
	Feature::Uid featureUid;
	QString lockText;
	int duration{ 0 };
};

PersistentLockInfo loadPersistentLock()
{
	PersistentLockInfo info;

	const auto paths = persistentLockFilePaths();
	for( const auto& filePath : paths )
	{
		QFile file( filePath );
		if( file.open( QIODevice::ReadOnly ) )
		{
			const auto doc = QJsonDocument::fromJson( file.readAll() );
			if( doc.isObject() )
			{
				const auto obj = doc.object();
				if( obj.value( QStringLiteral("isLocked") ).toBool() )
				{
					info.isLocked = true;
					if( info.featureUid.isNull() )
					{
						info.featureUid = Feature::Uid( obj.value( QStringLiteral("featureUid") ).toString() );
					}
					if( info.lockText.isEmpty() )
					{
						info.lockText = obj.value( QStringLiteral("lockText") ).toString();
					}
					info.duration = obj.value( QStringLiteral("duration") ).toInt( 0 );
					file.close();
					break;
				}
			}
			file.close();
		}
	}

#ifdef Q_OS_WIN
	for( const auto& company : { QStringLiteral("Veyon Solutions"), QStringLiteral("DigiAgent") } )
	{
		QSettings settings( QSettings::Registry64Format, QSettings::SystemScope,
							company, QStringLiteral("Veyon") );
		settings.beginGroup( QStringLiteral("PersistentLock") );
		if( settings.contains( QStringLiteral("IsLocked") ) )
		{
			const bool regLocked = settings.value( QStringLiteral("IsLocked") ).toBool();
			if( regLocked )
			{
				info.isLocked = true;
				if( info.featureUid.isNull() )
				{
					info.featureUid = Feature::Uid( settings.value( QStringLiteral("FeatureUid") ).toString() );
				}
				if( info.lockText.isEmpty() )
				{
					info.lockText = settings.value( QStringLiteral("LockText") ).toString();
				}
				info.duration = settings.value( QStringLiteral("Duration"), 0 ).toInt();
			}
		}
		settings.endGroup();
	}
#endif

	return info;
}

void savePersistentLock( bool locked, const Feature::Uid& uid = {}, const QString& text = {}, int duration = 0 )
{
	savePersistentLockToRegistry( locked, uid, text );

	const auto paths = persistentLockFilePaths();
	for( const auto& filePath : paths )
	{
		QFileInfo fi( filePath );
		QDir().mkpath( fi.absolutePath() );

		QFile file( filePath );
		if( locked )
		{
			if( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
			{
				QJsonObject obj;
				obj.insert( QStringLiteral("isLocked"), true );
				obj.insert( QStringLiteral("featureUid"), uid.toString() );
				obj.insert( QStringLiteral("lockText"), text );
				if( duration > 0 )
				{
					obj.insert( QStringLiteral("duration"), duration );
				}
				obj.insert( QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate) );
				file.write( QJsonDocument( obj ).toJson() );
				file.close();
			}
		}
		else
		{
			if( file.exists() )
			{
				if( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
				{
					QJsonObject obj;
					obj.insert( QStringLiteral("isLocked"), false );
					file.write( QJsonDocument( obj ).toJson() );
					file.close();
				}
				file.remove();
			}
		}
	}
}

} // namespace


ScreenLockFeaturePlugin::ScreenLockFeaturePlugin( QObject* parent ) :
	QObject( parent ),
	m_screenLockFeature( QStringLiteral( "ScreenLock" ),
						 Feature::Flag::Action | Feature::Flag::AllComponents,
						 Feature::Uid( "ccb535a2-1d24-4cc1-a709-8b47d2b2ac79" ),
						 Feature::Uid(),
						 tr( "Freeze" ), {},
						 tr( "To reclaim all user's full attention you can freeze "
							 "their computers using this button." ),
						 QStringLiteral(":/screenlock/system-lock-screen.png") ),
	m_freezeScreenFeature( QStringLiteral( "FreezeScreen" ),
						   Feature::Flag::Action | Feature::Flag::AllComponents,
						   Feature::Uid( "e1f486a0-5cb2-4a0b-93f5-7489ab102831" ),
						   m_screenLockFeature.uid(),
						   tr( "Freeze screen" ), {},
						   tr( "Pause screen and block client keyboard and mouse." ),
						   QStringLiteral(":/screenlock/system-lock-screen.png") ),
	m_textLockFeature( QStringLiteral( "ScreenLockWithMessage" ),
					   Feature::Flag::Action | Feature::Flag::AllComponents,
					   Feature::Uid( "5c2ea69f-d16c-4d7b-b7d2-60e507d3a905" ),
					   m_screenLockFeature.uid(),
					   tr( "Freeze with text" ), {},
					   tr( "Lock the screen and input devices while displaying a message." ),
					   QStringLiteral(":/screenlock/system-lock-screen.png") ),
	m_lockInputDevicesFeature( QStringLiteral( "InputDevicesLock" ),
							   Feature::Flag::Action | Feature::Flag::AllComponents,
							   Feature::Uid( "e4a77879-e544-4fec-bc18-e534f33b934c" ),
							   m_screenLockFeature.uid(),
							   tr( "Freeze input devices only" ), {},
							   tr( "Block keyboard and mouse while keeping client desktop visible." ),
							   QStringLiteral(":/screenlock/system-lock-screen.png") ),
	m_timedLockFeature( QStringLiteral( "TimedLock" ),
						Feature::Flag::Action | Feature::Flag::AllComponents,
						Feature::Uid( "a84f31c2-9e12-4c8d-b903-7281f6d54e19" ),
						Feature::Uid(),
						tr( "Timed Lock" ), {},
						tr( "Lock workstation screens with a countdown timer (Focus Session) for a set duration." ),
						QStringLiteral(":/screenlock/system-lock-screen.png") ),
	m_unfreezeFeature( QStringLiteral( "UnfreezeScreen" ),
					   Feature::Flag::Action | Feature::Flag::AllComponents,
					   Feature::Uid( "d2b78114-6fa3-42e5-bc72-6804d9b4b7c2" ),
					   m_screenLockFeature.uid(),
					   tr( "Unfreeze" ), {},
					   tr( "Unfreeze all selected computers." ),
					   QStringLiteral(":/screenlock/system-lock-screen.png") ),
	m_features( { m_screenLockFeature, m_freezeScreenFeature, m_textLockFeature, m_lockInputDevicesFeature, m_timedLockFeature, m_unfreezeFeature } ),
	m_lockWidget( nullptr )
{
	if (VeyonCore::component() == VeyonCore::Component::Service)
	{
		connect (VeyonCore::instance(), &VeyonCore::initialized,
				 this, [this]() {
			const auto lockInfo = loadPersistentLock();
			if( !lockInfo.isLocked )
			{
				VeyonCore::platform().inputDeviceFunctions().enableInputDevices();
			}
			else
			{
				VeyonCore::platform().inputDeviceFunctions().disableInputDevices();
			}

			auto* serviceWatchdog = new QTimer( this );
			serviceWatchdog->setInterval( 1000 );
			connect( serviceWatchdog, &QTimer::timeout, this, [this]() {
				const auto info = loadPersistentLock();
				if( info.isLocked )
				{
					VeyonCore::platform().inputDeviceFunctions().disableInputDevices();
				}
			});
			serviceWatchdog->start();
		});
	}
}



bool ScreenLockFeaturePlugin::startFeature( VeyonMasterInterface& master, const Feature& feature,
													const ComputerControlInterfaceList& computerControlInterfaces )
{
	if( feature == m_textLockFeature )
	{
		QInputDialog dialog( master.mainWindow() );
		dialog.setWindowTitle( tr( "Freeze screens with a message" ) );
		dialog.setLabelText( tr( "Message to display on the selected computers:" ) );
		dialog.setInputMode( QInputDialog::TextInput );
		dialog.setOption( QInputDialog::UsePlainTextEditForTextInput );
		dialog.setTextValue( tr( "Please wait for your teacher's instructions." ) );

		if( dialog.exec() != QDialog::Accepted )
		{
			return true;
		}

		const auto message = dialog.textValue().trimmed();
		if( message.isEmpty() )
		{
			return true;
		}

		return controlFeature( m_textLockFeature.uid(), Operation::Start,
							   { { argToString( Argument::Text ), message } },
							   computerControlInterfaces );
	}
	else if( feature == m_timedLockFeature )
	{
		QDialog dialog( master.mainWindow() );
		dialog.setWindowTitle( tr( "Timed Lock / Focus Session" ) );
		dialog.setMinimumWidth( 400 );
		dialog.setStyleSheet( QStringLiteral(
			"QDialog { background-color: #FFFFFF; color: #27352A; }"
			"QLabel { color: #27352A; font-size: 13px; }"
			"QComboBox, QSpinBox { background-color: #F0F4EB; color: #27352A; border: 1px solid #DCDDD2; border-radius: 4px; padding: 6px 10px; font-size: 13px; }"
			"QPushButton { background-color: #537B5B; color: #FFFFFF; border: 1px solid #426749; border-radius: 4px; padding: 7px 16px; font-weight: 600; font-size: 13px; }"
			"QPushButton:hover { background-color: #1D4ED8; }"
			"QPushButton#btnCancel { background-color: #F0F4EB; color: #657064; border: 1px solid #DCDDD2; }"
			"QPushButton#btnCancel:hover { background-color: #DCDDD2; color: #FFFFFF; }"
		) );

		auto* layout = new QVBoxLayout( &dialog );
		layout->setSpacing( 14 );
		layout->setContentsMargins( 22, 22, 22, 22 );

		auto* headerLabel = new QLabel( tr( "Select Focus Session Duration:" ), &dialog );
		headerLabel->setStyleSheet( QStringLiteral("font-weight: 700; font-size: 14px; color: #27352A;") );
		layout->addWidget( headerLabel );

		auto* subLabel = new QLabel( tr( "The student screen displays the live countdown ONLY when this Timed Lock is used. Workstations automatically unlock when the timer reaches 00:00." ), &dialog );
		subLabel->setStyleSheet( QStringLiteral("color: #657064; font-size: 12px; line-height: 1.4;") );
		subLabel->setWordWrap( true );
		layout->addWidget( subLabel );

		auto* comboDuration = new QComboBox( &dialog );
		comboDuration->addItem( tr( "5 minutes (Quick Break)" ), 5 );
		comboDuration->addItem( tr( "10 minutes (Focus Burst)" ), 10 );
		comboDuration->addItem( tr( "15 minutes (Standard Session)" ), 15 );
		comboDuration->addItem( tr( "20 minutes (Exam / Review)" ), 20 );
		comboDuration->addItem( tr( "30 minutes (Deep Focus)" ), 30 );
		comboDuration->addItem( tr( "45 minutes (Lecture Block)" ), 45 );
		comboDuration->addItem( tr( "60 minutes (1 Hour Session)" ), 60 );
		comboDuration->addItem( tr( "Custom Duration..." ), -1 );
		comboDuration->setCurrentIndex( 2 ); // Default 15 min
		layout->addWidget( comboDuration );

		auto* customContainer = new QWidget( &dialog );
		auto* customLayout = new QHBoxLayout( customContainer );
		customLayout->setContentsMargins( 0, 0, 0, 0 );
		auto* spinMinutes = new QSpinBox( customContainer );
		spinMinutes->setRange( 1, 300 );
		spinMinutes->setValue( 15 );
		spinMinutes->setSuffix( tr( " min" ) );
		customLayout->addWidget( new QLabel( tr( "Custom Duration:" ), customContainer ) );
		customLayout->addWidget( spinMinutes );
		customContainer->setVisible( false );
		layout->addWidget( customContainer );

		QObject::connect( comboDuration, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [=]( int idx ) {
			customContainer->setVisible( comboDuration->itemData( idx ).toInt() == -1 );
		});

		auto* btnBox = new QHBoxLayout();
		btnBox->addStretch();
		auto* btnCancel = new QPushButton( tr( "Cancel" ), &dialog );
		btnCancel->setObjectName( QStringLiteral("btnCancel") );
		auto* btnStart = new QPushButton( tr( "⏱️ Start Timed Lock" ), &dialog );
		btnBox->addWidget( btnCancel );
		btnBox->addWidget( btnStart );
		layout->addLayout( btnBox );

		QObject::connect( btnCancel, &QPushButton::clicked, &dialog, &QDialog::reject );
		QObject::connect( btnStart, &QPushButton::clicked, &dialog, &QDialog::accept );

		if( dialog.exec() != QDialog::Accepted )
		{
			return true;
		}

		int minutes = comboDuration->currentData().toInt();
		if( minutes == -1 )
		{
			minutes = spinMinutes->value();
		}
		if( minutes <= 0 )
		{
			minutes = 15;
		}

		const int durationSeconds = minutes * 60;
		return controlFeature( m_timedLockFeature.uid(), Operation::Start,
							   { { argToString( Argument::Duration ), durationSeconds } },
							   computerControlInterfaces );
	}

	return controlFeature( feature.uid(), Operation::Start, {}, computerControlInterfaces );
}



ScreenLockFeaturePlugin::~ScreenLockFeaturePlugin()
{
	delete m_lockWidget;
}



bool ScreenLockFeaturePlugin::controlFeature( Feature::Uid featureUid, Operation operation,
											 const QVariantMap& arguments,
											 const ComputerControlInterfaceList& computerControlInterfaces )
{
	if( hasFeature( featureUid ) == false )
	{
		return false;
	}

	if( featureUid == m_unfreezeFeature.uid() || operation == Operation::Stop )
	{
		for( const auto& uid : { m_screenLockFeature.uid(), m_freezeScreenFeature.uid(),
								 m_lockInputDevicesFeature.uid(), m_textLockFeature.uid(),
								 m_timedLockFeature.uid(), m_unfreezeFeature.uid() } )
		{
			sendFeatureMessage( FeatureMessage{ uid, FeatureCommand::StopLock }, computerControlInterfaces );
		}
		for( const auto& controlInterface : computerControlInterfaces )
		{
			if( controlInterface->designatedModeFeature() == m_screenLockFeature.uid() ||
				controlInterface->designatedModeFeature() == m_freezeScreenFeature.uid() ||
				controlInterface->designatedModeFeature() == m_lockInputDevicesFeature.uid() ||
				controlInterface->designatedModeFeature() == m_textLockFeature.uid() ||
				controlInterface->designatedModeFeature() == m_timedLockFeature.uid() )
			{
				controlInterface->setDesignatedModeFeature( Feature::Uid() );
			}
		}
		return true;
	}

	auto lockControlInterfaces = computerControlInterfaces;
	lockControlInterfaces.removeLocalHostInterfaces();

	auto targetUid = m_screenLockFeature.uid();
	if( featureUid == m_lockInputDevicesFeature.uid() )
	{
		targetUid = m_lockInputDevicesFeature.uid();
	}
	else if( featureUid == m_textLockFeature.uid() )
	{
		targetUid = m_textLockFeature.uid();
	}
	else if( featureUid == m_timedLockFeature.uid() )
	{
		targetUid = m_timedLockFeature.uid();
	}

	for( const auto& controlInterface : lockControlInterfaces )
	{
		controlInterface->setDesignatedModeFeature( targetUid );
	}

	auto message = FeatureMessage{ targetUid, FeatureCommand::StartLock };
	if( featureUid == m_textLockFeature.uid() )
	{
		message.addArgument( Argument::Text, arguments.value( argToString( Argument::Text ) ).toString() );
	}
	else if( featureUid == m_timedLockFeature.uid() )
	{
		const int dur = arguments.value( argToString( Argument::Duration ) ).toInt();
		message.addArgument( Argument::Duration, dur > 0 ? dur : 15 * 60 );
	}
	sendFeatureMessage( message, lockControlInterfaces );

	return true;
}



bool ScreenLockFeaturePlugin::handleFeatureMessage( VeyonServerInterface& server,
													const MessageContext& messageContext,
													const FeatureMessage& message )
{
	Q_UNUSED(messageContext)

	if( message.featureUid() == m_screenLockFeature.uid() ||
		message.featureUid() == m_freezeScreenFeature.uid() ||
		message.featureUid() == m_lockInputDevicesFeature.uid() ||
		message.featureUid() == m_textLockFeature.uid() ||
		message.featureUid() == m_timedLockFeature.uid() ||
		message.featureUid() == m_unfreezeFeature.uid() )
	{
		m_server = &server;

		if (message.command<FeatureCommand>() == FeatureCommand::StopLock ||
			message.featureUid() == m_unfreezeFeature.uid())
		{
			++m_lockGeneration;
			if( m_timedLockTimer )
			{
				m_timedLockTimer->stop();
			}
			savePersistentLock( false );

			for (const auto& uid : { m_screenLockFeature.uid(), m_freezeScreenFeature.uid(),
									 m_lockInputDevicesFeature.uid(), m_textLockFeature.uid(),
									 m_timedLockFeature.uid(), m_unfreezeFeature.uid() })
			{
				if (server.featureWorkerManager().isWorkerRunning(uid))
				{
					server.featureWorkerManager().sendMessageToManagedSystemWorker(
						FeatureMessage{ uid, FeatureCommand::StopLock } );
					server.featureWorkerManager().stopWorker( uid );
				}
			}

			VeyonCore::platform().inputDeviceFunctions().enableInputDevices();
			VeyonCore::platform().coreFunctions().setSystemUiState( true );

			return true;
		}

		if( message.command<FeatureCommand>() == FeatureCommand::StartLock )
		{
			QString lockText;
			if( message.hasArgument( Argument::Text ) )
			{
				lockText = message.argument( Argument::Text ).toString();
			}
			int dur = 0;
			if( message.hasArgument( Argument::Duration ) )
			{
				dur = message.argument( Argument::Duration ).toInt();
			}

			if( message.featureUid() == m_timedLockFeature.uid() && dur > 0 )
			{
				// Timed locks are temporary focus sessions: NOT persistently locked across reboots!
				savePersistentLock( false );

				// Invalidate any prior timed lock timer before arming a new generation
				const quint64 gen = ++m_lockGeneration;
				if( m_timedLockTimer == nullptr )
				{
					m_timedLockTimer = new QTimer( this );
					m_timedLockTimer->setSingleShot( true );
				}
				else
				{
					m_timedLockTimer->stop();
					m_timedLockTimer->disconnect( this );
				}

				connect( m_timedLockTimer, &QTimer::timeout, this, [this, gen]() {
					if( gen != m_lockGeneration || m_server.isNull() )
					{
						return;
					}
					vInfo() << "Timed lock focus session elapsed on server. Cleanly unlocking workstation.";
					savePersistentLock( false );
					for( const auto& uid : { m_screenLockFeature.uid(), m_freezeScreenFeature.uid(),
											 m_lockInputDevicesFeature.uid(), m_textLockFeature.uid(),
											 m_timedLockFeature.uid(), m_unfreezeFeature.uid() } )
					{
						if( m_server->featureWorkerManager().isWorkerRunning( uid ) )
						{
							m_server->featureWorkerManager().sendMessageToManagedSystemWorker(
								FeatureMessage{ uid, FeatureCommand::StopLock } );
							m_server->featureWorkerManager().stopWorker( uid );
						}
					}
					VeyonCore::platform().inputDeviceFunctions().enableInputDevices();
					VeyonCore::platform().coreFunctions().setSystemUiState( true );
				});
				m_timedLockTimer->start( dur * 1000 );
			}
			else
			{
				// Standard freeze locks cancel any pending timed-lock timer
				++m_lockGeneration;
				if( m_timedLockTimer )
				{
					m_timedLockTimer->stop();
				}
				savePersistentLock( true, message.featureUid(), lockText, 0 );
			}
		}

		if( VeyonCore::platform().sessionFunctions().currentSessionHasUser() == false )
		{
			vDebug() << "not locking screen since not running in a user session";
			return true;
		}

		// forward message to worker
		server.featureWorkerManager().sendMessageToManagedSystemWorker( message );

		return true;
	}

	return false;
}



bool ScreenLockFeaturePlugin::handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message )
{
	Q_UNUSED(worker);

	if( message.featureUid() == m_screenLockFeature.uid() ||
		message.featureUid() == m_freezeScreenFeature.uid() ||
		message.featureUid() == m_lockInputDevicesFeature.uid() ||
		message.featureUid() == m_textLockFeature.uid() ||
		message.featureUid() == m_timedLockFeature.uid() ||
		message.featureUid() == m_unfreezeFeature.uid() )
	{
		switch (message.command<FeatureCommand>())
		{
		case FeatureCommand::StartLock:
			if( m_lockWidget == nullptr )
			{
				VeyonCore::platform().coreFunctions().disableScreenSaver();

				auto mode = LockWidget::BackgroundPixmap;
				QString lockText;
				int duration = 0;

				if( message.featureUid() == m_lockInputDevicesFeature.uid() )
				{
					mode = LockWidget::DesktopVisible;
				}
				else if( message.featureUid() == m_textLockFeature.uid() )
				{
					mode = LockWidget::TextMessage;
					lockText = message.argument( Argument::Text ).toString();
				}
				else if( message.featureUid() == m_timedLockFeature.uid() )
				{
					if( message.hasArgument( Argument::Duration ) )
					{
						duration = message.argument( Argument::Duration ).toInt();
					}
					if( duration <= 0 )
					{
						duration = 15 * 60;
					}
				}

				m_lockWidget = new LockWidget( mode,
											   QPixmap( QStringLiteral(":/screenlock/locked-screen-background.png" ) ),
											   nullptr, lockText, duration );

				connect( m_lockWidget, &LockWidget::expired, this, [this]() {
					vInfo() << "Timed lock expired. Automatically unlocking workstation.";
					savePersistentLock( false );
					if( m_lockWidget )
					{
						delete m_lockWidget;
						m_lockWidget = nullptr;
					}

					VeyonCore::platform().coreFunctions().restoreScreenSaverSettings();
					VeyonCore::platform().inputDeviceFunctions().enableInputDevices();
					VeyonCore::platform().coreFunctions().setSystemUiState( true );

					QCoreApplication::quit();
				});
			}
			return true;

		case FeatureCommand::StopLock:
			if( m_lockWidget )
			{
				delete m_lockWidget;
				m_lockWidget = nullptr;
			}

			VeyonCore::platform().coreFunctions().restoreScreenSaverSettings();
			VeyonCore::platform().inputDeviceFunctions().enableInputDevices();
			VeyonCore::platform().coreFunctions().setSystemUiState( true );

			QCoreApplication::quit();

			return true;

		default:
			break;
		}
	}

	return false;
}



void ScreenLockFeaturePlugin::initServer( VeyonServerInterface& server )
{
	m_server = &server;

	if( m_watchdogTimer == nullptr )
	{
		m_watchdogTimer = new QTimer( this );
		m_watchdogTimer->setInterval( 1000 );
		connect( m_watchdogTimer, &QTimer::timeout, this, &ScreenLockFeaturePlugin::checkAndEnforcePersistentLock );
		m_watchdogTimer->start();
	}

	checkAndEnforcePersistentLock();
}



void ScreenLockFeaturePlugin::checkAndEnforcePersistentLock()
{
	if( m_server.isNull() )
	{
		return;
	}

	const auto lockInfo = loadPersistentLock();
	if( !lockInfo.isLocked || lockInfo.featureUid == m_timedLockFeature.uid() || lockInfo.duration > 0 )
	{
		return;
	}

	// Persistently keep input devices disabled while locked
	VeyonCore::platform().inputDeviceFunctions().disableInputDevices();

	if( VeyonCore::platform().sessionFunctions().currentSessionHasUser() == false )
	{
		return;
	}

	bool workerRunning = false;
	for( const auto& uid : { m_screenLockFeature.uid(), m_freezeScreenFeature.uid(),
							 m_lockInputDevicesFeature.uid(), m_textLockFeature.uid(),
							 m_timedLockFeature.uid() } )
	{
		if( m_server->featureWorkerManager().isWorkerRunning( uid ) )
		{
			workerRunning = true;
			break;
		}
	}

	if( !workerRunning )
	{
		auto targetUid = lockInfo.featureUid;
		if( targetUid.isNull() || !hasFeature( targetUid ) )
		{
			targetUid = m_screenLockFeature.uid();
		}

		vInfo() << "Enforcing persistent screen lock after boot/logon or worker exit for feature" << targetUid;

		FeatureMessage msg{ targetUid, FeatureCommand::StartLock };
		if( !lockInfo.lockText.isEmpty() )
		{
			msg.addArgument( Argument::Text, lockInfo.lockText );
		}
		if( lockInfo.duration > 0 )
		{
			msg.addArgument( Argument::Duration, lockInfo.duration );
		}
		m_server->featureWorkerManager().sendMessageToManagedSystemWorker( msg );
	}
}



bool ScreenLockFeaturePlugin::isFeatureActive( VeyonServerInterface& server, Feature::Uid featureUid ) const
{
	const auto lockInfo = loadPersistentLock();
	if( lockInfo.isLocked )
	{
		if( featureUid == lockInfo.featureUid ||
			featureUid == m_screenLockFeature.uid() ||
			featureUid == m_freezeScreenFeature.uid() ||
			featureUid == m_timedLockFeature.uid() )
		{
			return true;
		}
	}

	for( const auto& uid : { m_screenLockFeature.uid(), m_freezeScreenFeature.uid(),
							 m_lockInputDevicesFeature.uid(), m_textLockFeature.uid(),
							 m_timedLockFeature.uid() } )
	{
		if( server.featureWorkerManager().isWorkerRunning( uid ) )
		{
			return ( featureUid == uid || featureUid == m_screenLockFeature.uid() );
		}
	}

	return false;
}
