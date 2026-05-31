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

#include "FramebufferInfo.h"
#include "GraphicsView.h"
#include "ComboBoxBehavior.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPoint>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QtGlobal>
#include <QVariant>
#include <QWidget>

#include <cmath>

static const int s_toneParamCount = 4;


static int toneSliderFromValue(double value)
{
    return static_cast<int>(std::round(value * 100.));
}


static double toneValueFromSlider(int value)
{
    return double(value) / 100.;
}


RGBFramebufferWidget::RGBFramebufferWidget(QWidget* parent)
  : QWidget(parent)
  , ui(new Ui::RGBFramebufferWidget)
  , m_model(nullptr)
  , m_previewMode(RGBFramebufferModel::Preview_Exposure)
  , m_toneParamDefaults{ 0., 0., 0., 0. }
  , m_zoomLevel(1.)
{
    ui->setupUi(this);
    ui->fileInfoButton->setIcon(
      style()->standardIcon(QStyle::SP_MessageBoxInformation));
    ui->fileInfoButton->setToolTip(QString());
    ui->fileInfoButton->installEventFilter(this);
    updateFramebufferSummary();

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

    ui->cbToneMappingMethod->blockSignals(true);
    ui->cbToneMappingMethod->addItem(
      tr("Reinhard"),
      RGBFramebufferModel::Tone_Reinhard);
    ui->cbToneMappingMethod->addItem(
      tr("ACES fitted"),
      RGBFramebufferModel::Tone_ACES);
    ui->cbToneMappingMethod->addItem(
      tr("Filmic/Hable"),
      RGBFramebufferModel::Tone_Filmic);
    ui->cbToneMappingMethod->addItem(
      tr("Log"),
      RGBFramebufferModel::Tone_Log);
    ui->cbToneMappingMethod->addItem(
      tr("Clamp"),
      RGBFramebufferModel::Tone_Clamp);
    ui->cbToneMappingMethod->setCurrentIndex(0);
    ui->cbToneMappingMethod->blockSignals(false);
    applyComboBoxBehavior(this);

    installCompactSpinBox(ui->sbExposure);
    installCompactSpinBox(ui->sbToneParam0);
    installCompactSpinBox(ui->sbToneParam1);
    installCompactSpinBox(ui->sbToneParam2);
    installCompactSpinBox(ui->sbToneParam3);
    updateToneMappingControls();
    setPreviewMode(m_previewMode);
}


RGBFramebufferWidget::~RGBFramebufferWidget()
{
    delete ui;

    if (m_model) delete m_model;
}


void RGBFramebufferWidget::setModel(RGBFramebufferModel* model)
{
    m_model = model;
    m_model->setExposure(ui->sbExposure->value());
    m_model->setPreviewMode(m_previewMode);
    m_model->setToneMappingMethod(currentToneMappingMethod());
    syncToneParamsToModel();
    ui->graphicsView->setModel(m_model);
    connect(
      m_model,
      SIGNAL(imageLoaded()),
      this,
      SLOT(updateFramebufferSummary()));
    onQueryPixelInfo(0, 0);
    updateFramebufferSummary();
}


void RGBFramebufferWidget::setExposure(double value)
{
    if (m_model) {
        m_model->setExposure(value);
    }
}


void RGBFramebufferWidget::setPreviewMode(RGBFramebufferModel::PreviewMode mode)
{
    const bool toneMapping = mode == RGBFramebufferModel::Preview_ToneMapping;

    m_previewMode = mode;

    ui->exposureButton->setVisible(!toneMapping);
    ui->slExposure->setVisible(!toneMapping);
    ui->sbExposure->setVisible(!toneMapping);
    ui->toneMappingControlsWidget->setVisible(toneMapping);

    if (m_model) m_model->setPreviewMode(mode);
}


void RGBFramebufferWidget::setSpinBoxCompact(
  QDoubleSpinBox* spinBox, bool compact)
{
    spinBox->setReadOnly(compact);
    spinBox->setFrame(!compact);
    spinBox->setFixedWidth(compact ? 66 : 84);
    spinBox->setButtonSymbols(
      compact ? QAbstractSpinBox::NoButtons : QAbstractSpinBox::UpDownArrows);
}


void RGBFramebufferWidget::installCompactSpinBox(QDoubleSpinBox* spinBox)
{
    spinBox->installEventFilter(this);

    QLineEdit* editor = spinBox->findChild<QLineEdit*>();
    if (editor) editor->installEventFilter(this);

    setSpinBoxCompact(spinBox, true);
}


QDoubleSpinBox* RGBFramebufferWidget::compactSpinBox(QObject* watched) const
{
    QWidget* watchedWidget = qobject_cast<QWidget*>(watched);
    QDoubleSpinBox* spinBoxes[] = {
      ui->sbExposure,
      ui->sbToneParam0,
      ui->sbToneParam1,
      ui->sbToneParam2,
      ui->sbToneParam3,
    };

    for (QDoubleSpinBox* spinBox: spinBoxes) {
        if (
          watched == spinBox
          || (watchedWidget && spinBox->isAncestorOf(watchedWidget))) {
            return spinBox;
        }
    }

    return nullptr;
}


