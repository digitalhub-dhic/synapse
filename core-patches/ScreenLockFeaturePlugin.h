/*
 * ScreenLockFeaturePlugin.h - declaration of ScreenLockFeaturePlugin class
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

#pragma once

#include <QPointer>

#include "FeatureProviderInterface.h"

class LockWidget;
class QTimer;

class ScreenLockFeaturePlugin : public QObject, FeatureProviderInterface, PluginInterface
{
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "io.veyon.Veyon.Plugins.ScreenLock")
	Q_INTERFACES(PluginInterface FeatureProviderInterface)
public:
	explicit ScreenLockFeaturePlugin( QObject* parent = nullptr );
	~ScreenLockFeaturePlugin() override;

	Plugin::Uid uid() const override
	{
		return Plugin::Uid{ QStringLiteral("2ad98ccb-e9a5-43ef-8c4c-876ac5efbcb1") };
	}

	QVersionNumber version() const override
	{
		return QVersionNumber( 1, 2 );
	}

	QString name() const override
	{
		return QStringLiteral("ScreenLock");
	}

	QString description() const override
	{
		return tr( "Lock screen and input devices of a computer" );
	}

	QString vendor() const override
	{
		return QStringLiteral("DigiAgent Community");
	}

	QString copyright() const override
	{
		return QStringLiteral("Tobias Junghans");
	}

	const FeatureList& featureList() const override
	{
		return m_features;
	}

	bool controlFeature( Feature::Uid featureUid, Operation operation, const QVariantMap& arguments,
						const ComputerControlInterfaceList& computerControlInterfaces ) override;
	bool startFeature( VeyonMasterInterface& master, const Feature& feature,
					   const ComputerControlInterfaceList& computerControlInterfaces ) override;

	bool handleFeatureMessage( VeyonServerInterface& server,
							   const MessageContext& messageContext,
							   const FeatureMessage& message ) override;

	bool handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message ) override;

	void initServer( VeyonServerInterface& server ) override;

	bool isFeatureActive( VeyonServerInterface& server, Feature::Uid featureUid ) const override;

public:
	enum class FeatureCommand
	{
		StartLock,
		StopLock
	};

	enum class Argument
	{
		Text,
		Duration
	};
	Q_ENUM(Argument)

private Q_SLOTS:
	void checkAndEnforcePersistentLock();

private:
	const Feature m_screenLockFeature;
	const Feature m_freezeScreenFeature;
	const Feature m_textLockFeature;
	const Feature m_lockInputDevicesFeature;
	const Feature m_timedLockFeature;
	const Feature m_unfreezeFeature;
	const FeatureList m_features;

	LockWidget* m_lockWidget;
	QPointer<VeyonServerInterface> m_server;
	QTimer* m_watchdogTimer{ nullptr };
	QTimer* m_timedLockTimer{ nullptr };
	quint64 m_lockGeneration{ 0 };

};
