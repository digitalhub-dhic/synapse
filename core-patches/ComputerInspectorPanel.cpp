/*
 * ComputerInspectorPanel.cpp - Modern docked inspector with Public (Global) and Separate Computer modes
 *
 * Part of Synapse Master
 */

#include "ComputerInspectorPanel.h"
#include "VeyonMaster.h"
#include "FeatureManager.h"
#include "ComputerControlListModel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QComboBox>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QStackedWidget>

// Custom painted laptop illustration widget matching the mockup
class InspectorIllustrationWidget : public QWidget
{
public:
	explicit InspectorIllustrationWidget( QWidget* parent = nullptr ) : QWidget( parent )
	{
		setFixedHeight( 80 );
	}

protected:
	void paintEvent( QPaintEvent* ) override
	{
		QPainter p( this );
		p.setRenderHint( QPainter::Antialiasing );

		const int cx = width() / 2;
		const int cy = height() / 2;

		// Subtle network connection lines and nodes
		QPen linePen( QColor( 0x3b, 0x82, 0xf6, 120 ), 1.5, Qt::DashLine );
		p.setPen( linePen );
		p.drawLine( cx - 75, cy, cx - 40, cy );
		p.drawLine( cx + 40, cy, cx + 75, cy );

		// Floating network nodes
		p.setBrush( QColor( 0x3b, 0x82, 0xf6, 200 ) );
		p.setPen( Qt::NoPen );
		p.drawEllipse( cx - 75, cy - 3, 6, 6 );
		p.drawEllipse( cx - 60, cy - 12, 4, 4 );
		p.drawEllipse( cx + 75, cy - 3, 6, 6 );
		p.drawEllipse( cx + 60, cy + 12, 4, 4 );

		// Laptop base
		const int baseW = 76;
		const int baseH = 8;
		const int baseY = cy + 16;
		QPainterPath basePath;
		basePath.addRoundedRect( cx - baseW / 2, baseY, baseW, baseH, 4, 4 );
		p.setBrush( QColor( 0x16, 0x22, 0x38 ) );
		p.setPen( QPen( QColor( 0x3b, 0x82, 0xf6 ), 1.2 ) );
		p.drawPath( basePath );

		// Laptop screen bezel
		const int screenW = 60;
		const int screenH = 40;
		const int screenY = cy - 22;
		QPainterPath screenPath;
		screenPath.addRoundedRect( cx - screenW / 2, screenY, screenW, screenH, 5, 5 );
		p.setBrush( QColor( 0x0b, 0x10, 0x20 ) );
		p.setPen( QPen( QColor( 0x28, 0x38, 0x4f ), 1.5 ) );
		p.drawPath( screenPath );

		// Screen inner display
		p.setBrush( QColor( 0x11, 0x1a, 0x2d ) );
		p.setPen( Qt::NoPen );
		p.drawRoundedRect( cx - screenW / 2 + 4, screenY + 4, screenW - 8, screenH - 8, 3, 3 );

		// Glowing green checkmark badge in center
		const int badgeRadius = 11;
		p.setBrush( QColor( 0x05, 0x96, 0x69 ) );
		p.setPen( QPen( QColor( 0x34, 0xd3, 0x99 ), 1.5 ) );
		p.drawEllipse( QPoint( cx, screenY + screenH / 2 ), badgeRadius, badgeRadius );

		// Crisp white checkmark
		p.setPen( QPen( Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
		QPainterPath checkPath;
		checkPath.moveTo( cx - 4, screenY + screenH / 2 );
		checkPath.lineTo( cx - 1, screenY + screenH / 2 + 3 );
		checkPath.lineTo( cx + 5, screenY + screenH / 2 - 4 );
		p.drawPath( checkPath );
	}
};

ComputerInspectorPanel::ComputerInspectorPanel( VeyonMaster& master, QWidget* parent ) :
	QWidget( parent ),
	m_master( master ),
	m_countdownTimer( new QTimer( this ) ),
	m_clockTimer( new QTimer( this ) )
{
	connect( m_countdownTimer, &QTimer::timeout, this, &ComputerInspectorPanel::onCountdownTimerTick );
	connect( m_clockTimer, &QTimer::timeout, this, &ComputerInspectorPanel::onClockTick );
	connect( &m_master.computerControlListModel(), &ComputerControlListModel::stateChanged,
			 this, &ComputerInspectorPanel::onComputerStateChanged );
	m_clockTimer->start( 1000 );

	setupUi();
	switchTab( InspectorTab::Global );
}



void ComputerInspectorPanel::setupUi()
{
	setMinimumWidth( 220 );
	setMaximumWidth( 320 );
	setStyleSheet( QStringLiteral(
		"ComputerInspectorPanel, QWidget#ComputerInspectorPanel {"
		"   background-color: #090E1A;"
		"   border-left: 1px solid #21262D;"
		"}"
		"QScrollArea, QScrollArea > QWidget, QWidget#qt_scrollarea_viewport, QStackedWidget {"
		"   background-color: #090E1A;"
		"}"
		"QScrollArea { border: none; background-color: #090E1A; }"
		"QScrollBar:vertical {"
		"   border: none; background: #090E1A; width: 6px; margin: 0px;"
		"}"
		"QScrollBar::handle:vertical {"
		"   background: #0F172A; min-height: 20px; border-radius: 3px; border: 1px solid #2A323D;"
		"}"
		"QScrollBar::handle:vertical:hover { background: #238636; }"
	) );

	auto* mainLayout = new QVBoxLayout( this );
	mainLayout->setContentsMargins( 0, 0, 0, 10 );
	mainLayout->setSpacing( 0 );

	// 1. Header Frame (Exact 32px flush subheader matching simulator)
	auto* inspectorHeader = new QFrame( this );
	inspectorHeader->setFixedHeight( 32 );
	inspectorHeader->setStyleSheet( QStringLiteral( "QFrame { background-color: #090E1A; border-bottom: 1px solid #21262D; }" ) );

	auto* headerLayout = new QHBoxLayout( inspectorHeader );
	headerLayout->setContentsMargins( 12, 0, 12, 0 );
	headerLayout->setSpacing( 8 );

	auto* titleLabel = new QLabel( tr( "Inspector" ), inspectorHeader );
	titleLabel->setStyleSheet( QStringLiteral( "font-size: 12px; font-weight: 700; color: #E6EDF3;" ) );

	auto* subtitleLabel = new QLabel( tr( "Computer Controls" ), inspectorHeader );
	subtitleLabel->setStyleSheet( QStringLiteral( "font-size: 10px; color: #6B7280;" ) );

	headerLayout->addWidget( titleLabel );
	headerLayout->addStretch( 1 );
	headerLayout->addWidget( subtitleLabel );

	mainLayout->addWidget( inspectorHeader );

	auto* contentWidget = new QWidget( this );
	auto* contentLayout = new QVBoxLayout( contentWidget );
	contentLayout->setContentsMargins( 10, 8, 10, 8 );
	contentLayout->setSpacing( 8 );

	// 2. Segmented Navigation Bar: [ Global ]  [ Computer ]  [ Details ]
	auto* navFrame = new QFrame( contentWidget );
	navFrame->setStyleSheet( QStringLiteral(
		"QFrame {"
		"   background-color: #0F172A; border: 1px solid #2A323D; border-radius: 6px; padding: 2px;"
		"}"
	) );
	auto* navLayout = new QHBoxLayout( navFrame );
	navLayout->setContentsMargins( 2, 2, 2, 2 );
	navLayout->setSpacing( 4 );

	m_btnGlobal = new QPushButton( tr( "Global" ), navFrame );
	m_btnComputer = new QPushButton( tr( "Computer" ), navFrame );
	m_btnDetails = new QPushButton( tr( "Details" ), navFrame );

	for( auto* b : { m_btnGlobal, m_btnComputer, m_btnDetails } )
	{
		b->setFixedHeight( 24 );
		b->setCursor( Qt::PointingHandCursor );
	}

	navLayout->addWidget( m_btnGlobal, 1 );
	navLayout->addWidget( m_btnComputer, 1 );
	navLayout->addWidget( m_btnDetails, 1 );
	contentLayout->addWidget( navFrame );

	connect( m_btnGlobal, &QPushButton::clicked, this, [this]() { switchTab( InspectorTab::Global ); } );
	connect( m_btnComputer, &QPushButton::clicked, this, [this]() { switchTab( InspectorTab::Computer ); } );
	connect( m_btnDetails, &QPushButton::clicked, this, [this]() { switchTab( InspectorTab::Details ); } );

	// 3. Stacked Widget (Global, Computer, Details)
	m_stackedWidget = new QStackedWidget( contentWidget );
	m_stackedWidget->addWidget( createGlobalTabWidget() );    // Index 0
	m_stackedWidget->addWidget( createComputerTabWidget() );  // Index 1
	m_stackedWidget->addWidget( createDetailsTabWidget() );   // Index 2

	contentLayout->addWidget( m_stackedWidget, 1 );

	mainLayout->addWidget( contentWidget, 1 );
}

void ComputerInspectorPanel::switchTab( InspectorTab tab )
{
	const QString activeStyle = QStringLiteral(
		"QPushButton {"
		"   background-color: #238636; color: #ffffff; font-weight: bold; font-size: 11px;"
		"   border: none; border-radius: 6px; padding: 4px 8px;"
		"}"
	);
	const QString inactiveStyle = QStringLiteral(
		"QPushButton {"
		"   background-color: transparent; color: #8B949E; font-weight: 600; font-size: 11px;"
		"   border: none; border-radius: 6px; padding: 4px 8px;"
		"}"
		"QPushButton:hover { background-color: #0F172A; color: #E6EDF3; }"
	);

	m_btnGlobal->setStyleSheet( tab == InspectorTab::Global ? activeStyle : inactiveStyle );
	m_btnComputer->setStyleSheet( tab == InspectorTab::Computer ? activeStyle : inactiveStyle );
	m_btnDetails->setStyleSheet( tab == InspectorTab::Details ? activeStyle : inactiveStyle );

	m_stackedWidget->setCurrentIndex( static_cast<int>( tab ) );
	if( tab == InspectorTab::Global )
	{
		updateQuickStats();
	}
}



void ComputerInspectorPanel::onClockTick()
{
	if( m_footerClockLabel )
	{
		m_footerClockLabel->setText( QDateTime::currentDateTime().toString( QStringLiteral( "MMM d, yyyy  HH:mm" ) ) );
	}
	updateQuickStats();
}



void ComputerInspectorPanel::updateQuickStats()
{
	const auto& allInterfaces = m_master.allComputerControlInterfaces();
	const int total = allInterfaces.size();
	int online = 0;
	int offline = 0;
	int idle = 0;

	for( const auto& ctrl : allInterfaces )
	{
		if( ctrl && ctrl->state() == ComputerControlInterface::State::Connected )
		{
			online++;
			if( ctrl->userLoginName().trimmed().isEmpty() )
			{
				idle++;
			}
		}
		else
		{
			offline++;
		}
	}

	if( m_statTotalVal ) m_statTotalVal->setText( QString::number( total ) );
	if( m_statOnlineVal ) m_statOnlineVal->setText( QString::number( online ) );
	if( m_statIdleVal ) m_statIdleVal->setText( QString::number( idle ) );
	if( m_statOfflineVal ) m_statOfflineVal->setText( QString::number( offline ) );
}



QWidget* ComputerInspectorPanel::createGlobalTabWidget()
{
	auto* scrollArea = new QScrollArea( this );
	scrollArea->setWidgetResizable( true );
	scrollArea->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	scrollArea->setStyleSheet( QStringLiteral( "QScrollArea, QWidget#qt_scrollarea_viewport { background-color: #090E1A; border: none; }" ) );

	auto* container = new QWidget();
	container->setStyleSheet( QStringLiteral( "background-color: #090E1A;" ) );
	auto* layout = new QVBoxLayout( container );
	layout->setContentsMargins( 0, 4, 0, 4 );
	layout->setSpacing( 10 );

	// A. Dynamic Countdowns Container
	m_globalCountdownsContainer = new QWidget( container );
	m_globalCountdownsLayout = new QVBoxLayout( m_globalCountdownsContainer );
	m_globalCountdownsLayout->setContentsMargins( 0, 0, 0, 0 );
	m_globalCountdownsLayout->setSpacing( 8 );
	layout->addWidget( m_globalCountdownsContainer );
	rebuildGlobalTab();

	// B. "Quick Stats" Card
	auto* statsCard = new QFrame( container );
	statsCard->setStyleSheet( QStringLiteral(
		"QFrame {"
		"   background-color: #0F172A; border: 1px solid #2A323D; border-radius: 8px; padding: 6px;"
		"}"
	) );
	auto* statsLayout = new QVBoxLayout( statsCard );
	statsLayout->setContentsMargins( 10, 8, 10, 8 );
	statsLayout->setSpacing( 6 );

	auto* statsTitle = new QLabel( tr( "QUICK STATS" ), statsCard );
	statsTitle->setStyleSheet( QStringLiteral( "font-size: 10px; font-weight: 700; color: #8B949E; border: none; letter-spacing: 0.5px;" ) );
	statsLayout->addWidget( statsTitle );

	auto addStatRow = [&]( const QString& labelText, QLabel*& valLabel, const QString& valColor ) {
		auto* row = new QHBoxLayout();
		row->setSpacing( 8 );

		auto* nameLbl = new QLabel( labelText, statsCard );
		nameLbl->setStyleSheet( QStringLiteral( "font-size: 11px; color: #8B949E; border: none;" ) );

		valLabel = new QLabel( QStringLiteral( "0" ), statsCard );
		valLabel->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
		valLabel->setStyleSheet( QStringLiteral( "font-size: 11px; font-weight: bold; color: %1; border: none;" ).arg( valColor ) );

		row->addWidget( nameLbl );
		row->addStretch( 1 );
		row->addWidget( valLabel );
		statsLayout->addLayout( row );
	};

	addStatRow( tr( "Total Workstations" ), m_statTotalVal, QStringLiteral( "#FAF5EF" ) );
	addStatRow( tr( "Online Connected" ), m_statOnlineVal, QStringLiteral( "#10B981" ) );
	addStatRow( tr( "Standby / Logon Screen" ), m_statIdleVal, QStringLiteral( "#F59E0B" ) );
	addStatRow( tr( "Offline" ), m_statOfflineVal, QStringLiteral( "#6B7280" ) );

	layout->addWidget( statsCard );

	// C. "Common Actions" Card
	auto* actionsCard = new QFrame( container );
	actionsCard->setStyleSheet( QStringLiteral(
		"QFrame {"
		"   background-color: #0F172A; border: 1px solid #2A323D; border-radius: 8px; padding: 6px;"
		"}"
	) );
	auto* actionsLayout = new QVBoxLayout( actionsCard );
	actionsLayout->setContentsMargins( 10, 8, 10, 8 );
	actionsLayout->setSpacing( 6 );

	auto* actionsTitle = new QLabel( tr( "COMMON ACTIONS" ), actionsCard );
	actionsTitle->setStyleSheet( QStringLiteral( "font-size: 10px; font-weight: 700; color: #8B949E; border: none; letter-spacing: 0.5px;" ) );
	actionsLayout->addWidget( actionsTitle );

	auto addActionRow = [&]( const QString& iconText, const QString& titleText, const QString& featureName ) {
		auto* btn = new QPushButton( actionsCard );
		btn->setFixedHeight( 30 );
		btn->setCursor( Qt::PointingHandCursor );
		btn->setStyleSheet( QStringLiteral(
			"QPushButton {"
			"   background-color: #0F172A; border: 1px solid #2A323D; border-radius: 6px;"
			"   padding: 0px 8px; text-align: left;"
			"}"
			"QPushButton:hover { background-color: #1F2630; border-color: #238636; }"
		) );

		auto* bLayout = new QHBoxLayout( btn );
		bLayout->setContentsMargins( 6, 0, 6, 0 );
		auto* ic = new QLabel( iconText, btn );
		ic->setStyleSheet( QStringLiteral( "font-size: 11px; border: none; background: transparent;" ) );
		auto* txt = new QLabel( titleText, btn );
		txt->setStyleSheet( QStringLiteral( "font-size: 11px; font-weight: 500; color: #FAF5EF; border: none; background: transparent;" ) );
		auto* arr = new QLabel( QStringLiteral( "›" ), btn );
		arr->setStyleSheet( QStringLiteral( "font-size: 13px; color: #6B7280; font-weight: bold; border: none; background: transparent;" ) );

		bLayout->addWidget( ic );
		bLayout->addWidget( txt );
		bLayout->addStretch( 1 );
		bLayout->addWidget( arr );

		connect( btn, &QPushButton::clicked, this, [this, featureName]() {
			executeFeatureByName( featureName, {} );
		} );
		actionsLayout->addWidget( btn );
	};

	addActionRow( QStringLiteral( "💬" ), tr( "Send Message" ), QStringLiteral( "textmessage" ) );
	addActionRow( QStringLiteral( "🌐" ), tr( "Open Website" ), QStringLiteral( "openwebsite" ) );

	layout->addWidget( actionsCard );
	layout->addStretch( 1 );

	scrollArea->setWidget( container );
	return scrollArea;
}



void ComputerInspectorPanel::rebuildGlobalTab()
{
	if( !m_globalCountdownsLayout )
	{
		return;
	}

	// Clear old widgets
	QLayoutItem* item;
	while( ( item = m_globalCountdownsLayout->takeAt( 0 ) ) != nullptr )
	{
		if( item->widget() )
		{
			item->widget()->deleteLater();
		}
		delete item;
	}

	if( m_activeCountdowns.isEmpty() )
	{
		m_globalCountdownsContainer->hide();
		return;
	}

	m_globalCountdownsContainer->show();

	// Single compact fleet operation summary card
	m_globalCountdownsLayout->addWidget( createActiveOperationSummaryCard( m_globalCountdownsContainer ) );
}



QWidget* ComputerInspectorPanel::createComputerTabWidget()
{
	auto* scrollArea = new QScrollArea( this );
	scrollArea->setWidgetResizable( true );
	scrollArea->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	scrollArea->setStyleSheet( QStringLiteral( "QScrollArea, QWidget#qt_scrollarea_viewport { background-color: #090E1A; border: none; }" ) );

	m_computerContentContainer = new QWidget();
	m_computerContentContainer->setStyleSheet( QStringLiteral( "background-color: #090E1A;" ) );
	auto* layout = new QVBoxLayout( m_computerContentContainer );
	layout->setContentsMargins( 0, 4, 0, 4 );
	layout->setSpacing( 10 );
	m_computerContentLayout = layout;

	rebuildComputerTab();

	scrollArea->setWidget( m_computerContentContainer );
	return scrollArea;
}



void ComputerInspectorPanel::rebuildComputerTab()
{
	if( !m_computerContentLayout )
	{
		return;
	}

	// Clear existing widgets safely
	QLayoutItem* child;
	while( ( child = m_computerContentLayout->takeAt( 0 ) ) != nullptr )
	{
		if( child->widget() )
		{
			child->widget()->deleteLater();
		}
		delete child;
	}

	// Empty State: No computer selected
	if( m_selectedInterfaces.isEmpty() )
	{
		auto* emptyCard = new QFrame( m_computerContentContainer );
		emptyCard->setStyleSheet( QStringLiteral(
			"QFrame {"
			"   background-color: #0F172A; border: 1px dashed #2A323D; border-radius: 10px; padding: 20px;"
			"}"
		) );
		auto* eLayout = new QVBoxLayout( emptyCard );
		eLayout->setSpacing( 8 );
		eLayout->setAlignment( Qt::AlignCenter );

		auto* eIcon = new QLabel( QStringLiteral( "🖥" ), emptyCard );
		eIcon->setAlignment( Qt::AlignCenter );
		eIcon->setStyleSheet( QStringLiteral( "font-size: 36px; color: #238636; border: none; padding-bottom: 4px;" ) );

		auto* eTitle = new QLabel( tr( "No Computer Selected" ), emptyCard );
		eTitle->setAlignment( Qt::AlignCenter );
		eTitle->setStyleSheet( QStringLiteral( "font-size: 14px; font-weight: bold; color: #E6EDF3; border: none;" ) );

		auto* eDesc = new QLabel( tr( "Click on any computer in the grid to inspect its separate live controls, countdown timer, and screen telemetry." ), emptyCard );
		eDesc->setAlignment( Qt::AlignCenter );
		eDesc->setWordWrap( true );
		eDesc->setStyleSheet( QStringLiteral( "font-size: 11px; color: #8B949E; border: none;" ) );

		auto* btnGlobalSwitch = new QPushButton( tr( "🌐 View Global Lab Stats" ), emptyCard );
		btnGlobalSwitch->setFixedHeight( 32 );
		btnGlobalSwitch->setCursor( Qt::PointingHandCursor );
		btnGlobalSwitch->setStyleSheet( QStringLiteral(
			"QPushButton {"
			"   background-color: #238636; color: #ffffff; font-weight: bold; font-size: 11px;"
			"   border: none; border-radius: 6px; padding: 4px 12px; margin-top: 6px;"
			"}"
			"QPushButton:hover { background-color: #238636; }"
		) );
		connect( btnGlobalSwitch, &QPushButton::clicked, this, [this]() { switchTab( InspectorTab::Global ); } );

		eLayout->addWidget( eIcon );
		eLayout->addWidget( eTitle );
		eLayout->addWidget( eDesc );
		eLayout->addWidget( btnGlobalSwitch );

		m_computerContentLayout->addWidget( emptyCard );
		m_computerContentLayout->addStretch( 1 );
		return;
	}

	// When multiple computers are selected: show computer switcher dropdown
	if( m_selectedInterfaces.size() > 1 )
	{
		auto* selectorCard = new QFrame( m_computerContentContainer );
		selectorCard->setStyleSheet( QStringLiteral(
			"QFrame { background-color: #0F172A; border: 1px solid #2A323D; border-radius: 8px; padding: 4px; }"
		) );
		auto* sLayout = new QHBoxLayout( selectorCard );
		sLayout->setContentsMargins( 6, 4, 6, 4 );

		auto* sLbl = new QLabel( tr( "Inspect:" ), selectorCard );
		sLbl->setStyleSheet( QStringLiteral( "font-size: 11px; font-weight: bold; color: #8B949E; border: none;" ) );

		m_computerSelectorCombo = new QComboBox( selectorCard );
		m_computerSelectorCombo->setFixedHeight( 28 );
		m_computerSelectorCombo->setStyleSheet( QStringLiteral(
			"QComboBox {"
			"   background-color: #0F172A; border: 1px solid #2A323D; border-radius: 6px;"
			"   color: #E6EDF3; font-size: 11px; font-weight: 600; padding: 2px 8px;"
			"}"
			"QComboBox::drop-down { border: none; width: 20px; }"
			"QComboBox QAbstractItemView {"
			"   background-color: #0F172A; color: #E6EDF3; selection-background-color: #238636; border: 1px solid #2A323D;"
			"}"
		) );

		for( int i = 0; i < m_selectedInterfaces.size(); ++i )
		{
			const auto& ctrl = m_selectedInterfaces[i];
			const QString cName = ctrl->computerName().isEmpty() ? tr( "Computer %1" ).arg( i + 1 ) : ctrl->computerName();
			const QString status = ( ctrl->state() == ComputerControlInterface::State::Connected ) ? QStringLiteral( "Online" ) : QStringLiteral( "Offline" );
			m_computerSelectorCombo->addItem( QStringLiteral( "💻 %1 (%2)" ).arg( cName, status ), i );
		}
		m_computerSelectorCombo->addItem( tr( "👥 All Selected (%1 Computers)" ).arg( m_selectedInterfaces.size() ), -1 );

		if( m_currentComputerIndex >= m_selectedInterfaces.size() )
		{
			m_currentComputerIndex = 0;
		}
		const int comboIdx = ( m_currentComputerIndex == -1 ) ? m_selectedInterfaces.size() : m_currentComputerIndex;
		m_computerSelectorCombo->setCurrentIndex( comboIdx );

		connect( m_computerSelectorCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this]( int idx ) {
			m_currentComputerIndex = m_computerSelectorCombo->itemData( idx ).toInt();
			QTimer::singleShot( 0, this, [this]() { rebuildComputerTab(); } );
		} );

		sLayout->addWidget( sLbl );
		sLayout->addWidget( m_computerSelectorCombo, 1 );
		m_computerContentLayout->addWidget( selectorCard );
	}

	// Determine targeted computers
	ComputerControlInterfaceList targets;
	ComputerControlInterface::Pointer singleCtrl;

	if( m_selectedInterfaces.size() == 1 || ( m_currentComputerIndex >= 0 && m_currentComputerIndex < m_selectedInterfaces.size() ) )
	{
		singleCtrl = m_selectedInterfaces.at( m_currentComputerIndex >= 0 ? m_currentComputerIndex : 0 );
		targets = { singleCtrl };
	}
	else
	{
		// Batch control all selected
		targets = m_selectedInterfaces;
	}

	// 1. Specific Countdown Card for this machine
	if( singleCtrl )
	{
		const QString cKey = computerKey( singleCtrl );
		if( m_activeCountdowns.contains( cKey ) )
		{
			m_computerContentLayout->addWidget( createCountdownCard( m_computerContentContainer, m_activeCountdowns[cKey] ) );
		}
		// 2. Machine Info Card
		m_computerContentLayout->addWidget( createMachineInfoCard( m_computerContentContainer, singleCtrl ) );
	}
	else
	{
		// Group info card
		auto* grpCard = new QFrame( m_computerContentContainer );
		grpCard->setStyleSheet( QStringLiteral(
			"QFrame { background-color: #0F172A; border: 1px solid #2A323D; border-radius: 10px; padding: 10px; }"
		) );
		auto* gLayout = new QVBoxLayout( grpCard );
		auto* gTitle = new QLabel( tr( "Batch Control: %1 Computers" ).arg( targets.size() ), grpCard );
		gTitle->setStyleSheet( QStringLiteral( "font-size: 13px; font-weight: bold; color: #E6EDF3; border: none;" ) );
		auto* gSub = new QLabel( tr( "Any action taken here applies simultaneously to all selected computers." ), grpCard );
		gSub->setStyleSheet( QStringLiteral( "font-size: 11px; color: #8B949E; border: none;" ) );
		gLayout->addWidget( gTitle );
		gLayout->addWidget( gSub );
		m_computerContentLayout->addWidget( grpCard );

		// If any countdowns are active, display Master Batch Countdown Card
		if( !m_activeCountdowns.isEmpty() )
		{
			m_computerContentLayout->addWidget( createMasterBatchCountdownCard( m_computerContentContainer ) );
		}
	}

	// 3. Power Operations Hub Card
	auto* powerCard = createActionHubCard( m_computerContentContainer, tr( "Power Operations" ), QStringLiteral( "#F87171" ) );
	auto* powerGrid = new QGridLayout();
	powerGrid->setSpacing( 6 );

	auto* btnWOL = new QPushButton( tr( "⚡ Power On" ), powerCard );
	auto* btnReboot = new QPushButton( tr( "🔄 Reboot" ), powerCard );
	auto* btnPowerDown = new QPushButton( tr( "⏻ Shut Down" ), powerCard );
	auto* btnTimed = new QPushButton( tr( "⏱ Timed Power Down" ), powerCard );

	const QString actionBtnStyle = QStringLiteral(
		"QPushButton {"
		"   background: #0F172A; border: 1px solid #2A323D; border-radius: 6px;"
		"   font-size: 11px; font-weight: 500; color: #E6EDF3; padding: 4px 6px; min-height: 26px;"
		"}"
		"QPushButton:hover { background: #1F2630; border-color: #238636; }"
	);
	for( auto* b : { btnWOL, btnReboot, btnPowerDown, btnTimed } )
	{
		b->setStyleSheet( actionBtnStyle );
		b->setCursor( Qt::PointingHandCursor );
	}

	connect( btnWOL, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("poweron"), targets ); } );
	connect( btnReboot, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("reboot"), targets ); } );
	connect( btnPowerDown, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("powerdown"), targets ); } );
	connect( btnTimed, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("powerdowndelayed"), targets ); } );

	powerGrid->addWidget( btnWOL, 0, 0 );
	powerGrid->addWidget( btnReboot, 0, 1 );
	powerGrid->addWidget( btnPowerDown, 1, 0 );
	powerGrid->addWidget( btnTimed, 1, 1 );
	qobject_cast<QVBoxLayout*>( powerCard->layout() )->addLayout( powerGrid );
	m_computerContentLayout->addWidget( powerCard );

	// 4. Freeze & Screen Lock Card
	auto* freezeCard = createActionHubCard( m_computerContentContainer, tr( "Freeze & Screen Lock" ), QStringLiteral( "#238636" ) );
	auto* freezeGrid = new QGridLayout();
	freezeGrid->setSpacing( 6 );

	auto* btnFreeze = new QPushButton( tr( "❄️ Freeze Screen" ), freezeCard );
	auto* btnTimedLock = new QPushButton( tr( "⏱️ Timed Lock" ), freezeCard );
	auto* btnInputLock = new QPushButton( tr( "🔒 Lock Inputs Only" ), freezeCard );
	auto* btnUnfreeze = new QPushButton( tr( "🟢 Unfreeze" ), freezeCard );

	for( auto* b : { btnFreeze, btnTimedLock, btnInputLock, btnUnfreeze } )
	{
		b->setStyleSheet( actionBtnStyle );
		b->setCursor( Qt::PointingHandCursor );
	}

	connect( btnFreeze, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("ScreenLock"), targets ); } );
	connect( btnTimedLock, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("TimedLock"), targets ); } );
	connect( btnInputLock, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("InputDevicesLock"), targets ); } );
	connect( btnUnfreeze, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("UnfreezeScreen"), targets ); } );

	freezeGrid->addWidget( btnFreeze, 0, 0 );
	freezeGrid->addWidget( btnTimedLock, 0, 1 );
	freezeGrid->addWidget( btnInputLock, 1, 0 );
	freezeGrid->addWidget( btnUnfreeze, 1, 1 );
	qobject_cast<QVBoxLayout*>( freezeCard->layout() )->addLayout( freezeGrid );
	m_computerContentLayout->addWidget( freezeCard );

	// 5. Remote Access & Control Card
	auto* remoteCard = createActionHubCard( m_computerContentContainer, tr( "Remote Access & Control" ), QStringLiteral( "#60A5FA" ) );
	auto* remoteGrid = new QGridLayout();
	remoteGrid->setSpacing( 6 );

	auto* btnRemoteCtrl = new QPushButton( tr( "🖥 Remote Control" ), remoteCard );
	auto* btnRemoteView = new QPushButton( tr( "👁 Remote View" ), remoteCard );

	for( auto* b : { btnRemoteCtrl, btnRemoteView } )
	{
		b->setStyleSheet( actionBtnStyle );
		b->setCursor( Qt::PointingHandCursor );
	}

	connect( btnRemoteCtrl, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("RemoteControl"), targets ); } );
	connect( btnRemoteView, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("RemoteView"), targets ); } );

	remoteGrid->addWidget( btnRemoteCtrl, 0, 0 );
	remoteGrid->addWidget( btnRemoteView, 0, 1 );
	qobject_cast<QVBoxLayout*>( remoteCard->layout() )->addLayout( remoteGrid );
	m_computerContentLayout->addWidget( remoteCard );

	// 6. Classroom Tools Card
	auto* toolsCard = createActionHubCard( m_computerContentContainer, tr( "Classroom Tools" ), QStringLiteral( "#2563EB" ) );
	auto* toolsGrid = new QGridLayout();
	toolsGrid->setSpacing( 6 );

	auto* btnMsg = new QPushButton( tr( "💬 Send Message" ), toolsCard );
	auto* btnFiles = new QPushButton( tr( "📁 Distribute Files" ), toolsCard );
	auto* btnLogoff = new QPushButton( tr( "🚪 Log Off User" ), toolsCard );

	for( auto* b : { btnMsg, btnFiles, btnLogoff } )
	{
		b->setStyleSheet( actionBtnStyle );
		b->setCursor( Qt::PointingHandCursor );
	}

	connect( btnMsg, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("TextMessage"), targets ); } );
	connect( btnFiles, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("DistributeFiles"), targets ); } );
	connect( btnLogoff, &QPushButton::clicked, this, [this, targets]() { executeFeatureByName( QStringLiteral("UserLogoff"), targets ); } );

	toolsGrid->addWidget( btnMsg, 0, 0 );
	toolsGrid->addWidget( btnFiles, 0, 1 );
	toolsGrid->addWidget( btnLogoff, 1, 0, 1, 2 );
	qobject_cast<QVBoxLayout*>( toolsCard->layout() )->addLayout( toolsGrid );
	m_computerContentLayout->addWidget( toolsCard );

	m_computerContentLayout->addStretch( 1 );
	updateCountdownDisplay();
}



