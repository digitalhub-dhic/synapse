/*
 * PowerControlFeaturePlugin.cpp - implementation of PowerControlFeaturePlugin class
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

#include <QtConcurrent>
#include <QEvent>
#include <QCloseEvent>
#include <QKeyEvent>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>

static void setTaskManagerDisabled( bool disabled )
{
	// 1. HKCU Policy
	HKEY hKey = nullptr;
	if( RegCreateKeyExW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
						 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr ) == ERROR_SUCCESS )
	{
		if( disabled )
		{
			DWORD val = 1;
			RegSetValueExW( hKey, L"DisableTaskMgr", 0, REG_DWORD, reinterpret_cast<const BYTE*>( &val ), sizeof( val ) );
		}
		else
		{
			RegDeleteValueW( hKey, L"DisableTaskMgr" );
		}
		RegCloseKey( hKey );
	}

	// 2. HKLM Policy (requires admin / SYSTEM, which server has)
	if( RegCreateKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
						 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr ) == ERROR_SUCCESS )
	{
		if( disabled )
		{
			DWORD val = 1;
			RegSetValueExW( hKey, L"DisableTaskMgr", 0, REG_DWORD, reinterpret_cast<const BYTE*>( &val ), sizeof( val ) );
		}
		else
		{
			RegDeleteValueW( hKey, L"DisableTaskMgr" );
		}
		RegCloseKey( hKey );
	}

	// 3. Terminate running taskmgr.exe if disabled
	if( disabled )
	{
		HANDLE hSnap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
		if( hSnap != INVALID_HANDLE_VALUE )
		{
			PROCESSENTRY32W pe;
			pe.dwSize = sizeof( pe );
			if( Process32FirstW( hSnap, &pe ) )
			{
				do
				{
					if( _wcsicmp( pe.szExeFile, L"taskmgr.exe" ) == 0 )
					{
						HANDLE hProc = OpenProcess( PROCESS_TERMINATE, FALSE, pe.th32ProcessID );
						if( hProc )
						{
							TerminateProcess( hProc, 0 );
							CloseHandle( hProc );
						}
					}
				} while( Process32NextW( hSnap, &pe ) );
			}
			CloseHandle( hSnap );
		}
	}
}
#endif
#include <QMessageBox>
#include <QNetworkInterface>
#include <QProgressBar>
#include <QProgressDialog>
#include <QUdpSocket>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

class ClientShutdownCountdownDialog : public QDialog
{
public:
	explicit ClientShutdownCountdownDialog( QWidget* parent = nullptr ) :
		QDialog( parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint )
	{
		setAttribute( Qt::WA_DeleteOnClose, false );
	}

	void reject() override
	{
		// Block Esc key or reject() from cancelling the shutdown countdown
	}

protected:
	void closeEvent( QCloseEvent* event ) override
	{
		// Block Alt+F4 or window close attempts
		event->ignore();
	}

	void keyPressEvent( QKeyEvent* event ) override
	{
		if( event->key() == Qt::Key_Escape ||
			( event->key() == Qt::Key_F4 && event->modifiers().testFlag( Qt::AltModifier ) ) ||
			( event->key() == Qt::Key_W && event->modifiers().testFlag( Qt::ControlModifier ) ) )
		{
			event->ignore();
			return;
		}
		QDialog::keyPressEvent( event );
	}
};

#include "Computer.h"
#include "ComputerControlInterface.h"
#include "FeatureWorkerManager.h"
#include "NetworkObjectDirectoryManager.h"
#include "PlatformCoreFunctions.h"
#include "PlatformUserFunctions.h"
#include "PowerControlFeaturePlugin.h"
#include "PowerDownTimeInputDialog.h"
#include "VeyonConfiguration.h"
#include "VeyonMasterInterface.h"
#include "VeyonServerInterface.h"


PowerControlFeaturePlugin::PowerControlFeaturePlugin( QObject* parent ) :
	QObject( parent ),
	m_commands( {
{ QStringLiteral("on"), tr( "Power on a computer via Wake-on-LAN (WOL)" ) },
				} ),
	m_powerOnFeature( QStringLiteral( "PowerOn" ),
					  Feature::Flag::Action | Feature::Flag::AllComponents,
					  Feature::Uid( "f483c659-b5e7-4dbc-bd91-2c9403e70ebd" ),
					  Feature::Uid(),
					  tr( "Power on" ), {},
					  tr( "Click this button to power on all computers. "
						  "This way you do not have to power on each computer by hand." ),
					  QStringLiteral(":/powercontrol/preferences-system-power-management.png") ),
	m_rebootFeature( QStringLiteral( "Reboot" ),
					 Feature::Flag::Action | Feature::Flag::AllComponents,
					 Feature::Uid( "4f7d98f0-395a-4fff-b968-e49b8d0f748c" ),
					 Feature::Uid(),
					 tr( "Reboot" ), {},
					 tr( "Click this button to reboot all computers." ),
					 QStringLiteral(":/powercontrol/system-reboot.png") ),
	m_powerDownFeature( QStringLiteral( "PowerDown" ),
						Feature::Flag::Action | Feature::Flag::AllComponents,
						Feature::Uid( "6f5a27a0-0e2f-496e-afcc-7aae62eede10" ),
						Feature::Uid(),
						tr( "Power down" ), {},
						tr( "Click this button to power down all computers. "
							"This way you do not have to power down each computer by hand." ),
						QStringLiteral(":/powercontrol/system-shutdown.png") ),
	m_powerDownNowFeature( QStringLiteral( "PowerDownNow" ),
								   Feature::Flag::Action | Feature::Flag::AllComponents,
								   Feature::Uid( "a88039f2-6716-40d8-b4e1-9f5cd48e91ed" ),
								   m_powerDownFeature.uid(),
								   tr( "Power down now" ), {}, {} ),
	m_installUpdatesAndPowerDownFeature( QStringLiteral( "InstallUpdatesAndPowerDown" ),
								   Feature::Flag::Action | Feature::Flag::AllComponents,
								   Feature::Uid( "09bcb3a1-fc11-4d03-8cf1-efd26be8655b" ),
								   m_powerDownFeature.uid(),
								   tr( "Install updates and power down" ), {}, {} ),
	m_powerDownConfirmedFeature( QStringLiteral( "PowerDownConfirmed" ),
								   Feature::Flag::Action | Feature::Flag::AllComponents,
								   Feature::Uid( "ea2406be-d5c7-42b8-9f04-53469d3cc34c" ),
								   m_powerDownFeature.uid(),
								   tr( "Power down after user confirmation" ), {}, {} ),
	m_powerDownDelayedFeature( QStringLiteral( "PowerDownDelayed" ),
								   Feature::Flag::Action | Feature::Flag::AllComponents,
								   Feature::Uid( "352de795-7fc4-4850-bc57-525bcb7033f5" ),
								   m_powerDownFeature.uid(),
								   tr( "Power down after timeout" ), {}, {} ),
	m_revertPowerDownFeature( QStringLiteral( "RevertPowerDown" ),
								   Feature::Flag::Action | Feature::Flag::AllComponents,
								   Feature::Uid( "c78e12d4-4a25-4c07-b2e1-512683fb3911" ),
								   m_powerDownFeature.uid(),
								   tr( "Revert scheduled power down" ), {}, {} ),
	m_features( { m_powerOnFeature, m_rebootFeature, m_powerDownFeature, m_powerDownNowFeature,
				m_installUpdatesAndPowerDownFeature, m_powerDownConfirmedFeature, m_powerDownDelayedFeature, m_revertPowerDownFeature } )
{
	if( VeyonCore::component() == VeyonCore::Component::Service )
	{
		connect( VeyonCore::instance(), &VeyonCore::initialized,
				 this, []() {
#ifdef Q_OS_WIN
			setTaskManagerDisabled( false );
#endif
		} );
	}
}

PowerControlFeaturePlugin::~PowerControlFeaturePlugin()
{
#ifdef Q_OS_WIN
	if( m_serverShutdownActive )
	{
		setTaskManagerDisabled( false );
	}
#endif
}




QStringList PowerControlFeaturePlugin::commands() const
{
	return m_commands.keys();
}



QString PowerControlFeaturePlugin::commandHelp( const QString& command ) const
{
	return m_commands.value( command );
}



const FeatureList &PowerControlFeaturePlugin::featureList() const
{
	return m_features;
}



bool PowerControlFeaturePlugin::controlFeature( Feature::Uid featureUid,
											   Operation operation,
											   const QVariantMap& arguments,
											   const ComputerControlInterfaceList& computerControlInterfaces )
{
	if( hasFeature( featureUid ) == false || operation != Operation::Start )
	{
		return false;
	}

	if( featureUid == m_powerOnFeature.uid() )
	{
		QList<Computer> hosts;
		hosts.reserve(computerControlInterfaces.size());
		for (const auto& controlInterface : computerControlInterfaces)
		{
			hosts.append(controlInterface->computer());
		}

		(void) QtConcurrent::run([hosts]() {
			const auto directory = VeyonCore::networkObjectDirectoryManager().configuredDirectory();

			for (const auto& host : hosts)
			{
				auto macAddress = host.macAddress();
				if (macAddress.isEmpty())
				{
					macAddress = directory->queryObjectAttribute(host.networkObjectUid(),
																 NetworkObject::Attribute::MacAddress).toString();
				}

				if (macAddress.isEmpty() == false)
				{
					broadcastWOLPacket(macAddress);
				}
				else
				{
					vWarning() << "no MAC address available for host" << host.hostName() << "with ID" << host.networkObjectUid();
				}
			}
		});
	}
	else if( featureUid == m_powerDownDelayedFeature.uid() )
	{
		int shutdownTimeout = arguments.value( argToString(Argument::ShutdownTimeout), -999 ).toInt();
		if( shutdownTimeout == -999 )
		{
			shutdownTimeout = arguments.value( QStringLiteral("ShutdownTimeout"), -999 ).toInt();
		}
		if( shutdownTimeout == -999 )
		{
			shutdownTimeout = arguments.value( QStringLiteral("shutdownTimeout"), 60 ).toInt();
		}
		const auto cancel = arguments.value( argToString(Argument::Cancel), false ).toBool() ||
							arguments.value( QStringLiteral("Cancel"), false ).toBool() ||
							arguments.value( QStringLiteral("cancel"), false ).toBool();

		sendFeatureMessage(FeatureMessage{featureUid, FeatureMessage::Command::Default}
						   .addArgument( Argument::ShutdownTimeout, shutdownTimeout )
						   .addArgument( Argument::Cancel, cancel ),
						   computerControlInterfaces );
	}
	else if( featureUid == m_revertPowerDownFeature.uid() )
	{
		sendFeatureMessage(FeatureMessage{m_powerDownDelayedFeature.uid(), FeatureMessage::Command::Default}
						   .addArgument( Argument::ShutdownTimeout, -1 )
						   .addArgument( Argument::Cancel, true ),
						   computerControlInterfaces );
	}
	else
	{
		sendFeatureMessage(FeatureMessage{featureUid, FeatureMessage::Command::Default}, computerControlInterfaces );
	}

	return true;
}



bool PowerControlFeaturePlugin::startFeature( VeyonMasterInterface& master, const Feature& feature,
											  const ComputerControlInterfaceList& computerControlInterfaces )
{
	if( feature == m_powerOnFeature )
	{
		return controlFeature( feature.uid(), Operation::Start, {}, computerControlInterfaces );
	}

	const auto executeOnAllComputers =
		computerControlInterfaces.size() >= master.filteredComputerControlInterfaces().size();

	if (confirmFeatureExecution(feature, executeOnAllComputers, master.mainWindow()) == false)
	{
		return false;
	}

	if (feature == m_powerDownDelayedFeature)
	{
		const auto targets = computerControlInterfaces.isEmpty()
			? master.filteredComputerControlInterfaces()
			: computerControlInterfaces;
		const int targetCount = targets.isEmpty()
			? master.allComputerControlInterfaces().size()
			: targets.size();

		PowerDownTimeInputDialog dialog( master.mainWindow(), targetCount );

		if (dialog.exec())
		{
			const int initialSeconds = dialog.seconds();
			controlFeature(feature.uid(), Operation::Start,
						  {{argToString(Argument::ShutdownTimeout), initialSeconds},
						   {argToString(Argument::Cancel), false}},
						  computerControlInterfaces);

			showMasterPowerDownController( master, initialSeconds, computerControlInterfaces );
			return true;
		}

		return true;
	}

	if (feature == m_revertPowerDownFeature)
	{
		controlFeature(feature.uid(), Operation::Start,
					  {{argToString(Argument::ShutdownTimeout), -1},
					   {argToString(Argument::Cancel), true}},
					  computerControlInterfaces);

		if (m_masterControllerDialog)
		{
			m_masterControllerDialog->close();
		}
		master.cancelCountdown( computerControlInterfaces );
		return true;
	}


	return controlFeature( feature.uid(), Operation::Start, {}, computerControlInterfaces );
}



bool PowerControlFeaturePlugin::handleFeatureMessage( VeyonServerInterface& server,
													  const MessageContext& messageContext,
													  const FeatureMessage& message )
{
	Q_UNUSED(messageContext)

	auto& featureWorkerManager = server.featureWorkerManager();

	if( message.featureUid() == m_powerDownFeature.uid() ||
		message.featureUid() == m_powerDownNowFeature.uid() ||
		message.featureUid() == m_installUpdatesAndPowerDownFeature.uid() )
	{
		const auto installUpdates = message.featureUid() == m_installUpdatesAndPowerDownFeature.uid();
		VeyonCore::platform().coreFunctions().powerDown( installUpdates );
	}
	else if( message.featureUid() == m_powerDownConfirmedFeature.uid() )
	{
		if( VeyonCore::platform().userFunctions().isAnyUserLoggedOn() == false )
		{
			VeyonCore::platform().coreFunctions().powerDown( false );
		}
		else
		{
			featureWorkerManager.sendMessageToManagedSystemWorker( message );
		}
	}
	else if( message.featureUid() == m_powerDownDelayedFeature.uid() )
	{
		m_server = &server;

		const bool cancel = message.argument( Argument::Cancel ).toBool() ||
							message.argument( Argument::ShutdownTimeout ).toInt() < 0;
		if( cancel )
		{
			m_serverShutdownActive = false;
			if( m_serverShutdownTimer )
			{
				m_serverShutdownTimer->stop();
			}
#ifdef Q_OS_WIN
			setTaskManagerDisabled( false );
#endif
			featureWorkerManager.sendMessageToUnmanagedSessionWorker( message );
			featureWorkerManager.sendMessageToManagedSystemWorker( message );
			featureWorkerManager.stopWorker( m_powerDownDelayedFeature.uid() );
#ifdef Q_OS_WIN
			HANDLE token = nullptr;
			if( OpenProcessToken( GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token ) )
			{
				TOKEN_PRIVILEGES tp{};
				LookupPrivilegeValue( nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid );
				tp.PrivilegeCount = 1;
				tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
				AdjustTokenPrivileges( token, FALSE, &tp, 0, nullptr, nullptr );
				CloseHandle( token );
			}
			AbortSystemShutdown( nullptr );
#endif
			return true;
		}

		const int timeout = message.argument( Argument::ShutdownTimeout ).toInt();
		m_serverRemainingSeconds = timeout;
		m_serverShutdownActive = true;

#ifdef Q_OS_WIN
		setTaskManagerDisabled( true );
#endif

		if( m_serverShutdownTimer == nullptr )
		{
			m_serverShutdownTimer = new QTimer( this );
			connect( m_serverShutdownTimer, &QTimer::timeout, this, [this]() {
				if( !m_serverShutdownActive )
				{
					if( m_serverShutdownTimer )
					{
						m_serverShutdownTimer->stop();
					}
					return;
				}

				m_serverRemainingSeconds--;

#ifdef Q_OS_WIN
				// Actively ensure Task Manager remains blocked and any instance terminated
				setTaskManagerDisabled( true );
#endif

				if( m_serverRemainingSeconds <= 0 )
				{
#ifdef Q_OS_WIN
					setTaskManagerDisabled( true );
#endif
					vWarning() << "Server shutdown countdown reached 0 - executing forced power down!";
					VeyonCore::platform().coreFunctions().powerDown( false );
					if( m_serverShutdownTimer )
					{
						m_serverShutdownTimer->setInterval( 2000 );
					}
				}
				else if( m_server )
				{
					auto& workerMgr = m_server->featureWorkerManager();
					FeatureMessage updateMsg{ m_powerDownDelayedFeature.uid(), FeatureMessage::Command::Default };
					updateMsg.addArgument( Argument::ShutdownTimeout, m_serverRemainingSeconds );
					updateMsg.addArgument( Argument::Cancel, false );
					workerMgr.sendMessageToManagedSystemWorker( updateMsg );
				}
			} );
		}
		m_serverShutdownTimer->start( 1000 );

		featureWorkerManager.sendMessageToManagedSystemWorker( message );
	}
	else if( message.featureUid() == m_rebootFeature.uid() )
	{
		VeyonCore::platform().coreFunctions().reboot();
	}
	else
	{
		return false;
	}

	return true;
}



bool PowerControlFeaturePlugin::handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message )
{
	Q_UNUSED(worker)

	if( message.featureUid() == m_powerDownConfirmedFeature.uid() )
	{
		confirmShutdown();
		return true;
	}
	else if( message.featureUid() == m_powerDownDelayedFeature.uid() )
	{
		const bool cancel = message.argument( Argument::Cancel ).toBool() ||
							message.argument( Argument::ShutdownTimeout ).toInt() < 0;
		if( cancel )
		{
#ifdef Q_OS_WIN
			setTaskManagerDisabled( false );
#endif
			if( m_clientShutdownTimer )
			{
				m_clientShutdownTimer->stop();
			}
			if( m_clientShutdownDialog )
			{
				m_clientShutdownDialog->close();
				m_clientShutdownDialog->deleteLater();
				m_clientShutdownDialog = nullptr;
			}
			QCoreApplication::quit();
			return true;
		}

		displayShutdownTimeout( message.argument( Argument::ShutdownTimeout ).toInt() );
		return true;
	}

	return false;
}



CommandLinePluginInterface::RunResult PowerControlFeaturePlugin::handle_help( const QStringList& arguments )
{
	const auto command = arguments.value( 0 );

	const QMap<QString, QStringList> commands = {
		{ QStringLiteral("on"),
		  QStringList( { QStringLiteral("<%1>").arg( tr("MAC ADDRESS") ),
						 tr( "This command broadcasts a Wake-on-LAN (WOL) packet to the network in order to power on the computer with the given MAC address." ) } ) },
	};

	if( commands.contains( command ) )
	{
		const auto& helpString = commands[command];
		print( QStringLiteral("\n%1 %2 %3\n\n%4\n\n").arg( commandLineModuleName(), command, helpString[0], helpString[1] ) );

		return NoResult;
	}

	return InvalidCommand;
}



CommandLinePluginInterface::RunResult PowerControlFeaturePlugin::handle_on( const QStringList& arguments )
{
	if( arguments.size() < 1 )
	{
		return NotEnoughArguments;
	}

	return broadcastWOLPacket( arguments.first() ) ? Successful : Failed;
}



bool PowerControlFeaturePlugin::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::Close &&
		qobject_cast<QProgressDialog *>(watched))
	{
		event->ignore();
		return true;
	}

	return QObject::eventFilter(watched, event);
}



bool PowerControlFeaturePlugin::confirmFeatureExecution( const Feature& feature, bool all, QWidget* parent )
{
	if( feature == m_powerDownNowFeature )
	{
		return QMessageBox::question( parent, tr( "Confirm power down now" ),
									  all ? tr( "Do you really want to power down <b>ALL</b> computers now?" )
										  : tr( "Do you really want to power down the selected computers now?" ) ) ==
				QMessageBox::Yes;
	}

	if( VeyonCore::config().confirmUnsafeActions() == false )
	{
		return true;
	}

	if( feature == m_rebootFeature )
	{
		return QMessageBox::question( parent, tr( "Confirm reboot" ),
									  all ? tr( "Do you really want to reboot <b>ALL</b> computers?" )
										  : tr( "Do you really want to reboot the selected computers?" ) ) ==
				QMessageBox::Yes;
	}
	else if( feature == m_powerDownFeature ||
			 feature == m_powerDownNowFeature ||
			 feature == m_installUpdatesAndPowerDownFeature ||
			 feature == m_powerDownConfirmedFeature ||
			 feature == m_powerDownDelayedFeature )
	{
		if( feature == m_powerDownDelayedFeature )
		{
			// The dedicated Power Down configuration & confirmation dialog handles user confirmation directly
			return true;
		}
		return QMessageBox::question( parent, tr( "Confirm power down" ),
									  all ? tr( "Do you really want to power down <b>ALL</b> computers?" )
										  : tr( "Do you really want to power down the selected computers?" ) ) ==
				QMessageBox::Yes;
	}

	return true;
}



bool PowerControlFeaturePlugin::broadcastWOLPacket( QString macAddress )
{
	if (macAddress.isEmpty())
	{
		return false;
	}

	const auto originalMacAddress = macAddress;

	// remove all possible delimiters
	macAddress.replace( QLatin1Char(':'), QString() );
	macAddress.replace( QLatin1Char('-'), QString() );
	macAddress.replace( QLatin1Char('.'), QString() );

	const auto macAddressBytes = QByteArray::fromHex(macAddress.toUtf8());
	static constexpr auto MacAddressSize = 6;

	if (macAddressBytes.size() != MacAddressSize)
	{
		CommandLineIO::error( tr( "Invalid MAC address specified!" ) );
		vWarning() << "invalid MAC address" << originalMacAddress;
		return false;
	}

	static constexpr auto MagicPacketFieldCount = 17;

	QByteArray datagram(MacAddressSize * MagicPacketFieldCount, char(0xff));

	for (int i = 1; i < MagicPacketFieldCount; ++i)
	{
		datagram.replace(i * MacAddressSize, MacAddressSize, macAddressBytes);
	}

	QUdpSocket udpSocket;

	vDebug() << "broadcasting WOL packet for" << originalMacAddress;
	bool success = ( udpSocket.writeDatagram( datagram, QHostAddress::Broadcast, 9 ) == datagram.size() );

	const auto networkInterfaces = QNetworkInterface::allInterfaces();
	for( const auto& networkInterface : networkInterfaces )
	{
		const auto addressEntries = networkInterface.addressEntries();
		for( const auto& addressEntry : addressEntries )
		{
			if( addressEntry.broadcast().isNull() == false )
			{
				vDebug() << "broadcasting WOL packet for" << originalMacAddress << "via" << addressEntry.broadcast().toString();
				success &= ( udpSocket.writeDatagram( datagram, addressEntry.broadcast(), 9 ) == datagram.size() );
			}
		}
	}

	return success;
}



void PowerControlFeaturePlugin::confirmShutdown()
{
	QMessageBox m( QMessageBox::Question, tr( "Confirm power down" ),
				   tr( "The computer was remotely requested to power down. Do you want to power down the computer now?" ),
				   QMessageBox::Yes | QMessageBox::No );
	m.show();
	VeyonCore::platform().coreFunctions().raiseWindow( &m, true );

	if( m.exec() == QMessageBox::Yes )
	{
		VeyonCore::platform().coreFunctions().powerDown( false );
	}
}





void PowerControlFeaturePlugin::displayShutdownTimeout( int shutdownTimeout )
{
	m_clientRemainingSeconds = shutdownTimeout;

	if( m_clientShutdownDialog )
	{
		const int mins = m_clientRemainingSeconds / 60;
		const int secs = m_clientRemainingSeconds % 60;
		QString timeDesc;
		if( mins > 0 )
		{
			timeDesc = QStringLiteral( "%1 minute%2, %3 second%4" )
				.arg( mins )
				.arg( mins == 1 ? QString() : QStringLiteral("s") )
				.arg( secs )
				.arg( secs == 1 ? QString() : QStringLiteral("s") );
		}
		else
		{
			timeDesc = QStringLiteral( "%1 second%2" )
				.arg( secs )
				.arg( secs == 1 ? QString() : QStringLiteral("s") );
		}
		if( m_clientProgressBar )
		{
			m_clientProgressBar->setMaximum( qMax( m_clientProgressBar->maximum(), shutdownTimeout ) );
			m_clientProgressBar->setValue( m_clientProgressBar->maximum() - m_clientRemainingSeconds );
		}
		if( m_clientRemainingTimeLabel )
		{
			m_clientRemainingTimeLabel->setText( QStringLiteral("%1:%2 remaining")
				.arg( mins, 2, 10, QLatin1Char('0') )
				.arg( secs, 2, 10, QLatin1Char('0') ) );
		}
		if( m_clientLabel )
		{
			m_clientLabel->setText( tr( "Synapse will power this computer down in %1.\n\nPlease save your work and close all programs." ).arg( timeDesc ) );
		}
		if( m_clientShutdownDialog->isHidden() )
		{
			m_clientShutdownDialog->show();
		}
		m_clientShutdownDialog->raise();
		m_clientShutdownDialog->activateWindow();
#ifdef Q_OS_WIN
		HWND hwnd = reinterpret_cast<HWND>( m_clientShutdownDialog->winId() );
		if( hwnd )
		{
			SetWindowPos( hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW );
			SetForegroundWindow( hwnd );
		}
#endif
		return;
	}

	auto formatClientShutdownMsg = []( int totalSecs ) -> QString {
		const int mins = totalSecs / 60;
		const int secs = totalSecs % 60;
		QString timeDesc;
		if( mins > 0 )
		{
			timeDesc = QStringLiteral( "%1 minute%2, %3 second%4" )
				.arg( mins )
				.arg( mins == 1 ? QString() : QStringLiteral("s") )
				.arg( secs )
				.arg( secs == 1 ? QString() : QStringLiteral("s") );
		}
		else
		{
			timeDesc = QStringLiteral( "%1 second%2" )
				.arg( secs )
				.arg( secs == 1 ? QString() : QStringLiteral("s") );
		}
		return QCoreApplication::translate("PowerControlFeaturePlugin",
			"Synapse will power this computer down in %1.\n\nPlease save your work and close all programs.")
			.arg( timeDesc );
	};

	m_clientShutdownDialog = new ClientShutdownCountdownDialog();
	m_clientShutdownDialog->setWindowTitle( tr( "Synapse Power Down" ) );
	m_clientShutdownDialog->setFixedSize( 480, 180 );
	m_clientShutdownDialog->setStyleSheet( QStringLiteral(
		"QDialog { background-color: #0B1324; border: 2px solid #2563EB; border-radius: 12px; }"
		"QLabel { color: #FFFFFF; font-size: 13px; font-weight: 500; font-family: 'Segoe UI', Arial, sans-serif; }"
		"QProgressBar { background-color: #1E293B; border: 1px solid #334155; border-radius: 4px; }"
		"QProgressBar::chunk { background-color: #EF4444; border-radius: 3px; }"
	) );

	if( auto* screen = QGuiApplication::primaryScreen() )
	{
		const auto center = screen->geometry().center();
		m_clientShutdownDialog->move( center.x() - 240, center.y() - 90 );
	}

	auto* layout = new QVBoxLayout( m_clientShutdownDialog );
	layout->setContentsMargins( 24, 16, 24, 20 );
	layout->setSpacing( 12 );

	auto* headerBar = new QWidget( m_clientShutdownDialog );
	auto* headerLayout = new QHBoxLayout( headerBar );
	headerLayout->setContentsMargins( 0, 0, 0, 0 );
	auto* headerTitle = new QLabel( QStringLiteral( "⏻  Synapse Power Down" ), headerBar );
	headerTitle->setStyleSheet( QStringLiteral( "color: #60A5FA; font-size: 13px; font-weight: 700; font-family: 'Segoe UI', Arial, sans-serif; letter-spacing: 0.5px;" ) );
	headerLayout->addWidget( headerTitle );
	headerLayout->addStretch();
	layout->addWidget( headerBar );

	m_clientLabel = new QLabel( m_clientShutdownDialog );
	m_clientLabel->setWordWrap( true );
	m_clientLabel->setAlignment( Qt::AlignCenter );
	layout->addWidget( m_clientLabel );

	auto* progressHeader = new QWidget( m_clientShutdownDialog );
	auto* progressHeaderLayout = new QHBoxLayout( progressHeader );
	progressHeaderLayout->setContentsMargins( 0, 0, 0, 0 );
	auto* pTitle = new QLabel( tr( "Shutdown Countdown" ), progressHeader );
	pTitle->setStyleSheet( QStringLiteral( "color: #94A3B8; font-size: 11px; font-weight: 600;" ) );
	m_clientRemainingTimeLabel = new QLabel( progressHeader );
	m_clientRemainingTimeLabel->setStyleSheet( QStringLiteral( "color: #F87171; font-size: 11px; font-weight: 700; font-family: monospace;" ) );
	progressHeaderLayout->addWidget( pTitle );
	progressHeaderLayout->addStretch();
	progressHeaderLayout->addWidget( m_clientRemainingTimeLabel );
	layout->addWidget( progressHeader );

	m_clientProgressBar = new QProgressBar( m_clientShutdownDialog );
	m_clientProgressBar->setTextVisible( false );
	m_clientProgressBar->setFixedHeight( 8 );
	m_clientProgressBar->setMaximum( shutdownTimeout );
	m_clientProgressBar->setValue( 0 );
	layout->addWidget( m_clientProgressBar );

	auto updateDialogState = [this, formatClientShutdownMsg]() {
		if( m_clientProgressBar && m_clientLabel )
		{
			m_clientProgressBar->setValue( m_clientProgressBar->maximum() - m_clientRemainingSeconds );
			const int mins = m_clientRemainingSeconds / 60;
			const int secs = m_clientRemainingSeconds % 60;
			if( m_clientRemainingTimeLabel )
			{
				m_clientRemainingTimeLabel->setText( QStringLiteral("%1:%2 remaining")
					.arg( mins, 2, 10, QLatin1Char('0') )
					.arg( secs, 2, 10, QLatin1Char('0') ) );
			}
			m_clientLabel->setText( formatClientShutdownMsg( m_clientRemainingSeconds ) );
		}
	};

	updateDialogState();
	m_clientShutdownDialog->show();
	VeyonCore::platform().coreFunctions().raiseWindow( m_clientShutdownDialog, true );

#ifdef Q_OS_WIN
	HWND hwnd = reinterpret_cast<HWND>( m_clientShutdownDialog->winId() );
	if( hwnd )
	{
		SetWindowPos( hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW );
		SetForegroundWindow( hwnd );
		HMENU hMenu = GetSystemMenu( hwnd, FALSE );
		if( hMenu )
		{
			EnableMenuItem( hMenu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED );
			RemoveMenu( hMenu, SC_CLOSE, MF_BYCOMMAND );
		}
	}
	setTaskManagerDisabled( true );
#endif

	if( m_clientShutdownTimer == nullptr )
	{
		m_clientShutdownTimer = new QTimer( this );
		connect( m_clientShutdownTimer, &QTimer::timeout, this, [this, updateDialogState]() {
			m_clientRemainingSeconds--;
#ifdef Q_OS_WIN
			setTaskManagerDisabled( true );
#endif
			if( m_clientRemainingSeconds <= 0 )
			{
#ifdef Q_OS_WIN
				setTaskManagerDisabled( true );
				BlockInput( TRUE );
#endif
				if( m_clientShutdownDialog && m_clientLabel )
				{
					m_clientLabel->setText( tr( "Time has expired! Shutting down this computer now..." ) );
					m_clientShutdownDialog->show();
					VeyonCore::platform().coreFunctions().raiseWindow( m_clientShutdownDialog, true );
				}
				VeyonCore::platform().coreFunctions().powerDown( false );
				if( m_clientShutdownTimer )
				{
					m_clientShutdownTimer->setInterval( 2000 );
				}
			}
			else
			{
				updateDialogState();
				if( m_clientShutdownDialog )
				{
					if( m_clientShutdownDialog->isHidden() )
					{
						m_clientShutdownDialog->show();
					}
					VeyonCore::platform().coreFunctions().raiseWindow( m_clientShutdownDialog, true );
				}
			}
		} );
	}
	m_clientShutdownTimer->start( 1000 );
}



void PowerControlFeaturePlugin::showMasterPowerDownController( VeyonMasterInterface& master,
															   int initialSeconds,
															   const ComputerControlInterfaceList& computerControlInterfaces )
{
	if( m_masterControllerDialog )
	{
		m_masterControllerDialog->close();
		m_masterControllerDialog->deleteLater();
		m_masterControllerDialog = nullptr;
	}

	m_masterRemainingSeconds = initialSeconds;

	auto adjustHandler = [this, computerControlInterfaces]( int newSeconds ) {
		m_masterRemainingSeconds = newSeconds;
		controlFeature( m_powerDownDelayedFeature.uid(), Operation::Start,
						{{argToString(Argument::ShutdownTimeout), m_masterRemainingSeconds},
						 {argToString(Argument::Cancel), false}},
						computerControlInterfaces );
	};

	auto cancelHandler = [this, computerControlInterfaces]() {
		controlFeature( m_powerDownDelayedFeature.uid(), Operation::Start,
						{{argToString(Argument::ShutdownTimeout), -1},
						 {argToString(Argument::Cancel), true}},
						computerControlInterfaces );
	};

	master.notifyCountdown( tr( "Powering Down Computers" ),
							initialSeconds,
							adjustHandler,
							cancelHandler,
							computerControlInterfaces );
}
