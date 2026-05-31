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

#pragma once

#include <QPoint>
#include <QWidget>
#include <model/framebuffer/RGBFramebufferModel.h>

class QEvent;
class QDoubleSpinBox;

namespace Ui
{
    class RGBFramebufferWidget;
}

class RGBFramebufferWidget: public QWidget
{
    Q_OBJECT

  public:
    explicit RGBFramebufferWidget(QWidget* parent = nullptr);
    ~RGBFramebufferWidget();

    void setModel(RGBFramebufferModel* model);
    const FramebufferModel* framebufferModel() const { return m_model; }
    void setPreviewMode(RGBFramebufferModel::PreviewMode mode);

  signals:
    void openFileOnDropEvent(const QString& filename);
    void fileInfoHoverRequested(QWidget* widget, const QPoint& position);
    void fileInfoHoverLeft();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private slots:
    void onQueryPixelInfo(int x, int y);

    void on_sbExposure_valueChanged(double arg1);

    void on_slExposure_valueChanged(int value);
    void on_exposureButton_clicked();
    void on_cbToneMappingMethod_currentIndexChanged(int index);
    void on_sbToneParam0_valueChanged(double value);
    void on_slToneParam0_valueChanged(int value);
    void on_toneParamButton0_clicked();
    void on_sbToneParam1_valueChanged(double value);
    void on_slToneParam1_valueChanged(int value);
    void on_toneParamButton1_clicked();
    void on_sbToneParam2_valueChanged(double value);
    void on_slToneParam2_valueChanged(int value);
    void on_toneParamButton2_clicked();
    void on_sbToneParam3_valueChanged(double value);
    void on_slToneParam3_valueChanged(int value);
    void on_toneParamButton3_clicked();

    void onOpenFileOnDropEvent(const QString& filename);
    void onControlWheel(int delta);
    void updateZoomLevelText(double zoom);
    void updateFramebufferSummary();
    void on_zoomButton_clicked();

  private:
    void setExposure(double value);
    void setSpinBoxCompact(QDoubleSpinBox* spinBox, bool compact);
    void installCompactSpinBox(QDoubleSpinBox* spinBox);
    QDoubleSpinBox* compactSpinBox(QObject* watched) const;
    void updateToneMappingControls();
    void configureToneParam(
      int index,
      const QString& label,
      double minimum,
      double maximum,
      double step,
      double value);
    void hideToneParams(int firstHiddenIndex);
    void setToneParamValue(int index, double value);
    void resetToneParam(int index);
    void syncToneParamsToModel();
    QDoubleSpinBox* toneParamSpinBox(int index) const;
    RGBFramebufferModel::ToneMappingMethod currentToneMappingMethod() const;

    Ui::RGBFramebufferWidget* ui;
    RGBFramebufferModel*      m_model;
    RGBFramebufferModel::PreviewMode m_previewMode;
    double                    m_toneParamDefaults[4];
    double                    m_zoomLevel;
};