QWidget* ComputerInspectorPanel::createDetailsTabWidget()
{
	auto* scrollArea = new QScrollArea( this );
	scrollArea->setWidgetResizable( true );
	scrollArea->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	scrollArea->setStyleSheet( QStringLiteral( "QScrollArea, QWidget#qt_scrollarea_viewport { background-color: #090E1A; border: none; }" ) );

	auto* container = new QWidget();
	container->setStyleSheet( QStringLiteral( "background-color: #090E1A;" ) );
	auto* layout = new QVBoxLayout( container );
	layout->setContentsMargins( 0, 4, 0, 4 );
	layout->setSpacing( 10 );

	// Telemetry Card
	auto* telemCard = new QFrame( container );
	telemCard->setStyleSheet( QStringLiteral(
		"QFrame { background-color: #0F172A; border: 1px solid #2A323D; border-radius: 10px; padding: 12px; }"
	) );
	auto* tLayout = new QVBoxLayout( telemCard );
	tLayout->setSpacing( 8 );

	auto* tTitle = new QLabel( tr( "Synapse Command Engine" ), telemCard );
	tTitle->setStyleSheet( QStringLiteral( "font-size: 13px; font-weight: bold; color: #E6EDF3; border: none;" ) );
	tLayout->addWidget( tTitle );

	auto addDetailRow = [&]( const QString& key, const QString& val, const QString& color = QStringLiteral( "#8B949E" ) ) {
		auto* row = new QHBoxLayout();
		auto* kLbl = new QLabel( key, telemCard );
		kLbl->setStyleSheet( QStringLiteral( "font-size: 11px; color: #8B949E; border: none;" ) );
		auto* vLbl = new QLabel( val, telemCard );
		vLbl->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
		vLbl->setStyleSheet( QStringLiteral( "font-size: 11px; font-weight: 600; color: %1; border: none;" ).arg( color ) );
		row->addWidget( kLbl );
		row->addStretch( 1 );
		row->addWidget( vLbl );
		tLayout->addLayout( row );
	};

	addDetailRow( tr( "Version" ), QStringLiteral( "4.11.3 (x86_64)" ), QStringLiteral( "#238636" ) );
	addDetailRow( tr( "Security State" ), tr( "Shield Active" ), QStringLiteral( "#2563EB" ) );
	addDetailRow( tr( "UDP Auto-Discovery" ), tr( "Port 11400 (Active)" ), QStringLiteral( "#2563EB" ) );
	addDetailRow( tr( "TaskMgr Shield" ), tr( "Enforced on Shutdown" ), QStringLiteral( "#238636" ) );
	addDetailRow( tr( "AutoEndTasks" ), tr( "Enabled (Registry Force)" ), QStringLiteral( "#2563EB" ) );
	addDetailRow( tr( "Subnet Probe Sweep" ), tr( "Every 15 Seconds" ), QStringLiteral( "#8B949E" ) );

	layout->addWidget( telemCard );
	layout->addStretch( 1 );

	scrollArea->setWidget( container );
	return scrollArea;
}



