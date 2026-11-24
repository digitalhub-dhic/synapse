/*
 *  LockWidget.cpp - widget for locking a client
 *
 *  Copyright (c) 2006-2026 Tobias Junghans <tobydox@veyon.io>
 *
 *  This file is part of Veyon - https://veyon.io
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */

#include "LockWidget.h"
#include "PlatformCoreFunctions.h"
#include "PlatformInputDeviceFunctions.h"

#include <QApplication>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QWindow>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif


LockWidget::LockWidget( Mode mode, const QPixmap& background, QWidget* parent, const QString& message, int durationSeconds ) :
	QWidget( parent, Qt::X11BypassWindowManagerHint ),
	m_background( background ),
	m_desktopGrab(),
	m_mode( mode ),
	m_message( message )
{
	// Load Client Customization settings
	m_labTitle = QStringLiteral("DIGIHUB LAB");
	QFile configFile( QStringLiteral("C:/ProgramData/DigiAgent/client_config.json") );
	if( configFile.open( QIODevice::ReadOnly ) )
	{
		const auto doc = QJsonDocument::fromJson( configFile.readAll() );
		if( doc.isObject() )
		{
			const auto obj = doc.object();
			if( obj.contains( QStringLiteral("title") ) && !obj.value( QStringLiteral("title") ).toString().isEmpty() )
			{
				m_labTitle = obj.value( QStringLiteral("title") ).toString();
			}
			if( m_message.isEmpty() && obj.contains( QStringLiteral("message") ) && !obj.value( QStringLiteral("message") ).toString().isEmpty() )
			{
				m_message = obj.value( QStringLiteral("message") ).toString();
			}
		}
		configFile.close();
	}

	if( m_message.isEmpty() )
	{
		m_message = tr("Screen locked by instructor. Please pay attention to the front.");
	}

	// Distinct Timed Lock: live countdown ONLY active when durationSeconds > 0!
	if( durationSeconds > 0 )
	{
		m_hasTimer = true;
		m_countdownSeconds = durationSeconds;

		m_countdownTimer = new QTimer( this );
		m_countdownTimer->setInterval( 1000 );
		connect( m_countdownTimer, &QTimer::timeout, this, [this]() {
			if( m_countdownSeconds > 0 )
			{
				m_countdownSeconds--;
				update();
				if( m_countdownSeconds == 0 )
				{
					// Workstations automatically unlock when the timer reaches 00:00!
					m_countdownTimer->stop();
#ifdef Q_OS_WIN
					ClipCursor( NULL );
					BlockInput( FALSE );
#endif
					VeyonCore::platform().inputDeviceFunctions().enableInputDevices();
					VeyonCore::platform().coreFunctions().setSystemUiState( true );
					Q_EMIT expired();
				}
			}
		});
		m_countdownTimer->start();
	}
	else
	{
		m_hasTimer = false;
		m_countdownSeconds = 0;
	}

	auto leftMostScreen = QGuiApplication::primaryScreen();
	int minimumX = 0;
	const auto screens = QGuiApplication::screens();
	for (auto* screen : screens)
	{
		if (screen->geometry().x() < minimumX)
		{
			minimumX = screen->geometry().x();
			leftMostScreen = screen;
		}
	}

	m_desktopGrab = leftMostScreen->grabWindow(0);
#ifdef Q_OS_WIN
	if (m_desktopGrab.isNull() || m_desktopGrab.width() <= 1 || m_desktopGrab.height() <= 1)
	{
		HDC hdcScreen = GetDC(NULL);
		int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
		int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
		if (w > 0 && h > 0)
		{
			HDC hdcMem = CreateCompatibleDC(hdcScreen);
			HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
			HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hbm);
			BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY);
			SelectObject(hdcMem, hOld);
			DeleteDC(hdcMem);
			m_desktopGrab = QPixmap::fromImage(QImage::fromHBITMAP(hbm));
			DeleteObject(hbm);
		}
		ReleaseDC(NULL, hdcScreen);
	}
#endif
	if (mode == DesktopVisible)
	{
		m_background = m_desktopGrab;
	}

	VeyonCore::platform().coreFunctions().setSystemUiState( false );
	VeyonCore::platform().inputDeviceFunctions().disableInputDevices();

	setWindowTitle( {} );

#ifdef Q_OS_LINUX
	show();
#endif
	move(leftMostScreen->geometry().topLeft());
#ifndef Q_OS_LINUX
	showFullScreen();
#endif
	windowHandle()->setScreen(leftMostScreen);
	setFixedSize(leftMostScreen->virtualSize());

	VeyonCore::platform().coreFunctions().raiseWindow(this, true);
#ifdef Q_OS_LINUX
	showFullScreen();
#endif

	setFocusPolicy( Qt::StrongFocus );
	setFocus();
	grabMouse();
	grabKeyboard();
	setCursor( Qt::BlankCursor );
	QGuiApplication::setOverrideCursor( Qt::BlankCursor );

	QCursor::setPos( mapToGlobal( QPoint( 0, 0 ) ) );

