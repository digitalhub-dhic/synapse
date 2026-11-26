/*
 * ComputerInspectorPanel.h - Modern docked inspector with Public (Global) and Separate Computer modes
 *
 * Part of Synapse Master
 */

#pragma once

#include <QWidget>
#include <QStackedWidget>
#include <QPointer>
#include <QTimer>
#include <QMap>
#include <QSet>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QComboBox>
#include <QVBoxLayout>
#include <functional>

#include "ComputerControlInterface.h"
#include "VeyonMasterInterface.h"

class VeyonMaster;
class QScrollArea;

class ComputerInspectorPanel : public QWidget
{
	Q_OBJECT
public:
	struct CountdownItem {
		QString key;
		QString title;
		int remainingSeconds{0};
		int totalSeconds{0};
		VeyonMasterInterface::CountdownAdjustCallback adjustCallback;
		VeyonMasterInterface::CountdownCancelCallback cancelCallback;
		ComputerControlInterfaceList targets;
	};

	struct CountdownUiSet {
		QPointer<QLabel> titleLabel;
		QPointer<QLabel> timeLabel;
		QPointer<QProgressBar> progressBar;
	};

	enum class InspectorTab {
		Global = 0,
		Computer = 1,
		Details = 2
	};

	explicit ComputerInspectorPanel( VeyonMaster& master, QWidget* parent = nullptr );
	~ComputerInspectorPanel() override = default;

	void setSelectedComputerControlInterfaces( const ComputerControlInterfaceList& interfaces );

	void setActiveCountdown( const QString& title,
							 int remainingSeconds,
							 const VeyonMasterInterface::CountdownAdjustCallback& adjustCallback,
							 const VeyonMasterInterface::CountdownCancelCallback& cancelCallback,
							 const ComputerControlInterfaceList& targets = {} );

	void clearActiveCountdown( const ComputerControlInterfaceList& targets = {} );

	static QString computerKey( const ComputerControlInterface::Pointer& controlInterface );

	const QMap<QString, CountdownItem>& activeCountdowns() const { return m_activeCountdowns; }

	void switchTab( InspectorTab tab );

Q_SIGNALS:
	void visibilityToggleRequested( bool visible );

private Q_SLOTS:
	void onCountdownTimerTick();
	void onClockTick();
	void onComputerStateChanged( const QModelIndex& index );

private:
	void setupUi();
	void updateQuickStats();
	void updateCountdownDisplay();
	void rebuildComputerTab();
	void rebuildGlobalTab();

	QWidget* createGlobalTabWidget();
	QWidget* createComputerTabWidget();
	QWidget* createDetailsTabWidget();

	QWidget* createCountdownCard( QWidget* parent, const CountdownItem& item );
	QWidget* createMasterBatchCountdownCard( QWidget* parent );
	QWidget* createActiveOperationSummaryCard( QWidget* parent );
	QWidget* createMachineInfoCard( QWidget* parent, const ComputerControlInterface::Pointer& controlInterface );
	QWidget* createActionHubCard( QWidget* parent, const QString& title, const QString& accentColor );

	void adjustAllCountdowns( int deltaSeconds );
	void cancelAllCountdowns();
	void forcePowerDownComputers( const ComputerControlInterfaceList& targets );
	void executeFeatureByName( const QString& featureName, const ComputerControlInterfaceList& targets );

	VeyonMaster& m_master;
	ComputerControlInterfaceList m_selectedInterfaces;
	int m_currentComputerIndex{0};

	// Top Navigation Buttons
	QPushButton* m_btnGlobal{};
	QPushButton* m_btnComputer{};
	QPushButton* m_btnDetails{};
	QStackedWidget* m_stackedWidget{};

	// Global Tab Components
	QWidget* m_globalCountdownsContainer{};
	QVBoxLayout* m_globalCountdownsLayout{};
	QLabel* m_statTotalVal{};
	QLabel* m_statOnlineVal{};
	QLabel* m_statOfflineVal{};
	QLabel* m_statIdleVal{};

	// Computer Tab Components
	QWidget* m_computerContentContainer{};
	QVBoxLayout* m_computerContentLayout{};
	QComboBox* m_computerSelectorCombo{};

	// Footer Components
	QLabel* m_footerClockLabel{};

	// Live Details Tab Labels
	QLabel* m_detailVersionVal{};
	QLabel* m_detailTargetVal{};
	QLabel* m_detailIpVal{};
	QLabel* m_detailUserVal{};
	QLabel* m_detailStateVal{};
	QLabel* m_detailScreensVal{};

	// Timers and State
	QMap<QString, CountdownItem> m_activeCountdowns;
	QMap<QString, QList<CountdownUiSet>> m_countdownUiMap;
	QTimer* m_countdownTimer{};
	QTimer* m_clockTimer{};
	qint64 m_lastGlobalCountdownExpiredTime{0};
	QMap<QString, qint64> m_recentExpiredTargets;
	QSet<QString> m_cancelledTargets;
};