void ComputerInspectorPanel::adjustAllCountdowns( int deltaSeconds )
{
	if( m_activeCountdowns.isEmpty() )
	{
		return;
	}

	QMap<int, ComputerControlInterfaceList> timeToTargets;

	for( auto it = m_activeCountdowns.begin(); it != m_activeCountdowns.end(); ++it )
	{
		auto& item = it.value();
		item.remainingSeconds = qMax( 1, item.remainingSeconds + deltaSeconds );
		if( deltaSeconds > 0 )
		{
			item.totalSeconds = qMax( item.totalSeconds, item.remainingSeconds );
		}

		if( item.key == QStringLiteral("global") || item.targets.isEmpty() )
		{
			if( item.adjustCallback )
			{
				item.adjustCallback( item.remainingSeconds );
			}
		}
		else
		{
			for( const auto& t : item.targets )
			{
				timeToTargets[item.remainingSeconds].append( t );
			}
		}
	}

	for( const auto& feat : m_master.allFeatures() )
	{
		if( feat.name().compare( QStringLiteral("PowerDownDelayed"), Qt::CaseInsensitive ) == 0 )
		{
			for( auto it = timeToTargets.begin(); it != timeToTargets.end(); ++it )
			{
				VeyonCore::featureManager().controlFeature( feat.uid(), FeatureProviderInterface::Operation::Start,
					{{QStringLiteral("shutdownTimeout"), it.key()},
					 {QStringLiteral("ShutdownTimeout"), it.key()},
					 {QStringLiteral("cancel"), false},
					 {QStringLiteral("Cancel"), false}}, it.value() );
			}
			break;
		}
	}

	updateCountdownDisplay();
}