#ifdef Q_OS_WIN
	RECT lockRect = { 0, 0, 1, 1 };
	ClipCursor( &lockRect );
	BlockInput( TRUE );

	HWND hwnd = reinterpret_cast<HWND>( winId() );
	if( hwnd )
	{
		HMENU hMenu = GetSystemMenu( hwnd, FALSE );
		if( hMenu )
		{
			EnableMenuItem( hMenu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED );
			RemoveMenu( hMenu, SC_CLOSE, MF_BYCOMMAND );
		}
	}
#endif
}



LockWidget::~LockWidget()
{
	releaseMouse();
	releaseKeyboard();
	unsetCursor();

#ifdef Q_OS_WIN
	ClipCursor( NULL );
	BlockInput( FALSE );
#endif

	VeyonCore::platform().inputDeviceFunctions().enableInputDevices();
	VeyonCore::platform().coreFunctions().setSystemUiState( true );

	QGuiApplication::restoreOverrideCursor();
}



void LockWidget::closeEvent( QCloseEvent *event )
{
	event->ignore();
}



void LockWidget::keyPressEvent( QKeyEvent *event )
{
	event->ignore();
}



void LockWidget::keyReleaseEvent( QKeyEvent *event )
{
	event->ignore();
}



void LockWidget::paintEvent( QPaintEvent* event )
{
	Q_UNUSED(event);

	QPainter p( this );
	switch( m_mode )
	{
	case DesktopVisible:
		if (!m_desktopGrab.isNull())
		{
			p.drawPixmap( rect(), m_desktopGrab );
		}
		else
		{
			p.drawPixmap( rect(), m_background );
		}
		break;

	case BackgroundPixmap:
	case TextMessage:
		if (!m_desktopGrab.isNull())
		{
			p.drawPixmap( rect(), m_desktopGrab );
		}
		else
		{
			p.fillRect( rect(), QColor( 5, 7, 10 ) );
		}

		{
			int cardW = qMin( width() - 80, 840 );
			int cardH = qMin( height() - 80, m_hasTimer ? 380 : 300 );
			QRect cardRect( (width() - cardW) / 2, (height() - cardH) / 2, cardW, cardH );

			p.setRenderHint( QPainter::Antialiasing, true );
			p.setBrush( QColor( 13, 17, 23, 235 ) );
			p.setPen( QPen( QColor( 46, 160, 67, 190 ), 2 ) );
			p.drawRoundedRect( cardRect, 20, 20 );

			// Title badge
			QRect badgeRect( cardRect.x() + (cardW - 220) / 2, cardRect.y() + 24, 220, 32 );
			p.setBrush( QColor( 35, 134, 54, 70 ) );
			p.setPen( QPen( QColor( 46, 160, 67, 140 ), 1 ) );
			p.drawRoundedRect( badgeRect, 16, 16 );
			p.setPen( QColor( 46, 160, 67 ) );
			p.setFont( QFont( p.font().family(), 11, QFont::Bold ) );
			p.drawText( badgeRect, Qt::AlignCenter, m_labTitle.toUpper() );

			// Message
			QRect msgRect( cardRect.x() + 30, cardRect.y() + 68, cardW - 60, m_hasTimer ? 120 : 160 );
			p.setPen( Qt::white );
			p.setFont( QFont( p.font().family(), 24, QFont::DemiBold ) );
			p.drawText( msgRect, Qt::AlignCenter | Qt::TextWordWrap, m_message );

			// Countdown timer (Displayed ONLY when Timed Lock / Focus Session is active)
			if( m_hasTimer )
			{
				int mins = m_countdownSeconds / 60;
				int secs = m_countdownSeconds % 60;
				QString timerStr = QStringLiteral("FOCUS SESSION • %1:%2 REMAINING")
					.arg( mins, 2, 10, QLatin1Char('0') )
					.arg( secs, 2, 10, QLatin1Char('0') );

				QRect timerBox( cardRect.x() + (cardW - 320) / 2, cardRect.y() + 200, 320, 50 );
				p.setBrush( QColor( 22, 27, 34, 240 ) );
				p.setPen( QPen( QColor( 59, 130, 246, 200 ), 1.5 ) );
				p.drawRoundedRect( timerBox, 14, 14 );

				p.setPen( QColor( 96, 165, 250 ) );
				p.setFont( QFont( p.font().family(), 15, QFont::Bold ) );
				p.drawText( timerBox, Qt::AlignCenter, timerStr );
			}

			// Subtitle footer
			QRect subRect( cardRect.x() + 30, cardRect.bottom() - 42, cardW - 60, 24 );
			p.setPen( QColor( 139, 148, 158 ) );
			p.setFont( QFont( p.font().family(), 11, QFont::Normal ) );
			p.drawText( subRect, Qt::AlignCenter, m_hasTimer ?
				tr("⏱️ Timed Focus Session in progress • Screen will auto-unlock at 00:00") :
				tr("🔒 Workstation locked by teacher • Mouse and keyboard disabled") );
		}
		break;

	default:
		break;
	}
}