QDoubleSpinBox* RGBFramebufferWidget::toneParamSpinBox(int index) const
{
    QDoubleSpinBox* spinBoxes[] = {
      ui->sbToneParam0,
      ui->sbToneParam1,
      ui->sbToneParam2,
      ui->sbToneParam3,
    };

    if (index < 0 || index >= s_toneParamCount) return nullptr;

    return spinBoxes[index];
}


RGBFramebufferModel::ToneMappingMethod
RGBFramebufferWidget::currentToneMappingMethod() const
{
    const int index = ui->cbToneMappingMethod->currentIndex();

    if (index < 0) return RGBFramebufferModel::Tone_Reinhard;

    return static_cast<RGBFramebufferModel::ToneMappingMethod>(
      ui->cbToneMappingMethod->itemData(index).toInt());
}


void RGBFramebufferWidget::configureToneParam(
  int index,
  const QString& label,
  double minimum,
  double maximum,
  double step,
  double value)
{
    QWidget* widgets[] = {
      ui->toneParamWidget0,
      ui->toneParamWidget1,
      ui->toneParamWidget2,
      ui->toneParamWidget3,
    };
    QPushButton* buttons[] = {
      ui->toneParamButton0,
      ui->toneParamButton1,
      ui->toneParamButton2,
      ui->toneParamButton3,
    };
    QSlider* sliders[] = {
      ui->slToneParam0,
      ui->slToneParam1,
      ui->slToneParam2,
      ui->slToneParam3,
    };
    QDoubleSpinBox* spinBoxes[] = {
      ui->sbToneParam0,
      ui->sbToneParam1,
      ui->sbToneParam2,
      ui->sbToneParam3,
    };

    if (index < 0 || index >= s_toneParamCount) return;

    m_toneParamDefaults[index] = value;

    widgets[index]->setVisible(true);
    buttons[index]->setText(label);

    sliders[index]->blockSignals(true);
    spinBoxes[index]->blockSignals(true);

    sliders[index]->setMinimum(toneSliderFromValue(minimum));
    sliders[index]->setMaximum(toneSliderFromValue(maximum));
    sliders[index]->setSingleStep(toneSliderFromValue(step));
    sliders[index]->setPageStep(toneSliderFromValue(step * 10.));
    sliders[index]->setValue(toneSliderFromValue(value));

    spinBoxes[index]->setDecimals(2);
    spinBoxes[index]->setMinimum(minimum);
    spinBoxes[index]->setMaximum(maximum);
    spinBoxes[index]->setSingleStep(step);
    spinBoxes[index]->setValue(value);

    sliders[index]->blockSignals(false);
    spinBoxes[index]->blockSignals(false);
}


void RGBFramebufferWidget::hideToneParams(int firstHiddenIndex)
{
    QWidget* widgets[] = {
      ui->toneParamWidget0,
      ui->toneParamWidget1,
      ui->toneParamWidget2,
      ui->toneParamWidget3,
    };

    for (int i = firstHiddenIndex; i < s_toneParamCount; i++) {
        widgets[i]->setVisible(false);
        m_toneParamDefaults[i] = 0.;
    }
}


void RGBFramebufferWidget::setToneParamValue(int index, double value)
{
    QSlider* sliders[] = {
      ui->slToneParam0,
      ui->slToneParam1,
      ui->slToneParam2,
      ui->slToneParam3,
    };
    QDoubleSpinBox* spinBox = toneParamSpinBox(index);

    if (!spinBox) return;

    sliders[index]->blockSignals(true);
    spinBox->blockSignals(true);

    sliders[index]->setValue(toneSliderFromValue(value));
    spinBox->setValue(value);

    sliders[index]->blockSignals(false);
    spinBox->blockSignals(false);

    syncToneParamsToModel();
}


void RGBFramebufferWidget::resetToneParam(int index)
{
    if (index < 0 || index >= s_toneParamCount) return;

    setToneParamValue(index, m_toneParamDefaults[index]);
}


void RGBFramebufferWidget::syncToneParamsToModel()
{
    if (!m_model) return;

    m_model->setToneParameters(
      ui->sbToneParam0->value(),
      ui->sbToneParam1->value(),
      ui->sbToneParam2->value(),
      ui->sbToneParam3->value());
}