void ComputerInspectorPanel::cancelAllCountdowns()
{
	if( m_activeCountdowns.isEmpty() )
	{
		return;
	}

	ComputerControlInterfaceList allTargets;
	for( const auto& item : m_activeCountdowns )
	{
		for( const auto& target : item.targets )
		{
			if( target && !allTargets.contains( target ) )
			{
				allTargets.append( target );
			}
		}
		m_cancelledTargets.insert( item.key );
	}

	if( allTargets.isEmpty() )
	{
		allTargets = m_master.allComputerControlInterfaces();
	}

	executeFeatureByName( QStringLiteral("RevertPowerDown"), allTargets );
	clearActiveCountdown( {} );
}



QWidget* ComputerInspectorPanel::createMasterBatchCountdownCard( QWidget* parent )
{
	return createActiveOperationSummaryCard( parent );
}



QWidget* ComputerInspectorPanel::createActiveOperationSummaryCard( QWidget* parent )
{
	auto* card = new QFrame( parent );
	card->setStyleSheet( QStringLiteral(
		"QFrame {"
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 8px;"
		"   padding: 2px;"
		"}"
	) );

	auto* layout = new QVBoxLayout( card );
	layout->setContentsMargins( 10, 8, 10, 8 );
	layout->setSpacing( 6 );

	// Row 1: Header - Title and In-Progress Status Tag
	auto* headerRow = new QHBoxLayout();
	headerRow->setSpacing( 4 );

	auto* titleLbl = new QLabel( tr( "POWER DOWN" ), card );
	titleLbl->setStyleSheet( QStringLiteral(
		"font-size: 11px; font-weight: 700; color: #E6EDF3; letter-spacing: 0.5px; border: none; background: transparent;"
	) );

	auto* statusTag = new QLabel( tr( "● Active" ), card );
	statusTag->setStyleSheet( QStringLiteral(
		"font-size: 9px; font-weight: 600; color: #F59E0B; background-color: #261B0E; border: 1px solid #78350F; border-radius: 4px; padding: 1px 5px;"
	) );

	headerRow->addWidget( titleLbl );
	headerRow->addStretch( 1 );
	headerRow->addWidget( statusTag );
	layout->addLayout( headerRow );

	// Divider
	auto* div1 = new QFrame( card );
	div1->setFrameShape( QFrame::HLine );
	div1->setStyleSheet( QStringLiteral( "background-color: #1E293B; max-height: 1px; border: none;" ) );
	layout->addWidget( div1 );

	// Row 2: Overview metrics: Count and Pending
	auto* metricsRow = new QHBoxLayout();
	metricsRow->setSpacing( 4 );

	const int numComputers = m_activeCountdowns.size();
	auto* countLbl = new QLabel( tr( "%1 computers" ).arg( numComputers ), card );
	countLbl->setStyleSheet( QStringLiteral(
		"font-size: 11px; font-weight: 600; color: #E2E8F0; border: none; background: transparent;"
	) );

	auto* pendingLbl = new QLabel( tr( "● %1 pending" ).arg( numComputers ), card );
	pendingLbl->setStyleSheet( QStringLiteral(
		"font-size: 10px; font-weight: 500; color: #F59E0B; border: none; background: transparent;"
	) );

	metricsRow->addWidget( countLbl );
	metricsRow->addStretch( 1 );
	metricsRow->addWidget( pendingLbl );
	layout->addLayout( metricsRow );

	// Row 3: Countdown Time + remaining label
	int minRemaining = 999999;
	int maxTotal = 1;
	for( const auto& item : m_activeCountdowns )
	{
		if( item.remainingSeconds < minRemaining ) minRemaining = item.remainingSeconds;
		if( item.totalSeconds > maxTotal ) maxTotal = item.totalSeconds;
	}
	if( minRemaining == 999999 ) minRemaining = 0;

	const auto timeStr = QStringLiteral( "%1:%2" )
						 .arg( minRemaining / 60, 2, 10, QLatin1Char('0') )
						 .arg( minRemaining % 60, 2, 10, QLatin1Char('0') );

	auto* timeRow = new QHBoxLayout();
	timeRow->setSpacing( 6 );

	auto* timeLbl = new QLabel( timeStr, card );
	timeLbl->setStyleSheet( QStringLiteral(
		"font-size: 20px; font-weight: 700; color: #F8FAFC; border: none; background: transparent; font-family: monospace;"
	) );

	auto* remLbl = new QLabel( tr( "remaining" ), card );
	remLbl->setStyleSheet( QStringLiteral(
		"font-size: 11px; color: #94A3B8; border: none; background: transparent; padding-top: 4px;"
	) );

	timeRow->addWidget( timeLbl );
	timeRow->addWidget( remLbl );
	timeRow->addStretch( 1 );
	layout->addLayout( timeRow );

	// Row 4: Progress Bar
	auto* pBar = new QProgressBar( card );
	pBar->setFixedHeight( 4 );
	pBar->setTextVisible( false );
	pBar->setStyleSheet( QStringLiteral(
		"QProgressBar {"
		"   background-color: #1E293B; border: none; border-radius: 2px;"
		"}"
		"QProgressBar::chunk {"
		"   background-color: #F59E0B; border-radius: 2px;"
		"}"
	) );
	pBar->setMaximum( maxTotal );
	pBar->setValue( maxTotal - minRemaining );
	layout->addWidget( pBar );

	// Row 5: Action Controls
	// Time adjustments row: [-1m] [+1m]
	auto* adjRow = new QHBoxLayout();
	adjRow->setSpacing( 4 );

	auto* btnM1 = new QPushButton( tr( "-1m" ), card );
	auto* btnP1 = new QPushButton( tr( "+1m" ), card );

	const QString adjBtnStyle = QStringLiteral(
		"QPushButton {"
		"   background-color: #1E293B; color: #E2E8F0; font-size: 11px; font-weight: 500;"
		"   border: 1px solid #334155; border-radius: 5px; padding: 3px 6px; min-height: 24px;"
		"}"
		"QPushButton:hover { background-color: #334155; border-color: #475569; color: #FFFFFF; }"
	);

	for( auto* b : { btnM1, btnP1 } )
	{
		b->setStyleSheet( adjBtnStyle );
		b->setCursor( Qt::PointingHandCursor );
	}
	btnM1->setToolTip( tr( "Reduce countdown by 1 minute on all computers" ) );
	btnP1->setToolTip( tr( "Add 1 minute to countdown on all computers" ) );

	connect( btnM1, &QPushButton::clicked, this, [this]() { adjustAllCountdowns( -60 ); } );
	connect( btnP1, &QPushButton::clicked, this, [this]() { adjustAllCountdowns( 60 ); } );

	adjRow->addWidget( btnM1 );
	adjRow->addWidget( btnP1 );
	layout->addLayout( adjRow );

	// Destructive Cancel Button
	auto* btnCancelAll = new QPushButton( tr( "Cancel Operation" ), card );
	btnCancelAll->setCursor( Qt::PointingHandCursor );
	btnCancelAll->setStyleSheet( QStringLiteral(
		"QPushButton {"
		"   background-color: #DC2626; color: #FFFFFF; font-weight: 600; font-size: 11px;"
		"   border: none; border-radius: 5px; padding: 4px 8px; min-height: 26px;"
		"}"
		"QPushButton:hover { background-color: #B91C1C; }"
	) );
	btnCancelAll->setToolTip( tr( "Cancel power down and abort shutdown on all computers" ) );
	connect( btnCancelAll, &QPushButton::clicked, this, [this]() { cancelAllCountdowns(); } );
	layout->addWidget( btnCancelAll );

	// Divider before Selected Computers
	auto* div2 = new QFrame( card );
	div2->setFrameShape( QFrame::HLine );
	div2->setStyleSheet( QStringLiteral( "background-color: #1E293B; max-height: 1px; border: none; margin-top: 4px;" ) );
	layout->addWidget( div2 );

	// Subheader: SELECTED COMPUTERS
	auto* compHeaderRow = new QHBoxLayout();
	compHeaderRow->setSpacing( 4 );

	auto* selTitle = new QLabel( tr( "SELECTED COMPUTERS" ), card );
	selTitle->setStyleSheet( QStringLiteral(
		"font-size: 10px; font-weight: 700; color: #8B949E; letter-spacing: 0.5px; border: none; background: transparent;"
	) );

	auto* selCount = new QLabel( tr( "%1 selected" ).arg( numComputers ), card );
	selCount->setStyleSheet( QStringLiteral(
		"font-size: 10px; color: #64748B; border: none; background: transparent;"
	) );

	compHeaderRow->addWidget( selTitle );
	compHeaderRow->addStretch( 1 );
	compHeaderRow->addWidget( selCount );
	layout->addLayout( compHeaderRow );

	// Compact scrollable list for computers
	auto* listScroll = new QScrollArea( card );
	listScroll->setWidgetResizable( true );
	listScroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	listScroll->setStyleSheet( QStringLiteral(
		"QScrollArea { background-color: #0B1120; border: 1px solid #1E293B; border-radius: 5px; }"
		"QWidget#qt_scrollarea_viewport { background-color: #0B1120; }"
		"QScrollBar:vertical { background: #0B1120; width: 6px; margin: 0; }"
		"QScrollBar::handle:vertical { background: #334155; border-radius: 3px; min-height: 14px; }"
		"QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
	) );
	listScroll->setMaximumHeight( 150 );

	auto* listContainer = new QWidget();
	listContainer->setStyleSheet( QStringLiteral( "background-color: #0B1120;" ) );
	auto* listLayout = new QVBoxLayout( listContainer );
	listLayout->setContentsMargins( 6, 4, 6, 4 );
	listLayout->setSpacing( 2 );

	for( const auto& cdItem : m_activeCountdowns )
	{
		auto* rowWidget = new QWidget( listContainer );
		auto* rowLayout = new QHBoxLayout( rowWidget );
		rowLayout->setContentsMargins( 2, 2, 2, 2 );
		rowLayout->setSpacing( 4 );

		// Clean computer name (strip "Shut Down: " prefix if present)
		QString cName = cdItem.title;
		if( cName.startsWith( QStringLiteral( "Shut Down: " ) ) )
		{
			cName = cName.mid( 11 );
		}
		if( cName.isEmpty() )
		{
			cName = cdItem.key;
		}

		auto* nameLbl = new QLabel( cName, rowWidget );
		nameLbl->setStyleSheet( QStringLiteral( "font-size: 11px; color: #CBD5E1; border: none; background: transparent;" ) );

		auto* statLbl = new QLabel( tr( "Pending" ), rowWidget );
		statLbl->setStyleSheet( QStringLiteral( "font-size: 10px; color: #F59E0B; border: none; background: transparent;" ) );

		rowLayout->addWidget( nameLbl, 1 );
		rowLayout->addWidget( statLbl );
		listLayout->addWidget( rowWidget );
	}
	listLayout->addStretch( 1 );
	listScroll->setWidget( listContainer );
	layout->addWidget( listScroll );

	// Register in m_countdownUiMap with special key "__master_batch__"
	auto& uiList = m_countdownUiMap[QStringLiteral( "__master_batch__" )];
	uiList.erase(
		std::remove_if( uiList.begin(), uiList.end(),
						[]( const CountdownUiSet& s ) { return s.timeLabel.isNull(); } ),
		uiList.end() );
	uiList.append( { pendingLbl, timeLbl, pBar } );

	return card;
}



