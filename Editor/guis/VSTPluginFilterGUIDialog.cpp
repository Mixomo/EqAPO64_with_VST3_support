/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QScreen>
#include <QApplication>
#include "VSTPluginFilterGUIDialog.h"
#include "ui_VSTPluginFilterGUIDialog.h"

using namespace std;
using namespace std::placeholders;

VSTPluginFilterGUIDialog::VSTPluginFilterGUIDialog(QWidget* parent, VSTPluginInstance* effect, bool autoApply, bool processingPrepared)
	: QDialog(parent, Qt::WindowCloseButtonHint),
	ui(new Ui::VSTPluginFilterGUIDialog),
	effect(effect),processingPrepared(processingPrepared)
{
	ui->setupUi(this);
    // Keep the owned interactive engineering window above the test runner.
    // Set the Qt flag before creating native children; native SetWindowPos
    // alone can be reset by Qt when its window state is reconciled.
    if(qApp->property("rowLiveAuditOnTop").toBool())setWindowFlag(Qt::WindowStaysOnTopHint,true);
	ui->autoApplyCheckBox->setChecked(autoApply);

	QString name = QString::fromStdWString(effect->getName());
	setWindowTitle(name);
	ui->frame->setFixedSize(400, 300);

	connect(&idleTimer, &QTimer::timeout, this, [this]() {
		if (this->editorStarted)
		{
			this->effect->doIdle();
            if(this->effect->pendingRestartMask())this->effect->serviceRestartRequests();
            this->effect->setEditorScale(float(devicePixelRatioF()));
			int width = 0;
			int height = 0;
			if (this->effect->getEditorSize(&width, &height) && width > 0 && height > 0
				&& (this->ui->frame->width() != qRound(width/devicePixelRatioF()) || this->ui->frame->height() != qRound(height/devicePixelRatioF())))
				this->ui->frame->setFixedSize(qRound(width/devicePixelRatioF()),qRound(height/devicePixelRatioF()));
		}
	});
	idleTimer.setTimerType(Qt::PreciseTimer);
	idleTimer.setInterval(16);
}

VSTPluginFilterGUIDialog::~VSTPluginFilterGUIDialog()
{
	idleTimer.stop();
    if(editorStarted)effect->stopEditing();
	effect->setSizeWindowFunc(nullptr);

	delete ui;
}

void VSTPluginFilterGUIDialog::done(int result)
{
    // QDialog::done hides its native parent. Detach the plugin view while
    // that parent is still visible and its owner message loop is intact.
    idleTimer.stop();
    if(editorStarted) {
        effect->setSizeWindowFunc(nullptr);
        effect->stopEditing();
        editorStarted=false;
    }
    QDialog::done(result);
}

QPushButton* VSTPluginFilterGUIDialog::getApplyButton()
{
	return ui->buttonBox->button(QDialogButtonBox::Apply);
}

QCheckBox* VSTPluginFilterGUIDialog::getAutoApplyCheckBox()
{
	return ui->autoApplyCheckBox;
}

void VSTPluginFilterGUIDialog::onSizeWindow(int w, int h)
{
	ui->frame->setFixedSize(qRound(w/devicePixelRatioF()),qRound(h/devicePixelRatioF()));
}

void VSTPluginFilterGUIDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	QTimer::singleShot(0, this, [this]() { startEditor(); });
}

void VSTPluginFilterGUIDialog::startEditor()
{
	if (editorStarted)
		return;
	editorStarted = true;

	HWND hwnd = (HWND)ui->frame->winId();
	short width = 400;
	short height = 300;
    if(!processingPrepared)effect->prepareForProcessing(48000,512);

	if (effect->startEditing(hwnd, &width, &height))
	{
		ui->frame->setFixedSize(qRound(width/devicePixelRatioF()),qRound(height/devicePixelRatioF()));
		effect->setSizeWindowFunc(bind(&VSTPluginFilterGUIDialog::onSizeWindow, this, _1, _2));
		idleTimer.start();
		return;
	}

	QVBoxLayout* layout = new QVBoxLayout(ui->frame);
	QLabel* label = new QLabel(tr("This VST3 plug-in did not expose an embeddable editor."), ui->frame);
	label->setWordWrap(true);
	label->setAlignment(Qt::AlignCenter);
	layout->addWidget(label);
	ui->frame->setFixedSize(420, 120);
}

void VSTPluginFilterGUIDialog::on_autoApplyCheckBox_clicked(bool checked)
{
	if (checked)
		getApplyButton()->click();
}
