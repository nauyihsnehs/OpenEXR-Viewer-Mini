#pragma once

#include <QWidget>

class FramebufferModel;

class CropIndicator : public QWidget
{
    Q_OBJECT
  public:
    explicit CropIndicator(QWidget* parent = nullptr);
    void setModel(const FramebufferModel* model);

  protected:
    void paintEvent(QPaintEvent*) override;
};