QWidget* ComputerInspectorPanel::createCountdownCard( QWidget* parent, const CountdownItem& item )
{
	auto* card = new QFrame( parent );
	card->setStyleSheet( QStringLiteral(
		"QFrame {"
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 8px;"
		"   padding: 2px;"
		"}"
	) );

	auto* layout = new QVBoxLayout( card );
	layout->setContentsMargins( 10, 8, 10, 8 );
	layout->setSpacing( 6 );

	// Header: Computer name & Status
	auto* headerRow = new QHBoxLayout();
	headerRow->setSpacing( 4 );

	QString titleText = item.title;
	if( titleText.isEmpty() )
	{
		titleText = tr( "Power Down" );
	}
	auto* titleLbl = new QLabel( titleText, card );
	titleLbl->setStyleSheet( QStringLiteral(
		"font-size: 11px; font-weight: 700; color: #E6EDF3; letter-spacing: 0.5px; border: none; background: transparent;"
	) );

	auto* statusTag = new QLabel( tr( "● Pending" ), card );
	statusTag->setStyleSheet( QStringLiteral(
		"font-size: 9px; font-weight: 600; color: #F59E0B; background-color: #261B0E; border: 1px solid #78350F; border-radius: 4px; padding: 1px 5px;"
	) );

	headerRow->addWidget( titleLbl, 1 );
	headerRow->addWidget( statusTag );
	layout->addLayout( headerRow );

	// Divider
	auto* div = new QFrame( card );
	div->setFrameShape( QFrame::HLine );
	div->setStyleSheet( QStringLiteral( "background-color: #1E293B; max-height: 1px; border: none;" ) );
	layout->addWidget( div );

	// Time Row
	const auto timeStr = QStringLiteral( "%1:%2" )
						 .arg( item.remainingSeconds / 60, 2, 10, QLatin1Char('0') )
						 .arg( item.remainingSeconds % 60, 2, 10, QLatin1Char('0') );

	auto* timeRow = new QHBoxLayout();
	timeRow->setSpacing( 6 );

	auto* timeLbl = new QLabel( timeStr, card );
	timeLbl->setStyleSheet( QStringLiteral(
		"font-size: 18px; font-weight: 700; color: #F8FAFC; border: none; background: transparent; font-family: monospace;"
	) );

	auto* remLbl = new QLabel( tr( "remaining" ), card );
	remLbl->setStyleSheet( QStringLiteral(
		"font-size: 11px; color: #94A3B8; border: none; background: transparent; padding-top: 2px;"
	) );

	timeRow->addWidget( timeLbl );
	timeRow->addWidget( remLbl );
	timeRow->addStretch( 1 );
	layout->addLayout( timeRow );

	// Progress bar
	auto* pBar = new QProgressBar( card );
	pBar->setFixedHeight( 4 );
	pBar->setTextVisible( false );
	pBar->setStyleSheet( QStringLiteral(
		"QProgressBar {"
		"   background-color: #1E293B; border: none; border-radius: 2px;"
		"}"
		"QProgressBar::chunk {"
		"   background-color: #F59E0B; border-radius: 2px;"
		"}"
	) );
	pBar->setMaximum( qMax( 1, item.totalSeconds ) );
	pBar->setValue( item.totalSeconds - item.remainingSeconds );
	layout->addWidget( pBar );

	// Buttons: [-1m] [+1m]
	auto* btnRow = new QHBoxLayout();
	btnRow->setSpacing( 4 );

	auto* btnDec = new QPushButton( tr( "-1m" ), card );
	auto* btnInc = new QPushButton( tr( "+1m" ), card );

	const QString btnSubStyle = QStringLiteral(
		"QPushButton {"
		"   background-color: #1E293B; color: #E2E8F0; font-size: 11px; font-weight: 500;"
		"   border: 1px solid #334155; border-radius: 5px; padding: 3px 6px; min-height: 24px;"
		"}"
		"QPushButton:hover { background-color: #334155; border-color: #475569; color: #FFFFFF; }"
	);
	btnDec->setStyleSheet( btnSubStyle );
	btnInc->setStyleSheet( btnSubStyle );
	btnDec->setCursor( Qt::PointingHandCursor );
	btnInc->setCursor( Qt::PointingHandCursor );

	btnRow->addWidget( btnDec );
	btnRow->addWidget( btnInc );
	layout->addLayout( btnRow );

	// Cancel button
	auto* btnCancel = new QPushButton( tr( "Cancel" ), card );
	btnCancel->setCursor( Qt::PointingHandCursor );
	btnCancel->setStyleSheet( QStringLiteral(
		"QPushButton {"
		"   background-color: #DC2626; color: #FFFFFF; font-weight: 600; font-size: 11px;"
		"   border: none; border-radius: 5px; padding: 4px 8px; min-height: 26px;"
		"}"
		"QPushButton:hover { background-color: #B91C1C; }"
	) );
	layout->addWidget( btnCancel );

	const QString itemKey = item.key;
	const auto targets = item.targets;
	const auto adjCb = item.adjustCallback;
	const auto canCb = item.cancelCallback;

	connect( btnInc, &QPushButton::clicked, this, [this, itemKey, targets, adjCb]() {
		if( m_activeCountdowns.contains( itemKey ) )
		{
			auto& it = m_activeCountdowns[itemKey];
			it.remainingSeconds += 60;
			it.totalSeconds += 60;
			if( itemKey == QStringLiteral("global") || targets.isEmpty() )
			{
				if( adjCb ) adjCb( it.remainingSeconds );
			}
			else
			{
				for( const auto& feat : m_master.allFeatures() )
				{
					if( feat.name().compare( QStringLiteral("PowerDownDelayed"), Qt::CaseInsensitive ) == 0 )
					{
						VeyonCore::featureManager().controlFeature( feat.uid(), FeatureProviderInterface::Operation::Start,
							{{QStringLiteral("shutdownTimeout"), it.remainingSeconds},
							 {QStringLiteral("ShutdownTimeout"), it.remainingSeconds},
							 {QStringLiteral("cancel"), false},
							 {QStringLiteral("Cancel"), false}}, targets );
						break;
					}
				}
			}
			updateCountdownDisplay();
		}
	} );

	connect( btnDec, &QPushButton::clicked, this, [this, itemKey, targets, adjCb]() {
		if( m_activeCountdowns.contains( itemKey ) )
		{
			auto& it = m_activeCountdowns[itemKey];
			it.remainingSeconds = qMax( 1, it.remainingSeconds - 60 );
			if( itemKey == QStringLiteral("global") || targets.isEmpty() )
			{
				if( adjCb ) adjCb( it.remainingSeconds );
			}
			else
			{
				for( const auto& feat : m_master.allFeatures() )
				{
					if( feat.name().compare( QStringLiteral("PowerDownDelayed"), Qt::CaseInsensitive ) == 0 )
					{
						VeyonCore::featureManager().controlFeature( feat.uid(), FeatureProviderInterface::Operation::Start,
							{{QStringLiteral("shutdownTimeout"), it.remainingSeconds},
							 {QStringLiteral("ShutdownTimeout"), it.remainingSeconds},
							 {QStringLiteral("cancel"), false},
							 {QStringLiteral("Cancel"), false}}, targets );
						break;
					}
				}
			}
			updateCountdownDisplay();
		}
	} );

	connect( btnCancel, &QPushButton::clicked, this, [this, itemKey, targets, canCb]() {
		if( itemKey == QStringLiteral("global") )
		{
			if( canCb )
			{
				canCb();
			}
			executeFeatureByName( QStringLiteral("RevertPowerDown"), m_master.allComputerControlInterfaces() );
			clearActiveCountdown( {} );
		}
		else
		{
			ComputerControlInterfaceList cancelTargets = targets;
			if( cancelTargets.isEmpty() )
			{
				for( const auto& c : m_master.allComputerControlInterfaces() )
				{
					if( computerKey( c ) == itemKey )
					{
						cancelTargets.append( c );
						break;
					}
				}
			}

			if( !cancelTargets.isEmpty() )
			{
				executeFeatureByName( QStringLiteral("RevertPowerDown"), cancelTargets );
			}
			m_cancelledTargets.insert( itemKey );
			clearActiveCountdown( cancelTargets.isEmpty() ? targets : cancelTargets );
		}
	} );

	// Remove dead UI pointers for this key
	auto& uiList = m_countdownUiMap[itemKey];
	uiList.erase(
		std::remove_if( uiList.begin(), uiList.end(),
						[]( const CountdownUiSet& s ) { return s.timeLabel.isNull(); } ),
		uiList.end() );

	uiList.append( { titleLbl, timeLbl, pBar } );
	return card;
}



