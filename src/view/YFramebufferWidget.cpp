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

#include "YFramebufferWidget.h"
#include "CropIndicator.h"
#include "NonFiniteIndicator.h"
#include "DepthRangeWidget.h"
#include "ProjectionControls.h"
#include "ResolutionLevelWidget.h"
#include "ScientificDoubleSpinBox.h"
#include <QToolButton>
#include <QSignalBlocker>
#include "ui_YFramebufferWidget.h"

#include "ComboBoxBehavior.h"
#include "WorkspaceWidgets.h"
#include "ViewerIcons.h"
#include "FramebufferInfo.h"

#include <QPoint>
#include <QStyle>
#include <QtGlobal>

#include <util/ColormapModule.h>

YFramebufferWidget::YFramebufferWidget(QWidget* parent)
  : QWidget(parent)
  , ui(new Ui::YFramebufferWidget)
  , m_model(nullptr)
  , m_zoomLevel(1.)
{
    ui->setupUi(this);
    ViewerIcons::setupButton(ui->buttonAuto, ViewerIcons::AutoRange,
      tr("Automatic Range"), tr("Use the full finite value range\nAdjust either bound to return to a manual range."));
    ViewerIcons::setupButton(ui->cbScale, ViewerIcons::ColorScale,
      tr("Color Scale"), tr("Show or hide the color scale"));
    ui->cbColormap->setAccessibleName(tr("Colormap"));
    ui->cbColormap->setToolTip(tr("Colormap"));
    updateZoomLevelText(m_zoomLevel);
    m_cropIndicator = new CropIndicator(this);
    ui->horizontalLayout_3->insertWidget(1, m_cropIndicator, 0, Qt::AlignVCenter);
    m_nonFiniteIndicator = new NonFiniteIndicator(this);
    ui->horizontalLayout_3->insertWidget(3, m_nonFiniteIndicator, 0, Qt::AlignVCenter);
    setRange(0., 1.);
    connect(ui->anomalyMarkerButton, &QToolButton::toggled, this, [this](bool enabled) {
        if (m_model) m_model->setHighlightNonFinite(enabled);
    });
    ui->horizontalLayout->addWidget(new ProjectionControls(this));
    ui->horizontalLayout->insertWidget(0, new ResolutionLevelWidget(this));
    wrapPreviewControls(ui->verticalLayout);
    ui->graphicsView->watchOutsideZoom(this, ui->horizontalLayout_3);
    ui->verticalLayout->insertWidget(1, new DepthRangeWidget(this));
    connect(ui->graphicsView, &GraphicsView::minimalViewRequested,
            this, &YFramebufferWidget::minimalViewRequested);
    connect(ui->graphicsView, &GraphicsView::resetParametersRequested,
            this, &YFramebufferWidget::resetCurrentMode);
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
        ui->graphicsView, SIGNAL(queryPixelInfo(int, int)),
        this,             SLOT(onQueryPixelInfo(int, int)));

    connect(
        ui->graphicsView, SIGNAL(zoomLevelChanged(double)),
        this,             SLOT(updateZoomLevelText(double)));
    // clang-format on

    for (int i = 0; i < ColormapModule::N_MAPS; i++) {
        ui->cbColormap->addItem(
          QString::fromStdString(
            ColormapModule::toString((ColormapModule::Map)i)));
    }
    applyComboBoxBehavior(this);
}


YFramebufferWidget::~YFramebufferWidget()
{
    delete ui;
}


void YFramebufferWidget::setModel(YFramebufferModel* model)
{
    bindModel(model, true);
}

void YFramebufferWidget::adoptPreparedModel(YFramebufferModel* model, const PreviewState& state)
{
    Q_ASSERT(model && model->isPreviewReady());
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    // Apply the prepared controls without writing parameters back into either model.
    m_model = nullptr;
    restorePreviewState(state);
    bindModel(model, false);
}

void YFramebufferWidget::bindModel(YFramebufferModel* model, bool initialize)
{
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    m_nonFiniteIndicator->setModel(model);
    ui->selectInfoLabel->setModel(model);
    findChild<DepthRangeWidget*>()->setModel(model);
    findChild<ProjectionControls*>()->setModel(model);
    if (initialize) m_model->setHighlightNonFinite(ui->anomalyMarkerButton->isChecked());
    if (m_model && m_model->parent() != this) m_model->setParent(this);
    ui->graphicsView->setModel(model);
    connect(model, &FramebufferModel::imageChanged, this, [this] {
        if (m_autoRange && m_model->hasFiniteDisplay())
            setRange(m_model->displayMinimum(), m_model->displayMaximum());
        updateFramebufferSummary();
    });
    connect(
      model,
      &FramebufferModel::readinessChanged,
      this,
      &YFramebufferWidget::updateFramebufferSummary);
    connect(
      m_model,
      SIGNAL(imageLoaded()),
      this,
      SLOT(updateFramebufferSummary()));
    updateFramebufferSummary();
}


bool YFramebufferWidget::eventFilter(QObject* watched, QEvent* event)
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

    return QWidget::eventFilter(watched, event);
}


void YFramebufferWidget::onQueryPixelInfo(int x, int y)
{
    ui->selectInfoLabel->queryPixel(x, y);
}


