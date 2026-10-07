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
#include "ScalarMappingControls.h"
#include <QSignalBlocker>
#include "ui_YFramebufferWidget.h"

#include "ComboBoxBehavior.h"
#include "WorkspaceWidgets.h"
#include "ViewerIcons.h"
#include "FramebufferInfo.h"

#include <QPoint>
#include <QStyle>
#include <QtGlobal>

#include <util/Colormap.h>

YFramebufferWidget::YFramebufferWidget(QWidget* parent)
  : QWidget(parent)
  , ui(new Ui::YFramebufferWidget)
  , m_model(nullptr)
  , m_zoomLevel(1.)
{
    ui->setupUi(this);
    m_mappingControls = new ScalarMappingControls(
      ColormapModule::GRAYSCALE, ui->scaleWidget, this);
    ui->scalarMappingLayout->addWidget(m_mappingControls);
    ui->horizontalLayout->insertWidget(ui->horizontalLayout->indexOf(ui->zoomButton) + 1,
      m_mappingControls->colorScaleButton());
    connect(m_mappingControls, &ScalarMappingControls::colormapChanged,
      this, [this](ColormapModule::Map map) {
          if (m_model) m_model->setColormap(map);
      });
    connect(m_mappingControls, &ScalarMappingControls::rangeChanged,
      this, [this](double low, double high) {
          if (m_model) m_model->setRange(low, high);
      });
    connect(m_mappingControls, &ScalarMappingControls::automaticChanged,
      this, [this](bool enabled) {
          if (m_model) m_model->setAutomaticRange(enabled);
      });
    updateZoomLevelText(m_zoomLevel);
    m_cropIndicator = new CropIndicator(this);
    ui->horizontalLayout_3->insertWidget(1, m_cropIndicator, 0, Qt::AlignVCenter);
    m_nonFiniteIndicator = new NonFiniteIndicator(this);
    ui->horizontalLayout_3->insertWidget(3, m_nonFiniteIndicator, 0, Qt::AlignVCenter);
    ui->horizontalLayout->addWidget(new ProjectionControls(this));
    ui->horizontalLayout->insertWidget(0, new ResolutionLevelWidget(this));
    wrapPreviewControls(ui->verticalLayout);
    ui->graphicsView->watchOutsideZoom(this, ui->horizontalLayout_3);
    ui->verticalLayout->insertWidget(1, new DepthRangeWidget(this));
    connect(ui->graphicsView, &GraphicsView::minimalViewRequested,
            this, &YFramebufferWidget::minimalViewRequested);
    connect(ui->graphicsView, &GraphicsView::resetParametersRequested,
            this, &YFramebufferWidget::resetCurrentMode);
    connect(ui->graphicsView, &GraphicsView::controlWheel,
            this, &YFramebufferWidget::onControlWheel);
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
    m_nonFiniteIndicator->setModel(nullptr);
    m_model = nullptr;
    restorePreviewState(state);
    bindModel(model, false);
}

void YFramebufferWidget::bindModel(YFramebufferModel* model, bool initialize)
{
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    const bool markers = m_nonFiniteIndicator->isChecked();
    m_nonFiniteIndicator->setModel(nullptr);
    m_model = model;
    m_mappingControls->setSourceName(QString::fromStdString(model->getLayerName()));
    if (initialize) {
        m_model->setHighlightNonFinite(markers);
        syncScalarMappingToModel();
    }
    m_nonFiniteIndicator->setModel(model);
    ui->selectInfoLabel->setModel(model);
    findChild<DepthRangeWidget*>()->setModel(model);
    findChild<ProjectionControls*>()->setModel(model);
    if (m_model && m_model->parent() != this) m_model->setParent(this);
    ui->graphicsView->setModel(model);
    connect(model, &FramebufferModel::imageChanged, this, [this] {
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
    if (initialize) {
        updateFramebufferSummary();
    } else {
        const QSignalBlocker blocker(m_mappingControls);
        updateFramebufferSummary();
    }
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


void YFramebufferWidget::onOpenFileOnDropEvent(const QString& filename)
{
    emit openFileOnDropEvent(filename);
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
    ui->framebufferSummaryLabel->setSummary(m_model);
    ui->framebufferSummaryLabel->setToolTip(framebufferSummaryToolTip(m_model));
    const bool loaded = m_model && m_model->isImageLoaded();
    const bool finite = loaded && m_model->hasFiniteDisplay();
    m_mappingControls->setFiniteRange(finite,
      finite ? m_model->displayMinimum() : 0.,
      finite ? m_model->displayMaximum() : 1.);
}


void YFramebufferWidget::on_zoomButton_clicked()
{
    if (qRound(m_zoomLevel * 100.) == 100) {
        ui->graphicsView->autoscale();
        return;
    }

    ui->graphicsView->setZoomLevel(1.);
}

PreviewState YFramebufferWidget::previewState() const
{
    PreviewState state;
    if (m_model) { state.depth = m_model->depthRange(); state.projection = m_model->projectionState(); }
    m_mappingControls->saveState(state);
    state.highlightNonFinite = m_model ? m_model->highlightNonFinite()
                                      : m_nonFiniteIndicator->isChecked();
    return state;
}
void YFramebufferWidget::restorePreviewState(const PreviewState& state)
{
    if (m_model) {
        m_model->setDepthRange(state.depth);
        m_model->setProjectionState(state.projection);
    }
    if (m_model) m_model->setHighlightNonFinite(state.highlightNonFinite);
    else m_nonFiniteIndicator->setChecked(state.highlightNonFinite);
    m_mappingControls->restoreState(state);
    if (m_model) syncScalarMappingToModel();
}

void YFramebufferWidget::resetCurrentMode()
{
    if (!m_model || !m_model->isImageLoaded()) return;
    m_mappingControls->reset();
}

QString YFramebufferWidget::currentParameterText() const
{
    return m_mappingControls->parameterText();
}


void YFramebufferWidget::syncScalarMappingToModel()
{
    m_model->setColormap(m_mappingControls->colormap());
    m_model->setRange(m_mappingControls->minimum(), m_mappingControls->maximum());
    m_model->setAutomaticRange(m_mappingControls->automatic());
}

void YFramebufferWidget::onControlWheel(double steps)
{
    if (!m_model || !m_model->isImageLoaded()) return;
    m_mappingControls->adjustMaximum(steps);
}