QWidget* ComputerInspectorPanel::createMachineInfoCard( QWidget* parent, const ComputerControlInterface::Pointer& controlInterface )
{
	auto* card = new QFrame( parent );
	card->setStyleSheet( QStringLiteral(
		"QFrame { background-color: #0F172A; border: 1px solid #2A323D; border-radius: 10px; padding: 10px;"
		"}"
	) );

	auto* layout = new QVBoxLayout( card );
	layout->setContentsMargins( 12, 10, 12, 10 );
	layout->setSpacing( 6 );

	// Hostname & State
	auto* topRow = new QHBoxLayout();
	topRow->setSpacing( 6 );

	auto* nameLbl = new QLabel( controlInterface ? controlInterface->computerName() : tr( "Unknown Machine" ), card );
	nameLbl->setStyleSheet( QStringLiteral( "font-size: 14px; font-weight: bold; color: #E6EDF3; border: none;" ) );

	const bool isConnected = controlInterface && controlInterface->state() == ComputerControlInterface::State::Connected;
	auto* statusBadge = new QLabel( isConnected ? tr( "● Online" ) : tr( "○ Offline" ), card );
	statusBadge->setStyleSheet( isConnected
		? QStringLiteral( "background: rgba(52, 211, 153, 0.15); color: #2563EB; font-size: 10px; font-weight: bold; padding: 2px 8px; border-radius: 4px; border: 1px solid #2563EB;" )
		: QStringLiteral( "background: rgba(248, 113, 113, 0.15); color: #F87171; font-size: 10px; font-weight: bold; padding: 2px 8px; border-radius: 4px; border: 1px solid #F87171;" ) );

	topRow->addWidget( nameLbl );
	topRow->addStretch( 1 );
	topRow->addWidget( statusBadge );
	layout->addLayout( topRow );

	// Host / IP & User
	if( controlInterface )
	{
		const auto hostAddrStr = controlInterface->computer().hostAddress().isNull()
			? controlInterface->computer().hostName() : controlInterface->computer().hostAddress().toString();
		auto* ipLbl = new QLabel( tr( "Host IP: %1" ).arg( hostAddrStr ), card );
		ipLbl->setStyleSheet( QStringLiteral( "font-size: 11px; color: #8B949E; border: none;" ) );
		layout->addWidget( ipLbl );

		const QString user = controlInterface->userFullName().isEmpty()
			? controlInterface->userLoginName() : controlInterface->userFullName();
		const QString userDisplay = user.isEmpty() ? tr( "[No logged-in user]" ) : user;
		auto* userLbl = new QLabel( tr( "Logged User: %1" ).arg( userDisplay ), card );
		userLbl->setStyleSheet( QStringLiteral( "font-size: 11px; color: #E6EDF3; font-weight: 500; border: none;" ) );
		userLbl->setWordWrap( false );
		userLbl->setMaximumWidth( 290 );
		userLbl->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
		userLbl->setTextInteractionFlags( Qt::TextSelectableByMouse );
		userLbl->setToolTip( userDisplay );
		layout->addWidget( userLbl );
	}

	return card;
}



