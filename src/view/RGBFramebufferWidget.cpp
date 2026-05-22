/**
 * Copyright (c) 2021 Alban Fichet <alban dot fichet at gmx dot fr>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *  * Neither the name of the organization(s) nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "RGBFramebufferWidget.h"
#include "ui_RGBFramebufferWidget.h"

#include "GraphicsView.h"

#include <QAbstractSpinBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStyle>
#include <QtGlobal>

#include <cmath>

RGBFramebufferWidget::RGBFramebufferWidget(QWidget* parent)
  : QWidget(parent)
  , ui(new Ui::RGBFramebufferWidget)
  , m_model(nullptr)
  , m_zoomLevel(1.)
{
    ui->setupUi(this);
    ui->fileInfoButton->setIcon(
      style()->standardIcon(QStyle::SP_MessageBoxInformation));

    // clang-format off
    connect(
      ui->graphicsView, SIGNAL(openFileOnDropEvent(QString)),
      this,             SLOT(onOpenFileOnDropEvent(QString)));

    connect(
        ui->graphicsView, SIGNAL(queryPixelInfo(int,int)),
        this,             SLOT(onQueryPixelInfo(int,int)));

    connect(
        ui->graphicsView, SIGNAL(zoomLevelChanged(double)),
        this,             SLOT(updateZoomLevelText(double)));

    connect(
        ui->graphicsView, SIGNAL(controlWheel(int)),
        this,             SLOT(onControlWheel(int)));
    // clang-format on

    ui->sbExposure->installEventFilter(this);
    QLineEdit* exposureEditor = ui->sbExposure->findChild<QLineEdit*>();
    if (exposureEditor) exposureEditor->installEventFilter(this);
    setExposureCompact(true);
}


RGBFramebufferWidget::~RGBFramebufferWidget()
{
    delete ui;

    if (m_model) delete m_model;
}


void RGBFramebufferWidget::setModel(RGBFramebufferModel* model)
{
    m_model = model;
    ui->graphicsView->setModel(m_model);
    onQueryPixelInfo(0, 0);
}


void RGBFramebufferWidget::setExposure(double value)
{
    if (m_model) {
        m_model->setExposure(value);
    }
}


void RGBFramebufferWidget::setExposureCompact(bool compact)
{
    ui->sbExposure->setReadOnly(compact);
    ui->sbExposure->setFrame(!compact);
    ui->sbExposure->setFixedWidth(compact ? 66 : 84);
    ui->sbExposure->setButtonSymbols(
      compact ? QAbstractSpinBox::NoButtons : QAbstractSpinBox::UpDownArrows);
}


bool RGBFramebufferWidget::eventFilter(QObject* watched, QEvent* event)
{
    QWidget* watchedWidget = qobject_cast<QWidget*>(watched);
    const bool exposureTarget =
      watched == ui->sbExposure
      || (watchedWidget && ui->sbExposure->isAncestorOf(watchedWidget));

    if (!exposureTarget) {
        return QWidget::eventFilter(watched, event);
    }

    if (
      event->type() == QEvent::MouseButtonPress
      || event->type() == QEvent::FocusIn) {
        setExposureCompact(false);
    }

    if (event->type() == QEvent::FocusOut) {
        ui->sbExposure->interpretText();
        setExposureCompact(true);
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        const bool commitKey =
          keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;

        if (commitKey) {
            ui->sbExposure->interpretText();
            ui->sbExposure->clearFocus();
            setExposureCompact(true);
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}


void RGBFramebufferWidget::onQueryPixelInfo(int x, int y)
{
    ui->pixelValueLabel->setText(
      QString::fromStdString(m_model->getColorInfo(x, y)));
}


void RGBFramebufferWidget::on_sbExposure_valueChanged(double value)
{
    const int sliderValue = static_cast<int>(std::round(value * 10.));

    ui->slExposure->blockSignals(true);
    ui->slExposure->setValue(sliderValue);
    ui->slExposure->blockSignals(false);

    setExposure(value);
}


void RGBFramebufferWidget::on_slExposure_valueChanged(int value)
{
    const double exposure = double(value) / 10.;

    ui->sbExposure->blockSignals(true);
    ui->sbExposure->setValue(exposure);
    ui->sbExposure->blockSignals(false);

    setExposure(exposure);
}


void RGBFramebufferWidget::on_exposureButton_clicked()
{
    ui->sbExposure->setValue(0.);
}


void RGBFramebufferWidget::onOpenFileOnDropEvent(const QString& filename)
{
    emit openFileOnDropEvent(filename);
}


void RGBFramebufferWidget::onControlWheel(int delta)
{
    if (!m_model || !m_model->isImageLoaded() || delta == 0) return;

    const double direction = delta > 0 ? 1. : -1.;
    ui->sbExposure->setValue(
      ui->sbExposure->value() + direction * ui->sbExposure->singleStep());
}


void RGBFramebufferWidget::updateZoomLevelText(double zoom)
{
    m_zoomLevel = zoom;
    ui->zoomButton->setText(tr("Zoom: %1%").arg(qRound(zoom * 100.)));
}


void RGBFramebufferWidget::on_zoomButton_clicked()
{
    if (qRound(m_zoomLevel * 100.) == 100) {
        ui->graphicsView->autoscale();
        return;
    }

    ui->graphicsView->setZoomLevel(1.);
}


void RGBFramebufferWidget::on_fileInfoButton_clicked()
{
    emit fileInfoRequested(this);
}
