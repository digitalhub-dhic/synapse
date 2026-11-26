/*
 * ComputerSelectPanel.cpp - provides a view for a network object tree
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

#include <QFileDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMessageBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>

#include "ComputerSelectPanel.h"
#include "ComputerManager.h"
#include "NetworkObjectModel.h"
#include "LocationDialog.h"
#include "VeyonConfiguration.h"

#include "ui_ComputerSelectPanel.h"

class LocationTreeItemDelegate : public QStyledItemDelegate
{
public:
	explicit LocationTreeItemDelegate( QTreeView* treeView, QObject* parent = nullptr ) :
		QStyledItemDelegate( parent ), m_treeView( treeView )
	{
	}

	QSize sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const override
	{
		Q_UNUSED( option )
		Q_UNUSED( index )
		return QSize( 180, 26 );
	}

	bool editorEvent( QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index ) override
	{
		if( !index.isValid() || index.column() != 0 )
		{
			return false;
		}
		if( event->type() == QEvent::MouseButtonRelease )
		{
			auto* me = static_cast<QMouseEvent*>( event );
			if( me->button() == Qt::LeftButton )
			{
				int level = 0;
				QModelIndex p = index.parent();
				while( p.isValid() )
				{
					++level;
					p = p.parent();
				}
				int indent = option.rect.left() + 8 + ( level * 16 );
				const bool hasChildren = model->rowCount( index ) > 0;

				QRect chevronRect( indent - 2, option.rect.top() + (option.rect.height() - 16) / 2, 18, 16 );
				indent += 14;
				QRect cbRect( indent - 2, option.rect.top() + (option.rect.height() - 16) / 2, 20, 16 );

				if( hasChildren && chevronRect.contains( me->pos() ) )
				{
					if( m_treeView )
					{
						m_treeView->setExpanded( index, !m_treeView->isExpanded( index ) );
					}
					return true;
				}

				// Clicking checkbox strictly toggles check state
				if( cbRect.contains( me->pos() ) )
				{
					const auto curState = model->data( index, Qt::CheckStateRole ).toInt();
					const auto newState = ( curState == Qt::Checked ) ? Qt::Unchecked : Qt::Checked;
					model->setData( index, newState, Qt::CheckStateRole );
					return true;
				}

				// Clicking the label/row allows normal item selection without toggling checkbox
				return QStyledItemDelegate::editorEvent( event, model, option, index );
			}
		}
		return QStyledItemDelegate::editorEvent( event, model, option, index );
	}

	void paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const override
	{
		if( !index.isValid() || index.column() != 0 )
		{
			return;
		}

		painter->save();
		painter->setRenderHint( QPainter::Antialiasing );

		const bool isSelected = ( option.state & QStyle::State_Selected );
		const bool isHovered = ( option.state & QStyle::State_MouseOver );

		QRect rowRect = option.rect.adjusted( 4, 2, -4, -2 );

		// 1. Draw rounded background pill
		if( isSelected )
		{
			painter->setBrush( QColor(23, 37, 84) ); // #172554 WCAG AA selection
			painter->setPen( QPen( QColor(59, 130, 246), 1 ) );
			painter->drawRoundedRect( rowRect, 6, 6 );
		}
		else if( isHovered )
		{
			painter->setBrush( QColor(22, 32, 50) ); // #162032 elevated hover
			painter->setPen( Qt::NoPen );
			painter->drawRoundedRect( rowRect, 6, 6 );
		}

		// Calculate indentation level
		int level = 0;
		QModelIndex p = index.parent();
		while( p.isValid() )
		{
			++level;
			p = p.parent();
		}
		int indent = option.rect.left() + 8 + ( level * 16 );

		const auto model = index.model();
		const int childCount = model->rowCount( index );
		const bool hasChildren = childCount > 0;

		// 2. Expand/collapse chevron (single clean indicator on left)
		if( hasChildren )
		{
			const bool isExpanded = m_treeView ? m_treeView->isExpanded( index ) : false;
			painter->setPen( isSelected ? QColor(37, 99, 235) : QColor(139, 148, 158) );
			painter->setFont( QFont( QStringLiteral("Segoe UI"), 7, QFont::Bold ) );
			QRect chevronRect( indent, option.rect.top() + (option.rect.height() - 16) / 2, 12, 16 );
			painter->drawText( chevronRect, Qt::AlignCenter, isExpanded ? QStringLiteral("▼") : QStringLiteral("▶") );
		}
		indent += 14;

		// 2.5 Checkbox for every row (Location group and Computer)
		const auto checkStateVal = index.data( Qt::CheckStateRole );
		const int checkState = checkStateVal.isValid() ? checkStateVal.toInt() : static_cast<int>( Qt::Unchecked );
		QRect cbRect( indent, option.rect.top() + (option.rect.height() - 14) / 2, 14, 14 );

		painter->save();
		painter->setRenderHint( QPainter::Antialiasing, true );
		if( checkState == Qt::Checked )
		{
			painter->setBrush( QColor( 37, 99, 235 ) );
			painter->setPen( QColor( 59, 130, 246 ) );
			painter->drawRoundedRect( cbRect, 3, 3 );

			painter->setPen( QPen( Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
			painter->drawLine( cbRect.left() + 3, cbRect.top() + 7, cbRect.left() + 6, cbRect.top() + 10 );
			painter->drawLine( cbRect.left() + 6, cbRect.top() + 10, cbRect.left() + 11, cbRect.top() + 4 );
		}
		else if( checkState == Qt::PartiallyChecked )
		{
			painter->setBrush( QColor( 30, 41, 59 ) );
			painter->setPen( QColor( 59, 130, 246 ) );
			painter->drawRoundedRect( cbRect, 3, 3 );

			painter->setPen( QPen( QColor( 96, 165, 250 ), 2, Qt::SolidLine, Qt::RoundCap ) );
			painter->drawLine( cbRect.left() + 3, cbRect.top() + 7, cbRect.right() - 3, cbRect.top() + 7 );
		}
		else
		{
			painter->setBrush( QColor( 13, 17, 23, 200 ) );
			painter->setPen( QPen( QColor( 80, 95, 115 ), 1.2 ) );
			painter->drawRoundedRect( cbRect, 3, 3 );
		}
		painter->restore();
		indent += 18;

		// 3. Icon (Clean Outline Vector Folder for Location, Outline Monitor for Computer)
		QRectF iconRect( indent, option.rect.top() + (option.rect.height() - 16) / 2.0, 16.0, 16.0 );
		const auto typeVal = index.data( NetworkObjectModel::TypeRole );
		const bool isLocation = hasChildren || ( typeVal.isValid() && typeVal.value<NetworkObject::Type>() == NetworkObject::Type::Location );
		painter->save();
		painter->setRenderHint( QPainter::Antialiasing );
		if( isLocation )
		{
			QPen fPen( isSelected ? QColor( 0xFF, 0xFF, 0xFF ) : QColor( 0x3B, 0x82, 0xF6 ), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin );
			painter->setPen( fPen );
			painter->setBrush( Qt::NoBrush );
			QPainterPath fp;
			fp.moveTo( iconRect.left() + 1.0, iconRect.top() + 4.0 );
			fp.lineTo( iconRect.left() + 6.0, iconRect.top() + 4.0 );
			fp.lineTo( iconRect.left() + 8.0, iconRect.top() + 6.0 );
			fp.lineTo( iconRect.right() - 1.0, iconRect.top() + 6.0 );
			fp.lineTo( iconRect.right() - 1.0, iconRect.bottom() - 1.0 );
			fp.lineTo( iconRect.left() + 1.0, iconRect.bottom() - 1.0 );
			fp.closeSubpath();
			painter->drawPath( fp );
		}
		else
		{
			QPen mPen( isSelected ? QColor( 0xFF, 0xFF, 0xFF ) : QColor( 0x8B, 0x94, 0x9E ), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin );
			painter->setPen( mPen );
			painter->setBrush( Qt::NoBrush );
			painter->drawRoundedRect( QRectF( iconRect.left() + 1.0, iconRect.top() + 2.0, 14.0, 9.0 ), 1.5, 1.5 );
			painter->drawLine( QPointF( iconRect.left() + 8.0, iconRect.top() + 11.0 ), QPointF( iconRect.left() + 8.0, iconRect.top() + 14.0 ) );
			painter->drawLine( QPointF( iconRect.left() + 4.0, iconRect.top() + 14.0 ), QPointF( iconRect.left() + 12.0, iconRect.top() + 14.0 ) );
		}
		painter->restore();
		indent += 22;

		// 4. Count pill badge on the far right
		int rightLimit = option.rect.right() - 8;
		if( isLocation && childCount > 0 )
		{
			const QString countStr = QString::number( childCount );
			QFont badgeFont( QStringLiteral("Segoe UI"), 9, QFont::DemiBold );
			QFontMetrics bfm( badgeFont );
			int badgeWidth = qMax( 26, bfm.horizontalAdvance( countStr ) + 12 );
			int badgeHeight = 20;
			QRect badgeRect( rightLimit - badgeWidth, option.rect.top() + (option.rect.height() - badgeHeight) / 2, badgeWidth, badgeHeight );

			if( isSelected )
			{
				painter->setBrush( QColor(37, 99, 235) );
				painter->setPen( Qt::NoPen );
				painter->drawRoundedRect( badgeRect, 10, 10 );
				painter->setPen( QColor( 0xFF, 0xFF, 0xFF ) );
			}
			else
			{
				painter->setBrush( QColor(15, 23, 42) );
				painter->setPen( QColor(42, 50, 61) );
				painter->drawRoundedRect( badgeRect, 10, 10 );
				painter->setPen( QColor(37, 99, 235) );
			}

			painter->setFont( badgeFont );
			painter->drawText( badgeRect, Qt::AlignCenter, countStr );
			rightLimit = badgeRect.left() - 6;
		}

		// 5. Item label text
		QString text = index.data( Qt::DisplayRole ).toString();
		QRect textRect( indent, option.rect.top(), qMax( 10, rightLimit - indent ), option.rect.height() );
		QFont textFont( QStringLiteral("Segoe UI"), 10, isSelected ? QFont::DemiBold : QFont::Medium );
		painter->setFont( textFont );
		painter->setPen( isSelected ? QColor(248, 250, 252) : QColor(230, 237, 243) );
		painter->drawText( textRect, Qt::AlignVCenter | Qt::AlignLeft, painter->fontMetrics().elidedText( text, Qt::ElideRight, textRect.width() ) );

		painter->restore();
	}

private:
	QPointer<QTreeView> m_treeView;
};


ComputerSelectPanel::ComputerSelectPanel( ComputerManager& computerManager, QWidget *parent ) :
	QWidget(parent),
	ui(new Ui::ComputerSelectPanel),
	m_computerManager( computerManager ),
	m_filterProxyModel(new QSortFilterProxyModel(this))
{
	m_filterProxyModel->setSourceModel( computerManager.computerTreeModel() );
	m_filterProxyModel->setFilterCaseSensitivity( Qt::CaseInsensitive );
	m_filterProxyModel->setFilterKeyColumn( -1 ); // filter all columns instead of first one only
	m_filterProxyModel->setRecursiveFilteringEnabled(true);

	ui->setupUi(this);

	if( auto* vLayout = qobject_cast<QVBoxLayout*>( layout() ) )
	{
		vLayout->setContentsMargins( 0, 0, 0, 8 );
		vLayout->setSpacing( 6 );
	}

	auto* selectHeader = new QFrame( this );
	selectHeader->setFixedHeight( 32 );
	selectHeader->setStyleSheet( QStringLiteral( "QFrame { background-color: #070E1E; border-bottom: 1px solid #122547; }" ) );
	auto* headerLayout = new QHBoxLayout( selectHeader );
	headerLayout->setContentsMargins( 12, 0, 8, 0 );
	headerLayout->setSpacing( 6 );

	auto* titleText = new QLabel( tr( "Locations / Computers" ), selectHeader );
	titleText->setStyleSheet( QStringLiteral( "font-weight: 700; font-size: 11px; color: #E6EDF3;" ) );
	headerLayout->addWidget( titleText );
	headerLayout->addStretch();

	auto* closeButton = new QPushButton( QStringLiteral( "✕" ), selectHeader );
	closeButton->setFixedSize( 22, 22 );
	closeButton->setCursor( Qt::PointingHandCursor );
	closeButton->setToolTip( tr( "Close Panel" ) );
	closeButton->setStyleSheet( QStringLiteral(
		"QPushButton {"
		"   background: transparent; border: none; border-radius: 4px;"
		"   color: #8B949E; font-size: 11px; font-weight: bold; padding: 0px;"
		"}"
		"QPushButton:hover { background: #dc2626; color: #ffffff; }"
	) );
	connect( closeButton, &QPushButton::clicked, this, [this]() {
		this->hide();
	} );
	headerLayout->addWidget( closeButton );

	if( auto* vLayout = qobject_cast<QVBoxLayout*>( layout() ) )
	{
		vLayout->insertWidget( 0, selectHeader );
	}

	ui->filterLineEdit->setPlaceholderText( tr( "Search computers..." ) );
	ui->filterLineEdit->setFixedHeight( 28 );
	ui->saveListButton->setText( tr( "Save computer list" ) );
	ui->saveListButton->setFixedHeight( 28 );
	ui->saveListButton->setCursor( Qt::PointingHandCursor );

		setStyleSheet( QStringLiteral(
		"ComputerSelectPanel, QWidget#ComputerSelectPanel {"
		"   background-color: #090E1A;"
		"   border-right: 1px solid #21262D;"
		"}"
		"QTreeView {"
		"   background-color: #090E1A;"
		"   color: #E6EDF3;"
		"   border: none;"
		"   outline: none;"
		"}"
		"QTreeView::item {"
		"   background: transparent;"
		"}"
		"QLineEdit {"
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 6px;"
		"   padding: 4px 10px;"
		"   margin: 0px 8px;"
		"   color: #E6EDF3;"
		"   font-size: 11px;"
		"}"
		"QLineEdit:focus {"
		"   border: 1px solid #2563EB;"
		"   background-color: #090E1A;"
		"}"
		"QPushButton {"
		"   background-color: #0F172A;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 6px;"
		"   margin: 0px 8px;"
		"   color: #E6EDF3;"
		"   font-size: 11px;"
		"   font-weight: 500;"
		"   padding: 4px 10px;"
		"}"
		"QPushButton:hover {"
		"   background-color: #1F2630;"
		"   border-color: #2563EB;"
		"   color: #2563EB;"
		"}"
	) );
	ui->treeView->viewport()->setStyleSheet( QStringLiteral( "background-color: #090E1A;" ) );

	// Hide table header and style tree view
	ui->treeView->setHeaderHidden( true );
	ui->treeView->setRootIsDecorated( false );
	ui->treeView->setIndentation( 0 );
	ui->treeView->setUniformRowHeights( false );
	ui->treeView->setItemDelegate( new LocationTreeItemDelegate( ui->treeView, this ) );

	// capture keyboard events for tree view
	ui->treeView->installEventFilter( this );

	// set computer tree model as data model
	ui->treeView->setModel( m_filterProxyModel );

	// set default sort order
	ui->treeView->sortByColumn( 0, Qt::AscendingOrder );

	// Hide all extra columns so only Column 0 (with name, checkbox, and count) is shown
	for( int col = 1; col < 10; ++col )
	{
		ui->treeView->hideColumn( col );
	}

	ui->addLocationButton->setVisible( VeyonCore::config().showCurrentLocationOnly() &&
									   VeyonCore::config().allowAddingHiddenLocations() );

	ui->filterLineEdit->setPlaceholderText( tr( "Filter locations / computers..." ) );
	ui->filterLineEdit->setClearButtonEnabled( true );
	ui->filterLineEdit->setFixedHeight( 28 );
	ui->filterLineEdit->setVisible( true );

	connect( ui->filterLineEdit, &QLineEdit::textChanged,
			 this, &ComputerSelectPanel::updateFilter );

	connect(m_filterProxyModel, &QAbstractItemModel::modelReset,
			this, &ComputerSelectPanel::fetchAndExpandAll);
	connect(m_filterProxyModel, &QAbstractItemModel::rowsInserted,
			this, &ComputerSelectPanel::fetchAndExpandAll);
	fetchAndExpandAll();
}



ComputerSelectPanel::~ComputerSelectPanel()
{
	delete ui;
}



bool ComputerSelectPanel::eventFilter( QObject *watched, QEvent *event )
{
	if( watched == ui->treeView &&
		event->type() == QEvent::KeyPress &&
		dynamic_cast<QKeyEvent*>(event) != nullptr &&
		dynamic_cast<QKeyEvent*>(event)->key() == Qt::Key_Delete )
	{
		removeLocation();
		return true;
	}

	return QWidget::eventFilter( watched, event );
}



void ComputerSelectPanel::addLocation()
{
	LocationDialog dialog( m_computerManager.networkObjectModel(), this );
	if( dialog.exec() && dialog.selectedLocation().isEmpty() == false )
	{
		m_computerManager.addLocation( dialog.selectedLocation() );
	}
}



void ComputerSelectPanel::removeLocation()
{
	auto model = m_computerManager.computerTreeModel();
	const auto proxyIndex = ui->treeView->selectionModel()->currentIndex();
	const auto index = m_filterProxyModel->mapToSource( proxyIndex );

	if( index.isValid() &&
		model->data( index, NetworkObjectModel::TypeRole ).value<NetworkObject::Type>() == NetworkObject::Type::Location )
	{
		m_computerManager.removeLocation( model->data( index, NetworkObjectModel::NameRole ).toString() );
	}
}



void ComputerSelectPanel::saveList()
{
	QString fileName = QFileDialog::getSaveFileName( this, tr( "Select output filename" ),
													 QDir::homePath(), tr( "CSV files (*.csv)" ) );
	if( fileName.isEmpty() == false )
	{
		if( m_computerManager.saveComputerAndUsersList( fileName ) == false )
		{
			QMessageBox::critical( this, tr( "File error"),
								   tr( "Could not write the computer and users list to %1! "
									   "Please check the file access permissions." ).arg( fileName ) );
		}
	}
}



void ComputerSelectPanel::updateFilter()
{
	const auto filter = ui->filterLineEdit->text();
	auto model = ui->treeView->model();

	if( filter.isEmpty() )
	{
		m_filterProxyModel->setFilterWildcard( filter );

		for( int i = 0; i < model->rowCount(); ++i )
		{
			const auto index = model->index( i, 0 );
			ui->treeView->setExpanded( index, m_expandedGroups.contains( index ) );
		}

		m_previousFilter.clear();
	}
	else
	{
		if( m_previousFilter.isEmpty() )
		{
			m_expandedGroups.clear();

			for( int i = 0; i < model->rowCount(); ++i )
			{
				const auto index = model->index( i, 0 );
				if( ui->treeView->isExpanded( index ) )
				{
					m_expandedGroups.append( index );
				}
			}
		}

		m_previousFilter = filter;

		m_filterProxyModel->setFilterWildcard( filter );
		ui->treeView->expandAll();
	}
}



void ComputerSelectPanel::fetchAndExpandAll()
{
	const int rootRows = m_filterProxyModel->rowCount();
	for( int i = 0; i < rootRows; ++i )
	{
		fetchAll( m_filterProxyModel->index( i, 0 ) );
	}
	if( !m_expandTimer )
	{
		m_expandTimer = new QTimer( this );
		m_expandTimer->setSingleShot( true );
		m_expandTimer->setInterval( 50 );
		connect( m_expandTimer, &QTimer::timeout, this, [this]() { ui->treeView->expandAll(); } );
	}
	m_expandTimer->start();
}



void ComputerSelectPanel::fetchAll(const QModelIndex& index)
{
	if (m_filterProxyModel->canFetchMore(index))
	{
		m_filterProxyModel->fetchMore(index);
	}

	const auto rowCount = m_filterProxyModel->rowCount(index);
	for (int i = 0; i < rowCount; ++i)
	{
		fetchAll(m_filterProxyModel->index(i, 0, index));
	}
}
