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
#include "CropIndicator.h"
#include "DepthRangeWidget.h"
#include <QSignalBlocker>
#include "ui_RGBFramebufferWidget.h"

#include "FramebufferInfo.h"
#include "GraphicsView.h"
#include "ComboBoxBehavior.h"
#include "WorkspaceWidgets.h"
#include "ScientificDoubleSpinBox.h"

#include <QAbstractSpinBox>
#include <QToolButton>
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

#include <algorithm>
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
  , m_toneParamDefaults {0., 0., 0., 0.}
  , m_zoomLevel(1.)
  , m_falseColorAutoRange(false)
  , m_savedFalseColorMin(0.)
  , m_savedFalseColorMax(1.)
{
    ui->setupUi(this);
    m_cropIndicator = new CropIndicator(this);
    ui->horizontalLayout_2->insertWidget(1, m_cropIndicator, 0, Qt::AlignVCenter);
    connect(ui->anomalyMarkerButton, &QToolButton::toggled, this, [this](bool enabled) {
        if (m_model) m_model->setHighlightNonFinite(enabled);
    });
    wrapPreviewControls(ui->verticalLayout);
    ui->verticalLayout->insertWidget(1, new DepthRangeWidget(this));
    connect(ui->graphicsView, &GraphicsView::minimalViewRequested,
            this, &RGBFramebufferWidget::minimalViewRequested);
    connect(ui->graphicsView, &GraphicsView::resetParametersRequested,
            this, &RGBFramebufferWidget::resetCurrentMode);
    m_toneParamControls[0] = {
      ui->toneParamWidget0,
      ui->toneParamButton0,
      ui->slToneParam0,
      ui->sbToneParam0,
    };
    m_toneParamControls[1] = {
      ui->toneParamWidget1,
      ui->toneParamButton1,
      ui->slToneParam1,
      ui->sbToneParam1,
    };
    m_toneParamControls[2] = {
      ui->toneParamWidget2,
      ui->toneParamButton2,
      ui->slToneParam2,
      ui->sbToneParam2,
    };
    m_toneParamControls[3] = {
      ui->toneParamWidget3,
      ui->toneParamButton3,
      ui->slToneParam3,
      ui->sbToneParam3,
    };

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
        ui->graphicsView, SIGNAL(controlWheel(double)),
        this,             SLOT(onControlWheel(double)));
    // clang-format on

    const QSignalBlocker blocker_cbToneMappingMethod(ui->cbToneMappingMethod);
    ui->cbToneMappingMethod->addItem(
      tr("Reinhard"),
      RGBFramebufferModel::Tone_Reinhard);
    ui->cbToneMappingMethod->addItem(
      tr("ACES fitted"),
      RGBFramebufferModel::Tone_ACES);
    ui->cbToneMappingMethod->addItem(
      tr("Filmic/Hable"),
      RGBFramebufferModel::Tone_Filmic);
    ui->cbToneMappingMethod->addItem(tr("Log"), RGBFramebufferModel::Tone_Log);
    ui->cbToneMappingMethod->addItem(
      tr("Clamp"),
      RGBFramebufferModel::Tone_Clamp);
    ui->cbToneMappingMethod->setCurrentIndex(0);

    const QSignalBlocker blocker_cbFalseColorColormap(ui->cbFalseColorColormap);
    for (int i = 0; i < ColormapModule::N_MAPS; i++) {
        ui->cbFalseColorColormap->addItem(
          QString::fromStdString(
            ColormapModule::toString((ColormapModule::Map)i)));
    }
    ui->cbFalseColorColormap->setCurrentIndex(ColormapModule::TURBO);
    ui->falseColorAutoButton->setCheckable(true);
    ui->falseColorRangeSlider->setBounds(0., 1.);
    ui->falseColorRangeSlider->setRange(0., 1.);
    ui->falseColorScaleWidget->setColormap(ColormapModule::TURBO);
    setFalseColorRange(0., 1., false);

    applyComboBoxBehavior(this);

    installCompactSpinBox(ui->sbExposure);
    installCompactSpinBox(ui->sbFalseColorMinValue);
    installCompactSpinBox(ui->sbFalseColorMaxValue);
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
}


