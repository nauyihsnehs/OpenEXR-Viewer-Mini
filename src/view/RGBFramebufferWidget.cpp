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
#include "NonFiniteIndicator.h"
#include "DepthRangeWidget.h"
#include "ProjectionControls.h"
#include "ResolutionLevelWidget.h"
#include <QSignalBlocker>
#include "ui_RGBFramebufferWidget.h"

#include "FramebufferInfo.h"
#include "GraphicsView.h"
#include "ComboBoxBehavior.h"
#include "WorkspaceWidgets.h"
#include "ViewerIcons.h"
#include "ScientificDoubleSpinBox.h"
#include "ScalarMappingControls.h"

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
{
    ui->setupUi(this);
    m_mappingControls = new ScalarMappingControls(
      ColormapModule::TURBO, ui->scalarMappingScaleWidget, this);
    ui->scalarMappingLayout->addWidget(m_mappingControls);
    ui->horizontalLayout->insertWidget(ui->horizontalLayout->indexOf(ui->zoomButton) + 1,
      m_mappingControls->colorScaleButton());
    m_mappingControls->setSourceName(tr("Luminance"));
    connect(m_mappingControls, &ScalarMappingControls::colormapChanged,
      this, [this](ColormapModule::Map map) {
          if (m_model) m_model->setScalarMappingColormap(map);
      });
    connect(m_mappingControls, &ScalarMappingControls::rangeChanged,
      this, [this](double low, double high) {
          if (m_model) m_model->setScalarMappingRange(low, high);
      });
    connect(m_mappingControls, &ScalarMappingControls::automaticChanged,
      this, [this](bool enabled) {
          if (m_model) m_model->setScalarMappingAutomatic(enabled);
      });
    ui->exposureButton->setToolTip(tr("Exposure (EV)\nClick to reset to 0 EV."));
    ui->exposureButton->setAccessibleName(tr("Reset Exposure"));
    ui->sbExposure->setAccessibleName(tr("Exposure (EV)"));
    updateZoomLevelText(m_zoomLevel);
    m_cropIndicator = new CropIndicator(this);
    ui->horizontalLayout_2->insertWidget(1, m_cropIndicator, 0, Qt::AlignVCenter);
    m_nonFiniteIndicator = new NonFiniteIndicator(this);
    ui->horizontalLayout_2->insertWidget(3, m_nonFiniteIndicator, 0, Qt::AlignVCenter);
    connect(ui->graphicsView, &GraphicsView::hdrStatusChanged,
            this, &RGBFramebufferWidget::updateFramebufferSummary);
    ui->horizontalLayout->addWidget(new ProjectionControls(this));
    ui->horizontalLayout->insertWidget(0, new ResolutionLevelWidget(this));
    wrapPreviewControls(ui->verticalLayout);
    ui->toneMappingLayout->setSpacing(12);
    ui->toneParamsLayout->setHorizontalSpacing(12);
    ui->graphicsView->watchOutsideZoom(this, ui->horizontalLayout_2);
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
    ui->fileInfoButton->setAccessibleName(tr("Image Information"));
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
      tr("ACES"),
      RGBFramebufferModel::Tone_ACES);
    ui->cbToneMappingMethod->addItem(
      tr("Filmic/Hable"),
      RGBFramebufferModel::Tone_Filmic);
    ui->cbToneMappingMethod->addItem(tr("Log"), RGBFramebufferModel::Tone_Log);
    ui->cbToneMappingMethod->addItem(
      tr("Clamp"),
      RGBFramebufferModel::Tone_Clamp);
    ui->cbToneMappingMethod->setCurrentIndex(0);
    ui->cbToneMappingMethod->setItemData(1, tr("ACES fitted"), Qt::ToolTipRole);
    ui->cbToneMappingMethod->setAccessibleName(tr("Tone Mapping Method"));

    applyComboBoxBehavior(this);

    installCompactSpinBox(ui->sbExposure);
    ui->sbToneParam0->setToolbarCompact(true);
    ui->sbToneParam1->setToolbarCompact(true);
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
    bindModel(model, true);
}

void RGBFramebufferWidget::adoptPreparedModel(RGBFramebufferModel* model, const PreviewState& state)
{
    Q_ASSERT(model && model->isPreviewReady());
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    // Apply the prepared controls without writing parameters back into either model.
    m_nonFiniteIndicator->setModel(nullptr);
    m_model = nullptr;
    restorePreviewState(state);
    bindModel(model, false);
}

