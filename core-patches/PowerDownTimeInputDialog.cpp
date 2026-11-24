/*
 * PowerDownTimeInputDialog.h - implementation of PowerDownTimeInputDialog class
 *
 * Copyright (c) 2019-2026 Tobias Junghans <tobydox@veyon.io>
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

#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

#include "PowerDownTimeInputDialog.h"
#include "ui_PowerDownTimeInputDialog.h"


PowerDownTimeInputDialog::PowerDownTimeInputDialog( QWidget *parent, int computerCount ) :
	QDialog( parent ),
	ui( new Ui::PowerDownTimeInputDialog ),
	m_seconds( 300 ),
	m_computerCount( computerCount )
{
	ui->setupUi( this );
	setFixedSize( 400, 246 );
	setWindowTitle( tr( "Power down" ) );
	setWindowFlags( ( windowFlags() & ~Qt::WindowContextHelpButtonHint ) | Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint );
	setModal( true );
	setWindowModality( Qt::ApplicationModal );

	ui->label_3->hide();

	// Clear out the auto-generated layout safely without corrupting child widgets
	QLayoutItem* item;
	while( ( item = layout()->takeAt( 0 ) ) != nullptr )
	{
		delete item;
	}
	delete layout();

	auto* mainLayout = new QVBoxLayout( this );
	mainLayout->setContentsMargins( 24, 20, 24, 20 );
	mainLayout->setSpacing( 10 );

	auto* titleLabel = new QLabel( tr( "POWER DOWN COMPUTERS" ), this );
	titleLabel->setStyleSheet( QStringLiteral( "font-size: 13px; font-weight: 700; color: #F8FAFC; letter-spacing: 0.5px;" ) );
	mainLayout->addWidget( titleLabel );

	QString countText;
	if( m_computerCount <= 0 )
	{
		countText = tr( "All lab computers selected" );
	}
	else if( m_computerCount == 1 )
	{
		countText = tr( "1 computer selected" );
	}
	else
	{
		countText = tr( "%1 computers selected" ).arg( m_computerCount );
	}
	auto* countLabel = new QLabel( countText, this );
	countLabel->setStyleSheet( QStringLiteral( "font-size: 12px; font-weight: 600; color: #38BDF8; padding-bottom: 2px;" ) );
	mainLayout->addWidget( countLabel );

	auto* divider1 = new QFrame( this );
	divider1->setFrameShape( QFrame::HLine );
	divider1->setStyleSheet( QStringLiteral( "background-color: #1E293B; max-height: 1px; border: none;" ) );
	mainLayout->addWidget( divider1 );

	auto* promptLabel = new QLabel( tr( "Shutdown after:" ), this );
	promptLabel->setStyleSheet( QStringLiteral( "font-size: 12px; font-weight: 500; color: #94A3B8; padding-top: 2px;" ) );
	mainLayout->addWidget( promptLabel );

	ui->minutesSpinBox->setAlignment( Qt::AlignCenter );
	ui->secondsSpinBox->setAlignment( Qt::AlignCenter );
	ui->minutesSpinBox->setRange( 0, 1440 );
	ui->secondsSpinBox->setRange( 0, 59 );
	ui->minutesSpinBox->setValue( 5 );
	ui->secondsSpinBox->setValue( 0 );

	auto* timeInputLayout = new QHBoxLayout();
	timeInputLayout->setSpacing( 8 );
	timeInputLayout->addWidget( ui->minutesSpinBox );
	timeInputLayout->addWidget( ui->label );
	timeInputLayout->addWidget( ui->secondsSpinBox );
	timeInputLayout->addWidget( ui->label_2 );
	timeInputLayout->addStretch( 1 );
	mainLayout->addLayout( timeInputLayout );

	auto* noteLabel = new QLabel( tr( "This will power down the selected computers after the specified timeout." ), this );
	noteLabel->setWordWrap( true );
	noteLabel->setStyleSheet( QStringLiteral( "font-size: 11px; color: #64748B; padding-top: 2px; padding-bottom: 2px;" ) );
	mainLayout->addWidget( noteLabel );

	auto* divider2 = new QFrame( this );
	divider2->setFrameShape( QFrame::HLine );
	divider2->setStyleSheet( QStringLiteral( "background-color: #1E293B; max-height: 1px; border: none;" ) );
	mainLayout->addWidget( divider2 );

	mainLayout->addWidget( ui->buttonBox );

	setStyleSheet( QStringLiteral(
		"QDialog {"
		"   background-color: #0F172A;"
		"   color: #F8FAFC;"
		"   border: 1px solid #2A323D;"
		"   border-radius: 10px;"
		"}"
		"QLabel {"
		"   border: none;"
		"   background: transparent;"
		"}"
		"QLabel#label, QLabel#label_2 {"
		"   color: #94A3B8;"
		"   font-size: 12px;"
		"   font-weight: 500;"
		"   margin-left: 3px;"
		"   margin-right: 12px;"
		"}"
		"QSpinBox {"
		"   background-color: #1E293B;"
		"   color: #F8FAFC;"
		"   border: 1px solid #334155;"
		"   border-radius: 6px;"
		"   padding: 2px 4px;"
		"   font-size: 13px;"
		"   font-weight: 600;"
		"   min-width: 52px;"
		"   max-width: 60px;"
		"   min-height: 28px;"
		"   max-height: 30px;"
		"}"
		"QSpinBox:focus {"
		"   border-color: #38BDF8;"
		"   background-color: #0D1626;"
		"}"
		"QSpinBox::up-button, QSpinBox::down-button {"
		"   width: 16px;"
		"   background: #243044;"
		"   border-left: 1px solid #334155;"
		"}"
		"QSpinBox::up-button:hover, QSpinBox::down-button:hover {"
		"   background: #3B82F6;"
		"}"
		"QPushButton {"
		"   background-color: #DC2626;"
		"   color: #FFFFFF;"
		"   font-weight: 600;"
		"   font-size: 12px;"
		"   border: none;"
		"   border-radius: 6px;"
		"   padding: 6px 18px;"
		"   min-width: 90px;"
		"   min-height: 28px;"
		"}"
		"QPushButton:hover { background-color: #B91C1C; }"
		"QPushButton:disabled { background-color: #451A1A; color: #7F1D1D; }"
		"QPushButton[text*=\"Cancel\"] {"
		"   background-color: #1E293B;"
		"   color: #94A3B8;"
		"   border: 1px solid #334155;"
		"   font-weight: 500;"
		"   min-width: 80px;"
		"}"
		"QPushButton[text*=\"Cancel\"]:hover { background-color: #334155; color: #F8FAFC; }"
	) );

	auto* okBtn = ui->buttonBox->button( QDialogButtonBox::Ok );
	if( okBtn )
	{
		okBtn->setText( tr( "Power Down" ) );
	}
	auto* cancelBtn = ui->buttonBox->button( QDialogButtonBox::Cancel );
	if( cancelBtn )
	{
		cancelBtn->setText( tr( "Cancel" ) );
	}

	updateSeconds();

	connect( ui->minutesSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &PowerDownTimeInputDialog::updateSeconds );
	connect( ui->secondsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &PowerDownTimeInputDialog::updateSeconds );

	ui->minutesSpinBox->setFocus();
}



PowerDownTimeInputDialog::~PowerDownTimeInputDialog()
{
	delete ui;
}



int PowerDownTimeInputDialog::seconds() const
{
	return m_seconds;
}



void PowerDownTimeInputDialog::updateSeconds()
{
	m_seconds = ui->minutesSpinBox->value() * 60 + ui->secondsSpinBox->value();

	auto* okBtn = ui->buttonBox->button( QDialogButtonBox::Ok );
	if( okBtn )
	{
		okBtn->setEnabled( m_seconds > 0 );
	}
}
