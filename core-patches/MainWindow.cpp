#include "ComputerMonitoringModel.h"
#include <QBuffer>
#include <QComboBox>
#include <QShortcut>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPainter>
#include <QPainterPath>
/*
 * MainWindow.cpp - implementation of MainWindow class
 *
 * Copyright (c) 2004-2026 Tobias Junghans <tobydox@veyon.io>
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

#include <QCloseEvent>
#include <QShowEvent>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QHostAddress>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QSplitter>
#include <QDateTime>
#include <QDir>
#include <QTimer>

#include "AboutDialog.h"
#include "AccessControlProvider.h"
#include "MainWindow.h"
#include <cmath>
#include "BuiltinFeatures.h"
#include "AuthenticationCredentials.h"
#include "ComputerControlListModel.h"
#include "ComputerManager.h"
#include "ComputerInspectorPanel.h"
#include "ComputerSelectPanel.h"
#include "ScreenshotManagementPanel.h"
#include "FeatureManager.h"
#include "Filesystem.h"
#include "MonitoringMode.h"
#include "NetworkObjectDirectory.h"
#include "NetworkObjectDirectoryManager.h"
#include "PlatformUserFunctions.h"
#include "SlideshowPanel.h"
#include "SpotlightPanel.h"
#include "ToolButton.h"
#include "VeyonConfiguration.h"
#include "VeyonMaster.h"
#include "UserConfig.h"

#include "ui_MainWindow.h"


MainWindow::MainWindow( VeyonMaster &masterCore, QWidget* parent ) :
	QMainWindow( parent ),
	ui( new Ui::MainWindow ),
	m_master( masterCore ),
	m_modeGroup( new QButtonGroup( this ) )
{
	ui->setupUi( this );

#ifdef Q_OS_WIN
	typedef HRESULT (WINAPI *pfnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
	HMODULE hDwm = LoadLibraryA("dwmapi.dll");
	if (hDwm)
	{
		auto pDwmSetWindowAttribute = reinterpret_cast<pfnDwmSetWindowAttribute>(GetProcAddress(hDwm, "DwmSetWindowAttribute"));
		if (pDwmSetWindowAttribute)
		{
			BOOL darkMode = FALSE;
			pDwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));
		}
	}
#endif

	QPalette sagePalette;
	sagePalette.setColor( QPalette::Window, QColor( 0xF4, 0xF5, 0xF7 ) ); // #F4F5F7 Warm Alabaster
	sagePalette.setColor( QPalette::WindowText, QColor( 0x18, 0x18, 0x1B ) ); // #18181B
	sagePalette.setColor( QPalette::Base, QColor( 0xFF, 0xFF, 0xFF ) ); // #FFFFFF
	sagePalette.setColor( QPalette::AlternateBase, QColor( 0xF9, 0xFA, 0xFB ) ); // #F9FAFB
	sagePalette.setColor( QPalette::ToolTipBase, QColor( 0x18, 0x18, 0x1B ) );
	sagePalette.setColor( QPalette::ToolTipText, QColor( 0xFF, 0xFF, 0xFF ) );
	sagePalette.setColor( QPalette::Text, QColor( 0x18, 0x18, 0x1B ) );
	sagePalette.setColor( QPalette::Button, QColor( 0xFF, 0xFF, 0xFF ) );
	sagePalette.setColor( QPalette::ButtonText, QColor( 0x18, 0x18, 0x1B ) );
	sagePalette.setColor( QPalette::BrightText, Qt::white );
	sagePalette.setColor( QPalette::Highlight, QColor( 0x6D, 0x28, 0xD9 ) ); // #6D28D9 Botanical Sage
	sagePalette.setColor( QPalette::HighlightedText, Qt::white );
	setPalette( sagePalette );

	// Warm Sage Enterprise Top Header Toolbar
	m_headerToolBar = new QToolBar( tr("Header"), this );
	m_headerToolBar->setObjectName( QStringLiteral("headerToolBar") );
	m_headerToolBar->setMovable( false );
	m_headerToolBar->setFloatable( false );
	m_headerToolBar->setFixedHeight( 52 );
	addToolBar( Qt::TopToolBarArea, m_headerToolBar );
	addToolBarBreak( Qt::TopToolBarArea );

	auto* logo = new QLabel( m_headerToolBar );
	logo->setFixedSize( 34, 34 );
	logo->setAlignment( Qt::AlignCenter );
	QPixmap logoPix( 28, 28 );
	logoPix.fill( Qt::transparent );
	{
		QPainter lp( &logoPix );
		lp.setRenderHint( QPainter::Antialiasing );
		lp.scale( 28.0 / 64.0, 28.0 / 64.0 );

		// Organic human brain silhouette (#6D28D9)
		QPainterPath brain;
		brain.moveTo( 30.0, 8.0 );
		brain.cubicTo( 25.0, 5.0, 19.0, 8.0, 18.0, 14.0 );
		brain.cubicTo( 11.0, 13.0, 7.0, 19.0, 9.0, 25.0 );
		brain.cubicTo( 4.0, 30.0, 7.0, 36.0, 10.0, 39.0 );
		brain.cubicTo( 6.0, 45.0, 9.0, 52.0, 16.0, 54.0 );
		brain.cubicTo( 17.0, 61.0, 25.0, 61.0, 31.0, 57.0 );
		brain.lineTo( 32.0, 12.0 );
		brain.closeSubpath();
		lp.setPen( Qt::NoPen );
		lp.setBrush( QColor( 0x6D, 0x28, 0xD9 ) );
		lp.drawPath( brain );

		// Clean neural folds (#F9FAFB)
		lp.setPen( QPen( QColor( 0xF9, 0xFA, 0xFB ), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
		lp.setBrush( Qt::NoBrush );
		QPainterPath fold1;
		fold1.moveTo( 27.0, 13.0 );
		fold1.cubicTo( 21.0, 11.0, 17.0, 16.0, 20.0, 21.0 );
		fold1.cubicTo( 22.0, 24.0, 26.0, 23.0, 27.0, 27.0 );
		lp.drawPath( fold1 );

		QPainterPath fold2;
		fold2.moveTo( 14.0, 44.0 );
		fold2.cubicTo( 17.0, 39.0, 23.0, 39.0, 26.0, 43.0 );
		fold2.cubicTo( 28.0, 46.0, 26.0, 49.0, 23.0, 51.0 );
		lp.drawPath( fold2 );

		// Rounded microchip core (#5B21B6) + amber processor highlight (#F59E0B)
		lp.setPen( Qt::NoPen );
		lp.setBrush( QColor( 0x5B, 0x21, 0xB6 ) );
		lp.drawRoundedRect( QRectF( 28.0, 20.0, 19.0, 25.0 ), 5.0, 5.0 );
		lp.setBrush( QColor( 0xF5, 0x9E, 0x0B ) );
		lp.drawRoundedRect( QRectF( 33.0, 27.0, 9.0, 11.0 ), 2.5, 2.5 );

		// Radiating PCB circuit traces (#6D28D9)
		lp.setPen( QPen( QColor( 0x6D, 0x28, 0xD9 ), 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
		lp.setBrush( Qt::NoBrush );
		QPainterPath t1; t1.moveTo( 41.0, 20.0 ); t1.lineTo( 47.0, 14.0 ); t1.lineTo( 52.0, 14.0 ); lp.drawPath( t1 );
		QPainterPath t2; t2.moveTo( 47.0, 32.0 ); t2.lineTo( 57.0, 32.0 ); lp.drawPath( t2 );
		QPainterPath t3; t3.moveTo( 46.0, 39.0 ); t3.lineTo( 52.0, 45.0 ); t3.lineTo( 56.0, 45.0 ); lp.drawPath( t3 );
		QPainterPath t4; t4.moveTo( 38.0, 45.0 ); t4.lineTo( 38.0, 54.0 ); lp.drawPath( t4 );

		// Amber circuit endpoints (#F59E0B)
		lp.setPen( Qt::NoPen );
		lp.setBrush( QColor( 0xF5, 0x9E, 0x0B ) );
		lp.drawEllipse( QPointF( 54.0, 14.0 ), 3.2, 3.2 );
		lp.drawEllipse( QPointF( 59.0, 32.0 ), 3.2, 3.2 );
		lp.drawEllipse( QPointF( 58.0, 45.0 ), 3.2, 3.2 );
		lp.drawEllipse( QPointF( 38.0, 56.0 ), 3.2, 3.2 );
	}
	logo->setPixmap( logoPix );
	{
		QIcon appWinIcon;
		appWinIcon.addPixmap( logoPix );
		appWinIcon.addPixmap( logoPix.scaled( 16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
		appWinIcon.addPixmap( logoPix.scaled( 32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
		appWinIcon.addPixmap( logoPix.scaled( 64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
		setWindowIcon( appWinIcon );
		if( qApp )
		{
			qApp->setWindowIcon( appWinIcon );
		}
	}
	logo->setObjectName( QStringLiteral("logoIconLabel") );
	logo->setStyleSheet( QStringLiteral(
		"background-color: #EDE9FE; border-radius: 8px; border: 1px solid #DDD6FE;"
	) );

	auto* titleWidget = new QWidget( m_headerToolBar );
	titleWidget->setMinimumSize( 240, 40 );
	titleWidget->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Fixed );
	auto* titleLayout = new QVBoxLayout( titleWidget );
	titleLayout->setContentsMargins( 10, 3, 0, 3 );
	titleLayout->setSpacing( 2 );
	auto* appTitle = new QLabel( QStringLiteral("Synapse Command"), titleWidget );
	appTitle->setFixedHeight( 18 );
	appTitle->setStyleSheet( QStringLiteral("font-size: 13px; font-weight: 700; color: #18181B; letter-spacing: 0.2px; background: transparent;") );
	auto* appSub = new QLabel( QStringLiteral("One network  •  Every classroom"), titleWidget );
	appSub->setFixedHeight( 14 );
	appSub->setStyleSheet( QStringLiteral("font-size: 10px; font-weight: 500; color: #6D28D9; letter-spacing: 0.2px; background: transparent;") );
	titleLayout->addWidget( appTitle );
	titleLayout->addWidget( appSub );

	m_headerToolBar->addWidget( logo );
	m_headerToolBar->addWidget( titleWidget );

	auto* headerSpacer = new QWidget( m_headerToolBar );
	headerSpacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
	m_headerToolBar->addWidget( headerSpacer );

	auto* topSearch = new QLineEdit( m_headerToolBar );
	topSearch->setObjectName( QStringLiteral("globalSearchEdit") );
	topSearch->setPlaceholderText( tr("Search computers...") );
	topSearch->setFixedWidth( 200 );
	topSearch->setFixedHeight( 28 );
		topSearch->setStyleSheet( QStringLiteral(
		"QLineEdit { background-color: #FFFFFF; border: 1px solid #E1E3E8; border-radius: 14px; padding: 4px 12px; color: #18181B; font-size: 11px; } "
		"QLineEdit:focus { border-color: #6D28D9; background-color: #F4F5F7; }"
	) );
	connect( topSearch, &QLineEdit::textChanged, this, [this]( const QString& t ) {
		ui->computerMonitoringWidget->setSearchFilter( t );
		if( ui->filterLineEdit->text() != t )
		{
			ui->filterLineEdit->setText( t );
		}
	} );
	// Ctrl+K shortcut to focus search bar
	auto* ctrlKShortcut = new QShortcut( QKeySequence( QStringLiteral("Ctrl+K") ), this );
	connect( ctrlKShortcut, &QShortcut::activated, topSearch, [topSearch]() {
		topSearch->setFocus();
		topSearch->selectAll();
	} );
	m_headerToolBar->addWidget( topSearch );

	auto* bellBtn = new QToolButton( m_headerToolBar );
	bellBtn->setObjectName( QStringLiteral("bellButton") );
	bellBtn->setFixedSize( 28, 28 );
	QPixmap bellPix( 20, 20 );
	bellPix.fill( Qt::transparent );
	{
		QPainter bp( &bellPix );
		bp.setRenderHint( QPainter::Antialiasing );
		bp.setPen( QPen( QColor( 0x18, 0x18, 0x1B ), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
		bp.setBrush( Qt::NoBrush );
		QPainterPath path;
		path.moveTo( 10, 4 );
		path.cubicTo( 13, 4, 14, 7, 14, 11 );
		path.lineTo( 16, 14 );
		path.lineTo( 4, 14 );
		path.lineTo( 6, 11 );
		path.cubicTo( 6, 7, 7, 4, 10, 4 );
		bp.drawPath( path );
		bp.drawLine( 10, 2, 10, 4 );
		bp.drawLine( 8, 16, 12, 16 );
	}
	bellBtn->setIcon( QIcon( bellPix ) );
	bellBtn->setIconSize( QSize( 16, 16 ) );
		bellBtn->setStyleSheet( QStringLiteral(
		"QToolButton#bellButton { background-color: #FFFFFF; border: 1px solid #E1E3E8; border-radius: 14px; color: #18181B; font-size: 12px; min-width: 28px; max-width: 28px; min-height: 28px; max-height: 28px; padding: 0px; } "
		"QToolButton#bellButton:hover { border-color: #6D28D9; background-color: #EDE9FE; color: #18181B; }"
	) );
	auto* bellDot = new QLabel( bellBtn );
	bellDot->setObjectName( QStringLiteral("bellDot") );
	bellDot->setFixedSize( 6, 6 );
	bellDot->setStyleSheet( QStringLiteral( "background-color: #DC2626; border-radius: 3px; border: 1px solid #F9FAFB;" ) );
	bellDot->move( 18, 3 );
	bellDot->hide();
	bellBtn->setToolTip( tr( "Fleet Alerts" ) );
	connect( bellBtn, &QToolButton::clicked, this, [this, bellBtn]() {
		auto* menu = new QMenu( this );
		menu->setAttribute( Qt::WA_DeleteOnClose );
		menu->setStyleSheet( QStringLiteral(
			"QMenu { background-color: #FFFFFF; border: 1px solid #F5F3FF; border-radius: 8px; padding: 6px; color: #18181B; }"
			"QMenu::item { padding: 6px 12px; border-radius: 4px; font-size: 11px; }"
			"QMenu::item:selected { background-color: #EDE9FE; }"
		) );

		// 1. Load live AI Sentinel alerts (if any) for 1-click teacher review & freeze
		QFile alertFile( QStringLiteral("C:/SynapseViolations/live_alerts.json") );
		if( alertFile.exists() && alertFile.open( QIODevice::ReadOnly ) )
		{
			const auto doc = QJsonDocument::fromJson( alertFile.readAll() );
			alertFile.close();
			if( doc.isArray() && !doc.array().isEmpty() )
			{
				auto* aiHdr = menu->addAction( tr( "AI SENTINEL ALERTS (Click to Freeze)" ) );
				aiHdr->setEnabled( false );
				for( const auto& item : doc.array() )
				{
					const auto obj = item.toObject();
					const QString cName = obj.value( QStringLiteral("computer_name") ).toString();
					const QString cIp = obj.value( QStringLiteral("ip") ).toString();
					const QString reason = obj.value( QStringLiteral("violation_type") ).toString();
					const int conf = qRound( obj.value( QStringLiteral("confidence") ).toDouble( 0.0 ) * 100.0 );
					const QString label = conf > 0
						? tr( "Freeze %1 (%2 - %3%)" ).arg( cName, reason ).arg( conf )
						: tr( "Freeze %1 (%2)" ).arg( cName, reason );

					menu->addAction( label, this, [this, cIp, reason]() {
						ComputerControlInterfaceList targets;
						for( const auto& ci : m_master.allComputerControlInterfaces() )
						{
							if( ci && ci->computer().hostAddress().toString() == cIp )
							{
								targets.append( ci );
								break;
							}
						}
						if( !targets.isEmpty() )
						{
							for( const auto& feat : VeyonCore::featureManager().features() )
							{
								if( feat.name().compare( QStringLiteral("ScreenLockWithMessage"), Qt::CaseInsensitive ) == 0 ||
									feat.name().compare( QStringLiteral("ScreenLock"), Qt::CaseInsensitive ) == 0 )
								{
									VeyonCore::featureManager().startFeature( m_master, feat, targets );
									break;
								}
							}
						}
					} );
				}
				menu->addAction( tr( "Clear AI Alerts" ), this, []() {
					QFile::remove( QStringLiteral("C:/SynapseViolations/live_alerts.json") );
				} );
				menu->addSeparator();
			}
		}

		const auto& all = m_master.allComputerControlInterfaces();
		int offlineCount = 0;
		for( const auto& ci : all )
		{
			if( ci && ci->state() != ComputerControlInterface::State::Connected )
			{
				offlineCount++;
				if( offlineCount <= 8 )
				{
					const QString name = ci->computerName().isEmpty() ? ci->computer().hostAddress().toString() : ci->computerName();
					menu->addAction( tr( "Offline: %1" ).arg( name ) );
				}
			}
		}
		if( offlineCount == 0 )
		{
			auto* okAct = menu->addAction( tr( "All %1 workstations online" ).arg( all.size() ) );
			okAct->setEnabled( false );
		}
		else if( offlineCount > 8 )
		{
			auto* moreAct = menu->addAction( tr( "...and %1 more offline" ).arg( offlineCount - 8 ) );
			moreAct->setEnabled( false );
		}
		menu->popup( bellBtn->mapToGlobal( QPoint( 0, bellBtn->height() + 4 ) ) );
	} );
	m_headerToolBar->addWidget( bellBtn );

	auto* settingsBtn = new QToolButton( m_headerToolBar );
	settingsBtn->setObjectName( QStringLiteral("settingsButton") );
	settingsBtn->setFixedSize( 28, 28 );
	QPixmap gearPix( 20, 20 );
	gearPix.fill( Qt::transparent );
	{
		QPainter gp( &gearPix );
		gp.setRenderHint( QPainter::Antialiasing );
		gp.setPen( QPen( QColor( 0x18, 0x18, 0x1B ), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
		gp.setBrush( Qt::NoBrush );
		gp.drawEllipse( QPointF( 10, 10 ), 4.5, 4.5 );
		for( int i = 0; i < 6; ++i )
		{
			double angle = i * 3.14159265358979323846 / 3.0;
			gp.drawLine( QPointF( 10 + 4.8 * cos(angle), 10 + 4.8 * sin(angle) ),
			             QPointF( 10 + 7.5 * cos(angle), 10 + 7.5 * sin(angle) ) );
		}
	}
	settingsBtn->setIcon( QIcon( gearPix ) );
	settingsBtn->setIconSize( QSize( 16, 16 ) );
		settingsBtn->setStyleSheet( QStringLiteral(
		"QToolButton#settingsButton { background-color: #FFFFFF; border: 1px solid #E1E3E8; border-radius: 14px; color: #18181B; font-size: 13px; min-width: 28px; max-width: 28px; min-height: 28px; max-height: 28px; padding: 0px; } "
		"QToolButton#settingsButton:hover { border-color: #6D28D9; background-color: #EDE9FE; color: #18181B; }"
	) );
	connect( settingsBtn, &QToolButton::clicked, this, &MainWindow::showAboutDialog );
	m_headerToolBar->addWidget( settingsBtn );

		setStyleSheet( QStringLiteral(
				"QDialog {"
		"   background-color: #F4F5F7;"
		"   color: #18181B;"
		"}"
		"QDialog QLabel, QDialog QCheckBox, QDialog QRadioButton {"
		"   color: #18181B;"
		"}"
		"QDialog QPlainTextEdit, QDialog QTextEdit {"
		"   background-color: #FFFFFF;"
		"   border: 1px solid #E1E3E8;"
		"   border-radius: 6px;"
		"   color: #18181B;"
		"   padding: 6px;"
		"}"
		"QDialog QLineEdit {"
		"   background-color: #FFFFFF;"
		"   border: 1px solid #E1E3E8;"
		"   border-radius: 6px;"
		"   color: #18181B;"
		"   padding: 4px 8px;"
		"}"
		"QDialog QPushButton {"
		"   background-color: #FFFFFF;"
		"   border: 1px solid #E1E3E8;"
		"   border-radius: 6px;"
		"   color: #18181B;"
		"   padding: 6px 14px;"
		"   font-weight: 600;"
		"}"
		"QDialog QPushButton:hover {"
		"   background-color: #EDE9FE;"
		"   border-color: #6D28D9;"
		"   color: #FFFFFF;"
		"}"
		"QDialog QPushButton:default {"
		"   background-color: #6D28D9;"
		"   border: 1px solid #6D28D9;"
		"   color: #FFFFFF;"
		"}"
		"QMainWindow, QWidget#centralWidget {"
		"   background-color: #F4F5F7;"
		"}"
		"QToolBar#headerToolBar {"
		"   background-color: #F4F5F7;"
		"   border-bottom: 1px solid #E1E3E8;"
		"   padding: 4px 14px;"
		"}"
		"QToolBar#headerToolBar QToolButton {"
		"   background-color: #FFFFFF;"
		"   border: 1px solid #E1E3E8;"
		"   border-radius: 14px;"
		"   min-width: 28px;"
		"   max-width: 28px;"
		"   min-height: 28px;"
		"   max-height: 28px;"
		"   padding: 0px;"
		"}"
		"QToolBar {"
		"   background-color: #F4F5F7;"
		"   border-bottom: 1px solid #E1E3E8;"
		"   padding: 6px 10px;"
		"   spacing: 6px;"
		"}"
		"QToolBar QToolButton#qt_toolbar_ext_button {"
		"   qproperty-visible: false;"
		"   width: 0px; max-width: 0px; min-width: 0px;"
		"   height: 0px; max-height: 0px; min-height: 0px;"
		"   border: none; background: transparent; padding: 0px; margin: 0px;"
		"}"
		"QToolBar#mainToolBar QToolButton, QToolBar#toolBar QToolButton {"
		"   background-color: #FFFFFF;"
		"   border: 1px solid #E1E3E8;"
		"   border-radius: 8px;"
		"   color: #18181B;"
		"   font-weight: 500;"
		"   font-size: 10px;"
		"   padding: 4px 4px 4px 4px;"
		"   min-width: 64px;"
		"   max-width: 74px;"
		"   min-height: 54px;"
		"   max-height: 58px;"
		"}"
		"QToolBar#mainToolBar QToolButton:hover, QToolBar#toolBar QToolButton:hover {"
		"   background-color: #EDE9FE;"
		"   border-color: #6D28D9;"
		"   color: #18181B;"
		"}"
		"QToolBar#mainToolBar QToolButton:pressed, QToolBar#toolBar QToolButton:pressed {"
		"   background-color: #6D28D9;"
		"   border-color: #6D28D9;"
		"   color: #FFFFFF;"
		"}"
		"QToolBar#mainToolBar QToolButton:checked, QToolBar#toolBar QToolButton:checked {"
		"   background-color: #6D28D9;"
		"   border: 1px solid #6D28D9;"
		"   color: #FFFFFF;"
		"   font-weight: 600;"
		"}"
		"QToolBar#mainToolBar QToolButton:disabled, QToolBar#toolBar QToolButton:disabled {"
		"   background-color: #F4F5F7;"
		"   border-color: #E1E3E8;"
		"   color: #52525B;"
		"}"
				"QToolBar::separator {"
		"   width: 1px;"
		"   height: 28px;"
		"   background-color: #E1E3E8;"
		"   border: none;"
		"   margin: 6px 4px;"
		"}"
		"QToolBar::separator:horizontal {"
		"   width: 1px;"
		"   height: 28px;"
		"   background-color: #E1E3E8;"
		"   border: none;"
		"   margin: 6px 4px;"
		"}"
		"QToolBar::separator:vertical {"
		"   height: 1px;"
		"   width: 28px;"
		"   background-color: #E1E3E8;"
		"   border: none;"
		"   margin: 4px 6px;"
		"}"
		"QToolTip {"
		"   background-color: #FFFFFF;"
		"   color: #18181B;"
		"   border: 1px solid #E1E3E8;"
		"   border-radius: 6px;"
		"   padding: 5px 8px;"
		"   font-size: 11px;"
		"}"
"QMenu {"
		"   background-color: #FFFFFF;"
		"   border: 1px solid #E1E3E8;"
		"   border-radius: 8px;"
		"   padding: 6px;"
		"}"
		"QMenu::item {"
		"   padding: 6px 24px;"
		"   border-radius: 4px;"
		"   color: #18181B;"
		"   font-size: 12px;"
		"}"
		"QMenu::item:selected {"
		"   background-color: #6D28D9;"
		"   color: #FFFFFF;"
		"}"
		"QMenu::separator {"
		"   height: 1px;"
		"   background-color: #E1E3E8;"
		"   margin: 4px 8px;"
		"}"
		"QStatusBar, QWidget#panelButtons {"
		"   background-color: #F4F5F7;"
		"   border-top: 1px solid #E1E3E8;"
		"   color: #52525B;"
		"   min-height: 38px;"
		"   padding: 2px 8px;"
		"}"
		"QStatusBar QToolButton, QWidget#panelButtons QToolButton {"
		"   background-color: #FFFFFF;"
		"   border: 1px solid #E1E3E8;"
		"   border-radius: 6px;"
		"   padding: 4px 8px;"
		"   color: #18181B;"
		"   font-size: 11px;"
		"   font-weight: 600;"
		"}"
		"QStatusBar QToolButton:hover, QWidget#panelButtons QToolButton:hover {"
		"   background-color: #EDE9FE;"
		"   border-color: #6D28D9;"
		"   color: #FFFFFF;"
		"}"
		"QStatusBar QToolButton:checked, QWidget#panelButtons QToolButton:checked {"
		"   background-color: #6D28D9;"
		"   border: 1px solid #6D28D9;"
		"   color: #FFFFFF;"
		"   font-weight: 600;"
		"}"
		"QStatusBar QLabel {"
		"   color: #52525B;"
		"   font-size: 11px;"
		"}"
		"QMainWindow QLineEdit, QStatusBar QLineEdit, QLineEdit#filterLineEdit, QLineEdit#globalSearchEdit {"
		"   background-color: #FFFFFF;"
		"   border: 1px solid #E1E3E8;"
		"   border-radius: 6px;"
		"   padding: 4px 10px;"
		"   color: #18181B;"
		"   font-size: 11px;"
		"}"
		"QStatusBar QLineEdit:focus, QLineEdit#filterLineEdit:focus, QLineEdit:focus {"
		"   border-color: #6D28D9;"
		"   background-color: #F4F5F7;"
		"}"
		"QStatusBar QSlider::groove:horizontal, QSlider::groove:horizontal {"
		"   height: 6px;"
		"   background: #FFFFFF;"
		"   border: 1px solid #E1E3E8;"
		"   border-radius: 3px;"
		"}"
		"QStatusBar QSlider::sub-page:horizontal, QSlider::sub-page:horizontal {"
		"   background: #6D28D9;"
		"   border-radius: 3px;"
		"}"
		"QStatusBar QSlider::handle:horizontal, QSlider::handle:horizontal {"
		"   background: #18181B;"
		"   border: 1px solid #6D28D9;"
		"   width: 14px;"
		"   margin-top: -5px;"
		"   margin-bottom: -5px;"
		"   border-radius: 7px;"
		"}"
		"QStatusBar QSlider::handle:horizontal:hover, QSlider::handle:horizontal:hover {"
		"   background: #FFFFFF;"
		"   border-color: #6D28D9;"
		"}"
				"QSplitter::handle {"
		"   background-color: transparent;"
		"}"
		"QSplitter::handle:horizontal {"
		"   width: 1px;"
		"   background-color: #E1E3E8;"
		"}"
		"QSplitter::handle:vertical {"
		"   height: 1px;"
		"   background-color: #E1E3E8;"
		"}"
		"QSplitter::handle:hover {"
		"   background-color: #E1E3E8;"
		"}"
"QTreeView, QListView, QTableView {"
		"   background-color: #F4F5F7;"
		"   color: #18181B;"
		"   border: none;"
		"   font-size: 11px;"
		"}"
		"QTreeView::item:hover, QListView::item:hover {"
		"   background-color: #EDE9FE;"
		"   color: #FFFFFF;"
		"}"
		"QTreeView::item:selected, QListView::item:selected {"
		"   background-color: #DCFCE7;"
		"   color: #FFFFFF;"
		"   font-weight: 600;"
		"}"
		"QHeaderView::section {"
		"   background-color: #FFFFFF;"
		"   color: #52525B;"
		"   border: 1px solid #E1E3E8;"
		"   padding: 4px;"
		"}"
		"QPushButton {"
		"   background-color: #FFFFFF;"
		"   border: 1px solid #E1E3E8;"
		"   border-radius: 6px;"
		"   padding: 4px 10px;"
		"   color: #18181B;"
		"   font-size: 11px;"
		"   font-weight: 500;"
		"}"
		"QPushButton:hover {"
		"   background-color: #EDE9FE;"
		"   border-color: #6D28D9;"
		"   color: #FFFFFF;"
		"}"
		"QPushButton:checked { background: #6D28D9; color: #FFFFFF; font-weight: bold; border-color: #6D28D9; }"
		"QScrollBar:vertical {"
		"   background: #F4F5F7;"
		"   width: 8px;"
		"   margin: 0px;"
		"}"
		"QScrollBar::handle:vertical {"
		"   background: #E1E3E8;"
		"   min-height: 20px;"
		"   border-radius: 4px;"
		"}"
		"QScrollBar::handle:vertical:hover {"
		"   background: #6D28D9;"
		"}"
		"QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
		"   height: 0px;"
		"}"
		"QScrollBar:horizontal {"
		"   background: #F4F5F7;"
		"   height: 8px;"
		"   margin: 0px;"
		"}"
		"QScrollBar::handle:horizontal {"
		"   background: #E1E3E8;"
		"   min-width: 20px;"
		"   border-radius: 4px;"
		"}"
		"QScrollBar::handle:horizontal:hover {"
		"   background: #6D28D9;"
		"}"
		"QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
		"   width: 0px;"
		"}"
		"QAbstractScrollArea::corner {"
		"   background: #F4F5F7;"
		"   border: none;"
		"}"
	) );

	applyCustomTheme();

	auto* themeWatcher = new QFileSystemWatcher( this );
	const QString themePath = QStringLiteral("C:/ProgramData/DigiAgent/custom_theme.qss");
	if( QFile::exists( themePath ) )
	{
		themeWatcher->addPath( themePath );
	}
	connect( themeWatcher, &QFileSystemWatcher::fileChanged, this, [this, themeWatcher, themePath]( const QString& ) {
		applyCustomTheme();
		if( !themeWatcher->files().contains( themePath ) && QFile::exists( themePath ) )
		{
			themeWatcher->addPath( themePath );
		}
	} );

	restoreState( QByteArray::fromBase64( m_master.userConfig().windowState().toUtf8() ), 2026 );
	restoreGeometry( QByteArray::fromBase64( m_master.userConfig().windowGeometry().toUtf8() ) );

	// Enforce 2-row toolbar layout: Row 1 = m_headerToolBar, Row 2 = ui->toolBar
	removeToolBar( m_headerToolBar );
	removeToolBar( ui->toolBar );
	addToolBar( Qt::TopToolBarArea, m_headerToolBar );
	addToolBarBreak( Qt::TopToolBarArea );
	addToolBar( Qt::TopToolBarArea, ui->toolBar );

	m_headerToolBar->setMovable( false );
	m_headerToolBar->setFloatable( false );
	m_headerToolBar->setVisible( true );
	m_headerToolBar->setFixedHeight( 52 );
	ui->toolBar->setMovable( false );
	ui->toolBar->setFloatable( false );
	ui->toolBar->setVisible( true );
	ui->toolBar->setFixedHeight( 74 );

	// add widgets to status bar
	auto* readyWidget = new QWidget( ui->statusBar );
	auto* readyLayout = new QHBoxLayout( readyWidget );
	readyLayout->setContentsMargins( 8, 0, 12, 0 );
	readyLayout->setSpacing( 6 );
	auto* readyDot = new QLabel( QStringLiteral("●"), readyWidget );
	readyDot->setObjectName( QStringLiteral("readyDot") );
	readyDot->setStyleSheet( QStringLiteral("color: #16A34A; font-size: 11px;") );
	auto* readyText = new QLabel( tr("System Ready"), readyWidget );
	readyText->setObjectName( QStringLiteral("readyText") );
	readyText->setStyleSheet( QStringLiteral("color: #18181B; font-size: 11px; font-weight: 500;") );
	readyLayout->addWidget( readyDot );
	readyLayout->addWidget( readyText );
	ui->statusBar->addWidget( readyWidget );

	// Restore Locations, Screenshots, and Inspector buttons in footer status bar
	ui->panelButtons->show();
	ui->statusBar->addWidget( ui->panelButtons );
	ui->computerSelectPanelButton->show();
	ui->screenshotManagementPanelButton->show();
	ui->slideshowPanelButton->hide();
	ui->spotlightPanelButton->hide();

	const QString footerBtnStyle = QStringLiteral(
		"QToolButton {"
		"   background: #F9FAFB; border: 1px solid #E1E3E8; border-radius: 4px;"
		"   color: #52525B; font-size: 11px; font-weight: 600; padding: 2px 8px; margin: 1px 2px;"
		"}"
		"QToolButton:hover { background: #F5F3FF; border-color: #5B21B6; color: #18181B; }"
		"QToolButton:checked { background: #DDD6FE; border-color: #5B21B6; color: #FFFFFF; font-weight: bold; }"
	);
	ui->computerSelectPanelButton->setStyleSheet( footerBtnStyle );
	ui->screenshotManagementPanelButton->setStyleSheet( footerBtnStyle );
	ui->spacerLabel1->hide();
	ui->filterLineEdit->hide();
	ui->filterPoweredOnComputersButton->hide();
	ui->filterComputersWithLoggedOnUsersButton->hide();
	ui->spacerLabel2->hide();
	// Restore footer Zoom Slider & Auto Button in bottom status bar
	ui->autoAdjustComputerIconSizeButton->show();
	ui->autoAdjustComputerIconSizeButton->setStyleSheet( QStringLiteral(
		"QToolButton { background: #F9FAFB; border: 1px solid #E1E3E8; border-radius: 4px; color: #52525B; font-size: 10px; font-weight: 600; padding: 2px 6px; }"
		"QToolButton:hover { border-color: #5B21B6; color: #FFFFFF; }"
		"QToolButton:checked { background: #6D28D9; color: #FFFFFF; border-color: #5B21B6; }"
	) );
	ui->gridSizeSlider->show();
	ui->gridSizeSlider->setFixedWidth( 110 );
	ui->gridSizeSlider->setStyleSheet( QStringLiteral(
		"QSlider::groove:horizontal { height: 4px; background: #EDE9FE; border-radius: 2px; }"
		"QSlider::sub-page:horizontal { background: #6D28D9; border-radius: 2px; }"
		"QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; background: #5B21B6; border-radius: 6px; }"
	) );
	ui->statusBar->addPermanentWidget( ui->autoAdjustComputerIconSizeButton );
	ui->statusBar->addPermanentWidget( ui->gridSizeSlider );
	ui->spacerLabel3->hide();
	ui->useCustomComputerPositionsButton->hide();
	ui->alignComputersButton->hide();
	ui->spacerLabel4->hide();
	ui->aboutButton->hide();


	auto* timeLabel = new QLabel( ui->statusBar );
	timeLabel->setStyleSheet( QStringLiteral("color: #52525B; font-size: 11px; font-family: monospace; padding-right: 12px;") );
	auto updateTime = [timeLabel]() {
		timeLabel->setText( QDateTime::currentDateTime().toString( QStringLiteral("MMM dd, yyyy  hh:mm") ) );
	};
	updateTime();
	auto* timeTimer = new QTimer( this );
	connect( timeTimer, &QTimer::timeout, this, updateTime );
	timeTimer->start( 10000 );
	ui->statusBar->addPermanentWidget( timeLabel );

	// create all views
	auto mainSplitter = new QSplitter( Qt::Horizontal, ui->centralWidget );
	mainSplitter->setChildrenCollapsible( false );
	mainSplitter->setObjectName( QStringLiteral("MainSplitter") );

	auto monitoringSplitter = new QSplitter( Qt::Vertical, mainSplitter );
	monitoringSplitter->setChildrenCollapsible( false );
	monitoringSplitter->setObjectName( QStringLiteral("MonitoringSplitter") );

	auto slideshowSpotlightSplitter = new QSplitter( Qt::Horizontal, monitoringSplitter );
	slideshowSpotlightSplitter->setChildrenCollapsible( false );
	slideshowSpotlightSplitter->setObjectName( QStringLiteral("SlideshowSpotlightSplitter") );

	auto computerSelectPanel = new ComputerSelectPanel( m_master.computerManager() );
	auto screenshotManagementPanel = new ScreenshotManagementPanel();
	auto slideshowPanel = new SlideshowPanel( m_master.userConfig(), ui->computerMonitoringWidget );
	auto spotlightPanel = new SpotlightPanel( m_master.userConfig(), ui->computerMonitoringWidget );

	slideshowSpotlightSplitter->addWidget( slideshowPanel );
	slideshowSpotlightSplitter->addWidget( spotlightPanel );
	slideshowSpotlightSplitter->setStretchFactor( slideshowSpotlightSplitter->indexOf(slideshowPanel), 1 );
	slideshowSpotlightSplitter->setStretchFactor( slideshowSpotlightSplitter->indexOf(spotlightPanel), 1 );

	auto* monitoringContainer = new QWidget( monitoringSplitter );
	monitoringContainer->setObjectName( QStringLiteral("MonitoringContainer") );
	auto* monitoringLayout = new QVBoxLayout( monitoringContainer );
	monitoringLayout->setContentsMargins( 0, 0, 0, 0 );
	monitoringLayout->setSpacing( 0 );

	auto* subHeader = new QFrame( ui->centralWidget );
	subHeader->setFixedHeight( 38 );
	subHeader->setStyleSheet( QStringLiteral("QFrame { background-color: #F4F5F7; border-bottom: 1px solid #E1E3E8; }") );
	auto* subHeaderLayout = new QHBoxLayout( subHeader );
	subHeaderLayout->setContentsMargins( 12, 0, 12, 0 );
	subHeaderLayout->setSpacing( 8 );

	auto* dotOnline = new QLabel( QStringLiteral("●"), subHeader );
	dotOnline->setStyleSheet( QStringLiteral("color: #6D28D9; font-size: 10px;") );
	auto* locationTitle = new QLabel( tr( "DIGIHUB / Lab" ), subHeader );
	locationTitle->setStyleSheet( QStringLiteral("font-weight: 700; font-size: 11px; color: #18181B;") );
	auto* countBadge = new QLabel( subHeader );
	countBadge->setStyleSheet( QStringLiteral("font-weight: 400; font-size: 11px; color: #52525B;") );

	// Quick filter pills: [All] [Online] [Offline]
	const QString pillBase = QStringLiteral(
		"QPushButton { background: #ECEDF0; border: 1px solid #F5F3FF; border-radius: 10px;"
		" color: #52525B; font-size: 10px; font-weight: 600; padding: 2px 10px; min-height: 20px; }"
		"QPushButton:checked { background: #6D28D9; color: #FFFFFF; font-weight: 700; border-color: #5B21B6; }"
		"QPushButton:hover { border-color: #5B21B6; color: #18181B; }");
	auto* pillAll     = new QPushButton( tr("All"),     subHeader );
	auto* pillOnline  = new QPushButton( tr("Online"),  subHeader );
	auto* pillOffline = new QPushButton( tr("Offline"), subHeader );
	for( auto* p : { pillAll, pillOnline, pillOffline } )
	{
		p->setCheckable( true );
		p->setStyleSheet( pillBase );
		p->setCursor( Qt::PointingHandCursor );
		p->setFixedHeight( 22 );
	}
	pillAll->setChecked( true );
	// Wire filter pills to monitoring widget filter
	connect( pillAll, &QPushButton::toggled, this, [this, pillAll, pillOnline, pillOffline]( bool on ) {
		if( on ) {
			pillOnline->setChecked( false );
			pillOffline->setChecked( false );
			ui->computerMonitoringWidget->setFilterOfflineComputers( false );
			ui->computerMonitoringWidget->setFilterPoweredOnComputers( false );
			ui->filterPoweredOnComputersButton->setChecked( false );
		}
	} );
	connect( pillOnline, &QPushButton::toggled, this, [this, pillAll, pillOnline, pillOffline]( bool on ) {
		if( on ) {
			pillAll->setChecked( false );
			pillOffline->setChecked( false );
			ui->computerMonitoringWidget->setFilterOfflineComputers( false );
			ui->computerMonitoringWidget->setFilterPoweredOnComputers( true );
			ui->filterComputersWithLoggedOnUsersButton->setChecked( false );
		}
	} );
	connect( pillOffline, &QPushButton::toggled, this, [this, pillAll, pillOnline, pillOffline]( bool on ) {
		if( on ) {
			pillAll->setChecked( false );
			pillOnline->setChecked( false );
			ui->computerMonitoringWidget->setFilterPoweredOnComputers( false );
			ui->computerMonitoringWidget->setFilterOfflineComputers( true );
		}
	} );

	subHeaderLayout->addWidget( dotOnline );
	subHeaderLayout->addWidget( locationTitle );
	subHeaderLayout->addWidget( countBadge );
	subHeaderLayout->addStretch();
	subHeaderLayout->addWidget( pillAll );
	subHeaderLayout->addWidget( pillOnline );
	subHeaderLayout->addWidget( pillOffline );
	subHeaderLayout->addSpacing( 6 );

	// Sort selector: Online First | Logged In First | Hostname | IP
	auto* sortCombo = new QComboBox( subHeader );
	sortCombo->setObjectName( QStringLiteral("sortCombo") );
	sortCombo->setStyleSheet( QStringLiteral(
		"QComboBox { background: #F9FAFB; border: 1px solid #E1E3E8; border-radius: 10px; color: #18181B; padding: 2px 8px; font-size: 10px; font-weight: 600; min-height: 20px; }"
		"QComboBox:hover { border-color: #5B21B6; color: #FFFFFF; }"
		"QComboBox QAbstractItemView { background: #ECEDF0; border: 1px solid #E1E3E8; color: #18181B; selection-background-color: #6D28D9; selection-color: #18181B; }"
	) );
	sortCombo->addItem( tr("Sort: Hostname A-Z"), 0 );
	sortCombo->addItem( tr("Sort: Online First"), 1 );
	sortCombo->addItem( tr("Sort: Offline First"), 4 );
	sortCombo->addItem( tr("Sort: Logged In"), 2 );
	sortCombo->addItem( tr("Sort: Logged Out"), 5 );
	sortCombo->addItem( tr("Sort: IP Address"), 3 );
	sortCombo->setCurrentIndex( 1 ); // Default: Online First

	connect( sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, sortCombo]( int ) {
		auto* model = qobject_cast<ComputerMonitoringModel*>( ui->computerMonitoringWidget->model() );
		if( model )
		{
			model->setCustomSortMode( static_cast<ComputerMonitoringModel::CustomSortMode>( sortCombo->currentData().toInt() ) );
		}
	} );

	subHeaderLayout->addWidget( sortCombo );
	subHeaderLayout->addSpacing( 8 );

	// View density modes: Compact (180px), Balanced (240px), Live (320px)
	auto* densityWidget = new QWidget( subHeader );
	auto* densityLayout = new QHBoxLayout( densityWidget );
	densityLayout->setContentsMargins( 0, 0, 0, 0 );
	densityLayout->setSpacing( 2 );

	auto* btnCompact = new QPushButton( tr("Compact"), densityWidget );
	auto* btnBalanced = new QPushButton( tr("Balanced"), densityWidget );
	auto* btnLive = new QPushButton( tr("Live"), densityWidget );

	const QString densityBtnStyle = QStringLiteral(
		"QPushButton { background: #ECEDF0; border: 1px solid #F5F3FF; border-radius: 6px; padding: 2px 8px; color: #52525B; font-size: 10px; font-weight: 600; }"
		"QPushButton:hover { background: #F9FAFB; color: #18181B; border-color: #5B21B6; }"
		"QPushButton:checked { background: #6D28D9; color: #FFFFFF; font-weight: 700; border-color: #5B21B6; }"
	);
	for (auto* b : { btnCompact, btnBalanced, btnLive })
	{
		b->setStyleSheet( densityBtnStyle );
		b->setCheckable( true );
		b->setFixedHeight( 22 );
	}
	btnBalanced->setChecked( true );

	auto* densityGroup = new QButtonGroup( densityWidget );
	densityGroup->addButton( btnCompact, 180 );
	densityGroup->addButton( btnBalanced, 240 );
	densityGroup->addButton( btnLive, 320 );

	connect( densityGroup, &QButtonGroup::idClicked, this, [this]( int size ) {
		ui->autoAdjustComputerIconSizeButton->setChecked( false );
		ui->computerMonitoringWidget->setAutoAdjustIconSize( false );
		ui->computerMonitoringWidget->setComputerScreenSize( size );
	} );

	densityLayout->addWidget( btnCompact );
	densityLayout->addWidget( btnBalanced );
	densityLayout->addWidget( btnLive );
	subHeaderLayout->addWidget( densityWidget );

	auto* countsTimer = new QTimer( this );
	countsTimer->setSingleShot( true );
	countsTimer->setInterval( 250 );

	auto updateCounts = [this, countBadge, locationTitle]() {
		const auto* model = ui->computerMonitoringWidget->model();
		const int total = model ? model->rowCount() : 0;
		int online = 0;
		int offline = 0;
		int idle = 0;
		QStringList locations;
		if (model)
		{
			for (int r = 0; r < total; ++r)
			{
				const auto idx = model->index(r, 0);
				const auto ci = model->data(idx, ComputerControlListModel::ControlInterfaceRole).value<ComputerControlInterface::Pointer>();
				if (ci)
				{
					const QString loc = ci->computer().location();
					if (!loc.isEmpty() && !locations.contains(loc))
					{
						locations.append(loc);
					}
				}
				if (ci && ci->state() == ComputerControlInterface::State::Connected)
				{
					online++;
					if (ci->userLoginName().trimmed().isEmpty())
					{
						idle++;
					}
				}
				else
				{
					offline++;
				}
			}
		}
		if (locations.isEmpty())
		{
			locationTitle->setText( tr( "All Locations" ) );
		}
		else if (locations.size() <= 2)
		{
			locationTitle->setText( locations.join( QStringLiteral("  •  ") ) );
		}
		else
		{
			locationTitle->setText( tr( "%1 Locations" ).arg( locations.size() ) );
		}
		if (auto* bDot = findChild<QLabel*>( QStringLiteral("bellDot") ))
		{
			bool hasAiAlerts = false;
			QFile af( QStringLiteral("C:/SynapseViolations/live_alerts.json") );
			if( af.exists() && af.open( QIODevice::ReadOnly ) )
			{
				const auto adoc = QJsonDocument::fromJson( af.readAll() );
				hasAiAlerts = adoc.isArray() && !adoc.array().isEmpty();
				af.close();
			}
			bDot->setVisible( offline > 0 || hasAiAlerts );
		}
		if (auto* rDot = findChild<QLabel*>( QStringLiteral("readyDot") ))
		{
			rDot->setStyleSheet( online > 0
				? QStringLiteral("color: #16A34A; font-size: 11px;")
				: QStringLiteral("color: #D97706; font-size: 11px;") );
		}
		if (auto* rText = findChild<QLabel*>( QStringLiteral("readyText") ))
		{
			rText->setText( tr( "Fleet: %1/%2 Online" ).arg( online ).arg( total ) );
		}
		countBadge->setText( tr( "%1 Computers  •  %2 Online  •  %3 Offline  •  %4 Idle" ).arg( total ).arg( online ).arg( offline ).arg( idle ) );
	};
	connect( countsTimer, &QTimer::timeout, this, updateCounts );
	auto scheduleCountsUpdate = [countsTimer]() {
		if( !countsTimer->isActive() )
		{
			countsTimer->start();
		}
	};
	updateCounts();
	if (ui->computerMonitoringWidget->model())
	{
		connect( ui->computerMonitoringWidget->model(), &QAbstractItemModel::rowsInserted, this, scheduleCountsUpdate );
		connect( ui->computerMonitoringWidget->model(), &QAbstractItemModel::rowsRemoved, this, scheduleCountsUpdate );
		connect( ui->computerMonitoringWidget->model(), &QAbstractItemModel::modelReset, this, scheduleCountsUpdate );
		connect( ui->computerMonitoringWidget->model(), &QAbstractItemModel::dataChanged, this, scheduleCountsUpdate );
	}

	// Dynamic Fleet Auto-Refresh Timer:
	// Incrementally syncs newly enrolled network objects every 20s without dropping live VNC connections
	auto* directoryRefreshTimer = new QTimer( this );
	connect( directoryRefreshTimer, &QTimer::timeout, this, [this]() {
		VeyonCore::networkObjectDirectoryManager().configuredDirectory()->update();
		m_master.computerControlListModel().update();
	} );
	directoryRefreshTimer->start( 20000 );

	monitoringLayout->addWidget( ui->computerMonitoringWidget );

	monitoringSplitter->addWidget( slideshowSpotlightSplitter );
	monitoringSplitter->addWidget( monitoringContainer );
	monitoringSplitter->setStretchFactor( monitoringSplitter->indexOf(slideshowSpotlightSplitter), 1 );
	monitoringSplitter->setStretchFactor( monitoringSplitter->indexOf(monitoringContainer), 1 );

	mainSplitter->addWidget( computerSelectPanel );
	mainSplitter->addWidget( screenshotManagementPanel );
	mainSplitter->addWidget( monitoringSplitter );

	m_inspectorPanel = new ComputerInspectorPanel( m_master, mainSplitter );
	m_inspectorPanel->setObjectName( QStringLiteral("ComputerInspectorPanel") );
	mainSplitter->addWidget( m_inspectorPanel );

	mainSplitter->setStretchFactor( mainSplitter->indexOf(monitoringSplitter), 3 );
	mainSplitter->setStretchFactor( mainSplitter->indexOf(m_inspectorPanel), 1 );

	auto* inspectorPanelButton = new QToolButton( ui->panelButtons );
	inspectorPanelButton->setObjectName( QStringLiteral("inspectorPanelButton") );
	inspectorPanelButton->setText( tr( "Inspector" ) );
	auto makeFooterIcon = []( const QString& kind ) -> QIcon {
		QPixmap px( 16, 16 );
		px.fill( Qt::transparent );
		QPainter p( &px );
		p.setRenderHint( QPainter::Antialiasing );
		p.setPen( QPen( QColor( 0x6D, 0x28, 0xD9 ), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
		p.setBrush( Qt::NoBrush );
		if( kind == QLatin1String("locations") )
		{
			p.drawRoundedRect( QRectF( 2.0, 3.0, 12.0, 10.0 ), 1.8, 1.8 );
			p.drawLine( QPointF( 5.5, 3.0 ), QPointF( 5.5, 13.0 ) );
		}
		else if( kind == QLatin1String("screenshots") )
		{
			p.drawRoundedRect( QRectF( 2.0, 4.0, 12.0, 9.0 ), 1.8, 1.8 );
			p.drawEllipse( QPointF( 8.0, 8.5 ), 2.2, 2.2 );
		}
		else
		{
			p.drawRoundedRect( QRectF( 2.0, 3.0, 12.0, 10.0 ), 1.8, 1.8 );
			p.drawLine( QPointF( 10.5, 3.0 ), QPointF( 10.5, 13.0 ) );
		}
		return QIcon( px );
	};
	ui->computerSelectPanelButton->setIcon( makeFooterIcon( QStringLiteral("locations") ) );
	ui->screenshotManagementPanelButton->setIcon( makeFooterIcon( QStringLiteral("screenshots") ) );
	inspectorPanelButton->setIcon( makeFooterIcon( QStringLiteral("inspector") ) );
	inspectorPanelButton->setCheckable( true );
	inspectorPanelButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
	inspectorPanelButton->setStyleSheet( QStringLiteral(
		"QToolButton {"
		"   background: #F9FAFB; border: 1px solid #E1E3E8; border-radius: 4px;"
		"   color: #52525B; font-size: 11px; font-weight: 600; padding: 2px 8px; margin: 1px 2px;"
		"}"
		"QToolButton:hover { background: #F5F3FF; border-color: #5B21B6; color: #18181B; }"
		"QToolButton:checked { background: #DDD6FE; border-color: #5B21B6; color: #FFFFFF; font-weight: bold; }"
	) );
	ui->panelButtons->layout()->addWidget( inspectorPanelButton );

	connect( inspectorPanelButton, &QToolButton::toggled, m_inspectorPanel, &QWidget::setVisible );
	connect( m_inspectorPanel, &ComputerInspectorPanel::visibilityToggleRequested, inspectorPanelButton, &QToolButton::setChecked );

	connect( ui->computerMonitoringWidget->selectionModel(), &QItemSelectionModel::selectionChanged,
			 this, [this, inspectorPanelButton]() {
				 const auto sel = selectedComputerControlInterfaces();
				 m_inspectorPanel->setSelectedComputerControlInterfaces( sel );
				 if( !sel.isEmpty() && !m_inspectorPanel->isVisible() )
				 {
					 inspectorPanelButton->setChecked( true );
				 }
			 } );

	static const QHash<QWidget *, QAbstractButton *> panelButtons{
		{ computerSelectPanel, ui->computerSelectPanelButton },
		{ screenshotManagementPanel, ui->screenshotManagementPanelButton },
		{ slideshowPanel, ui->slideshowPanelButton },
		{ spotlightPanel, ui->spotlightPanelButton },
		{ m_inspectorPanel, inspectorPanelButton }
	};

	for( auto it = panelButtons.constBegin(), end = panelButtons.constEnd(); it != end; ++it )
	{
		it.key()->hide();
		it.key()->installEventFilter( this );
		connect( *it, &QAbstractButton::toggled, it.key(), &QWidget::setVisible );
	}

	slideshowSpotlightSplitter->hide();

	auto updateSlideshowSplitter = [slideshowSpotlightSplitter, slideshowPanel, spotlightPanel]() {
		slideshowSpotlightSplitter->setVisible( slideshowPanel->isVisible() || spotlightPanel->isVisible() );
	};
	connect( ui->slideshowPanelButton, &QAbstractButton::toggled, this, updateSlideshowSplitter );
	connect( ui->spotlightPanelButton, &QAbstractButton::toggled, this, updateSlideshowSplitter );

	QList<int> splitterSizes;
	for( auto* splitter : { slideshowSpotlightSplitter, monitoringSplitter, mainSplitter } )
	{
		splitter->setHandleWidth( 1 );
		splitter->setStyleSheet( QStringLiteral(
			"QSplitter::handle { background-color: transparent; } "
			"QSplitter::handle:horizontal { width: 1px; background-color: #E1E3E8; } "
			"QSplitter::handle:vertical { height: 1px; background-color: #E1E3E8; } "
			"QSplitter::handle:hover { background-color: #E1E3E8; }"
		) );

		splitter->installEventFilter( this );

		int index = 0;

		const auto splitterStates = m_master.userConfig().splitterStates()[splitter->objectName()].toArray();
		splitterSizes.clear();
		splitterSizes.reserve( splitterStates.size() );

		for( const auto& sizeObject : splitterStates )
		{
			auto size = sizeObject.toInt();
			const auto widget = splitter->widget( index );
			const auto button = panelButtons.value( widget );

			if( widget )
			{
				if( button )
				{
					widget->setVisible( size > 0 );
					button->setChecked( size > 0 );
				}
				size = qAbs( size );
				if( splitter->orientation() == Qt::Horizontal )
				{
					widget->resize( size, widget->height() );
				}
				else
				{
					widget->resize( widget->width(), size );
				}

				widget->setProperty( originalSizePropertyName(), widget->size() );
			}
			splitterSizes.append( size );
			++index;
		}
		splitter->setSizes( splitterSizes );
	}

	const auto SplitterContentBaseSize = 500;

	if( spotlightPanel->property( originalSizePropertyName() ).isNull() ||
		slideshowPanel->property( originalSizePropertyName() ).isNull() )
	{
		slideshowSpotlightSplitter->setSizes( { SplitterContentBaseSize, SplitterContentBaseSize } );
	}

	if( slideshowSpotlightSplitter->property( originalSizePropertyName() ).isNull() ||
		monitoringContainer->property( originalSizePropertyName() ).isNull() )
	{
		monitoringSplitter->setSizes( { SplitterContentBaseSize, SplitterContentBaseSize } );
	}

	delete ui->centralLayout;
	auto* centralVBox = new QVBoxLayout( ui->centralWidget );
	centralVBox->setContentsMargins( 0, 0, 0, 0 );
	centralVBox->setSpacing( 0 );
	centralVBox->addWidget( subHeader );
	centralVBox->addWidget( mainSplitter, 1 );

	// Restore saved splitter layout if valid; otherwise apply clean 4-pane defaults
	const auto savedMainSizes = m_master.userConfig().splitterStates()[mainSplitter->objectName()].toArray();
	if( savedMainSizes.size() != 4 )
	{
		ui->computerSelectPanelButton->setChecked( true );
		computerSelectPanel->show();

		inspectorPanelButton->setChecked( true );
		m_inspectorPanel->show();

		mainSplitter->setSizes( { 200, 0, 800, 260 } );
	}

	computerSelectPanel->setMinimumWidth( 160 );
	computerSelectPanel->setMaximumWidth( 260 );
	m_inspectorPanel->setMinimumWidth( 220 );
	m_inspectorPanel->setMaximumWidth( 300 );

	mainSplitter->setStretchFactor( 0, 0 );
	mainSplitter->setStretchFactor( 1, 0 );
	mainSplitter->setStretchFactor( 2, 1 );
	mainSplitter->setStretchFactor( 3, 0 );

	// initialize filter controls
	connect( ui->filterLineEdit, &QLineEdit::textChanged,
			 this, [this]( const QString& filter ) { ui->computerMonitoringWidget->setSearchFilter( filter ); } );
	connect( ui->filterPoweredOnComputersButton, &QToolButton::toggled,
			 this, [this]( bool enabled ) { ui->computerMonitoringWidget->setFilterPoweredOnComputers( enabled ); } );
	connect( ui->filterComputersWithLoggedOnUsersButton, &QToolButton::toggled,
			 this, [this]( bool enabled ) { ui->computerMonitoringWidget->setFilterComputersWithLoggedOnUsers( enabled ); } );
	ui->filterPoweredOnComputersButton->setChecked(m_master.userConfig().filterPoweredOnComputers());
	ui->filterComputersWithLoggedOnUsersButton->setChecked(m_master.userConfig().filterComputersWithLoggedOnUsers());

	// initialize monitoring screen size slider
	ui->gridSizeSlider->setMinimum( ComputerMonitoringWidget::MinimumComputerScreenSize );
	ui->gridSizeSlider->setMaximum( ComputerMonitoringWidget::MaximumComputerScreenSize );

	ui->autoAdjustComputerIconSizeButton->setChecked( ui->computerMonitoringWidget->autoAdjustIconSize() );

	connect( ui->gridSizeSlider, &QSlider::valueChanged,
			 this, [this]( int size ) { ui->computerMonitoringWidget->setComputerScreenSize( size ); } );
	connect( ui->computerMonitoringWidget, &ComputerMonitoringWidget::computerScreenSizeAdjusted,
			 ui->gridSizeSlider, &QSlider::setValue );
	connect( ui->autoAdjustComputerIconSizeButton, &QToolButton::toggled,
			 this, [this]( bool enabled ) {
				 ui->computerMonitoringWidget->setAutoAdjustIconSize( enabled );
				 m_master.userConfig().setAutoAdjustMonitoringIconSize( enabled );
			 } );

	int size = ComputerMonitoringWidget::DefaultComputerScreenSize;
	if( m_master.userConfig().monitoringScreenSize() >= ComputerMonitoringWidget::MinimumComputerScreenSize )
	{
		size = m_master.userConfig().monitoringScreenSize();
	}

	ui->gridSizeSlider->setValue( size );
	ui->computerMonitoringWidget->setComputerScreenSize( size );
	startSentinelFeedServer();

	// initialize computer placement controls
	auto customComputerPositionsControlMenu = new QMenu(this);
	const auto darkSuffix = VeyonCore::useDarkMode() ? QStringLiteral("-dark") : QString();
	customComputerPositionsControlMenu->addAction(QIcon(QStringLiteral(":/core/document-open%1.png").arg(darkSuffix)),
													tr("Load computer positions"),
													this, &MainWindow::loadComputerPositions);
	customComputerPositionsControlMenu->addAction(QIcon(QStringLiteral(":/core/document-save%1.png").arg(darkSuffix)),
													tr("Save computer positions"),
													this, &MainWindow::saveComputerPositions);
	ui->useCustomComputerPositionsButton->setMenu(customComputerPositionsControlMenu);

	ui->useCustomComputerPositionsButton->setChecked(m_master.userConfig().useCustomComputerPositions());
	connect(ui->useCustomComputerPositionsButton, &QToolButton::toggled,
			ui->computerMonitoringWidget, &ComputerMonitoringWidget::setUseCustomComputerPositions);
	connect(ui->alignComputersButton, &QToolButton::clicked,
			ui->computerMonitoringWidget, &ComputerMonitoringWidget::alignComputers);


	const auto toolButtons = findChildren<QToolButton*>();
	for(auto* btn : toolButtons)
	{
		btn->setIconSize(QSize(20, 20));
	}

	if (VeyonCore::useDarkMode())
	{
		ui->aboutButton->setIcon(QIcon(QStringLiteral(":/core/help-about-dark.png")));
		ui->filterComputersWithLoggedOnUsersButton->setIcon(QIcon(QStringLiteral(":/core/user-group-new-dark.png")));
		ui->autoAdjustComputerIconSizeButton->setIcon(QIcon(QStringLiteral(":/master/zoom-fit-best-dark.png")));
		ui->alignComputersButton->setIcon(QIcon(QStringLiteral(":/master/align-grid-dark.png")));
		ui->useCustomComputerPositionsButton->setIcon(QIcon(QStringLiteral(":/master/exchange-positions-zorder-dark.png")));
		ui->filterPoweredOnComputersButton->setIcon(QIcon(QStringLiteral(":/master/powered-on-dark.png")));
	}

	// create the main toolbar
	ui->toolBar->layout()->setSpacing( 4 );
	ui->toolBar->toggleViewAction()->setEnabled( false );
	ui->toolBar->setFixedHeight( 82 );
	ui->toolBar->setMovable( false );
	ui->toolBar->setFloatable( false );
	ui->toolBar->setVisible( true );

	addFeaturesToToolBar();
	reloadSubFeatures();

	const auto monitoringModeButton = m_modeGroup->button(int(qHash(VeyonCore::builtinFeatures().monitoringMode().feature().uid()))); // clazy:exclude=qt6-qhash-signature
	if (monitoringModeButton)
	{
		monitoringModeButton->setChecked(true);
	}
}



MainWindow::~MainWindow()
{
	ui->computerMonitoringWidget->saveConfiguration();

	delete ui;
}



bool MainWindow::initAuthentication()
{
	if( VeyonCore::instance()->initAuthentication() )
	{
		return true;
	}

	if( VeyonCore::config().authenticationMethod() == VeyonCore::AuthenticationMethod::KeyFileAuthentication )
	{
		QMessageBox::information(nullptr,
								 tr("Authentication impossible"),
								 tr("No authentication key files were found or your current ones "
										"are outdated. Please create new key files using Synapse "
										"Synapse Agent. Alternatively set up logon authentication "
										"using Synapse Agent. Otherwise you won't be "
										"able to access computers using Synapse."));
	}

	return false;
}



bool MainWindow::initAccessControl()
{
	if (VeyonCore::config().accessControlForMasterEnabled())
	{
		const auto accessingUser = VeyonCore::authenticationCredentials().hasCredentials(AuthenticationCredentials::Type::UserLogon) ?
									   VeyonCore::authenticationCredentials().logonUsername() :
									   VeyonCore::platform().userFunctions().queryCurrentUserProperty(PlatformUserFunctions::UserProperty::LoginName);

		// Built-in Administrator is always granted access
		if( accessingUser.compare( QStringLiteral("administrator"), Qt::CaseInsensitive ) == 0 ||
			accessingUser.endsWith( QStringLiteral("\\administrator"), Qt::CaseInsensitive ) )
		{
			return true;
		}

		const auto accessControlResult = VeyonCore::builtinFeatures().accessControlProvider()
										 .checkAccess(accessingUser,
													  QHostAddress(QHostAddress::LocalHost).toString(),
													  {});
		if( accessControlResult.access == AccessControlProvider::Access::Deny )
		{
			vWarning() << "user" << accessingUser
					   << "is not allowed to access computers";
			QMessageBox::critical( nullptr, tr( "Access denied" ),
								   tr( "According to the local configuration you're not allowed "
									   "to access computers in the network. Please log in with a different "
									   "account or let your system administrator check the local configuration." ) );
			return false;
		}
	}

	return true;
}



void MainWindow::reloadSubFeatures()
{
	for( const auto& feature : m_master.features() )
	{
		auto button = ui->toolBar->findChild<QToolButton *>( feature.name() );
		if( button )
		{
			addSubFeaturesToToolButton( button, feature );
		}
	}
}



ComputerControlInterfaceList MainWindow::selectedComputerControlInterfaces() const
{
	return ui->computerMonitoringWidget->selectedComputerControlInterfaces();
}



void MainWindow::closeEvent( QCloseEvent* event )
{
	if( m_master.currentMode() != VeyonCore::builtinFeatures().monitoringMode().feature().uid() )
	{
		const Feature& activeFeature = VeyonCore::featureManager().feature( m_master.currentMode() );

		QMessageBox::information(this, tr("Feature active"),
								 tr("The feature \"%1\" is still active. Please stop it before closing Synapse.")
								 .arg(activeFeature.displayName()));
		event->ignore();
		return;
	}

	QJsonObject splitterStates;
	const auto splitters = findChildren<QSplitter *>();
	for( const auto* splitter : splitters )
	{
		const auto sizes = splitter->sizes();
		QJsonArray splitterSizes;
		int i = 0;
		int hiddenSize = 0;
		for( auto size : sizes )
		{
			auto widget = splitter->widget(i);
			const auto originalSize = widget->property( originalSizePropertyName() ).toSize();
			if( widget->size().isEmpty() && originalSize.isEmpty() == false )
			{
				size = splitter->orientation() == Qt::Horizontal ? -originalSize.width() : -originalSize.height();
				hiddenSize += qAbs(size);
			}
			else
			{
				size -= hiddenSize;
			}

			splitterSizes.append( size );
			++i;
		}
		splitterStates[splitter->objectName()] = splitterSizes;
	}

	m_master.userConfig().setSplitterStates( splitterStates );

	m_master.userConfig().setWindowState( QString::fromLatin1( saveState( 2026 ).toBase64() ) );
	m_master.userConfig().setWindowGeometry( QString::fromLatin1( saveGeometry().toBase64() ) );

	QMainWindow::closeEvent( event );
}



void MainWindow::showEvent( QShowEvent* event )
{
	QMainWindow::showEvent( event );
	if( m_headerToolBar )
	{
		m_headerToolBar->setVisible( true );
		m_headerToolBar->show();
	}
	if( ui && ui->toolBar )
	{
		ui->toolBar->setVisible( true );
		ui->toolBar->show();
	}
}



bool MainWindow::eventFilter( QObject* object, QEvent* event )
{
	if( event->type() == QEvent::Resize )
	{
		const auto widget = qobject_cast<QWidget *>( object );
		const auto resizeEvent = static_cast<QResizeEvent *>( event );

		if( resizeEvent->oldSize().isEmpty() == false )
		{
			widget->setProperty( originalSizePropertyName(), resizeEvent->oldSize() );
		}
	}

	return QMainWindow::eventFilter( object, event );
}



void MainWindow::keyPressEvent( QKeyEvent* event )
{
	switch( event->key() )
	{
	case Qt::Key_F5:
		VeyonCore::networkObjectDirectoryManager().configuredDirectory()->update();
		m_master.computerControlListModel().reload();
		event->accept();
		break;
	case Qt::Key_F11:
		QWidget::setWindowState( QWidget::windowState() ^ Qt::WindowFullScreen );
		event->accept();
		break;
	case Qt::Key_Slash:
		if( auto* edit = findChild<QLineEdit*>( QStringLiteral("globalSearchEdit") ) )
		{
			edit->setFocus();
			edit->selectAll();
			event->accept();
			return;
		}
		break;
	default:
		QMainWindow::keyPressEvent( event );
		break;
	}
}



void MainWindow::showAboutDialog()
{
	AboutDialog( this ).exec();
}



void MainWindow::applyCustomTheme()
{
	const QStringList candidatePaths = {
		QStringLiteral("C:/ProgramData/DigiAgent/custom_theme.qss"),
		QCoreApplication::applicationDirPath() + QStringLiteral("/custom_theme.qss"),
		QStringLiteral("/etc/veyon/custom_theme.qss")
	};
	for( const auto& path : candidatePaths )
	{
		QFile customTheme( path );
		if( customTheme.exists() && customTheme.open( QIODevice::ReadOnly | QIODevice::Text ) )
		{
			const QString qss = QString::fromUtf8( customTheme.readAll() );
			customTheme.close();
			if( qApp )
			{
				qApp->setStyleSheet( qss );
			}
			else
			{
				setStyleSheet( qss );
			}
			return;
		}
	}
}



void MainWindow::startSentinelFeedServer()
{
	if( m_sentinelFeedServer )
	{
		return;
	}
	m_sentinelFeedServer = new QTcpServer( this );
	if( !m_sentinelFeedServer->listen( QHostAddress::LocalHost, 11410 ) )
	{
		return;
	}

	connect( m_sentinelFeedServer, &QTcpServer::newConnection, this, [this]() {
		while( m_sentinelFeedServer->hasPendingConnections() )
		{
			auto* sock = m_sentinelFeedServer->nextPendingConnection();
			connect( sock, &QTcpSocket::readyRead, this, [this, sock]() {
				QByteArray req = sock->property( "sentinelHttpRequest" ).toByteArray();
				req += sock->readAll();
				sock->setProperty( "sentinelHttpRequest", req );
				if( req.size() > 4096 )
				{
					sock->write( "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" );
					sock->disconnectFromHost();
					return;
				}
				const auto headerEnd = req.indexOf( "\r\n\r\n" );
				if( headerEnd < 0 )
				{
					return;
				}
				const QByteArray headers = req.left( headerEnd );
				const QByteArray requestBody = req.mid( headerEnd + 4 );
				int contentLength = 0;
				for( const auto& line : headers.split( '\n' ) )
				{
					if( line.trimmed().toLower().startsWith( "content-length:" ) )
					{
						contentLength = line.mid( line.indexOf( ':' ) + 1 ).trimmed().toInt();
					}
				}
				if( contentLength < 0 || contentLength > 1024 )
				{
					sock->write( "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" );
					sock->disconnectFromHost();
					return;
				}
				if( requestBody.size() < contentLength )
				{
					return;
				}

				auto sendJson = [sock]( int status, const QByteArray& statusText, const QJsonObject& object ) {
					const QByteArray responseBody = QJsonDocument( object ).toJson( QJsonDocument::Compact );
					sock->write( "HTTP/1.1 " + QByteArray::number( status ) + " " + statusText + "\r\nContent-Type: application/json\r\nContent-Length: "
						+ QByteArray::number( responseBody.size() ) + "\r\nConnection: close\r\n\r\n" + responseBody );
					sock->disconnectFromHost();
				};

				const auto requestLine = headers.left( headers.indexOf( '\n' ) ).trimmed();
				if( requestLine.startsWith( "POST /screenshot " ) )
				{
					QJsonParseError parseError;
					const auto requestDoc = QJsonDocument::fromJson( requestBody.left( contentLength ), &parseError );
					const QString requestedIp = requestDoc.object().value( QStringLiteral("ip") ).toString().trimmed();
					if( parseError.error != QJsonParseError::NoError || !requestDoc.isObject() || requestedIp.isEmpty() )
					{
						sendJson( 400, "Bad Request", {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Expected JSON with a computer IP")}} );
						return;
					}

					ComputerControlInterface::Pointer target;
					for( const auto& ci : m_master.allComputerControlInterfaces() )
					{
						if( ci && ci->state() == ComputerControlInterface::State::Connected &&
							ci->computer().hostAddress().toString() == requestedIp )
						{
							target = ci;
							break;
						}
					}
					if( !target )
					{
						sendJson( 404, "Not Found", {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Computer is not connected")}} );
						return;
					}

					const QString screenshotDir = VeyonCore::filesystem().screenshotDirectoryPath();
					const QString hostMarker = QStringLiteral("_") + target->computer().hostName() + QStringLiteral("_");
					QDateTime latestBefore;
					const QFileInfoList existing = QDir( screenshotDir ).entryInfoList( { QStringLiteral("*.png") }, QDir::Files, QDir::Time );
					for( const auto& file : existing )
					{
						if( file.fileName().contains( hostMarker, Qt::CaseInsensitive ) )
						{
							latestBefore = file.lastModified();
							break;
						}
					}

					bool screenshotFeatureFound = false;
					for( const auto& feature : VeyonCore::featureManager().features() )
					{
						if( feature.name().compare( QStringLiteral("Screenshot"), Qt::CaseInsensitive ) == 0 )
						{
							screenshotFeatureFound = true;
							VeyonCore::featureManager().controlFeature(
								feature.uid(), FeatureProviderInterface::Operation::Start, {}, { target } );
							break;
						}
					}
					if( !screenshotFeatureFound )
					{
						sendJson( 503, "Service Unavailable", {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Screenshot feature is unavailable")}} );
						return;
					}

					QString screenshotPath;
					const QFileInfoList captured = QDir( screenshotDir ).entryInfoList( { QStringLiteral("*.png") }, QDir::Files, QDir::Time );
					for( const auto& file : captured )
					{
						if( file.fileName().contains( hostMarker, Qt::CaseInsensitive ) &&
							(!file.lastModified().isValid() || !latestBefore.isValid() || file.lastModified() > latestBefore) )
						{
							screenshotPath = file.absoluteFilePath();
							break;
						}
					}
					if( screenshotPath.isEmpty() )
					{
						sendJson( 500, "Internal Server Error", {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Screenshot feature ran but saved file could not be found")}} );
						return;
					}
					sendJson( 200, "OK", {{QStringLiteral("ok"), true}, {QStringLiteral("screenshot_path"), screenshotPath}} );
					return;
				}
				if( !requestLine.startsWith( "GET /frames " ) )
				{
					sock->write( "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" );
					sock->disconnectFromHost();
					return;
				}

				QJsonArray arr;
				for( const auto& ci : m_master.allComputerControlInterfaces() )
				{
					if( !ci || ci->state() != ComputerControlInterface::State::Connected )
					{
						continue;
					}
					const QImage fb = ci->framebuffer();
					if( fb.isNull() )
					{
						continue;
					}
					const QImage scaled = ( fb.width() > 640 )
						? fb.scaledToWidth( 640, Qt::FastTransformation )
						: fb;
					QByteArray jpgBytes;
					QBuffer buf( &jpgBytes );
					buf.open( QIODevice::WriteOnly );
					scaled.save( &buf, "JPEG", 75 );

					QJsonObject obj;
					obj.insert( QStringLiteral("name"), ci->computerName() );
					obj.insert( QStringLiteral("ip"), ci->computer().hostAddress().toString() );
					obj.insert( QStringLiteral("user"), ci->userLoginName() );
					obj.insert( QStringLiteral("frame_b64"), QString::fromLatin1( jpgBytes.toBase64() ) );
					arr.append( obj );
				}

				QJsonObject root;
				root.insert( QStringLiteral("computers"), arr );
				const QByteArray body = QJsonDocument( root ).toJson( QJsonDocument::Compact );

				const QByteArray hdr = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
					+ QByteArray::number( body.size() )
					+ "\r\nConnection: close\r\n\r\n";
				sock->write( hdr );
				sock->write( body );
				sock->disconnectFromHost();
			} );
			connect( sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater );
		}
	} );
}



void MainWindow::addFeaturesToToolBar()
{
	QMap<QString, Feature> featureMap;
	for( const auto& feature : m_master.features() )
	{
		if( !feature.testFlag( Feature::Flag::Meta ) )
		{
			featureMap.insert( feature.name().toLower(), feature );
		}
	}

	auto createButton = [this]( const Feature& feature ) {
		const QString featName = feature.name();
		const QString k = featName.toLower().remove( QLatin1Char(' ') ).remove( QLatin1Char('_') ).remove( QLatin1Char('-') );

		auto drawFeatureGlyph = []( const QString& key, const QColor& fg, const QColor& bgBadge ) -> QPixmap {
			QPixmap px( 24, 24 );
			px.fill( Qt::transparent );
			QPainter p( &px );
			p.setRenderHint( QPainter::Antialiasing );
			p.setPen( Qt::NoPen );
			p.setBrush( bgBadge );
			p.drawRoundedRect( QRectF( 1.0, 1.0, 22.0, 22.0 ), 6.0, 6.0 );

			p.setPen( QPen( fg, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
			p.setBrush( Qt::NoBrush );

			if( key.contains( QStringLiteral("monitor") ) )
			{
				p.drawRoundedRect( QRectF( 5.0, 5.0, 6.0, 6.0 ), 1.2, 1.2 );
				p.drawRoundedRect( QRectF( 13.0, 5.0, 6.0, 6.0 ), 1.2, 1.2 );
				p.drawRoundedRect( QRectF( 5.0, 13.0, 6.0, 6.0 ), 1.2, 1.2 );
				p.drawRoundedRect( QRectF( 13.0, 13.0, 6.0, 6.0 ), 1.2, 1.2 );
			}
			else if( key.contains( QStringLiteral("remote") ) || key.contains( QStringLiteral("vnc") ) )
			{
				p.drawRoundedRect( QRectF( 4.5, 5.5, 15.0, 10.0 ), 2.0, 2.0 );
				p.drawLine( QPointF( 9.5, 18.5 ), QPointF( 14.5, 18.5 ) );
				p.drawLine( QPointF( 12.0, 15.5 ), QPointF( 12.0, 18.5 ) );
				p.drawLine( QPointF( 9.0, 10.5 ), QPointF( 15.0, 10.5 ) );
			}
			else if( key.contains( QStringLiteral("timedlock") ) || key.contains( QStringLiteral("timedfreeze") ) )
			{
				p.drawEllipse( QPointF( 12.0, 12.5 ), 6.2, 6.2 );
				p.drawLine( QPointF( 12.0, 9.0 ), QPointF( 12.0, 12.5 ) );
				p.drawLine( QPointF( 12.0, 12.5 ), QPointF( 14.8, 14.0 ) );
			}
			else if( key.contains( QStringLiteral("freeze") ) || key.contains( QStringLiteral("lock") ) )
			{
				p.drawRoundedRect( QRectF( 6.5, 11.0, 11.0, 8.0 ), 2.0, 2.0 );
				p.drawArc( QRectF( 8.5, 5.5, 7.0, 8.0 ), 0, 180 * 16 );
			}
			else if( key.contains( QStringLiteral("poweron") ) )
			{
				p.drawArc( QRectF( 6.0, 6.5, 12.0, 12.0 ), 125 * 16, 290 * 16 );
				p.drawLine( QPointF( 12.0, 5.0 ), QPointF( 12.0, 11.5 ) );
			}
			else if( key.contains( QStringLiteral("reboot") ) )
			{
				p.drawArc( QRectF( 6.0, 6.0, 12.0, 12.0 ), 35 * 16, 285 * 16 );
				p.drawLine( QPointF( 16.5, 6.5 ), QPointF( 18.0, 10.0 ) );
				p.drawLine( QPointF( 16.5, 6.5 ), QPointF( 13.2, 7.5 ) );
			}
			else if( key.contains( QStringLiteral("powerdown") ) )
			{
				p.drawArc( QRectF( 6.0, 6.5, 12.0, 12.0 ), 125 * 16, 290 * 16 );
				p.drawLine( QPointF( 12.0, 5.0 ), QPointF( 12.0, 11.5 ) );
				p.drawLine( QPointF( 7.0, 17.0 ), QPointF( 17.0, 7.0 ) );
			}
			else if( key.contains( QStringLiteral("login") ) )
			{
				p.drawEllipse( QPointF( 10.0, 8.0 ), 2.5, 2.5 );
				p.drawArc( QRectF( 5.5, 12.5, 9.0, 6.0 ), 0, 180 * 16 );
				p.drawLine( QPointF( 15.0, 12.0 ), QPointF( 19.0, 12.0 ) );
				p.drawLine( QPointF( 17.0, 10.0 ), QPointF( 19.0, 12.0 ) );
				p.drawLine( QPointF( 17.0, 14.0 ), QPointF( 19.0, 12.0 ) );
			}
			else if( key.contains( QStringLiteral("logoff") ) )
			{
				p.drawEllipse( QPointF( 9.5, 8.0 ), 2.5, 2.5 );
				p.drawArc( QRectF( 5.0, 12.5, 9.0, 6.0 ), 0, 180 * 16 );
				p.drawLine( QPointF( 19.0, 12.0 ), QPointF( 15.0, 12.0 ) );
				p.drawLine( QPointF( 17.0, 10.0 ), QPointF( 19.0, 12.0 ) );
				p.drawLine( QPointF( 17.0, 14.0 ), QPointF( 19.0, 12.0 ) );
			}
			else if( key.contains( QStringLiteral("textmessage") ) )
			{
				p.drawRoundedRect( QRectF( 5.0, 6.0, 14.0, 9.5 ), 2.2, 2.2 );
				p.drawLine( QPointF( 8.0, 15.5 ), QPointF( 7.0, 19.0 ) );
				p.drawLine( QPointF( 7.0, 19.0 ), QPointF( 11.5, 15.5 ) );
				p.drawLine( QPointF( 8.0, 9.5 ), QPointF( 16.0, 9.5 ) );
				p.drawLine( QPointF( 8.0, 12.2 ), QPointF( 13.5, 12.2 ) );
			}
			else if( key.contains( QStringLiteral("startapp") ) )
			{
				p.drawRoundedRect( QRectF( 5.0, 5.5, 14.0, 13.0 ), 2.2, 2.2 );
				p.drawLine( QPointF( 5.0, 9.0 ), QPointF( 19.0, 9.0 ) );
				QPolygonF play;
				play << QPointF( 10.5, 11.5 ) << QPointF( 15.0, 13.8 ) << QPointF( 10.5, 16.0 );
				p.setBrush( fg );
				p.drawPolygon( play );
			}
			else if( key.contains( QStringLiteral("filetransfer") ) || key.contains( QStringLiteral("distribute") ) )
			{
				p.drawRoundedRect( QRectF( 5.0, 6.0, 14.0, 12.0 ), 2.0, 2.0 );
				p.drawLine( QPointF( 12.0, 15.0 ), QPointF( 12.0, 9.0 ) );
				p.drawLine( QPointF( 9.5, 11.5 ), QPointF( 12.0, 9.0 ) );
				p.drawLine( QPointF( 14.5, 11.5 ), QPointF( 12.0, 9.0 ) );
			}
			else
			{
				p.drawEllipse( QPointF( 12.0, 12.0 ), 6.0, 6.0 );
				p.drawLine( QPointF( 6.5, 12.0 ), QPointF( 17.5, 12.0 ) );
			}
			return px;
		};

		QColor accentFg( 0x6D, 0x28, 0xD9 ); // Botanical Sage
		QColor accentBg( 0xED, 0xE9, 0xFE );
		if( k.contains( QStringLiteral("timedlock") ) || k.contains( QStringLiteral("timedfreeze") ) || k.contains( QStringLiteral("reboot") ) )
		{
			accentFg = QColor( 0xD9, 0x77, 0x06 ); // Warm Amber
			accentBg = QColor( 0xFE, 0xF3, 0xC7 );
		}
		else if( k.contains( QStringLiteral("freeze") ) || k.contains( QStringLiteral("lock") ) || k.contains( QStringLiteral("powerdown") ) )
		{
			accentFg = QColor( 0xDC, 0x26, 0x26 ); // Muted Terracotta
			accentBg = QColor( 0xFE, 0xE2, 0xE2 );
		}
		else if( k.contains( QStringLiteral("poweron") ) )
		{
			accentFg = QColor( 0x16, 0xA3, 0x4A ); // Semantic Online Green
			accentBg = QColor( 0xE4, 0xF1, 0xE7 );
		}
		else if( k.contains( QStringLiteral("logoff") ) )
		{
			accentFg = QColor( 0xEA, 0x58, 0x0C ); // Warm Warning
			accentBg = QColor( 0xFF, 0xED, 0xD5 );
		}

		QIcon richIcon;
		richIcon.addPixmap( drawFeatureGlyph( k, accentFg, accentBg ), QIcon::Normal, QIcon::Off );
		richIcon.addPixmap( drawFeatureGlyph( k, Qt::white, QColor( 0x5B, 0x21, 0xB6 ) ), QIcon::Normal, QIcon::On );

		QString text = feature.displayName();
		if( k.contains( QStringLiteral("monitor") ) )
		{
			text = tr( "Monitoring" );
		}
		else if( k.contains( QStringLiteral("demo") ) )
		{
			text = tr( "Demo" );
		}
		else if( k.contains( QStringLiteral("timedlock") ) || k.contains( QStringLiteral("timedfreeze") ) )
		{
			text = tr( "Timed lock" );
		}
		else if( k.contains( QStringLiteral("freeze") ) || k.contains( QStringLiteral("lock") ) )
		{
			text = tr( "Freeze" );
		}
		else if( k.contains( QStringLiteral("remote") ) )
		{
			text = tr( "Remote" );
		}
		else if( k.contains( QStringLiteral("powerdown") ) )
		{
			text = tr( "Power down" );
		}
		else if( k.contains( QStringLiteral("poweron") ) )
		{
			text = tr( "Power on" );
		}
		else if( k.contains( QStringLiteral("reboot") ) )
		{
			text = tr( "Reboot" );
		}
		else if( k.contains( QStringLiteral("login") ) )
		{
			text = tr( "Log in" );
		}
		else if( k.contains( QStringLiteral("logoff") ) )
		{
			text = tr( "Log off" );
		}
		else if( k.contains( QStringLiteral("textmessage") ) )
		{
			text = tr( "Text message" );
		}
		else if( k.contains( QStringLiteral("startapplication") ) || k.contains( QStringLiteral("startapp") ) )
		{
			text = tr( "Start app" );
		}

		auto btn = new ToolButton( richIcon,
								   text,
								   feature.displayNameActive(),
								   feature.description(),
								   feature.shortcut() );
		btn->setIconSize( QSize( 22, 22 ) );
		btn->setToolButtonStyle( Qt::ToolButtonTextUnderIcon );
		btn->setIconSize( QSize( 18, 18 ) );
		btn->setMinimumSize( 66, 72 );
		btn->setMaximumWidth( 76 );
		btn->setFixedHeight( 72 );
		connect(btn, &QToolButton::clicked, this, [=, this] () {
			if( feature.name().compare( QStringLiteral("PowerDown"), Qt::CaseInsensitive ) == 0 )
			{
				FeatureList candidateFeatures = m_master.allFeatures();
				for( const auto& f : VeyonCore::featureManager().features() )
				{
					if( !candidateFeatures.contains( f ) )
					{
						candidateFeatures.append( f );
					}
				}
				for( const auto& f : candidateFeatures )
				{
					if( f.name().compare( QStringLiteral("PowerDownDelayed"), Qt::CaseInsensitive ) == 0 )
					{
						m_master.runFeature( f );
						return;
					}
				}
			}
			m_master.runFeature( feature );
			updateModeButtonGroup();
			if( feature.testFlag( Feature::Flag::Mode ) )
			{
				reloadSubFeatures();
			}
		});
		btn->setObjectName( feature.name() );
		btn->addTo( ui->toolBar );

		if( feature.testFlag( Feature::Flag::Mode ) )
		{
			btn->setCheckable( true );
			m_modeGroup->addButton( btn, buttonId( feature ) );
		}

		addSubFeaturesToToolButton( btn, feature );
	};

	QFile layoutFile( QStringLiteral("C:/ProgramData/DigiAgent/toolbar_layout.json") );
	if( layoutFile.open( QIODevice::ReadOnly ) )
	{
		const auto doc = QJsonDocument::fromJson( layoutFile.readAll() );
		layoutFile.close();
		if( doc.isObject() )
		{
			const auto obj = doc.object();
			const auto orderArr = obj.value( QStringLiteral("order") ).toArray();
			const auto hiddenArr = obj.value( QStringLiteral("hidden") ).toArray();

			auto canonicalKey = []( const QString& str ) -> QString {
				const QString s = str.toLower().remove( QLatin1Char(' ') ).remove( QLatin1Char('_') ).remove( QLatin1Char('-') );
				if( s == QStringLiteral("computermonitoring") || s == QStringLiteral("monitoring") )
				{
					return QStringLiteral("monitoringmode");
				}
				if( s == QStringLiteral("timedlock") || s == QStringLiteral("timedfreeze") || s == QStringLiteral("focussession") )
				{
					return QStringLiteral("timedlock");
				}
				if( s == QStringLiteral("freezescreen") || s == QStringLiteral("freeze") || s == QStringLiteral("lock") || s == QStringLiteral("screenlock") )
				{
					return QStringLiteral("screenlock");
				}
				if( s == QStringLiteral("remote") || s == QStringLiteral("remotecontrol") || s == QStringLiteral("remoteview") || s == QStringLiteral("remoteaccess") )
				{
					return QStringLiteral("remoteaccess");
				}
				if( s == QStringLiteral("startapp") || s == QStringLiteral("startapplication") )
				{
					return QStringLiteral("startapp");
				}
				if( s == QStringLiteral("website") || s == QStringLiteral("openwebsite") )
				{
					return QStringLiteral("openwebsite");
				}
				if( s == QStringLiteral("distribute") || s == QStringLiteral("distributefiles") )
				{
					return QStringLiteral("distributefiles");
				}
				if( s == QStringLiteral("collect") || s == QStringLiteral("filecollect") )
				{
					return QStringLiteral("filecollect");
				}
				if( s == QStringLiteral("login") || s == QStringLiteral("userlogin") )
				{
					return QStringLiteral("userlogin");
				}
				if( s == QStringLiteral("logoff") || s == QStringLiteral("userlogoff") )
				{
					return QStringLiteral("userlogoff");
				}
				if( s == QStringLiteral("screenshot") )
				{
					return QStringLiteral("screenshot");
				}
				if( s == QStringLiteral("poweron") )
				{
					return QStringLiteral("poweron");
				}
				if( s == QStringLiteral("reboot") )
				{
					return QStringLiteral("reboot");
				}
				if( s == QStringLiteral("powerdown") )
				{
					return QStringLiteral("powerdown");
				}
				if( s == QStringLiteral("demo") )
				{
					return QStringLiteral("demo");
				}
				return s;
			};

			QSet<QString> hiddenSet;
			for( const auto& v : hiddenArr )
			{
				hiddenSet.insert( v.toString().toLower() );
				hiddenSet.insert( canonicalKey( v.toString() ) );
			}

			QSet<QString> addedFeatures;

			for( const auto& itemVal : orderArr )
			{
				const QString item = itemVal.toString().trimmed();
				if( item.compare( QStringLiteral("separator"), Qt::CaseInsensitive ) == 0 )
				{
					auto* sep = new QFrame( ui->toolBar );
					sep->setFrameShape( QFrame::VLine );
					sep->setFixedWidth( 1 );
					sep->setFixedHeight( 28 );
					sep->setStyleSheet( QStringLiteral("QFrame { background-color: #E1E3E8; border: none; margin: 8px 3px; }") );
					ui->toolBar->addWidget( sep );
					continue;
				}

				const QString key = item.toLower();
				const QString normKey = canonicalKey( item );
				if( hiddenSet.contains( key ) || hiddenSet.contains( normKey ) )
				{
					continue;
				}

				Feature matchedFeature;
				for( auto it = featureMap.begin(); it != featureMap.end(); ++it )
				{
					const QString featNorm = canonicalKey( it.key() );
					if( featNorm == normKey ||
						it.key() == key ||
						it.key() == normKey ||
						it.key().contains( normKey ) ||
						normKey.contains( it.key() ) ||
						it.value().displayName().compare( item, Qt::CaseInsensitive ) == 0 ||
						canonicalKey( it.value().displayName() ) == normKey )
					{
						matchedFeature = it.value();
						break;
					}
				}

				if( matchedFeature.isValid() && !addedFeatures.contains( matchedFeature.name().toLower() ) )
				{
					createButton( matchedFeature );
					addedFeatures.insert( matchedFeature.name().toLower() );
				}
			}

			return;
		}
	}

	bool lastWasMode = true;
	for( const auto& feature : m_master.features() )
	{
		if( feature.testFlag( Feature::Flag::Meta ) )
		{
			continue;
		}

		const bool isMode = feature.testFlag( Feature::Flag::Mode );
		if( lastWasMode && !isMode )
		{
			ui->toolBar->addSeparator();
			lastWasMode = false;
		}
		else if( !isMode && ( feature.name().contains( QLatin1String("power"), Qt::CaseInsensitive ) ||
							  feature.name().contains( QLatin1String("userSession"), Qt::CaseInsensitive ) ||
							  feature.name().contains( QLatin1String("textMessage"), Qt::CaseInsensitive ) ) )
		{
			ui->toolBar->addSeparator();
		}

		createButton( feature );
	}
}



void MainWindow::addSubFeaturesToToolButton( QToolButton* button, const Feature& parentFeature )
{
	if( button->menu() )
	{
		button->menu()->close();
		button->menu()->deleteLater();
		button->setMenu( nullptr );
	}

	const auto parentFeatureIsMode = parentFeature.testFlag( Feature::Flag::Mode );
	const auto subFeatures = m_master.subFeatures( parentFeature.uid() );

	if( subFeatures.isEmpty() ||
		( parentFeatureIsMode && button->isChecked() ) )
	{
		return;
	}

	auto menu = new QMenu( button );
	menu->setObjectName( parentFeature.name() );
	menu->setToolTipsVisible( true );

	for( const auto& subFeature : subFeatures )
	{
		auto action = menu->addAction(QIcon(subFeature.iconUrl()), subFeature.displayName(),
							   #if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
									  subFeature.shortcut(),
							   #endif
									  this, [=, this]()
		{
			m_master.runFeature( subFeature );
			if( parentFeatureIsMode )
			{
				if( subFeature.testFlag( Feature::Flag::Option ) == false )
				{
					button->setChecked( true );
				}
				reloadSubFeatures();
			}
		}
#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
		,subFeature.shortcut()
#endif
		);
		action->setToolTip( subFeature.description() );
		action->setObjectName( subFeature.uid().toString() );

		if( subFeature.testFlag( Feature::Flag::Option ) )
		{
			action->setCheckable( true );
			action->setChecked( subFeature.testFlag( Feature::Flag::Checked ) );
		}
	}

	button->setMenu( menu );
	button->setPopupMode( ToolButton::InstantPopup );
}



void MainWindow::updateModeButtonGroup()
{
	const auto& monitoringMode = VeyonCore::builtinFeatures().monitoringMode().feature();

	if( m_master.currentMode() == monitoringMode.uid() )
	{
		m_modeGroup->button( buttonId( monitoringMode ) )->setChecked( true );
	}
}



void MainWindow::loadComputerPositions()
{
	if (const auto fileName = QFileDialog::getOpenFileName(this, tr("Load computer positions"),
														   QDir::homePath(), tr("JSON files (*.json)"));
		!fileName.isEmpty())
	{
		if (QFile file(fileName); file.open(QFile::ReadOnly))
		{
			const auto& computerPositionsProperty = m_master.userConfig().computerPositionsProperty();
			const auto config = QJsonDocument::fromJson(file.readAll()).object();
			const auto uiConfig = config[computerPositionsProperty.parentKey()].toObject();
			const auto computerPositions = uiConfig[computerPositionsProperty.key()].toObject()[QStringLiteral("JsonStoreArray")].toArray();

			ui->computerMonitoringWidget->loadPositions(computerPositions);
			ui->computerMonitoringWidget->setUseCustomComputerPositions(true);
			ui->computerMonitoringWidget->doItemsLayout();
		}
	}
}



void MainWindow::saveComputerPositions()
{
	if (const auto fileName = QFileDialog::getSaveFileName(this, tr("Save computer positions"),
														   QDir::homePath(), tr("JSON files (*.json)"));
		!fileName.isEmpty())
	{
		if (QFile file(fileName); file.open(QFile::WriteOnly | QFile::Truncate))
		{
			const auto& computerPositionsProperty = m_master.userConfig().computerPositionsProperty();

			// create structure identical to UserConfig so file with positions can be used as template for UserConfig
			QJsonObject computerPositions;
			computerPositions[QStringLiteral("JsonStoreArray")] = ui->computerMonitoringWidget->savePositions();
			QJsonObject uiConfig;
			uiConfig[computerPositionsProperty.key()] = computerPositions;
			QJsonObject config;
			config[computerPositionsProperty.parentKey()] = uiConfig;
			file.write(QJsonDocument(config).toJson());
		}
	}
}



void MainWindow::notifyCountdown( const QString& title,
								  int remainingSeconds,
								  const VeyonMasterInterface::CountdownAdjustCallback& adjustCallback,
								  const VeyonMasterInterface::CountdownCancelCallback& cancelCallback,
								  const ComputerControlInterfaceList& targets )
{
	if( m_inspectorPanel )
	{
		m_inspectorPanel->setActiveCountdown( title, remainingSeconds, adjustCallback, cancelCallback, targets );
	}
}



void MainWindow::cancelCountdown( const ComputerControlInterfaceList& targets )
{
	if( m_inspectorPanel )
	{
		m_inspectorPanel->clearActiveCountdown( targets );
	}
}