void RGBFramebufferWidget::bindModel(RGBFramebufferModel* model, bool initialize)
{
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    const bool markers = m_nonFiniteIndicator->isChecked();
    m_nonFiniteIndicator->setModel(nullptr);
    m_model = model;
    if (initialize) m_model->setHighlightNonFinite(markers);
    m_nonFiniteIndicator->setModel(model);
    ui->pixelValueLabel->setModel(model);
    findChild<DepthRangeWidget*>()->setModel(model);
    findChild<ProjectionControls*>()->setModel(model);
    if (m_model && m_model->parent() != this) m_model->setParent(this);
    if (initialize) {
        m_model->setExposure(ui->sbExposure->value());
        m_model->setPreviewMode(m_previewMode);
        m_model->setToneMappingMethod(currentToneMappingMethod());
        syncScalarMappingToModel();
        syncToneParamsToModel();
    }
    ui->graphicsView->setModel(m_model);
    connect(model, &FramebufferModel::imageChanged, this, [this] {
        updateFramebufferSummary();
    });
    connect(
      m_model,
      SIGNAL(imageLoaded()),
      this,
      SLOT(updateFramebufferSummary()));
    connect(
      model,
      &FramebufferModel::readinessChanged,
      this,
      &RGBFramebufferWidget::updateFramebufferSummary);
    if (initialize) {
        updateFramebufferSummary();
    } else {
        const QSignalBlocker blocker(m_mappingControls);
        updateFramebufferSummary();
    }
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
    const bool scalarMapping = mode == RGBFramebufferModel::Preview_ScalarMapping;

    m_previewMode = mode;

    ui->exposureButton->setVisible(!toneMapping && !scalarMapping);
    ui->slExposure->setVisible(!toneMapping && !scalarMapping);
    ui->sbExposure->setVisible(!toneMapping && !scalarMapping);
    ui->toneMappingControlsWidget->setVisible(toneMapping);
    m_mappingControls->setMappingVisible(scalarMapping);

    if (m_model) m_model->setPreviewMode(mode);
    updateFramebufferSummary();
}


