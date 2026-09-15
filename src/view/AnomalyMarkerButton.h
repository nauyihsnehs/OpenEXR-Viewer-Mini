#pragma once

#include <QToolButton>

// Shared vector icon and behavior for both preview toolbars.
class AnomalyMarkerButton: public QToolButton
{
  public:
    explicit AnomalyMarkerButton(QWidget* parent = nullptr);

  protected:
    void paintEvent(QPaintEvent* event) override;
};
