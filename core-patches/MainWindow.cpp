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
			BOOL darkMode = TRUE;
			pDwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));
		}
	}
#endif

	QPalette darkPalette;
	darkPalette.setColor( QPalette::Window, QColor( 8, 14, 27 ) ); // #080e1b
	darkPalette.setColor( QPalette::WindowText, QColor( 248, 250, 252 ) );
	darkPalette.setColor( QPalette::Base, QColor( 12, 21, 39 ) ); // #0c1527
	darkPalette.setColor( QPalette::AlternateBase, QColor( 16, 27, 49 ) );
	darkPalette.setColor( QPalette::ToolTipBase, QColor( 16, 27, 49 ) );
	darkPalette.setColor( QPalette::ToolTipText, QColor( 248, 250, 252 ) );
	darkPalette.setColor( QPalette::Text, QColor( 248, 250, 252 ) );
	darkPalette.setColor( QPalette::Button, QColor( 14, 23, 42 ) );
	darkPalette.setColor( QPalette::ButtonText, QColor( 203, 213, 225 ) );
	darkPalette.setColor( QPalette::BrightText, Qt::white );
	darkPalette.setColor( QPalette::Highlight, QColor( 37, 99, 235 ) );
	darkPalette.setColor( QPalette::HighlightedText, Qt::white );
	setPalette( darkPalette );

	// Cyber Cockpit Top Header Toolbar
	m_headerToolBar = new QToolBar( tr("Header"), this );
	m_headerToolBar->setObjectName( QStringLiteral("headerToolBar") );
	m_headerToolBar->setMovable( false );
	m_headerToolBar->setFloatable( false );
	m_headerToolBar->setFixedHeight( 56 );
	addToolBar( Qt::TopToolBarArea, m_headerToolBar );
	addToolBarBreak( Qt::TopToolBarArea );

	auto* logo = new QLabel( m_headerToolBar );
	logo->setFixedSize( 32, 32 );
	logo->setAlignment( Qt::AlignCenter );
	QPixmap logoPix( 22, 22 );
	logoPix.fill( Qt::transparent );
	{
		QPainter lp( &logoPix );
		lp.setRenderHint( QPainter::Antialiasing );
		lp.setBrush( Qt::white );
		lp.setPen( Qt::NoPen );
		QPolygonF bolt;
		bolt << QPointF( 11, 2 ) << QPointF( 4, 10 ) << QPointF( 9, 10 )
		     << QPointF( 8, 18 ) << QPointF( 16, 8 ) << QPointF( 11, 8 );
		lp.drawPolygon( bolt );
	}
	logo->setPixmap( logoPix );
	logo->setObjectName( QStringLiteral("logoIconLabel") );
	logo->setStyleSheet( QStringLiteral(
		"background-color: #2563EB; border-radius: 6px; border: 1px solid #3B82F6;"
	) );

	auto* titleWidget = new QWidget( m_headerToolBar );
	auto* titleLayout = new QVBoxLayout( titleWidget );
	titleLayout->setContentsMargins( 8, 0, 0, 0 );
	titleLayout->setSpacing( 2 );
	auto* appTitle = new QLabel( QStringLiteral("Computer Monitoring"), titleWidget );
	appTitle->setStyleSheet( QStringLiteral("font-size: 14px; font-weight: 700; color: #FFFFFF; letter-spacing: 0.2px;") );
	auto* appSub = new QLabel( QStringLiteral("Manage  •  Monitor  •  Control"), titleWidget );
	appSub->setStyleSheet( QStringLiteral("font-size: 10px; font-weight: 500; color: #8B9CB5; letter-spacing: 0.2px;") );
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
		"QLineEdit { background-color: #0F172A; border: 1px solid #2A323D; border-radius: 14px; padding: 4px 12px; color: #E6EDF3; font-size: 11px; } "
		"QLineEdit:focus { border-color: #2563EB; background-color: #090E1A; }"
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
		bp.setPen( QPen( QColor( 0xE2, 0xE8, 0xF0 ), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
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
		"QToolButton#bellButton { background-color: #0F172A; border: 1px solid #2A323D; border-radius: 14px; color: #E6EDF3; font-size: 12px; min-width: 28px; max-width: 28px; min-height: 28px; max-height: 28px; padding: 0px; } "
		"QToolButton#bellButton:hover { border-color: #2563EB; background-color: #1F2630; color: #FFFFFF; }"
	) );
	auto* bellDot = new QLabel( bellBtn );
	bellDot->setObjectName( QStringLiteral("bellDot") );
	bellDot->setFixedSize( 6, 6 );
	bellDot->setStyleSheet( QStringLiteral( "background-color: #EF4444; border-radius: 3px; border: 1px solid #101827;" ) );
	bellDot->move( 18, 3 );
	bellDot->hide();
	bellBtn->setToolTip( tr( "Fleet Alerts" ) );
	connect( bellBtn, &QToolButton::clicked, this, [this, bellBtn]() {
		auto* menu = new QMenu( this );
		menu->setAttribute( Qt::WA_DeleteOnClose );
		menu->setStyleSheet( QStringLiteral(
			"QMenu { background-color: #0F172A; border: 1px solid #1E293B; border-radius: 8px; padding: 6px; color: #F1F5F9; }"
			"QMenu::item { padding: 6px 12px; border-radius: 4px; font-size: 11px; }"
			"QMenu::item:selected { background-color: #172554; }"
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
		gp.setPen( QPen( QColor( 0xE2, 0xE8, 0xF0 ), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
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
		"QToolButton#settingsButton { background-color: #0F172A; border: 1px solid #2A323D; border-radius: 14px; color: #E6EDF3; font-size: 13px; min-width: 28px; max-width: 28px; min-height: 28px; max-height: 28px; padding: 0px; } "
		"QToolButton#settingsButton:hover { border-color: #2563EB; background-color: #1F2630; color: #FFFFFF; }"
	) );
	connect( settingsBtn, &QToolButton::clicked, this, &MainWindow::showAboutDialog );
	m_headerToolBar->addWidget( settingsBtn );

		setStyleSheet( QStringLiteral(
				"QDialog {"
		"   background-color: #090E1A;"
		"   color: #E6EDF3;"
		"}"
		"QDialog QLabel, QDialog QCheckBox, QDialog QRadioButton {"
		"   color: #E6EDF3;"
		"}"
		"QDialog QPlainTextEdit, QDialog QTextEdit {"
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 6px;"
		"   color: #E6EDF3;"
		"   padding: 6px;"
		"}"
		"QDialog QLineEdit {"
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 6px;"
		"   color: #E6EDF3;"
		"   padding: 4px 8px;"
		"}"
		"QDialog QPushButton {"
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 6px;"
		"   color: #E6EDF3;"
		"   padding: 6px 14px;"
		"   font-weight: 600;"
		"}"
		"QDialog QPushButton:hover {"
		"   background-color: #1F2630;"
		"   border-color: #2563EB;"
		"   color: #FFFFFF;"
		"}"
		"QDialog QPushButton:default {"
		"   background-color: #238636;"
		"   border: 1px solid #2563EB;"
		"   color: #FFFFFF;"
		"}"
		"QMainWindow, QWidget#centralWidget {"
		"   background-color: #090E1A;"
		"}"
		"QToolBar#headerToolBar {"
		"   background-color: #090E1A;"
		"   border-bottom: 1px solid #21262D;"
		"   padding: 4px 14px;"
		"}"
		"QToolBar#headerToolBar QToolButton {"
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 14px;"
		"   min-width: 28px;"
		"   max-width: 28px;"
		"   min-height: 28px;"
		"   max-height: 28px;"
		"   padding: 0px;"
		"}"
		"QToolBar {"
		"   background-color: #090E1A;"
		"   border-bottom: 1px solid #21262D;"
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
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 8px;"
		"   color: #E6EDF3;"
		"   font-weight: 500;"
		"   font-size: 10px;"
		"   padding: 6px 4px 6px 4px;"
		"   min-width: 66px;"
		"   max-width: 76px;"
		"   min-height: 64px;"
		"   max-height: 68px;"
		"}"
		"QToolBar#mainToolBar QToolButton:hover, QToolBar#toolBar QToolButton:hover {"
		"   background-color: #1F2630;"
		"   border-color: #2563EB;"
		"   color: #FFFFFF;"
		"}"
		"QToolBar#mainToolBar QToolButton:pressed, QToolBar#toolBar QToolButton:pressed {"
		"   background-color: #238636;"
		"   border-color: #2563EB;"
		"   color: #FFFFFF;"
		"}"
		"QToolBar#mainToolBar QToolButton:checked, QToolBar#toolBar QToolButton:checked {"
		"   background-color: #238636;"
		"   border: 1px solid #2563EB;"
		"   color: #FFFFFF;"
		"   font-weight: 600;"
		"}"
		"QToolBar#mainToolBar QToolButton:disabled, QToolBar#toolBar QToolButton:disabled {"
		"   background-color: #090E1A;"
		"   border-color: #2A323D;"
		"   color: #8B949E;"
		"}"
				"QToolBar::separator {"
		"   width: 1px;"
		"   height: 28px;"
		"   background-color: #21262D;"
		"   border: none;"
		"   margin: 6px 4px;"
		"}"
		"QToolBar::separator:horizontal {"
		"   width: 1px;"
		"   height: 28px;"
		"   background-color: #21262D;"
		"   border: none;"
		"   margin: 6px 4px;"
		"}"
		"QToolBar::separator:vertical {"
		"   height: 1px;"
		"   width: 28px;"
		"   background-color: #21262D;"
		"   border: none;"
		"   margin: 4px 6px;"
		"}"
		"QToolTip {"
		"   background-color: #0F172A;"
		"   color: #E6EDF3;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 6px;"
		"   padding: 5px 8px;"
		"   font-size: 11px;"
		"}"
"QMenu {"
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 8px;"
		"   padding: 6px;"
		"}"
		"QMenu::item {"
		"   padding: 6px 24px;"
		"   border-radius: 4px;"
		"   color: #E6EDF3;"
		"   font-size: 12px;"
		"}"
		"QMenu::item:selected {"
		"   background-color: #2563EB;"
		"   color: #FFFFFF;"
		"}"
		"QMenu::separator {"
		"   height: 1px;"
		"   background-color: #21262D;"
		"   margin: 4px 8px;"
		"}"
		"QStatusBar, QWidget#panelButtons {"
		"   background-color: #090E1A;"
		"   border-top: 1px solid #21262D;"
		"   color: #8B949E;"
		"   min-height: 38px;"
		"   padding: 2px 8px;"
		"}"
		"QStatusBar QToolButton, QWidget#panelButtons QToolButton {"
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 6px;"
		"   padding: 4px 8px;"
		"   color: #E6EDF3;"
		"   font-size: 11px;"
		"   font-weight: 600;"
		"}"
		"QStatusBar QToolButton:hover, QWidget#panelButtons QToolButton:hover {"
		"   background-color: #1F2630;"
		"   border-color: #2563EB;"
		"   color: #FFFFFF;"
		"}"
		"QStatusBar QToolButton:checked, QWidget#panelButtons QToolButton:checked {"
		"   background-color: #238636;"
		"   border: 1px solid #2563EB;"
		"   color: #FFFFFF;"
		"   font-weight: 600;"
		"}"
		"QStatusBar QLabel {"
		"   color: #8B949E;"
		"   font-size: 11px;"
		"}"
		"QMainWindow QLineEdit, QStatusBar QLineEdit, QLineEdit#filterLineEdit, QLineEdit#globalSearchEdit {"
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 6px;"
		"   padding: 4px 10px;"
		"   color: #E6EDF3;"
		"   font-size: 11px;"
		"}"
		"QStatusBar QLineEdit:focus, QLineEdit#filterLineEdit:focus, QLineEdit:focus {"
		"   border-color: #2563EB;"
		"   background-color: #090E1A;"
		"}"
		"QStatusBar QSlider::groove:horizontal, QSlider::groove:horizontal {"
		"   height: 6px;"
		"   background: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 3px;"
		"}"
		"QStatusBar QSlider::sub-page:horizontal, QSlider::sub-page:horizontal {"
		"   background: #2563EB;"
		"   border-radius: 3px;"
		"}"
		"QStatusBar QSlider::handle:horizontal, QSlider::handle:horizontal {"
		"   background: #E6EDF3;"
		"   border: 1px solid #2563EB;"
		"   width: 14px;"
		"   margin-top: -5px;"
		"   margin-bottom: -5px;"
		"   border-radius: 7px;"
		"}"
		"QStatusBar QSlider::handle:horizontal:hover, QSlider::handle:horizontal:hover {"
		"   background: #FFFFFF;"
		"   border-color: #2563EB;"
		"}"
				"QSplitter::handle {"
		"   background-color: transparent;"
		"}"
		"QSplitter::handle:horizontal {"
		"   width: 1px;"
		"   background-color: #21262D;"
		"}"
		"QSplitter::handle:vertical {"
		"   height: 1px;"
		"   background-color: #21262D;"
		"}"
		"QSplitter::handle:hover {"
		"   background-color: #21262D;"
		"}"
"QTreeView, QListView, QTableView {"
		"   background-color: #090E1A;"
		"   color: #E6EDF3;"
		"   border: none;"
		"   font-size: 11px;"
		"}"
		"QTreeView::item:hover, QListView::item:hover {"
		"   background-color: #1F2630;"
		"   color: #FFFFFF;"
		"}"
		"QTreeView::item:selected, QListView::item:selected {"
		"   background-color: #173424;"
		"   color: #FFFFFF;"
		"   font-weight: 600;"
		"}"
		"QHeaderView::section {"
		"   background-color: #0F172A;"
		"   color: #8B949E;"
		"   border: 1px solid #21262D;"
		"   padding: 4px;"
		"}"
		"QPushButton {"
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 6px;"
		"   padding: 4px 10px;"
		"   color: #E6EDF3;"
		"   font-size: 11px;"
		"   font-weight: 500;"
		"}"
		"QPushButton:hover {"
		"   background-color: #1F2630;"
		"   border-color: #2563EB;"
		"   color: #FFFFFF;"
		"}"
		"QPushButton:checked { background: #238636; color: #FFFFFF; font-weight: bold; border-color: #2563EB; }"
		"QScrollBar:vertical {"
		"   background: #090E1A;"
		"   width: 8px;"
		"   margin: 0px;"
		"}"
		"QScrollBar::handle:vertical {"
		"   background: #2A323D;"
		"   min-height: 20px;"
		"   border-radius: 4px;"
		"}"
		"QScrollBar::handle:vertical:hover {"
		"   background: #2563EB;"
		"}"
		"QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
		"   height: 0px;"
		"}"
		"QScrollBar:horizontal {"
		"   background: #090E1A;"
		"   height: 8px;"
		"   margin: 0px;"
		"}"
		"QScrollBar::handle:horizontal {"
		"   background: #2A323D;"
		"   min-width: 20px;"
		"   border-radius: 4px;"
		"}"
		"QScrollBar::handle:horizontal:hover {"
		"   background: #2563EB;"
		"}"
		"QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
		"   width: 0px;"
		"}"
		"QAbstractScrollArea::corner {"
		"   background: #090E1A;"
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
	m_headerToolBar->setFixedHeight( 56 );
	ui->toolBar->setMovable( false );
	ui->toolBar->setFloatable( false );
	ui->toolBar->setVisible( true );
	ui->toolBar->setFixedHeight( 88 );

	// add widgets to status bar
	auto* readyWidget = new QWidget( ui->statusBar );
	auto* readyLayout = new QHBoxLayout( readyWidget );
	readyLayout->setContentsMargins( 8, 0, 12, 0 );
	readyLayout->setSpacing( 6 );
	auto* readyDot = new QLabel( QStringLiteral("●"), readyWidget );
	readyDot->setStyleSheet( QStringLiteral("color: #2EA043; font-size: 11px;") );
	auto* readyText = new QLabel( tr("System Ready"), readyWidget );
	readyText->setStyleSheet( QStringLiteral("color: #E6EDF3; font-size: 11px; font-weight: 500;") );
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
		"   background: #111A2D; border: 1px solid #21262D; border-radius: 4px;"
		"   color: #94A3B8; font-size: 11px; font-weight: 600; padding: 2px 8px; margin: 1px 2px;"
		"}"
		"QToolButton:hover { background: #162238; border-color: #3B82F6; color: #F1F5F9; }"
		"QToolButton:checked { background: #1E3A5F; border-color: #3B82F6; color: #FFFFFF; font-weight: bold; }"
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
		"QToolButton { background: #111A2D; border: 1px solid #21262D; border-radius: 4px; color: #94A3B8; font-size: 10px; font-weight: 600; padding: 2px 6px; }"
		"QToolButton:hover { border-color: #3B82F6; color: #FFFFFF; }"
		"QToolButton:checked { background: #2563EB; color: #FFFFFF; border-color: #3B82F6; }"
	) );
	ui->gridSizeSlider->show();
	ui->gridSizeSlider->setFixedWidth( 110 );
	ui->gridSizeSlider->setStyleSheet( QStringLiteral(
		"QSlider::groove:horizontal { height: 4px; background: #1F2D44; border-radius: 2px; }"
		"QSlider::sub-page:horizontal { background: #2563EB; border-radius: 2px; }"
		"QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; background: #3B82F6; border-radius: 6px; }"
	) );
	ui->statusBar->addPermanentWidget( ui->autoAdjustComputerIconSizeButton );
	ui->statusBar->addPermanentWidget( ui->gridSizeSlider );
	ui->spacerLabel3->hide();
	ui->useCustomComputerPositionsButton->hide();
	ui->alignComputersButton->hide();
	ui->spacerLabel4->hide();
	ui->aboutButton->hide();


	auto* timeLabel = new QLabel( ui->statusBar );
	timeLabel->setStyleSheet( QStringLiteral("color: #8B949E; font-size: 11px; font-family: monospace; padding-right: 12px;") );
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
	subHeader->setStyleSheet( QStringLiteral("QFrame { background-color: #090E1A; border-bottom: 1px solid #21262D; }") );
	auto* subHeaderLayout = new QHBoxLayout( subHeader );
	subHeaderLayout->setContentsMargins( 12, 0, 12, 0 );
	subHeaderLayout->setSpacing( 8 );

	auto* dotOnline = new QLabel( QStringLiteral("●"), subHeader );
	dotOnline->setStyleSheet( QStringLiteral("color: #2563EB; font-size: 10px;") );
	auto* locationTitle = new QLabel( tr( "DIGIHUB / Lab" ), subHeader );
	locationTitle->setStyleSheet( QStringLiteral("font-weight: 700; font-size: 11px; color: #E6EDF3;") );
	auto* countBadge = new QLabel( subHeader );
	countBadge->setStyleSheet( QStringLiteral("font-weight: 400; font-size: 11px; color: #8B949E;") );

	// Quick filter pills: [All] [Online] [Offline]
	const QString pillBase = QStringLiteral(
		"QPushButton { background: #111A2D; border: 1px solid #21262D; border-radius: 10px;"
		" color: #94A3B8; font-size: 10px; font-weight: 600; padding: 2px 10px; min-height: 20px; }"
		"QPushButton:checked { background: #238636; color: #FFFFFF; font-weight: bold; border-color: #2563EB; }"
		"QPushButton:hover { border-color: #3B82F6; color: #E2E8F0; }");
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
		"QComboBox { background: #111A2D; border: 1px solid #21262D; border-radius: 10px; color: #CBD5E1; padding: 2px 8px; font-size: 10px; font-weight: 600; min-height: 20px; }"
		"QComboBox:hover { border-color: #3B82F6; color: #FFFFFF; }"
		"QComboBox QAbstractItemView { background: #0D1729; border: 1px solid #21262D; color: #F1F5F9; selection-background-color: #2563EB; selection-color: #FFFFFF; }"
	) );
	sortCombo->addItem( tr("Sort: Hostname A-Z 🔤"), 0 );
	sortCombo->addItem( tr("Sort: Online First 🟢"), 1 );
	sortCombo->addItem( tr("Sort: Offline First 🔴"), 4 );
	sortCombo->addItem( tr("Sort: Logged In 👤"), 2 );
	sortCombo->addItem( tr("Sort: Logged Out 🔒"), 5 );
	sortCombo->addItem( tr("Sort: IP Address 🌐"), 3 );
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
		"QPushButton { background: #0F172A; border: 1px solid #2A323D; border-radius: 6px; padding: 2px 8px; color: #8B949E; font-size: 10px; font-weight: 600; }"
		"QPushButton:hover { background: #1F2630; color: #E6EDF3; border-color: #2563EB; }"
		"QPushButton:checked { background: #238636; color: #FFFFFF; font-weight: bold; border-color: #2563EB; }"
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
	// Automatically checks for newly enrolled network objects from Sentinel discovery every 20 seconds
	auto* directoryRefreshTimer = new QTimer( this );
	connect( directoryRefreshTimer, &QTimer::timeout, this, [this]() {
		VeyonCore::networkObjectDirectoryManager().configuredDirectory()->update();
		m_master.computerControlListModel().reload();
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
	inspectorPanelButton->setIcon( QIcon( QStringLiteral(":/core/icon32.png") ) );
	inspectorPanelButton->setCheckable( true );
	inspectorPanelButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
	inspectorPanelButton->setStyleSheet( QStringLiteral(
		"QToolButton {"
		"   background: #111A2D; border: 1px solid #21262D; border-radius: 4px;"
		"   color: #94A3B8; font-size: 11px; font-weight: 600; padding: 2px 8px; margin: 1px 2px;"
		"}"
		"QToolButton:hover { background: #162238; border-color: #3B82F6; color: #F1F5F9; }"
		"QToolButton:checked { background: #1E3A5F; border-color: #3B82F6; color: #FFFFFF; font-weight: bold; }"
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
			"QSplitter::handle:horizontal { width: 1px; background-color: #21262D; } "
			"QSplitter::handle:vertical { height: 1px; background-color: #21262D; } "
			"QSplitter::handle:hover { background-color: #21262D; }"
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

		QIcon richIcon;
		if( k.contains( QStringLiteral("monitor") ) )
		{
			richIcon = QIcon( QStringLiteral(":/remoteaccess/preferences-system-windows-effect-desktopgrid.png") );
		}
		else
		{
			richIcon = QIcon( feature.iconUrl() );
		}
		if( richIcon.isNull() )
		{
			richIcon = QIcon( feature.iconUrl() );
		}

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
					sep->setStyleSheet( QStringLiteral("QFrame { background-color: #21262D; border: none; margin: 8px 3px; }") );
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