void RGBFramebufferWidget::setModel(RGBFramebufferModel* model)
{
    m_model = model;
    findChild<DepthRangeWidget*>()->setModel(model);
    if (m_model) m_model->setHighlightNonFinite(ui->anomalyMarkerButton->isChecked());
    if (m_model && m_model->parent() != this) m_model->setParent(this);
    m_model->setExposure(ui->sbExposure->value());
    m_model->setPreviewMode(m_previewMode);
    m_model->setToneMappingMethod(currentToneMappingMethod());
    m_model->setFalseColorColormap(currentFalseColorMap());
    setFalseColorAutoRange(false);
    updateFalseColorRangeBounds();
    syncFalseColorRangeToModel();
    syncToneParamsToModel();
    ui->graphicsView->setModel(m_model);
    connect(model, &FramebufferModel::imageChanged, this, [this] {
        if (m_falseColorAutoRange && m_model->hasFiniteLuminanceSamples())
            setFalseColorRange(m_model->getLuminanceMin(), m_model->getLuminanceMax(), false);
        updateFramebufferSummary();
    });
    connect(
      m_model,
      SIGNAL(imageLoaded()),
      this,
      SLOT(updateFramebufferSummary()));
    connect(
      m_model,
      SIGNAL(imageLoaded()),
      this,
      SLOT(updateFalseColorRangeBounds()));
    connect(
      model,
      &FramebufferModel::readinessChanged,
      this,
      &RGBFramebufferWidget::updateFramebufferSummary);
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
    const bool falseColor  = mode == RGBFramebufferModel::Preview_FalseColor;

    m_previewMode = mode;

    ui->exposureButton->setVisible(!toneMapping && !falseColor);
    ui->slExposure->setVisible(!toneMapping && !falseColor);
    ui->sbExposure->setVisible(!toneMapping && !falseColor);
    ui->toneMappingControlsWidget->setVisible(toneMapping);
    ui->falseColorControlsWidget->setVisible(falseColor);
    ui->cbFalseColorScale->setVisible(falseColor);
    ui->falseColorScaleWidget->setVisible(
      falseColor && ui->cbFalseColorScale->isChecked());

    if (m_model) m_model->setPreviewMode(mode);
}


