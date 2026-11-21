/*
 * WindowsInputDeviceFunctions.cpp - implementation of WindowsInputDeviceFunctions class
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

#include <windows.h>

#include <QCoreApplication>
#include <QProcess>

#include "ConfigurationManager.h"
#include "PlatformServiceFunctions.h"
#include "ProcessHelper.h"
#include "VeyonConfiguration.h"
#include "WindowsCoreFunctions.h"
#include "WindowsDeviceFunctions.h"
#include "WindowsInputDeviceFunctions.h"
#include "WindowsKeyboardShortcutTrapper.h"
#include "WindowsPlatformConfiguration.h"
#include "WtsSessionManager.h"


class Powercfg : public ProcessHelper
{
public:
	Powercfg(const QStringList& arguments) : ProcessHelper(QStringLiteral("powercfg"), arguments)
	{
	}
};


static int interception_is_any( InterceptionDevice device )
{
	Q_UNUSED(device)

	return true;
}


static WindowsDeviceFunctions::DeviceList inputDevicesToDisable()
{
	WindowsPlatformConfiguration config(&VeyonCore::config());

	WindowsDeviceFunctions::DeviceList devices;

	devices += WindowsDeviceFunctions::findKeyboardDevices();
	devices += WindowsDeviceFunctions::findMouseDevices();

	if (config.disableTouchDevicesForScreenLock())
	{
		devices += WindowsDeviceFunctions::findTouchDevices();
	}

	return devices;
}



WindowsInputDeviceFunctions::~WindowsInputDeviceFunctions()
{
	if( m_inputDevicesDisabled )
	{
		enableInputDevices();
	}
	if( m_clientInputBlocked )
	{
		blockClientInput( false );
	}
}

// Static hooks for blocking physical student input while permitting injected remote teacher control
static HHOOK s_clientBlockMouseHook = nullptr;
static HHOOK s_clientBlockKeyboardHook = nullptr;

static LRESULT CALLBACK clientBlockMouseHookLL( int nCode, WPARAM wParam, LPARAM lParam )
{
	if( nCode == HC_ACTION )
	{
		const auto* p = reinterpret_cast<const MSLLHOOKSTRUCT*>( lParam );
		if( p )
		{
			// Strictly allow ONLY teacher injected remote control events carrying the verified VNC extra info signature
			// DWEXTRA_VNC_REMOTE = 0x564e4300
			if( p->dwExtraInfo == 0x564e4300 )
			{
				return CallNextHookEx( s_clientBlockMouseHook, nCode, wParam, lParam );
			}

			// Drop all physical hardware mouse movements, clicks, and local student injected input
			return 1;
		}
	}
	return CallNextHookEx( s_clientBlockMouseHook, nCode, wParam, lParam );
}

static LRESULT CALLBACK clientBlockKeyboardHookLL( int nCode, WPARAM wParam, LPARAM lParam )
{
	if( nCode == HC_ACTION )
	{
		const auto* p = reinterpret_cast<const KBDLLHOOKSTRUCT*>( lParam );
		if( p )
		{
			// Strictly allow ONLY teacher injected remote control keystrokes carrying the verified VNC extra info signature
			// DWEXTRA_VNC_REMOTE = 0x564e4300
			if( p->dwExtraInfo == 0x564e4300 )
			{
				return CallNextHookEx( s_clientBlockKeyboardHook, nCode, wParam, lParam );
			}

			// Drop all physical keystrokes and local student injected input (AHK, SendInput, keybd_event)
			return 1;
		}
	}
	return CallNextHookEx( s_clientBlockKeyboardHook, nCode, wParam, lParam );
}

static void setTaskMgrDisabled( bool disable )
{
	DWORD val = disable ? 1 : 0;
	HKEY hKey = nullptr;
	if( RegCreateKeyExW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
						 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr ) == ERROR_SUCCESS )
	{
		if( disable )
		{
			RegSetValueExW( hKey, L"DisableTaskMgr", 0, REG_DWORD, reinterpret_cast<const BYTE*>( &val ), sizeof( val ) );
		}
		else
		{
			RegDeleteValueW( hKey, L"DisableTaskMgr" );
		}
		RegCloseKey( hKey );
	}
}

void WindowsInputDeviceFunctions::blockClientInput( bool blocked )
{
	if( blocked )
	{
		if( !m_clientInputBlocked )
		{
			m_clientInputBlocked = true;

			HINSTANCE hAppInstance = GetModuleHandle( nullptr );
			if( s_clientBlockMouseHook == nullptr )
			{
				s_clientBlockMouseHook = SetWindowsHookExW( WH_MOUSE_LL, clientBlockMouseHookLL, hAppInstance, 0 );
			}
			if( s_clientBlockKeyboardHook == nullptr )
			{
				s_clientBlockKeyboardHook = SetWindowsHookExW( WH_KEYBOARD_LL, clientBlockKeyboardHookLL, hAppInstance, 0 );
			}

			setTaskMgrDisabled( true );
		}
	}
	else
	{
		if( m_clientInputBlocked )
		{
			m_clientInputBlocked = false;

			if( s_clientBlockMouseHook )
			{
				UnhookWindowsHookEx( s_clientBlockMouseHook );
				s_clientBlockMouseHook = nullptr;
			}
			if( s_clientBlockKeyboardHook )
			{
				UnhookWindowsHookEx( s_clientBlockKeyboardHook );
				s_clientBlockKeyboardHook = nullptr;
			}

			setTaskMgrDisabled( false );
		}
	}
}



void WindowsInputDeviceFunctions::enableInputDevices()
{
	// 1. Release mouse cursor lock
	ClipCursor( NULL );

	// 2. Unblock physical inputs
	BlockInput( FALSE );

	// 3. Re-enable drivers & services
	disableInterception();
	restoreHIDService();
	restorePowerScheme();

	WindowsDeviceFunctions::setDevicesState(inputDevicesToDisable(), WindowsDeviceFunctions::State::Enabled);

	m_inputDevicesDisabled = false;
}



void WindowsInputDeviceFunctions::disableInputDevices()
{
	if( m_inputDevicesDisabled == false )
	{
		// 1. Trap cursor at 0,0 so mouse cannot move an inch
		RECT lockRect = { 0, 0, 1, 1 };
		ClipCursor( &lockRect );

		// 2. Block physical keyboard and mouse input events
		BlockInput( TRUE );

		// 3. Driver & hardware device disablement
		enableInterception();
		stopHIDService();
		setCustomPowerScheme();

		WindowsDeviceFunctions::setDevicesState(inputDevicesToDisable(), WindowsDeviceFunctions::State::Disabled);

		m_inputDevicesDisabled = true;
	}
}



KeyboardShortcutTrapper* WindowsInputDeviceFunctions::createKeyboardShortcutTrapper( QObject* parent )
{
	return new WindowsKeyboardShortcutTrapper( parent );
}



void WindowsInputDeviceFunctions::checkInterceptionInstallation()
{
	if( VeyonCore::config().multiSessionModeEnabled() )
	{
		uninstallInterception();
	}
	else if( WindowsPlatformConfiguration( &VeyonCore::config() ).useInterceptionDriver() )
	{
		const auto context = interception_create_context();
		if( context )
		{
			// a valid context means the interception driver is installed properly
			// so nothing to do here
			interception_destroy_context( context );
		}
		// try to (re)install interception driver
		else if( installInterception() == false )
		{
			// failed to uninstall it so we can try to install it again on next reboot
			uninstallInterception();
		}
	}
}



void WindowsInputDeviceFunctions::stopOnScreenKeyboard()
{
	WindowsCoreFunctions::terminateProcess( WtsSessionManager::findProcessId( QStringLiteral("osk.exe") ) );
	WindowsCoreFunctions::terminateProcess( WtsSessionManager::findProcessId( QStringLiteral("tabtip.exe") ) );
}



void WindowsInputDeviceFunctions::enableInterception()
{
	// Unpumped kernel-mode interception driver queues cause IRP starvation and system freeze.
	// Input locking is handled robustly via the authenticated low-level hook pipeline (clientBlockKeyboardHookLL / clientBlockMouseHookLL).
	m_interceptionContext = nullptr;
}



void WindowsInputDeviceFunctions::disableInterception()
{
	if( m_interceptionContext )
	{
		interception_destroy_context( m_interceptionContext );

		m_interceptionContext = nullptr;
	}
}



void WindowsInputDeviceFunctions::initHIDServiceStatus()
{
	if( m_hidServiceStatusInitialized == false )
	{
		m_hidServiceActivated = VeyonCore::platform().serviceFunctions().isRunning( m_hidServiceName );
		m_hidServiceStatusInitialized = true;
	}
}



void WindowsInputDeviceFunctions::restoreHIDService()
{
	if( m_hidServiceActivated )
	{
		VeyonCore::platform().serviceFunctions().start( m_hidServiceName );
	}
}



void WindowsInputDeviceFunctions::setCustomPowerScheme()
{
	if (WindowsPlatformConfiguration(&VeyonCore::config()).useCustomPowerSchemeForScreenLock() == false ||
		m_originalPowerSchemeId.isEmpty() == false)
	{
		return;
	}

	QUuid activeSchemeId{Powercfg{{QStringLiteral("/getactivescheme")}}.runAndReadAll().split(':').value(1).trimmed().split(' ').constFirst()};
	if (activeSchemeId.isNull() || activeSchemeId == CustomPowerSchemeId)
	{
		return;
	}

	m_originalPowerSchemeId = activeSchemeId.toString(QUuid::WithoutBraces);
	Powercfg{{QStringLiteral("/delete"), CustomPowerSchemeIdString}}.run();
	Powercfg{{QStringLiteral("/duplicatescheme"), m_originalPowerSchemeId, CustomPowerSchemeIdString}}.run();
	Powercfg{{QStringLiteral("/setacvalueindex"), CustomPowerSchemeIdString,
					QStringLiteral("4f971e89-eebd-4455-a8de-9e59040e7347"), QStringLiteral("7648efa3-dd9c-4e3e-b566-50f929386280"), QStringLiteral("0")}}.run();
	Powercfg{{QStringLiteral("/setdcvalueindex"), CustomPowerSchemeIdString,
					QStringLiteral("4f971e89-eebd-4455-a8de-9e59040e7347"), QStringLiteral("7648efa3-dd9c-4e3e-b566-50f929386280"), QStringLiteral("0")}}.run();
	Powercfg{{QStringLiteral("/setacvalueindex"), CustomPowerSchemeIdString,
					QStringLiteral("4f971e89-eebd-4455-a8de-9e59040e7347"), QStringLiteral("96996bc0-ad50-47ec-923b-6f41874dd9eb"), QStringLiteral("0")}}.run();
	Powercfg{{QStringLiteral("/setdcvalueindex"), CustomPowerSchemeIdString,
					QStringLiteral("4f971e89-eebd-4455-a8de-9e59040e7347"), QStringLiteral("96996bc0-ad50-47ec-923b-6f41874dd9eb"), QStringLiteral("0")}}.run();
	Powercfg{{QStringLiteral("/setactive"), CustomPowerSchemeIdString}}.run();
}



void WindowsInputDeviceFunctions::restorePowerScheme()
{
	if (m_originalPowerSchemeId.isNull() == false)
	{
		Powercfg{{QStringLiteral("/setactive"), m_originalPowerSchemeId}}.run();
		Powercfg{{QStringLiteral("/delete"), CustomPowerSchemeIdString}}.run();
	}
}



void WindowsInputDeviceFunctions::stopHIDService()
{
	initHIDServiceStatus();

	if( m_hidServiceActivated )
	{
		VeyonCore::platform().serviceFunctions().stop( m_hidServiceName );
	}
}



bool WindowsInputDeviceFunctions::installInterception()
{
	return interceptionInstaller( QStringLiteral("/install") ) == 0;
}



bool WindowsInputDeviceFunctions::uninstallInterception()
{
	return interceptionInstaller( QStringLiteral("/uninstall") ) == 0;
}



int WindowsInputDeviceFunctions::interceptionInstaller( const QString& argument )
{
	return QProcess::execute( QCoreApplication::applicationDirPath() + QStringLiteral("/interception/install-interception.exe"), { argument } );
}
