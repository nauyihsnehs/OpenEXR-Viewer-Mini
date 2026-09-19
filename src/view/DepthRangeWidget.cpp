#include "DepthRangeWidget.h"
#include "RangeSliderWidget.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

DepthRangeWidget::DepthRangeWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("depthRangeControl");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 2, 8, 2);
    row->addWidget(new QLabel(tr("Depth"), this));
    m_minimum = new QLabel(this);
    m_maximum = new QLabel(this);
    m_slider = new RangeSliderWidget(this);
    m_slider->setObjectName("depthRangeSlider");
    m_slider->setAccessibleName(tr("Depth Range"));
    m_slider->installEventFilter(this);
    row->addWidget(m_minimum);
    row->addWidget(m_slider, 1);
    row->addWidget(m_maximum);
    setToolTip(tr("Keep samples with Z_min ≤ Z ≤ Z_max. Double-click the bar to restore all finite depths."));
    connect(m_slider, &RangeSliderWidget::rangeChanged, this, [this](double low, double high) {
        if (!m_model) return;
        DepthRange range;
        range.minimum = low; range.maximum = high; range.full = false;
        m_model->setDepthRange(range);
    });
    hide();
}

void DepthRangeWidget::setModel(FramebufferModel* model)
{
    if (m_model == model) { sync(); return; }
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (model) {
        connect(model, &FramebufferModel::imageLoaded, this, &DepthRangeWidget::sync);
        connect(model, &FramebufferModel::imageChanged, this, &DepthRangeWidget::sync);
        connect(model, &FramebufferModel::depthRangeChanged, this, &DepthRangeWidget::sync);
        connect(model, &QObject::destroyed, this, [this] { m_model = nullptr; sync(); });
    }
    sync();
}

void DepthRangeWidget::sync()
{
    const bool deep = m_model && m_model->hasDeepSamples();
    setVisible(deep);
    if (!deep) return;
    const auto bounds = m_model->depthBounds();
    const auto range = m_model->depthRange();
    const QSignalBlocker blocker(m_slider);
    m_slider->setBounds(bounds.minimum, bounds.maximum);
    m_slider->setRange(range.minimum, range.maximum);
    m_slider->setEnabled(bounds.finite && bounds.minimum < bounds.maximum);
    m_minimum->setText(tr("Z_min %1").arg(bounds.finite ? QString::number(range.minimum, 'g', 9) : tr("n/a")));
    m_maximum->setText(tr("Z_max %1").arg(bounds.finite ? QString::number(range.maximum, 'g', 9) : tr("n/a")));
}

bool DepthRangeWidget::eventFilter(QObject* object, QEvent* event)
{
    if (object == m_slider && event->type() == QEvent::MouseButtonDblClick && m_model) {
        m_model->setDepthRange(DepthRange());
        return true;
    }
    return QWidget::eventFilter(object, event);
}