QWidget* ComputerInspectorPanel::createActionHubCard( QWidget* parent, const QString& title, const QString& accentColor )
{
	auto* card = new QFrame( parent );
	card->setStyleSheet( QStringLiteral(
		"QFrame { background-color: #0F172A; border: 1px solid #2A323D; border-radius: 8px; padding: 4px; }"
	) );

	auto* layout = new QVBoxLayout( card );
	layout->setContentsMargins( 8, 6, 8, 6 );
	layout->setSpacing( 4 );

	auto* titleLbl = new QLabel( title, card );
	titleLbl->setStyleSheet( QStringLiteral( "font-size: 12px; font-weight: bold; color: %1; border: none;" ).arg( accentColor ) );
	layout->addWidget( titleLbl );

	return card;
}



void ComputerInspectorPanel::setSelectedComputerControlInterfaces( const ComputerControlInterfaceList& interfaces )
{
	m_selectedInterfaces = interfaces;
	m_currentComputerIndex = 0;
	rebuildComputerTab();

	// If computers were clicked, switch to Computer tab so the user sees the separate machine inspection
	if( !m_selectedInterfaces.isEmpty() )
	{
		switchTab( InspectorTab::Computer );
	}
}



QString ComputerInspectorPanel::computerKey( const ComputerControlInterface::Pointer& controlInterface )
{
	if( !controlInterface )
	{
		return QStringLiteral( "global" );
	}
	const auto host = controlInterface->computer().hostAddress().toString();
	if( !host.isEmpty() )
	{
		return host;
	}
	if( !controlInterface->computerName().isEmpty() )
	{
		return controlInterface->computerName();
	}
	return controlInterface->computer().networkObjectUid().toString();
}



void ComputerInspectorPanel::setActiveCountdown( const QString& title,
												int remainingSeconds,
												const VeyonMasterInterface::CountdownAdjustCallback& adjustCallback,
												const VeyonMasterInterface::CountdownCancelCallback& cancelCallback,
												const ComputerControlInterfaceList& targets )
{
	ComputerControlInterfaceList effectiveTargets = targets;
	if( effectiveTargets.isEmpty() )
	{
		effectiveTargets = m_master.filteredComputerControlInterfaces();
		if( effectiveTargets.isEmpty() )
		{
			effectiveTargets = m_master.allComputerControlInterfaces();
		}
	}

	const bool isMultiple = effectiveTargets.size() > 1;

	for( const auto& target : effectiveTargets )
	{
		if( !target )
		{
			continue;
		}
		const QString key = computerKey( target );
		m_cancelledTargets.remove( key );

		CountdownItem item;
		item.key = key;
		item.title = tr( "Shut Down: %1" ).arg( target->computerName().isEmpty() ? key : target->computerName() );
		item.remainingSeconds = remainingSeconds;
		item.totalSeconds = qMax( 1, remainingSeconds );
		item.adjustCallback = adjustCallback;
		item.cancelCallback = cancelCallback;
		item.targets = { target };
		m_activeCountdowns.insert( key, item );
	}

	if( !m_countdownTimer->isActive() )
	{
		m_countdownTimer->start( 1000 );
	}

	if( isMultiple )
	{
		switchTab( InspectorTab::Global );
	}
	else
	{
		switchTab( InspectorTab::Computer );
	}

	rebuildGlobalTab();
	rebuildComputerTab();
	show();
	Q_EMIT visibilityToggleRequested( true );
}



void ComputerInspectorPanel::clearActiveCountdown( const ComputerControlInterfaceList& targets )
{
	if( targets.isEmpty() )
	{
		m_activeCountdowns.clear();
		m_countdownUiMap.clear();
		m_lastGlobalCountdownExpiredTime = 0;
		m_recentExpiredTargets.clear();
	}
	else
	{
		for( const auto& target : targets )
		{
			const QString key = computerKey( target );
			m_activeCountdowns.remove( key );
			m_countdownUiMap.remove( key );
			m_recentExpiredTargets.remove( key );
		}
	}

	if( m_activeCountdowns.isEmpty() )
	{
		m_countdownTimer->stop();
	}

	rebuildGlobalTab();
	rebuildComputerTab();
}



void ComputerInspectorPanel::forcePowerDownComputers( const ComputerControlInterfaceList& targets )
{
	ComputerControlInterfaceList finalTargets = targets;
	if( finalTargets.isEmpty() )
	{
		finalTargets = m_master.allComputerControlInterfaces();
	}

	if( finalTargets.isEmpty() )
	{
		return;
	}

	for( const auto& feat : m_master.allFeatures() )
	{
		if( feat.name().compare( QStringLiteral( "PowerDownNow" ), Qt::CaseInsensitive ) == 0 ||
			feat.name().compare( QStringLiteral( "PowerDown" ), Qt::CaseInsensitive ) == 0 )
		{
			VeyonCore::featureManager().controlFeature( feat.uid(),
														FeatureProviderInterface::Operation::Start,
														{},
														finalTargets );
		}
	}
}



void ComputerInspectorPanel::onCountdownTimerTick()
{
	if( m_activeCountdowns.isEmpty() )
	{
		m_countdownTimer->stop();
		return;
	}

	QList<CountdownItem> expiredItems;
	for( auto it = m_activeCountdowns.begin(); it != m_activeCountdowns.end(); ++it )
	{
		it.value().remainingSeconds--;
		if( it.value().remainingSeconds <= 0 )
		{
			expiredItems.append( it.value() );
		}
	}

	for( const auto& item : expiredItems )
	{
		m_activeCountdowns.remove( item.key );
		m_countdownUiMap.remove( item.key );

		// User requirement: When timer ends, every computer should be powered off even though it's powered on then!
		if( item.key == QStringLiteral( "global" ) || item.targets.isEmpty() )
		{
			qInfo() << "Global countdown timer expired! Enforcing immediate forced power down on ALL computers.";
			m_lastGlobalCountdownExpiredTime = QDateTime::currentSecsSinceEpoch();
			forcePowerDownComputers( m_master.allComputerControlInterfaces() );

			// Sweep at 4s and 10s to ensure machines completing boot are caught
			QTimer::singleShot( 4000, this, [this]() {
				forcePowerDownComputers( m_master.allComputerControlInterfaces() );
			} );
			QTimer::singleShot( 10000, this, [this]() {
				forcePowerDownComputers( m_master.allComputerControlInterfaces() );
			} );
		}
		else
		{
			qInfo() << "Countdown timer expired for" << item.key << "! Enforcing immediate forced power down.";
			m_recentExpiredTargets.insert( item.key, QDateTime::currentSecsSinceEpoch() );
			forcePowerDownComputers( item.targets );

			const auto capturedTargets = item.targets;
			QTimer::singleShot( 4000, this, [this, capturedTargets]() {
				forcePowerDownComputers( capturedTargets );
			} );
			QTimer::singleShot( 10000, this, [this, capturedTargets]() {
				forcePowerDownComputers( capturedTargets );
			} );
		}
	}

	if( m_activeCountdowns.isEmpty() )
	{
		m_countdownTimer->stop();
		rebuildGlobalTab();
		rebuildComputerTab();
		return;
	}

	updateCountdownDisplay();
}