void RGBFramebufferWidget::updateToneMappingControls()
{
    switch (currentToneMappingMethod()) {
        case RGBFramebufferModel::Tone_ACES:
            configureToneParam(0, tr("Shoulder"), 0.10, 4.00, 0.01, 1.00);
            configureToneParam(1, tr("Toe"), 0.10, 4.00, 0.01, 1.00);
            hideToneParams(2);
            break;

        case RGBFramebufferModel::Tone_Filmic:
            configureToneParam(
              0, tr("Shoulder Strength"), 0.00, 1.00, 0.01, 0.15);
            configureToneParam(
              1, tr("Linear Strength"), 0.00, 1.00, 0.01, 0.50);
            configureToneParam(2, tr("Linear Angle"), 0.00, 1.00, 0.01, 0.10);
            configureToneParam(3, tr("Toe Strength"), 0.00, 1.00, 0.01, 0.20);
            break;

        case RGBFramebufferModel::Tone_Log:
            configureToneParam(0, tr("Range"), 1.00, 64.00, 0.10, 16.00);
            configureToneParam(1, tr("Compression"), 0.10, 8.00, 0.01, 1.00);
            hideToneParams(2);
            break;

        case RGBFramebufferModel::Tone_Clamp:
            configureToneParam(0, tr("Min"), 0.00, 16.00, 0.01, 0.00);
            configureToneParam(1, tr("Max"), 0.01, 64.00, 0.01, 1.00);
            hideToneParams(2);
            break;

        case RGBFramebufferModel::Tone_Reinhard:
        default:
            configureToneParam(0, tr("Key"), 0.01, 2.00, 0.01, 0.18);
            configureToneParam(1, tr("Shoulder"), 0.10, 1.00, 0.01, 1.00);
            hideToneParams(2);
            break;
    }

    syncToneParamsToModel();
}


bool RGBFramebufferWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == ui->fileInfoButton) {
        if (event->type() == QEvent::Enter) {
            const QPoint position =
              ui->fileInfoButton->mapToGlobal(QPoint(
                ui->fileInfoButton->width() + 8,
                ui->fileInfoButton->height() / 2));
            emit fileInfoHoverRequested(this, position);
        }

        if (event->type() == QEvent::Leave) {
            emit fileInfoHoverLeft();
        }

        return QWidget::eventFilter(watched, event);
    }

    QDoubleSpinBox* spinBox = compactSpinBox(watched);

    if (!spinBox) {
        return QWidget::eventFilter(watched, event);
    }

    if (
      event->type() == QEvent::MouseButtonPress
      || event->type() == QEvent::FocusIn) {
        setSpinBoxCompact(spinBox, false);
    }

    if (event->type() == QEvent::FocusOut) {
        spinBox->interpretText();
        setSpinBoxCompact(spinBox, true);
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        const bool commitKey =
          keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;

        if (commitKey) {
            spinBox->interpretText();
            spinBox->clearFocus();
            setSpinBoxCompact(spinBox, true);
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


void RGBFramebufferWidget::on_cbToneMappingMethod_currentIndexChanged(int)
{
    if (m_model) m_model->setToneMappingMethod(currentToneMappingMethod());

    updateToneMappingControls();
}


void RGBFramebufferWidget::on_sbToneParam0_valueChanged(double value)
{
    setToneParamValue(0, value);
}


void RGBFramebufferWidget::on_slToneParam0_valueChanged(int value)
{
    setToneParamValue(0, toneValueFromSlider(value));
}


void RGBFramebufferWidget::on_toneParamButton0_clicked()
{
    resetToneParam(0);
}


void RGBFramebufferWidget::on_sbToneParam1_valueChanged(double value)
{
    setToneParamValue(1, value);
}


void RGBFramebufferWidget::on_slToneParam1_valueChanged(int value)
{
    setToneParamValue(1, toneValueFromSlider(value));
}


void RGBFramebufferWidget::on_toneParamButton1_clicked()
{
    resetToneParam(1);
}


void RGBFramebufferWidget::on_sbToneParam2_valueChanged(double value)
{
    setToneParamValue(2, value);
}


void RGBFramebufferWidget::on_slToneParam2_valueChanged(int value)
{
    setToneParamValue(2, toneValueFromSlider(value));
}


void RGBFramebufferWidget::on_toneParamButton2_clicked()
{
    resetToneParam(2);
}


void RGBFramebufferWidget::on_sbToneParam3_valueChanged(double value)
{
    setToneParamValue(3, value);
}


void RGBFramebufferWidget::on_slToneParam3_valueChanged(int value)
{
    setToneParamValue(3, toneValueFromSlider(value));
}


void RGBFramebufferWidget::on_toneParamButton3_clicked()
{
    resetToneParam(3);
}


void RGBFramebufferWidget::onOpenFileOnDropEvent(const QString& filename)
{
    emit openFileOnDropEvent(filename);
}


void RGBFramebufferWidget::onControlWheel(int delta)
{
    if (!m_model || !m_model->isImageLoaded() || delta == 0) return;

    const double direction = delta > 0 ? 1. : -1.;
    QDoubleSpinBox* spinBox =
      m_previewMode == RGBFramebufferModel::Preview_ToneMapping
        ? ui->sbToneParam0
        : ui->sbExposure;

    spinBox->setValue(spinBox->value() + direction * spinBox->singleStep());
}


void RGBFramebufferWidget::updateZoomLevelText(double zoom)
{
    m_zoomLevel = zoom;
    ui->zoomButton->setText(tr("Zoom %1%").arg(qRound(zoom * 100.)));
}


void RGBFramebufferWidget::updateFramebufferSummary()
{
    ui->framebufferSummaryLabel->setText(framebufferSummaryText(m_model));
}


void RGBFramebufferWidget::on_zoomButton_clicked()
{
    if (qRound(m_zoomLevel * 100.) == 100) {
        ui->graphicsView->autoscale();
        return;
    }

    ui->graphicsView->setZoomLevel(1.);
}
