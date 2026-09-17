#pragma once
#include <QWidget>
#include <QPointer>
#include <model/framebuffer/FramebufferModel.h>

class QLabel;
class RangeSliderWidget;

class DepthRangeWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DepthRangeWidget(QWidget* parent = nullptr);
    void setModel(FramebufferModel* model);
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    void sync();
    QPointer<FramebufferModel> m_model;
    RangeSliderWidget* m_slider;
    QLabel* m_minimum;
    QLabel* m_maximum;
};