void ComputerInspectorPanel::onComputerStateChanged( const QModelIndex& index )
{
	auto controlInterface = m_master.computerControlListModel().computerControlInterface( index );
	if( !controlInterface || controlInterface->state() != ComputerControlInterface::State::Connected )
	{
		return;
	}

	const QString key = computerKey( controlInterface );
	if( m_cancelledTargets.contains( key ) )
	{
		return;
	}
	const qint64 now = QDateTime::currentSecsSinceEpoch();

	// 1. Check if a global or target-specific timer expired recently (within 180s).
	// If a computer boots up / powers on right when or after the timer ended, power it off immediately!
	const bool globalRecentlyExpired = ( m_lastGlobalCountdownExpiredTime > 0 &&
										( now - m_lastGlobalCountdownExpiredTime ) <= 180 );
	const bool targetRecentlyExpired = ( m_recentExpiredTargets.contains( key ) &&
										( now - m_recentExpiredTargets.value( key ) ) <= 180 );

	if( globalRecentlyExpired || targetRecentlyExpired )
	{
		qInfo() << "Computer" << key << "connected after countdown expiration. Forcing immediate power down.";
		forcePowerDownComputers( { controlInterface } );
		return;
	}

	// 2. Check if a countdown timer is currently active.
	// If so, synchronize the remaining countdown to this newly connected machine!
	if( m_activeCountdowns.contains( QStringLiteral( "global" ) ) )
	{
		const auto& globalItem = m_activeCountdowns.value( QStringLiteral( "global" ) );
		if( globalItem.remainingSeconds > 0 )
		{
			qInfo() << "Syncing active global countdown (" << globalItem.remainingSeconds
					  << "s) to newly connected computer:" << key;
			for( const auto& feat : m_master.allFeatures() )
			{
				if( feat.name().compare( QStringLiteral( "PowerDownDelayed" ), Qt::CaseInsensitive ) == 0 )
				{
					VeyonCore::featureManager().controlFeature( feat.uid(),
																FeatureProviderInterface::Operation::Start,
																{ { QStringLiteral( "shutdownTimeout" ), globalItem.remainingSeconds },
																  { QStringLiteral( "ShutdownTimeout" ), globalItem.remainingSeconds },
																  { QStringLiteral( "cancel" ), false },
																  { QStringLiteral( "Cancel" ), false } },
																{ controlInterface } );
					break;
				}
			}
		}
	}
	else if( m_activeCountdowns.contains( key ) )
	{
		const auto& item = m_activeCountdowns.value( key );
		if( item.remainingSeconds > 0 )
		{
			qInfo() << "Syncing active targeted countdown (" << item.remainingSeconds
					  << "s) to newly connected computer:" << key;
			for( const auto& feat : m_master.allFeatures() )
			{
				if( feat.name().compare( QStringLiteral( "PowerDownDelayed" ), Qt::CaseInsensitive ) == 0 )
				{
					VeyonCore::featureManager().controlFeature( feat.uid(),
																FeatureProviderInterface::Operation::Start,
																{ { QStringLiteral( "shutdownTimeout" ), item.remainingSeconds },
																  { QStringLiteral( "ShutdownTimeout" ), item.remainingSeconds },
																  { QStringLiteral( "cancel" ), false },
																  { QStringLiteral( "Cancel" ), false } },
																{ controlInterface } );
					break;
				}
			}
		}
	}
}



void ComputerInspectorPanel::updateCountdownDisplay()
{
	for( auto it = m_countdownUiMap.begin(); it != m_countdownUiMap.end(); ++it )
	{
		const QString& key = it.key();

		if( key == QStringLiteral( "__master_batch__" ) )
		{
			if( m_activeCountdowns.isEmpty() )
			{
				continue;
			}
			int minRemaining = 999999;
			int maxTotal = 1;
			for( const auto& cd : m_activeCountdowns )
			{
				if( cd.remainingSeconds < minRemaining ) minRemaining = cd.remainingSeconds;
				if( cd.totalSeconds > maxTotal ) maxTotal = cd.totalSeconds;
			}
			if( minRemaining == 999999 ) minRemaining = 0;
			const auto bTimeStr = QStringLiteral( "%1:%2" )
								  .arg( minRemaining / 60, 2, 10, QLatin1Char('0') )
								  .arg( minRemaining % 60, 2, 10, QLatin1Char('0') );
			for( const auto& uiSet : it.value() )
			{
				if( !uiSet.titleLabel.isNull() )
				{
					uiSet.titleLabel->setText( tr( "● %1 pending" ).arg( m_activeCountdowns.size() ) );
				}
				if( !uiSet.timeLabel.isNull() )
				{
					uiSet.timeLabel->setText( bTimeStr );
				}
				if( !uiSet.progressBar.isNull() )
				{
					uiSet.progressBar->setMaximum( maxTotal );
					uiSet.progressBar->setValue( maxTotal - minRemaining );
				}
			}
			continue;
		}

		if( !m_activeCountdowns.contains( key ) )
		{
			continue;
		}

		const auto& item = m_activeCountdowns[key];
		const auto timeStr = QStringLiteral( "%1:%2" )
							 .arg( item.remainingSeconds / 60, 2, 10, QLatin1Char('0') )
							 .arg( item.remainingSeconds % 60, 2, 10, QLatin1Char('0') );

		for( const auto& uiSet : it.value() )
		{
			if( !uiSet.timeLabel.isNull() )
			{
				uiSet.timeLabel->setText( timeStr );
			}
			if( !uiSet.progressBar.isNull() )
			{
				uiSet.progressBar->setMaximum( item.totalSeconds );
				uiSet.progressBar->setValue( item.totalSeconds - item.remainingSeconds );
			}
		}
	}
}



void ComputerInspectorPanel::executeFeatureByName( const QString& featureName, const ComputerControlInterfaceList& targets )
{
	ComputerControlInterfaceList finalTargets = targets;
	if( finalTargets.isEmpty() )
	{
		finalTargets = !m_selectedInterfaces.isEmpty() ? m_selectedInterfaces : m_master.allComputerControlInterfaces();
	}

	if( finalTargets.isEmpty() )
	{
		return;
	}

	QString canonicalName = featureName;
	if( featureName.compare( QStringLiteral("lock"), Qt::CaseInsensitive ) == 0 )
	{
		canonicalName = QStringLiteral("ScreenLock");
	}
	else if( featureName.compare( QStringLiteral("unlock"), Qt::CaseInsensitive ) == 0 )
	{
		canonicalName = QStringLiteral("UnfreezeScreen");
	}
	else if( featureName.compare( QStringLiteral("inputlock"), Qt::CaseInsensitive ) == 0 )
	{
		canonicalName = QStringLiteral("InputDevicesLock");
	}
	else if( featureName.compare( QStringLiteral("remoteaccess"), Qt::CaseInsensitive ) == 0 )
	{
		canonicalName = QStringLiteral("RemoteControl");
	}
	else if( featureName.compare( QStringLiteral("filetransfer"), Qt::CaseInsensitive ) == 0 )
	{
		canonicalName = QStringLiteral("DistributeFiles");
	}
	else if( featureName.compare( QStringLiteral("timedlock"), Qt::CaseInsensitive ) == 0 ||
			 featureName.compare( QStringLiteral("focussession"), Qt::CaseInsensitive ) == 0 )
	{
		canonicalName = QStringLiteral("TimedLock");
	}

	FeatureList searchFeatures = m_master.allFeatures();
	for( const auto& feat : VeyonCore::featureManager().features() )
	{
		if( !searchFeatures.contains( feat ) )
		{
			searchFeatures.append( feat );
		}
	}

	for( const auto& feat : searchFeatures )
	{
		if( feat.name().compare( canonicalName, Qt::CaseInsensitive ) == 0 ||
			feat.displayName().compare( canonicalName, Qt::CaseInsensitive ) == 0 )
		{
			if( canonicalName.compare( QLatin1String("reboot"), Qt::CaseInsensitive ) == 0 ||
				canonicalName.compare( QLatin1String("powerdown"), Qt::CaseInsensitive ) == 0 ||
				canonicalName.compare( QLatin1String("UserLogoff"), Qt::CaseInsensitive ) == 0 )
			{
				if( QMessageBox::question( this, tr( "Confirm Action" ),
										   tr( "Are you sure you want to execute '%1' on %2 computer(s)?" )
										   .arg( feat.displayName() ).arg( finalTargets.size() ),
										   QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) != QMessageBox::Yes )
				{
					return;
				}
			}
			VeyonCore::featureManager().startFeature( m_master, feat, finalTargets );
			return;
		}
	}

	for( const auto& feat : searchFeatures )
	{
		if( feat.name().contains( canonicalName, Qt::CaseInsensitive ) ||
			feat.displayName().contains( canonicalName, Qt::CaseInsensitive ) )
		{
			VeyonCore::featureManager().startFeature( m_master, feat, finalTargets );
			return;
		}
	}
}