void YFramebufferWidget::on_sbMinValue_valueChanged(double arg1)
{
    m_autoRange = false;
    if (m_model) m_model->setAutomaticRange(false);
    ui->sbMaxValue->setMinimum(arg1);
    ui->scaleWidget->setMin(arg1);
    if (m_model) m_model->setMinValue(arg1);
}


void YFramebufferWidget::on_sbMaxValue_valueChanged(double arg1)
{
    m_autoRange = false;
    if (m_model) m_model->setAutomaticRange(false);
    ui->sbMinValue->setMaximum(arg1);
    ui->scaleWidget->setMax(arg1);
    if (m_model) m_model->setMaxValue(arg1);
}


void YFramebufferWidget::on_buttonAuto_clicked()
{
    if (m_model && m_model->hasFiniteDisplay()) {
        m_autoRange = true;
        m_model->setAutomaticRange(true);
        setRange(m_model->displayMinimum(), m_model->displayMaximum());
    }
}


void YFramebufferWidget::onOpenFileOnDropEvent(const QString& filename)
{
    emit openFileOnDropEvent(filename);
}


void YFramebufferWidget::on_cbColormap_currentIndexChanged(int index)
{
    ColormapModule::Map cmap = (ColormapModule::Map)index;

    ui->scaleWidget->setColormap(cmap);

    if (m_model) m_model->setColormap(cmap);
}


void YFramebufferWidget::on_cbScale_toggled(bool checked)
{
    ui->scaleWidget->setVisible(checked);
}


void YFramebufferWidget::updateZoomLevelText(double zoom)
{
    m_zoomLevel = zoom;
    ui->zoomButton->setText(tr("%1%").arg(qRound(zoom * 100.)));
    ui->zoomButton->setAccessibleName(tr("Image Zoom"));
    ui->zoomButton->setToolTip(qRound(zoom * 100.) == 100
      ? tr("Click to fit image to window") : tr("Click to restore 100% zoom"));
}


void YFramebufferWidget::updateFramebufferSummary()
{
    m_cropIndicator->setModel(m_model);
    ui->framebufferSummaryLabel->setText(framebufferSummaryText(m_model));
    ui->framebufferSummaryLabel->setToolTip(framebufferSummaryToolTip(m_model));
    const bool loaded = m_model && m_model->isImageLoaded();
    ui->anomalyMarkerButton->setEnabled(loaded);
    ui->buttonAuto->setEnabled(loaded && m_model->hasFiniteDisplay());
}


void YFramebufferWidget::on_zoomButton_clicked()
{
    if (qRound(m_zoomLevel * 100.) == 100) {
        ui->graphicsView->autoscale();
        return;
    }

    ui->graphicsView->setZoomLevel(1.);
}

void YFramebufferWidget::setRange(double min, double max)
{
    const QSignalBlocker low(ui->sbMinValue), high(ui->sbMaxValue);
    ui->sbMinValue->setRange(-ScientificDoubleSpinBox::sampleLimit(), max);
    ui->sbMaxValue->setRange(min, ScientificDoubleSpinBox::sampleLimit());
    ui->sbMinValue->setValue(min);
    ui->sbMaxValue->setValue(max);
    ui->scaleWidget->setMin(ui->sbMinValue->value());
    ui->scaleWidget->setMax(ui->sbMaxValue->value());
    if (m_model)
        m_model->setRange(ui->sbMinValue->value(), ui->sbMaxValue->value());
}
PreviewState YFramebufferWidget::previewState() const
{
    PreviewState state;
    if (m_model) { state.depth = m_model->depthRange(); state.projection = m_model->projectionState(); }
    state.colormap     = ui->cbColormap->currentIndex();
    state.minimum      = ui->sbMinValue->value();
    state.maximum      = ui->sbMaxValue->value();
    state.automatic    = m_autoRange;
    state.scaleVisible = ui->cbScale->isChecked();
    state.highlightNonFinite = ui->anomalyMarkerButton->isChecked();
    return state;
}
void YFramebufferWidget::restorePreviewState(const PreviewState& state)
{
    if (m_model) {
        m_model->setDepthRange(state.depth);
        m_model->setProjectionState(state.projection);
    }
    ui->anomalyMarkerButton->setChecked(state.highlightNonFinite);
    ui->cbColormap->setCurrentIndex(state.colormap);
    ui->cbScale->setChecked(state.scaleVisible);
    m_autoRange = state.automatic && (!m_model || m_model->hasDeepSamples() || m_model->hasFiniteDisplay());
    if (m_model) m_model->setAutomaticRange(m_autoRange);
    if (m_autoRange && m_model)
        setRange(m_model->displayMinimum(), m_model->displayMaximum());
    else
        setRange(state.minimum, state.maximum);
}

void YFramebufferWidget::resetCurrentMode()
{
    if (!m_model || !m_model->isImageLoaded()) return;
    m_autoRange = false;
    if (m_model) m_model->setAutomaticRange(false);
    ui->cbColormap->setCurrentIndex(ColormapModule::GRAYSCALE);
    setRange(0., 1.);
    ui->cbScale->setChecked(true);
}

QString YFramebufferWidget::currentParameterText() const
{
    return tr("%1 | %2 - %3").arg(ui->cbColormap->currentText())
      .arg(ui->sbMinValue->value(), 0, 'g', 4)
      .arg(ui->sbMaxValue->value(), 0, 'g', 4);
}
