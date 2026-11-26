/*
 * ComputerItemDelegate.cpp - Modern 2026 Enterprise Card Delegate
 *
 * Copyright (c) 2025-2026 Tobias Junghans <tobydox@veyon.io>
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

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QApplication>
#include <QMouseEvent>

#include "ComputerControlListModel.h"
#include "ComputerItemDelegate.h"
#include "ComputerMonitoringView.h"
#include "FeatureManager.h"
#include "VeyonMaster.h"


ComputerItemDelegate::ComputerItemDelegate(QObject* parent) :
	QStyledItemDelegate(parent)
{
	QPixmapCache::setCacheLimit(128 * 1024);
	initFeaturePixmaps();
}



void ComputerItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	if (!index.isValid())
	{
		return;
	}

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->setRenderHint(QPainter::TextAntialiasing, true);

	const QRect rect = option.rect.adjusted(3, 3, -3, -3);
	const bool isSelected = (option.state & QStyle::State_Selected) != 0;
	const bool isHovered = (option.state & QStyle::State_MouseOver) != 0;

	// 1. Draw Card Surface & Border
	QPainterPath cardPath;
	cardPath.addRoundedRect(rect, 8, 8);

	if (isSelected)
	{
		painter->fillPath(cardPath, QColor(23, 37, 84)); // #172554 WCAG AA selection
		painter->setPen(QPen(QColor(59, 130, 246), 1.5)); // #3B82F6 focus border
		painter->drawPath(cardPath);
	}
	else if (isHovered)
	{
		painter->fillPath(cardPath, QColor(22, 32, 50)); // #162032 elevated hover
		painter->setPen(QPen(QColor(59, 130, 246), 1.0));
		painter->drawPath(cardPath);
	}
	else
	{
		painter->fillPath(cardPath, QColor(15, 22, 36)); // #0F1624 surface
		painter->setPen(QPen(QColor(30, 41, 59), 1.0)); // #1E293B subtle border
		painter->drawPath(cardPath);
	}

	// 2. Data Retrieval
	const auto controlInterface = index.model()->data(index, ComputerControlListModel::ControlInterfaceRole).value<ComputerControlInterface::Pointer>();
	const bool isOnline = controlInterface && (controlInterface->state() == ComputerControlInterface::State::Connected);
	const bool isConnecting = controlInterface && (controlInterface->state() == ComputerControlInterface::State::Connecting);

	QString compName = index.data(Qt::DisplayRole).toString();
	if (compName.isEmpty() && controlInterface)
	{
		compName = controlInterface->computerName();
	}
	if (compName.isEmpty())
	{
		compName = tr("Computer");
	}

	const QString user = index.data(ComputerControlListModel::UserLoginNameRole).toString();
	const QString ip = controlInterface ? controlInterface->computer().hostAddress().toString() : QString();

	// 3. 16:9 Screen Preview Area
	const int previewMargin = 6;
	const int previewW = rect.width() - (previewMargin * 2);
	// Leave 52px at bottom for computer name and status/user info
	const int previewH = qMax(48, rect.height() - 56);
	const QRect previewRect(rect.left() + previewMargin, rect.top() + previewMargin, previewW, previewH);

	QPainterPath previewClip;
	previewClip.addRoundedRect(previewRect, 6, 6);

	painter->save();
	painter->setClipPath(previewClip);

	if (isOnline)
	{
		QImage fb;
		if (controlInterface)
		{
			fb = controlInterface->scaledFramebuffer();
		}
		if (fb.isNull())
		{
			fb = index.data(ComputerControlListModel::FramebufferRole).value<QImage>();
		}
		if (fb.isNull() && controlInterface)
		{
			fb = controlInterface->framebuffer();
		}
		if (fb.isNull())
		{
			fb = index.data(Qt::DecorationRole).value<QImage>();
		}

		if (!fb.isNull())
		{
			painter->fillRect(previewRect, QColor(5, 7, 10));

			QPixmap cachedPixmap;
			const QString cacheKey = QStringLiteral("prev_%1_%2x%3")
										 .arg(ip)
										 .arg(previewRect.width())
										 .arg(previewRect.height());
			const qint64 fbKey = fb.cacheKey();

			if (m_lastFbKeys.value(cacheKey) != fbKey || !QPixmapCache::find(cacheKey, &cachedPixmap))
			{
				QImage scaled;
				if (fb.size() == previewRect.size())
				{
					scaled = fb;
				}
				else
				{
					scaled = fb.scaled(previewRect.size(), Qt::KeepAspectRatio, Qt::FastTransformation);
				}
				cachedPixmap = QPixmap::fromImage(scaled);
				QPixmapCache::insert(cacheKey, cachedPixmap);
				m_lastFbKeys[cacheKey] = fbKey;
			}

			if (!cachedPixmap.isNull())
			{
				const int px = previewRect.left() + (previewRect.width() - cachedPixmap.width()) / 2;
				const int py = previewRect.top() + (previewRect.height() - cachedPixmap.height()) / 2;
				painter->drawPixmap(px, py, cachedPixmap);
			}
		}
		else
		{
			painter->fillRect(previewRect, QColor(5, 7, 10));
			painter->setPen(QColor(37, 99, 235));
			QFont loadingFont(QStringLiteral("Segoe UI"), 8);
			painter->setFont(loadingFont);
			painter->drawText(previewRect, Qt::AlignCenter, tr("Connecting Stream..."));
		}
	}
	else if (isConnecting)
	{
		// Connecting State - Dark Container with Amber Radar Indicator
		painter->fillRect(previewRect, QColor(5, 7, 10));

		painter->setPen(QPen(QColor(245, 158, 11, 100), 1)); // Amber subtle border
		painter->drawRect(previewRect.adjusted(0, 0, -1, -1));

		const int cx = previewRect.center().x();
		const int cy = previewRect.center().y() - 14;

		// Concentric connecting pulses
		painter->setPen(QPen(QColor(245, 158, 11, 140), 1.2, Qt::DashLine));
		painter->setBrush(Qt::NoBrush);
		painter->drawEllipse(QPoint(cx, cy), 16, 16);

		painter->setPen(QPen(QColor(251, 191, 36), 1.4));
		painter->setBrush(QColor(245, 158, 11, 35));
		painter->drawEllipse(QPoint(cx, cy), 9, 9);

		painter->setBrush(QColor(245, 158, 11));
		painter->setPen(Qt::NoPen);
		painter->drawEllipse(QPoint(cx, cy), 3, 3);

		// Status text
		painter->setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::DemiBold));
		painter->setPen(QColor(251, 191, 36)); // Amber
		painter->drawText(QRect(previewRect.left(), cy + 18, previewRect.width(), 16),
						  Qt::AlignCenter, tr("Connecting..."));

		painter->setFont(QFont(QStringLiteral("Segoe UI"), 7));
		painter->setPen(QColor(148, 163, 184)); // Slate-400
		painter->drawText(QRect(previewRect.left(), cy + 34, previewRect.width(), 14),
						  Qt::AlignCenter, tr("Establishing Secure Link"));
	}
	else
	{
		// Offline Empty State - Dark Sleek Container (NO giant monitor icon)
		painter->fillRect(previewRect, QColor(5, 7, 10));

		// Subtle grid accent line inside preview
		painter->setPen(QPen(QColor(42, 50, 61), 1));
		painter->drawRect(previewRect.adjusted(0, 0, -1, -1));

		const int cx = previewRect.center().x();
		const int cy = previewRect.center().y() - 14;

		// Modern sleek monitor glyph
		painter->setPen(QPen(QColor(139, 148, 158), 1.4));
		painter->setBrush(QColor(15, 23, 42));
		painter->drawRoundedRect(QRect(cx - 16, cy - 10, 32, 20), 2, 2);
		// Stand & Base
		painter->drawLine(cx, cy + 10, cx, cy + 14);
		painter->drawLine(cx - 8, cy + 14, cx + 8, cy + 14);

		// Status text
		painter->setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::DemiBold));
		painter->setPen(QColor(139, 148, 158));
		painter->drawText(QRect(previewRect.left(), cy + 18, previewRect.width(), 16),
						  Qt::AlignCenter, tr("Computer Offline"));

		painter->setFont(QFont(QStringLiteral("Segoe UI"), 7));
		painter->setPen(QColor(139, 148, 158));
		painter->drawText(QRect(previewRect.left(), cy + 34, previewRect.width(), 14),
						  Qt::AlignCenter, tr("Standby • Ready for WOL"));
	}

	// Quick action hover overlay bar over bottom of preview if hovered
	if (isHovered && previewRect.height() >= 65)
	{
		const QRect hoverBar(previewRect.left() + 4, previewRect.bottom() - 22, previewRect.width() - 8, 20);
		painter->setBrush(QColor(15, 23, 42, 240));
		painter->setPen(QPen(QColor(56, 189, 248, 180), 1));
		painter->drawRoundedRect(hoverBar, 4, 4);

		painter->setFont(QFont(QStringLiteral("Segoe UI"), 7, QFont::DemiBold));
		painter->setPen(QColor(241, 245, 249));
		if (isOnline)
		{
			const int third = hoverBar.width() / 3;
			const QRect rView(hoverBar.left(), hoverBar.top(), third, hoverBar.height());
			const QRect rCtrl(hoverBar.left() + third, hoverBar.top(), third, hoverBar.height());
			const QRect rMsg(hoverBar.left() + third * 2, hoverBar.top(), hoverBar.width() - third * 2, hoverBar.height());
			painter->drawText(rView, Qt::AlignCenter, tr("View"));
			painter->drawText(rCtrl, Qt::AlignCenter, tr("Control"));
			painter->drawText(rMsg, Qt::AlignCenter, tr("Message"));
			painter->setPen(QColor(42, 50, 61));
			painter->drawLine(rCtrl.left(), hoverBar.top() + 3, rCtrl.left(), hoverBar.bottom() - 3);
			painter->drawLine(rMsg.left(), hoverBar.top() + 3, rMsg.left(), hoverBar.bottom() - 3);
		}
		else
		{
			painter->drawText(hoverBar, Qt::AlignCenter, tr("⚡ Power On (WOL)"));
		}
	}

	painter->restore(); // restore clipping

	// 4. Badges on top of preview
	if (isOnline)
	{
		// Pill badge in top right: "● LIVE" (strictly semantic green #22C55E)
		const QRect badgeRect(previewRect.right() - 48, previewRect.top() + 5, 44, 16);
		painter->setBrush(QColor(9, 13, 22, 230));
		painter->setPen(QPen(QColor(34, 197, 94, 180), 1));
		painter->drawRoundedRect(badgeRect, 8, 8);

		painter->setBrush(QColor(34, 197, 94));
		painter->setPen(Qt::NoPen);
		painter->drawEllipse(QPoint(badgeRect.left() + 7, badgeRect.center().y()), 3, 3);

		painter->setPen(QColor(74, 222, 128));
		painter->setFont(QFont(QStringLiteral("Segoe UI"), 6, QFont::Bold));
		painter->drawText(QRect(badgeRect.left() + 11, badgeRect.top(), badgeRect.width() - 12, badgeRect.height()),
						  Qt::AlignCenter, QStringLiteral("LIVE"));
	}
	else if (isConnecting)
	{
		// Pill badge in top right: "SYNCING"
		const QRect badgeRect(previewRect.right() - 72, previewRect.top() + 5, 68, 16);
		painter->setBrush(QColor(24, 20, 12, 230)); // Dark amber bg
		painter->setPen(QPen(QColor(245, 158, 11, 180), 1)); // Amber border
		painter->drawRoundedRect(badgeRect, 8, 8);

		painter->setBrush(QColor(245, 158, 11));
		painter->setPen(Qt::NoPen);
		painter->drawEllipse(QPoint(badgeRect.left() + 7, badgeRect.center().y()), 3, 3);

		painter->setPen(QColor(251, 191, 36));
		painter->setFont(QFont(QStringLiteral("Segoe UI"), 6, QFont::Bold));
		painter->drawText(QRect(badgeRect.left() + 11, badgeRect.top(), badgeRect.width() - 12, badgeRect.height()),
						  Qt::AlignCenter, QStringLiteral("SYNCING"));
	}
	else
	{
		// Pill badge in top right: "OFFLINE"
		const QRect badgeRect(previewRect.right() - 54, previewRect.top() + 5, 50, 16);
		painter->setBrush(QColor(9, 14, 26, 230));
		painter->setPen(QPen(QColor(42, 50, 61), 1));
		painter->drawRoundedRect(badgeRect, 8, 8);

		painter->setPen(QColor(139, 148, 158));
		painter->setFont(QFont(QStringLiteral("Segoe UI"), 6, QFont::DemiBold));
		painter->drawText(badgeRect, Qt::AlignCenter, QStringLiteral("OFFLINE"));
	}

	// Feature badges on top left
	drawFeatureIcons(painter, previewRect.topLeft() + QPoint(4, 4), controlInterface);

	// 5. Info Area Below Preview
	const int infoTop = previewRect.bottom() + 4;
	const QRect infoRect(rect.left() + 8, infoTop, rect.width() - 16, rect.bottom() - infoTop);

	// Computer Name
	painter->setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::Bold));
	painter->setPen(isSelected ? QColor(248, 250, 252) : QColor(230, 237, 243));
	QFontMetrics fmName(painter->font());
	const QString elidedName = fmName.elidedText(compName, Qt::ElideRight, infoRect.width());
	painter->drawText(QRect(infoRect.left(), infoRect.top(), infoRect.width(), 16), Qt::AlignLeft | Qt::AlignVCenter, elidedName);

	// Status Line: Status Dot + Details (User / IP / Status)
	const int statusY = infoRect.top() + 18;
	const QPoint dotCenter(infoRect.left() + 4, statusY + 7);

	QColor statusDotColor;
	if (isOnline)
	{
		statusDotColor = QColor(34, 197, 94); // #22C55E Semantic Online Green
		painter->setBrush(QColor(34, 197, 94, 45));
		painter->setPen(Qt::NoPen);
		painter->drawEllipse(dotCenter, 6, 6);
	}
	else if (isConnecting)
	{
		statusDotColor = QColor(251, 191, 36); // #FBBF24 Warning Amber
		painter->setBrush(QColor(251, 191, 36, 40));
		painter->setPen(Qt::NoPen);
		painter->drawEllipse(dotCenter, 6, 6);
	}
	else
	{
		statusDotColor = QColor(100, 116, 139); // Slate / Gray
	}

	painter->setBrush(statusDotColor);
	painter->setPen(Qt::NoPen);
	painter->drawEllipse(dotCenter, 3, 3);

	// Text beside dot
	painter->setFont(QFont(QStringLiteral("Segoe UI"), 8));
	painter->setPen(QColor(139, 148, 158));

	QString detailsText;
	if (isOnline)
	{
		if (!user.isEmpty())
		{
			detailsText = QStringLiteral("%1 • %2").arg(user, ip);
		}
		else if (!ip.isEmpty())
		{
			detailsText = QStringLiteral("Online • %1").arg(ip);
		}
		else
		{
			detailsText = tr("Online");
		}
	}
	else if (isConnecting)
	{
		if (!ip.isEmpty())
		{
			detailsText = QStringLiteral("Connecting • %1").arg(ip);
		}
		else
		{
			detailsText = tr("Connecting...");
		}
	}
	else
	{
		if (!ip.isEmpty())
		{
			detailsText = QStringLiteral("Offline • %1").arg(ip);
		}
		else
		{
			detailsText = tr("Offline");
		}
	}

	QFontMetrics fmDetails(painter->font());
	const QString elidedDetails = fmDetails.elidedText(detailsText, Qt::ElideRight, infoRect.width() - 14);
	painter->drawText(QRect(infoRect.left() + 12, statusY, infoRect.width() - 12, 16),
					  Qt::AlignLeft | Qt::AlignVCenter, elidedDetails);

	painter->restore();
}



QSize ComputerItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	Q_UNUSED(option);
	Q_UNUSED(index);

	int w = 240;
	if (!m_master)
	{
		m_master = VeyonCore::instance()->findChild<VeyonMaster*>();
	}
	if (m_master)
	{
		w = m_master->computerControlListModel().computerScreenSize().width();
	}
	if (w <= 0)
	{
		w = 230;
	}
	w = qBound(ComputerMonitoringView::MinimumComputerScreenSize, w, ComputerMonitoringView::MaximumComputerScreenSize);
	const int h = int((w - 12) * 9.0 / 16.0) + 64;
	return QSize(w, h);
}



bool ComputerItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
	if (event->type() == QEvent::MouseButtonRelease && index.isValid())
	{
		auto* me = static_cast<QMouseEvent*>(event);
		if (me->button() == Qt::LeftButton)
		{
			const QRect rect = option.rect.adjusted(3, 3, -3, -3);
			const int previewMargin = 6;
			const int previewW = rect.width() - (previewMargin * 2);
			const int previewH = qMax(48, rect.height() - 56);
			const QRect previewRect(rect.left() + previewMargin, rect.top() + previewMargin, previewW, previewH);
			const QRect hoverBar(previewRect.left() + 4, previewRect.bottom() - 22, previewRect.width() - 8, 20);

			if (previewRect.height() >= 65 && hoverBar.contains(me->pos()))
			{
				const auto controlInterface = model->data(index, ComputerControlListModel::ControlInterfaceRole).value<ComputerControlInterface::Pointer>();
				if (controlInterface)
				{
					if (!m_master)
					{
						m_master = VeyonCore::instance()->findChild<VeyonMaster*>();
					}
					if (m_master)
					{
						const bool isOnline = (controlInterface->state() == ComputerControlInterface::State::Connected);
						QString targetFeat;
						if (isOnline)
						{
							const int third = hoverBar.width() / 3;
							const int relX = me->pos().x() - hoverBar.left();
							if (relX < third)
							{
								targetFeat = QStringLiteral("RemoteView");
							}
							else if (relX < third * 2)
							{
								targetFeat = QStringLiteral("RemoteControl");
							}
							else
							{
								targetFeat = QStringLiteral("TextMessage");
							}
						}
						else
						{
							targetFeat = QStringLiteral("PowerOn");
						}

						for (const auto& feat : VeyonCore::featureManager().features())
						{
							if (feat.name().compare(targetFeat, Qt::CaseInsensitive) == 0)
							{
								VeyonCore::featureManager().startFeature(*m_master, feat, { controlInterface });
								return true;
							}
						}
					}
				}
			}
		}
	}

	return QStyledItemDelegate::editorEvent(event, model, option, index);
}



void ComputerItemDelegate::initFeaturePixmaps()
{
	for (const auto& feature : VeyonCore::featureManager().features() )
	{
		if (feature.testFlag(Feature::Flag::Master) && !feature.iconUrl().isEmpty())
		{
			m_featurePixmaps[feature.uid()] = QIcon(feature.iconUrl()).pixmap(QSize(OverlayIconSize, OverlayIconSize));
		}
	}
}



void ComputerItemDelegate::drawFeatureIcons(QPainter* painter, const QPoint& pos, ComputerControlInterface::Pointer controlInterface) const
{
	if (painter &&
		controlInterface &&
		controlInterface->state() == ComputerControlInterface::State::Connected)
	{
		auto count = 0;
		for (const auto& feature : controlInterface->activeFeatures())
		{
			if (m_featurePixmaps.contains(feature))
			{
				count++;
			}
		}

		if (count == 0)
		{
			return;
		}

		int x = pos.x();
		const int y = pos.y();

		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		painter->setBrush(QColor(15, 23, 42, 220));
		painter->setPen(QPen(QColor(56, 189, 248), 1));
		painter->drawRoundedRect(QRect(x, y, count * (OverlayIconSize + OverlayIconSpacing) + 4, OverlayIconSize + 4),
								 OverlayIconsRadius, OverlayIconsRadius);

		for (const auto& feature : controlInterface->activeFeatures())
		{
			const auto it = m_featurePixmaps.find(feature);
			if (it != m_featurePixmaps.constEnd())
			{
				painter->drawPixmap(QPoint(x + 2, y + 2), *it);
				x += OverlayIconSize + OverlayIconSpacing;
			}
		}
		painter->restore();
	}
}