void RGBFramebufferWidget::setSpinBoxCompact(
  QDoubleSpinBox* spinBox, bool compact)
{
    spinBox->setReadOnly(false);
    spinBox->setFrame(true);
    spinBox->setFixedSize(72, 28);
    spinBox->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
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


void RGBFramebufferWidget::configureToneParam(
  int            index,
  const QString& label,
  double         minimum,
  double         maximum,
  double         step,
  double         value,
  const QString& fullLabel)
{
    if (index < 0 || index >= s_toneParamCount) return;

    ToneParamControls& controls = m_toneParamControls[index];
    m_toneParamDefaults[index]  = value;

    controls.container->setVisible(true);
    const QString name = fullLabel.isEmpty() ? label : fullLabel;
    controls.button->setText(label);
    controls.button->setToolTip(tr("%1\nClick to reset to %2.").arg(name).arg(value));
    controls.button->setAccessibleName(tr("Reset %1").arg(name));
    controls.slider->setAccessibleName(name);
    controls.spinBox->setAccessibleName(name);
    controls.spinBox->setToolTip(name);

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

    if (currentToneMappingMethod() == RGBFramebufferModel::Tone_Clamp && index < 2) {
        setToneClampRange(index == 0 ? value : std::min(ui->sbToneParam0->value(), value),
                          index == 1 ? value : ui->sbToneParam1->value());
        return;
    }

    QSlider* slider = m_toneParamControls[index].slider;
    slider->blockSignals(true);
    spinBox->blockSignals(true);

    slider->setValue(toneSliderFromValue(value));
    spinBox->setValue(value);

    slider->blockSignals(false);
    spinBox->blockSignals(false);

    syncToneParamsToModel();
}


void RGBFramebufferWidget::setToneClampRange(double min, double max)
{
    if (!std::isfinite(min) || !std::isfinite(max)) return;
    if (min > max) std::swap(min, max);
    const bool loaded = m_model && m_model->isImageLoaded();
    // Preserve prepared/manual parameters until a model has published its
    // statistics. In particular, refresh restores controls before rebinding.
    const double upper = loaded ? m_model->toneClampUpperBound() : ScientificDoubleSpinBox::sampleLimit();
    max = std::max(0., std::min(max, upper));
    min = std::max(0., std::min(min, max));

    const QSignalBlocker blocker_sbToneParam0(ui->sbToneParam0);
    const QSignalBlocker blocker_sbToneParam1(ui->sbToneParam1);
    const QSignalBlocker blocker_toneClampRangeSlider(ui->toneClampRangeSlider);

    ui->sbToneParam0->setRange(0., max);
    ui->sbToneParam1->setRange(min, upper);
    ui->sbToneParam0->setValue(min);
    ui->sbToneParam1->setValue(max);
    ui->toneClampRangeSlider->setBounds(0., upper);
    ui->toneClampRangeSlider->setRange(min, max);
    ui->toneClampRangeSlider->setEnabled(loaded && upper > 0.);
    ui->sbToneParam0->setEnabled(loaded && upper > 0.);
    ui->sbToneParam1->setEnabled(loaded && upper > 0.);
    syncToneParamsToModel();
}

void RGBFramebufferWidget::updateToneClampBounds()
{
    if (currentToneMappingMethod() != RGBFramebufferModel::Tone_Clamp) return;
    if (!m_model || !m_model->isImageLoaded()) {
        m_toneParamDefaults[0] = 0.;
        m_toneParamDefaults[1] = 1.;
        ui->toneClampRangeSlider->setEnabled(false);
        ui->sbToneParam0->setEnabled(false);
        ui->sbToneParam1->setEnabled(false);
        return;
    }
    const double upper = m_model->toneClampUpperBound();
    m_toneParamDefaults[0] = 0.;
    m_toneParamDefaults[1] = std::min(1., upper);
    const double step = upper > 0. ? std::min(0.01, upper / 100.) : 0.01;
    ui->sbToneParam0->setSingleStep(step);
    ui->sbToneParam1->setSingleStep(step);
    const QString explanation = tr("Clamp white point: limited to the maximum finite display-linear RGB value (%1).\n"
                                   "Excludes alpha, NaN and Inf. Source Max can differ after color conversion or Deep composition.")
      .arg(QString::number(upper, 'g', 17));
    ui->sbToneParam0->setToolTip(explanation);
    ui->sbToneParam1->setToolTip(explanation);
    ui->toneParamButton1->setToolTip(tr("Max\nClick to reset to %1.").arg(QString::number(m_toneParamDefaults[1], 'g', 17)));
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


void RGBFramebufferWidget::updateToneMappingControls()
{
    const bool clamp = currentToneMappingMethod() == RGBFramebufferModel::Tone_Clamp;
    {
        const QSignalBlocker blocker0(ui->sbToneParam0);
        const QSignalBlocker blocker1(ui->sbToneParam1);
        ui->sbToneParam0->setScientificMode(clamp);
        ui->sbToneParam1->setScientificMode(clamp);
    }
    for (int i = 0; i < s_toneParamCount; ++i) m_toneParamControls[i].spinBox->setEnabled(true);
    setSpinBoxCompact(ui->sbToneParam0, ui->sbToneParam0->property("compactValue").toBool());
    setSpinBoxCompact(ui->sbToneParam1, ui->sbToneParam1->property("compactValue").toBool());
    const QString detail = ui->cbToneMappingMethod->currentData(Qt::ToolTipRole).toString();
    ui->cbToneMappingMethod->setToolTip(detail.isEmpty()
      ? ui->cbToneMappingMethod->currentText() : detail);
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
              tr("Shoulder"),
              0.00,
              1.00,
              0.01,
              0.15,
              tr("Shoulder Strength"));
            configureToneParam(
              1,
              tr("Linear"),
              0.00,
              1.00,
              0.01,
              0.50,
              tr("Linear Strength"));
            configureToneParam(2, tr("Angle"), 0.00, 1.00, 0.01, 0.10, tr("Linear Angle"));
            configureToneParam(3, tr("Toe"), 0.00, 1.00, 0.01, 0.20, tr("Toe Strength"));
            break;

        case RGBFramebufferModel::Tone_Log:
            configureToneParam(0, tr("Range"), 1.00, 64.00, 0.10, 16.00);
            configureToneParam(1, tr("Compress"), 0.10, 8.00, 0.01, 1.00, tr("Compression"));
            hideToneParams(2);
            break;

        case RGBFramebufferModel::Tone_Clamp:
            for (int i = 0; i < 2; ++i) {
                auto& controls = m_toneParamControls[i];
                const QString name = i == 0 ? tr("Min") : tr("Max");
                controls.container->setVisible(true);
                controls.button->setText(name);
                controls.button->setAccessibleName(tr("Reset %1").arg(name));
                controls.button->setToolTip(tr("%1\nClick to reset.").arg(name));
                controls.spinBox->setAccessibleName(name);
                const double upper = m_model && m_model->isImageLoaded() ? m_model->toneClampUpperBound() : 0.;
                controls.spinBox->setSingleStep(upper > 0. ? std::min(0.01, upper / 100.) : 0.01);
            }
            hideToneParams(2);
            ui->slToneParam0->setVisible(false);
            ui->slToneParam1->setVisible(false);
            ui->toneClampRangeWidget->setVisible(true);
            setToneClampRange(0., 1.);
            updateToneClampBounds();
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

    if (watched == spinBox && event->type() == QEvent::FontChange)
        setSpinBoxCompact(spinBox, spinBox->property("compactValue").toBool());

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
    ui->pixelValueLabel->queryPixel(x, y);
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
        case RGBFramebufferModel::Preview_HDR:
            ui->sbExposure->setValue(0.);
            break;
        case RGBFramebufferModel::Preview_ToneMapping:
            ui->cbToneMappingMethod->setCurrentIndex(0);
            updateToneMappingControls();
            break;
        case RGBFramebufferModel::Preview_ScalarMapping:
            m_mappingControls->reset();
            break;
    }
}

