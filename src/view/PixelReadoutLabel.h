#pragma once

#include <QLabel>
#include <QPointer>
#include <model/framebuffer/FramebufferModel.h>

// Keeps the last sample available for inspection without imposing a minimum width.
class PixelReadoutLabel : public QLabel
{
    Q_OBJECT

  public:
    explicit PixelReadoutLabel(QWidget* parent = nullptr);
    void setModel(const FramebufferModel* model);
    void queryPixel(int x, int y);
    void clearSample();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;

  private:
    QPointer<const FramebufferModel> m_model;
    FramebufferModel::PixelReadout m_sample;
};
