/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2016  Jonas Thedering

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

#include "GUIHelper.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QScreen>
#include <QStyleHints>
#include <QPalette>
#include <QWidget>

QSize GUIHelper::logicalPixels(QSize size)
{ return size; }

int GUIHelper::logicalPixels(double pixel)
{ return qRound(pixel); }

double GUIHelper::logicalZoom(double zoom)
{ return zoom; }

double GUIHelper::logicalValue(int pixel)
{ return pixel; }

double GUIHelper::logicalZoomValue(double zoom)
{ return zoom; }

bool GUIHelper::isDarkMode()
{
	return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

static void setTextColor(QPalette& palette, const QColor& color)
{
	for (auto group : {QPalette::Active, QPalette::Inactive})
	{
		palette.setColor(group, QPalette::WindowText, color);
		palette.setColor(group, QPalette::Text, color);
	}
}

void GUIHelper::applyWarningBoxPalette(QWidget* widget)
{
	if (!widget) return;
	const bool dark = isDarkMode();
	QPalette palette = widget->palette();
	const QColor base = dark ? QColor(74, 60, 10) : QColor(255, 255, 127);
	const QColor text = dark ? QColor(255, 226, 140) : QColor(0, 0, 0);
	for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
		palette.setColor(group, QPalette::Base, base);
	setTextColor(palette, text);
	widget->setPalette(palette);
}

void GUIHelper::applyErrorTextPalette(QWidget* widget)
{
	if (!widget) return;
	QPalette palette = widget->palette();
	setTextColor(palette, isDarkMode() ? QColor(255, 120, 120) : QColor(255, 0, 0));
	widget->setPalette(palette);
}

void GUIHelper::applyStatusTextPalette(QWidget* widget)
{
	if (!widget) return;
	QPalette palette = widget->palette();
	setTextColor(palette, isDarkMode() ? QColor(255, 255, 255) : QColor(0, 0, 0));
	widget->setPalette(palette);
}