QString RGBFramebufferWidget::currentParameterText() const
{
    if (m_previewMode == RGBFramebufferModel::Preview_ToneMapping)
        return tr("%1 | %2 %3")
          .arg(ui->cbToneMappingMethod->currentText(), ui->toneParamButton0->text())
          .arg(ui->sbToneParam0->value(), 0, 'g', 4);
    if (m_previewMode == RGBFramebufferModel::Preview_ScalarMapping)
        return m_mappingControls->parameterText();
    return tr("EV %1").arg(ui->sbExposure->value(), 0, 'f', 2);
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
    if (m_previewMode == RGBFramebufferModel::Preview_ScalarMapping) {
        m_mappingControls->adjustMaximum(steps);
        return;
    }
    QDoubleSpinBox* spinBox = m_previewMode == RGBFramebufferModel::Preview_ToneMapping
      ? ui->sbToneParam0 : ui->sbExposure;
    spinBox->setValue(spinBox->value() + steps * spinBox->singleStep());
}


void RGBFramebufferWidget::updateZoomLevelText(double zoom)
{
    m_zoomLevel = zoom;
    ui->zoomButton->setText(tr("%1%").arg(qRound(zoom * 100.)));
    ui->zoomButton->setAccessibleName(tr("Image Zoom"));
    ui->zoomButton->setToolTip(qRound(zoom * 100.) == 100
      ? tr("Click to fit image to window") : tr("Click to restore 100% zoom"));
}


void RGBFramebufferWidget::updateFramebufferSummary()
{
    updateToneClampBounds();
    m_cropIndicator->setModel(m_model);
    QString status;
    QString detail = framebufferSummaryToolTip(m_model);
    if (m_previewMode == RGBFramebufferModel::Preview_HDR) {
        status = ui->graphicsView->hdrStatusText();
        if (status.isEmpty()) status = tr("HDR — preparing preview");
        detail += "\n" + ui->graphicsView->hdrStatusDetail();
    }
    ui->framebufferSummaryLabel->setSummary(m_model, status);
    ui->framebufferSummaryLabel->setToolTip(detail);
    const bool loaded = m_model && m_model->isImageLoaded();
    const bool finite = loaded && m_model->hasFiniteLuminanceSamples();
    m_mappingControls->setFiniteRange(finite,
      finite ? m_model->getLuminanceMin() : 0.,
      finite ? m_model->getLuminanceMax() : 1.);
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
    m_mappingControls->saveState(state);
    state.highlightNonFinite = m_model ? m_model->highlightNonFinite()
                                      : m_nonFiniteIndicator->isChecked();
    return state;
}

void RGBFramebufferWidget::restorePreviewState(const PreviewState& state)
{
    if (m_model) {
        m_model->setDepthRange(state.depth);
        m_model->setProjectionState(state.projection);
    }
    if (m_model) m_model->setHighlightNonFinite(state.highlightNonFinite);
    else m_nonFiniteIndicator->setChecked(state.highlightNonFinite);
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
    m_mappingControls->restoreState(state);
    if (m_model) syncScalarMappingToModel();
}


void RGBFramebufferWidget::syncScalarMappingToModel()
{
    m_model->setScalarMappingColormap(m_mappingControls->colormap());
    m_model->setScalarMappingRange(m_mappingControls->minimum(), m_mappingControls->maximum());
    m_model->setScalarMappingAutomatic(m_mappingControls->automatic());
}