void RGBFramebufferWidget::setSpinBoxCompact(
  QDoubleSpinBox* spinBox, bool compact)
{
    spinBox->setReadOnly(compact);
    spinBox->setFrame(!compact);
    const int scientificWidth = spinBox->fontMetrics()
      .boundingRect(QStringLiteral("-1.2345678901234567e-38")).width();
    spinBox->setFixedWidth(spinBox->property("scientificRange").toBool()
      ? scientificWidth + (compact ? 16 : 36) : (compact ? 66 : 84));
    spinBox->setButtonSymbols(
      compact ? QAbstractSpinBox::NoButtons : QAbstractSpinBox::UpDownArrows);
    if (spinBox->property("compactValue") != QVariant(compact)) {
        spinBox->setProperty("compactValue", compact);
        spinBox->style()->unpolish(spinBox);
        spinBox->style()->polish(spinBox);
        spinBox->update();
    }
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
    QWidget*        watchedWidget = qobject_cast<QWidget*>(watched);
    QDoubleSpinBox* spinBoxes[]   = {
      ui->sbExposure,
      ui->sbFalseColorMinValue,
      ui->sbFalseColorMaxValue,
      ui->sbToneParam0,
      ui->sbToneParam1,
      ui->sbToneParam2,
      ui->sbToneParam3,
    };

    for (QDoubleSpinBox* spinBox : spinBoxes) {
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
    if (index < 0 || index >= s_toneParamCount) return nullptr;

    return m_toneParamControls[index].spinBox;
}


RGBFramebufferModel::ToneMappingMethod
RGBFramebufferWidget::currentToneMappingMethod() const
{
    const int index = ui->cbToneMappingMethod->currentIndex();

    if (index < 0) return RGBFramebufferModel::Tone_Reinhard;

    return static_cast<RGBFramebufferModel::ToneMappingMethod>(
      ui->cbToneMappingMethod->itemData(index).toInt());
}


ColormapModule::Map RGBFramebufferWidget::currentFalseColorMap() const
{
    const int index = ui->cbFalseColorColormap->currentIndex();

    if (index < 0) return ColormapModule::TURBO;

    return (ColormapModule::Map)index;
}


void RGBFramebufferWidget::configureToneParam(
  int            index,
  const QString& label,
  double         minimum,
  double         maximum,
  double         step,
  double         value)
{
    if (index < 0 || index >= s_toneParamCount) return;

    ToneParamControls& controls = m_toneParamControls[index];
    m_toneParamDefaults[index]  = value;

    controls.container->setVisible(true);
    controls.button->setText(label);

    controls.slider->blockSignals(true);
    controls.spinBox->blockSignals(true);

    controls.slider->setVisible(true);
    controls.slider->setMinimum(toneSliderFromValue(minimum));
    controls.slider->setMaximum(toneSliderFromValue(maximum));
    controls.slider->setSingleStep(toneSliderFromValue(step));
    controls.slider->setPageStep(toneSliderFromValue(step * 10.));
    controls.slider->setValue(toneSliderFromValue(value));

    controls.spinBox->setDecimals(2);
    controls.spinBox->setMinimum(minimum);
    controls.spinBox->setMaximum(maximum);
    controls.spinBox->setSingleStep(step);
    controls.spinBox->setValue(value);

    controls.slider->blockSignals(false);
    controls.spinBox->blockSignals(false);
}


void RGBFramebufferWidget::hideToneParams(int firstHiddenIndex)
{
    for (int i = firstHiddenIndex; i < s_toneParamCount; i++) {
        m_toneParamControls[i].container->setVisible(false);
        m_toneParamDefaults[i] = 0.;
    }
}


void RGBFramebufferWidget::setToneParamValue(int index, double value)
{
    QDoubleSpinBox* spinBox = toneParamSpinBox(index);

    if (!spinBox) return;

    QSlider* slider = m_toneParamControls[index].slider;
    slider->blockSignals(true);
    spinBox->blockSignals(true);

    slider->setValue(toneSliderFromValue(value));
    spinBox->setValue(value);

    slider->blockSignals(false);
    spinBox->blockSignals(false);

    if (
      currentToneMappingMethod() == RGBFramebufferModel::Tone_Clamp
      && index < 2) {
        syncToneClampRangeFromSpinBoxes();
        return;
    }

    syncToneParamsToModel();
}


void RGBFramebufferWidget::setToneClampRange(double min, double max)
{
    if (min > max) std::swap(min, max);

    const QSignalBlocker blocker_sbToneParam0(ui->sbToneParam0);
    const QSignalBlocker blocker_sbToneParam1(ui->sbToneParam1);
    const QSignalBlocker blocker_slToneParam0(ui->slToneParam0);
    const QSignalBlocker blocker_slToneParam1(ui->slToneParam1);
    const QSignalBlocker blocker_toneClampRangeSlider(ui->toneClampRangeSlider);

    ui->sbToneParam0->setMinimum(0.);
    ui->sbToneParam0->setMaximum(max);
    ui->sbToneParam1->setMinimum(min);
    ui->sbToneParam1->setMaximum(64.);
    ui->sbToneParam0->setValue(min);
    ui->sbToneParam1->setValue(max);
    ui->slToneParam0->setValue(toneSliderFromValue(min));
    ui->slToneParam1->setValue(toneSliderFromValue(max));
    ui->toneClampRangeSlider->setRange(min, max);


    syncToneParamsToModel();
}


void RGBFramebufferWidget::syncToneClampRangeFromSpinBoxes()
{
    setToneClampRange(ui->sbToneParam0->value(), ui->sbToneParam1->value());
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


void RGBFramebufferWidget::setFalseColorAutoRange(bool autoRange)
{
    m_falseColorAutoRange = autoRange;
    if (m_model) m_model->setFalseColorAutomatic(autoRange);
    const QSignalBlocker blocker_falseColorAutoButton(ui->falseColorAutoButton);
    ui->falseColorAutoButton->setChecked(autoRange);
}


void RGBFramebufferWidget::updateFalseColorRangeBounds()
{
    updateFalseColorRangeBounds(
      ui->sbFalseColorMinValue->value(),
      ui->sbFalseColorMaxValue->value());
}


void RGBFramebufferWidget::updateFalseColorRangeBounds(double min, double max)
{
    double boundMin = 0.;
    double boundMax = 1.;

    if (m_model && m_model->hasFiniteLuminanceSamples()) {
        boundMin = m_model->getLuminanceMin();
        boundMax = m_model->getLuminanceMax();
    }

    boundMin = std::min(boundMin, min);
    boundMax = std::max(boundMax, max);

    ui->falseColorRangeSlider->setBounds(boundMin, boundMax);
    ui->falseColorRangeSlider->setRange(min, max);
}


void RGBFramebufferWidget::setFalseColorRange(
  double min, double max, bool manual)
{
    if (min > max) std::swap(min, max);
    if (manual && m_falseColorAutoRange) setFalseColorAutoRange(false);

    updateFalseColorRangeBounds(min, max);

    const QSignalBlocker blocker_sbFalseColorMinValue(ui->sbFalseColorMinValue);
    const QSignalBlocker blocker_sbFalseColorMaxValue(ui->sbFalseColorMaxValue);
    const QSignalBlocker blocker_falseColorRangeSlider(
      ui->falseColorRangeSlider);

    ui->sbFalseColorMinValue->setRange(-ScientificDoubleSpinBox::sampleLimit(), max);
    ui->sbFalseColorMaxValue->setRange(min, ScientificDoubleSpinBox::sampleLimit());
    ui->sbFalseColorMinValue->setValue(min);
    ui->sbFalseColorMaxValue->setValue(max);
    ui->falseColorRangeSlider->setRange(min, max);


    syncFalseColorRangeToModel();
}


void RGBFramebufferWidget::syncFalseColorRangeToModel()
{
    ui->falseColorScaleWidget->setMin(ui->sbFalseColorMinValue->value());
    ui->falseColorScaleWidget->setMax(ui->sbFalseColorMaxValue->value());

    if (m_model) {
        m_model->setFalseColorRange(
          ui->sbFalseColorMinValue->value(),
          ui->sbFalseColorMaxValue->value());
    }
}


void RGBFramebufferWidget::updateToneMappingControls()
{
    ui->toneClampRangeWidget->setVisible(false);

    switch (currentToneMappingMethod()) {
        case RGBFramebufferModel::Tone_ACES:
            configureToneParam(0, tr("Shoulder"), 0.10, 4.00, 0.01, 1.00);
            configureToneParam(1, tr("Toe"), 0.10, 4.00, 0.01, 1.00);
            hideToneParams(2);
            break;

        case RGBFramebufferModel::Tone_Filmic:
            configureToneParam(
              0,
              tr("Shoulder Strength"),
              0.00,
              1.00,
              0.01,
              0.15);
            configureToneParam(
              1,
              tr("Linear Strength"),
              0.00,
              1.00,
              0.01,
              0.50);
            configureToneParam(2, tr("Linear Angle"), 0.00, 1.00, 0.01, 0.10);
            configureToneParam(3, tr("Toe Strength"), 0.00, 1.00, 0.01, 0.20);
            break;

        case RGBFramebufferModel::Tone_Log:
            configureToneParam(0, tr("Range"), 1.00, 64.00, 0.10, 16.00);
            configureToneParam(1, tr("Compression"), 0.10, 8.00, 0.01, 1.00);
            hideToneParams(2);
            break;

        case RGBFramebufferModel::Tone_Clamp:
            configureToneParam(0, tr("Min"), 0.00, 64.00, 0.01, 0.00);
            configureToneParam(1, tr("Max"), 0.00, 64.00, 0.01, 1.00);
            hideToneParams(2);
            ui->slToneParam0->setVisible(false);
            ui->slToneParam1->setVisible(false);
            ui->toneClampRangeWidget->setVisible(true);
            ui->toneClampRangeSlider->setBounds(0., 64.);
            setToneClampRange(0., 1.);
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
            const QPoint position = ui->fileInfoButton->mapToGlobal(QPoint(
              ui->fileInfoButton->width() + 8,
              ui->fileInfoButton->height() / 2));
            emit         fileInfoHoverRequested(this, position);
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
        QKeyEvent* keyEvent  = static_cast<QKeyEvent*>(event);
        const bool commitKey = keyEvent->key() == Qt::Key_Return
                               || keyEvent->key() == Qt::Key_Enter;

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

    const QSignalBlocker blocker_slExposure(ui->slExposure);
    ui->slExposure->setValue(sliderValue);

    setExposure(value);
}


void RGBFramebufferWidget::on_slExposure_valueChanged(int value)
{
    const double exposure = double(value) / 10.;

    const QSignalBlocker blocker_sbExposure(ui->sbExposure);
    ui->sbExposure->setValue(exposure);

    setExposure(exposure);
}


void RGBFramebufferWidget::on_exposureButton_clicked()
{
    ui->sbExposure->setValue(0.);
}

void RGBFramebufferWidget::resetCurrentMode()
{
    if (!m_model || !m_model->isImageLoaded()) return;
    switch (m_previewMode) {
        case RGBFramebufferModel::Preview_Exposure:
            ui->sbExposure->setValue(0.);
            break;
        case RGBFramebufferModel::Preview_ToneMapping:
            ui->cbToneMappingMethod->setCurrentIndex(0);
            updateToneMappingControls();
            break;
        case RGBFramebufferModel::Preview_FalseColor:
            ui->cbFalseColorColormap->setCurrentIndex(ColormapModule::TURBO);
            m_savedFalseColorMin = 0.;
            m_savedFalseColorMax = 1.;
            setFalseColorAutoRange(false);
            setFalseColorRange(0., 1., false);
            ui->cbFalseColorScale->setChecked(true);
            break;
    }
}

QString RGBFramebufferWidget::currentParameterText() const
{
    if (m_previewMode == RGBFramebufferModel::Preview_ToneMapping)
        return tr("%1 | %2 %3")
          .arg(ui->cbToneMappingMethod->currentText(), ui->toneParamButton0->text())
          .arg(ui->sbToneParam0->value(), 0, 'g', 4);
    if (m_previewMode == RGBFramebufferModel::Preview_FalseColor)
        return tr("%1 | %2 - %3").arg(ui->cbFalseColorColormap->currentText())
          .arg(ui->sbFalseColorMinValue->value(), 0, 'g', 4)
          .arg(ui->sbFalseColorMaxValue->value(), 0, 'g', 4);
    return tr("EV %1").arg(ui->sbExposure->value(), 0, 'f', 2);
}


void RGBFramebufferWidget::on_cbToneMappingMethod_currentIndexChanged(int)
{
    if (m_model) m_model->setToneMappingMethod(currentToneMappingMethod());

    updateToneMappingControls();
}


void RGBFramebufferWidget::on_cbFalseColorColormap_currentIndexChanged(
  int index)
{
    ColormapModule::Map cmap = (ColormapModule::Map)index;

    ui->falseColorScaleWidget->setColormap(cmap);

    if (m_model) m_model->setFalseColorColormap(cmap);
}


void RGBFramebufferWidget::on_sbFalseColorMinValue_valueChanged(double value)
{
    setFalseColorRange(value, ui->sbFalseColorMaxValue->value(), true);
}


void RGBFramebufferWidget::on_sbFalseColorMaxValue_valueChanged(double value)
{
    setFalseColorRange(ui->sbFalseColorMinValue->value(), value, true);
}


void RGBFramebufferWidget::on_falseColorRangeSlider_rangeChanged(
  double min, double max)
{
    setFalseColorRange(min, max, true);
}


void RGBFramebufferWidget::on_falseColorAutoButton_clicked()
{
    if (m_falseColorAutoRange) {
        setFalseColorAutoRange(false);
        setFalseColorRange(m_savedFalseColorMin, m_savedFalseColorMax, false);
        return;
    }

    if (!m_model || !m_model->hasFiniteLuminanceSamples()) {
        setFalseColorAutoRange(false);
        return;
    }

    m_savedFalseColorMin = ui->sbFalseColorMinValue->value();
    m_savedFalseColorMax = ui->sbFalseColorMaxValue->value();

    setFalseColorAutoRange(true);
    setFalseColorRange(
      m_model->getLuminanceMin(),
      m_model->getLuminanceMax(),
      false);
}


void RGBFramebufferWidget::on_cbFalseColorScale_stateChanged(int state)
{
    ui->falseColorScaleWidget->setVisible(
      state == Qt::Checked
      && m_previewMode == RGBFramebufferModel::Preview_FalseColor);
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


void RGBFramebufferWidget::on_toneClampRangeSlider_rangeChanged(
  double min, double max)
{
    setToneClampRange(min, max);
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


void RGBFramebufferWidget::onControlWheel(double steps)
{
    if (!m_model || !m_model->isImageLoaded() || steps == 0.) return;


    QDoubleSpinBox* spinBox
      = m_previewMode == RGBFramebufferModel::Preview_ToneMapping
          ? ui->sbToneParam0
        : m_previewMode == RGBFramebufferModel::Preview_FalseColor
          ? ui->sbFalseColorMaxValue
          : ui->sbExposure;

    spinBox->setValue(spinBox->value() + steps * spinBox->singleStep());
}


void RGBFramebufferWidget::updateZoomLevelText(double zoom)
{
    m_zoomLevel = zoom;
    ui->zoomButton->setText(tr("Zoom %1%").arg(qRound(zoom * 100.)));
}


void RGBFramebufferWidget::updateFramebufferSummary()
{
    m_cropIndicator->setModel(m_model);
    ui->framebufferSummaryLabel->setText(framebufferSummaryText(m_model));
    const bool loaded = m_model && m_model->isImageLoaded();
    ui->anomalyMarkerButton->setEnabled(loaded);
    ui->falseColorAutoButton->setEnabled(loaded && m_model->hasFiniteLuminanceSamples());
}


void RGBFramebufferWidget::on_zoomButton_clicked()
{
    if (qRound(m_zoomLevel * 100.) == 100) {
        ui->graphicsView->autoscale();
        return;
    }

    ui->graphicsView->setZoomLevel(1.);
}

PreviewState RGBFramebufferWidget::previewState() const
{
    PreviewState state;
    if (m_model) { state.depth = m_model->depthRange(); state.projection = m_model->projectionState(); }
    state.mode       = m_previewMode;
    state.toneMethod = ui->cbToneMappingMethod->currentIndex();
    state.exposure   = ui->sbExposure->value();
    for (int i = 0; i < 4; ++i)
        state.toneParameters[i] = toneParamSpinBox(i)->value();
    state.colormap     = ui->cbFalseColorColormap->currentIndex();
    state.minimum      = ui->sbFalseColorMinValue->value();
    state.maximum      = ui->sbFalseColorMaxValue->value();
    state.savedMinimum = m_savedFalseColorMin;
    state.savedMaximum = m_savedFalseColorMax;
    state.automatic    = m_falseColorAutoRange;
    state.scaleVisible = ui->cbFalseColorScale->isChecked();
    state.highlightNonFinite = ui->anomalyMarkerButton->isChecked();
    return state;
}

void RGBFramebufferWidget::restorePreviewState(const PreviewState& state)
{
    m_model->setDepthRange(state.depth);
    m_model->setProjectionState(state.projection);
    ui->anomalyMarkerButton->setChecked(state.highlightNonFinite);
    setPreviewMode(static_cast<RGBFramebufferModel::PreviewMode>(state.mode));
    ui->sbExposure->setValue(state.exposure);
    ui->cbToneMappingMethod->setCurrentIndex(state.toneMethod);
    if (currentToneMappingMethod() == RGBFramebufferModel::Tone_Clamp)
        setToneClampRange(state.toneParameters[0], state.toneParameters[1]);
    else
        for (int i = 0; i < 2; ++i)
            setToneParamValue(i, state.toneParameters[i]);
    for (int i = 2; i < 4; ++i)
        setToneParamValue(i, state.toneParameters[i]);
    ui->cbFalseColorColormap->setCurrentIndex(state.colormap);
    ui->cbFalseColorScale->setChecked(state.scaleVisible);
    m_savedFalseColorMin = state.savedMinimum;
    m_savedFalseColorMax = state.savedMaximum;
    setFalseColorAutoRange(state.automatic && (m_model->hasDeepSamples() || m_model->hasFiniteLuminanceSamples()));
    if (m_falseColorAutoRange)
        setFalseColorRange(
          m_model->getLuminanceMin(),
          m_model->getLuminanceMax(),
          false);
    else
        setFalseColorRange(state.minimum, state.maximum, false);
}
