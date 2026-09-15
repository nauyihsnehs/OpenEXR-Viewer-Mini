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
#include "ScientificDoubleSpinBox.h"
#include <QToolButton>
#include <QSignalBlocker>
#include "ui_YFramebufferWidget.h"

#include "ComboBoxBehavior.h"
#include "WorkspaceWidgets.h"
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
    setRange(0., 1.);
    connect(ui->anomalyMarkerButton, &QToolButton::toggled, this, [this](bool enabled) {
        if (m_model) m_model->setHighlightNonFinite(enabled);
    });
    wrapPreviewControls(ui->verticalLayout);
    connect(ui->graphicsView, &GraphicsView::minimalViewRequested,
            this, &YFramebufferWidget::minimalViewRequested);
    connect(ui->graphicsView, &GraphicsView::resetParametersRequested,
            this, &YFramebufferWidget::resetCurrentMode);
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
    m_model = model;
    if (m_model) m_model->setHighlightNonFinite(ui->anomalyMarkerButton->isChecked());
    if (m_model && m_model->parent() != this) m_model->setParent(this);
    ui->graphicsView->setModel(model);
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
    ui->selectInfoLabel->setText(
      QString::fromStdString(m_model->getColorInfo(x, y)));
}


void YFramebufferWidget::on_sbMinValue_valueChanged(double arg1)
{
    m_autoRange = false;
    ui->sbMaxValue->setMinimum(arg1);
    ui->scaleWidget->setMin(arg1);
    if (m_model) m_model->setMinValue(arg1);
}


void YFramebufferWidget::on_sbMaxValue_valueChanged(double arg1)
{
    m_autoRange = false;
    ui->sbMinValue->setMaximum(arg1);
    ui->scaleWidget->setMax(arg1);
    if (m_model) m_model->setMaxValue(arg1);
}


void YFramebufferWidget::on_buttonAuto_clicked()
{
    if (m_model && m_model->hasFiniteSamples()) {
        m_autoRange = true;
        setRange(m_model->getDatasetMin(), m_model->getDatasetMax());
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


void YFramebufferWidget::on_cbScale_stateChanged(int arg1)
{
    if (arg1 == Qt::Checked) {
        ui->scaleWidget->show();
    } else {
        ui->scaleWidget->hide();
    }
}


void YFramebufferWidget::updateZoomLevelText(double zoom)
{
    m_zoomLevel = zoom;
    ui->zoomButton->setText(tr("Zoom %1%").arg(qRound(zoom * 100.)));
}


void YFramebufferWidget::updateFramebufferSummary()
{
    ui->framebufferSummaryLabel->setText(framebufferSummaryText(m_model));
    const bool loaded = m_model && m_model->isImageLoaded();
    ui->anomalyMarkerButton->setEnabled(loaded);
    ui->buttonAuto->setEnabled(loaded && m_model->hasFiniteSamples());
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
    ui->anomalyMarkerButton->setChecked(state.highlightNonFinite);
    ui->cbColormap->setCurrentIndex(state.colormap);
    ui->cbScale->setChecked(state.scaleVisible);
    m_autoRange = state.automatic && m_model->hasFiniteSamples();
    if (m_autoRange)
        setRange(m_model->getDatasetMin(), m_model->getDatasetMax());
    else
        setRange(state.minimum, state.maximum);
}

void YFramebufferWidget::resetCurrentMode()
{
    if (!m_model || !m_model->isImageLoaded()) return;
    m_autoRange = false;
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
