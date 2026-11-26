#include <QHostAddress>
/*
 * ComputerMonitoringModel.cpp - implementation of ComputerMonitoringModel
 *
 * Copyright (c) 2018-2026 Tobias Junghans <tobydox@veyon.io>
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

#include "ComputerControlListModel.h"
#include "ComputerMonitoringModel.h"

#if defined(QT_TESTLIB_LIB)
#include <QAbstractItemModelTester>
#endif


ComputerMonitoringModel::ComputerMonitoringModel( QObject* parent ) :
	QSortFilterProxyModel( parent )
{
#if defined(QT_TESTLIB_LIB)
	new QAbstractItemModelTester( this, QAbstractItemModelTester::FailureReportingMode::Warning, this );
#endif

	setFilterCaseSensitivity( Qt::CaseInsensitive );
	setUserLoginNameRole( ComputerControlListModel::UserLoginNameRole );
}



void ComputerMonitoringModel::setStateRole( int role )
{
	beginResetModel();
	m_stateRole = role;
	endResetModel();
}



void ComputerMonitoringModel::setUserLoginNameRole( int role )
{
	beginResetModel();
	m_userLoginNameRole = role;
	endResetModel();
}



void ComputerMonitoringModel::setStateFilter( ComputerControlInterface::State state )
{
	beginResetModel();
	m_stateFilter = state;
	endResetModel();
}



void ComputerMonitoringModel::setFilterNonEmptyUserLoginNames( bool enabled )
{
	if( enabled != m_filterNonEmptyUserLoginNames )
	{
		beginResetModel();
		m_filterNonEmptyUserLoginNames = enabled;
		endResetModel();
	}
}



void ComputerMonitoringModel::setFilterOfflineOnly( bool enabled )
{
	if( enabled != m_filterOfflineOnly )
	{
		beginResetModel();
		m_filterOfflineOnly = enabled;
		endResetModel();
	}
}



bool ComputerMonitoringModel::filterAcceptsRow( int sourceRow, const QModelIndex& sourceParent ) const
{
	if( m_filterOfflineOnly && m_stateRole >= 0 &&
		sourceModel()->data( sourceModel()->index( sourceRow, 0, sourceParent ),
							 m_stateRole ).value<ComputerControlInterface::State>() == ComputerControlInterface::State::Connected )
	{
		return false;
	}

	if( m_stateFilter != ComputerControlInterface::State::None &&
		m_stateRole >= 0 &&
		sourceModel()->data( sourceModel()->index( sourceRow, 0, sourceParent ),
							 m_stateRole ).value<ComputerControlInterface::State>() != m_stateFilter )
	{
		return false;
	}

	if( m_filterNonEmptyUserLoginNames &&
		userLoginNameRole() >= 0 &&
		sourceModel()->data( sourceModel()->index( sourceRow, 0, sourceParent ),
							 m_userLoginNameRole ).toString().isEmpty() )
	{
		return false;
	}

	return QSortFilterProxyModel::filterAcceptsRow( sourceRow, sourceParent );
}


void ComputerMonitoringModel::setCustomSortMode( CustomSortMode mode )
{
	m_customSortMode = mode;
	invalidate();
	sort( 0, Qt::AscendingOrder );
}



bool ComputerMonitoringModel::lessThan( const QModelIndex& source_left, const QModelIndex& source_right ) const
{
	if( m_customSortMode == CustomSortMode::OnlineFirst )
	{
		const auto stateLeft = sourceModel()->data( source_left, m_stateRole ).value<ComputerControlInterface::State>();
		const auto stateRight = sourceModel()->data( source_right, m_stateRole ).value<ComputerControlInterface::State>();
		const bool onLeft = ( stateLeft == ComputerControlInterface::State::Connected );
		const bool onRight = ( stateRight == ComputerControlInterface::State::Connected );
		if( onLeft != onRight )
		{
			return onLeft > onRight; // true (online) comes first
		}
	}
	else if( m_customSortMode == CustomSortMode::OfflineFirst )
	{
		const auto stateLeft = sourceModel()->data( source_left, m_stateRole ).value<ComputerControlInterface::State>();
		const auto stateRight = sourceModel()->data( source_right, m_stateRole ).value<ComputerControlInterface::State>();
		const bool offLeft = ( stateLeft != ComputerControlInterface::State::Connected );
		const bool offRight = ( stateRight != ComputerControlInterface::State::Connected );
		if( offLeft != offRight )
		{
			return offLeft > offRight; // true (offline) comes first
		}
	}
	else if( m_customSortMode == CustomSortMode::LoggedInFirst )
	{
		const QString uLeft = sourceModel()->data( source_left, m_userLoginNameRole ).toString();
		const QString uRight = sourceModel()->data( source_right, m_userLoginNameRole ).toString();
		const bool loggedLeft = !uLeft.isEmpty();
		const bool loggedRight = !uRight.isEmpty();
		if( loggedLeft != loggedRight )
		{
			return loggedLeft > loggedRight; // true (logged in) comes first
		}
	}
	else if( m_customSortMode == CustomSortMode::LoggedOutFirst )
	{
		const QString uLeft = sourceModel()->data( source_left, m_userLoginNameRole ).toString();
		const QString uRight = sourceModel()->data( source_right, m_userLoginNameRole ).toString();
		const bool outLeft = uLeft.isEmpty();
		const bool outRight = uRight.isEmpty();
		if( outLeft != outRight )
		{
			return outLeft > outRight; // true (not logged in) comes first
		}
	}
	else if( m_customSortMode == CustomSortMode::IpAddress )
	{
		const auto ciLeft = sourceModel()->data( source_left, ComputerControlListModel::ControlInterfaceRole ).value<ComputerControlInterface::Pointer>();
		const auto ciRight = sourceModel()->data( source_right, ComputerControlListModel::ControlInterfaceRole ).value<ComputerControlInterface::Pointer>();
		const auto ipLeft = ciLeft ? ciLeft->computer().hostAddress().toIPv4Address() : 0;
		const auto ipRight = ciRight ? ciRight->computer().hostAddress().toIPv4Address() : 0;
		if( ipLeft != ipRight )
		{
			return ipLeft < ipRight;
		}
	}

	const QString nameLeft = sourceModel()->data( source_left, Qt::DisplayRole ).toString();
	const QString nameRight = sourceModel()->data( source_right, Qt::DisplayRole ).toString();
	return QString::compare( nameLeft, nameRight, Qt::CaseInsensitive ) < 0;
}
