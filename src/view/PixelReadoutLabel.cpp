#include "PixelReadoutLabel.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QResizeEvent>

PixelReadoutLabel::PixelReadoutLabel(QWidget* parent) : QLabel(parent)
{
    setTextFormat(Qt::PlainText);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setMinimumWidth(0);
    setAccessibleName(tr("Pixel Values"));
}

void PixelReadoutLabel::setModel(const FramebufferModel* model)
{
    if (m_model == model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    clearSample();
    if (!model) return;
    // Connect before the view so a newly committed frame can supply a fresh sample.
    connect(model, &FramebufferModel::imageChanged, this, &PixelReadoutLabel::clearSample);
    connect(model, &FramebufferModel::imageLoaded, this, &PixelReadoutLabel::clearSample);
    connect(model, &FramebufferModel::readinessChanged, this, [this] {
        if (!m_model || !m_model->isPreviewReady()) clearSample();
    });
    connect(model, &QObject::destroyed, this, &PixelReadoutLabel::clearSample);
}

void PixelReadoutLabel::queryPixel(int x, int y)
{
    if (!m_model || !m_model->isImageLoaded() || !m_model->isPreviewReady()) {
        clearSample();
        return;
    }
    // Leaving the image retains the last valid sample for its tooltip.
    if (x < 0 || y < 0 || !isVisible()) return;
    m_readout = QString::fromStdString(m_model->getColorInfo(x, y, true));
    const QString detail = QString::fromStdString(m_model->getColorInfo(x, y));
    setToolTip(detail.isEmpty() ? QString() : tr("Last sampled pixel\n%1").arg(detail));
    setAccessibleDescription(detail);
    updateText();
}

void PixelReadoutLabel::clearSample()
{
    m_readout.clear();
    setToolTip(QString());
    setAccessibleDescription(QString());
    clear();
}

void PixelReadoutLabel::updateText()
{
    setText(fontMetrics().elidedText(m_readout, Qt::ElideRight, contentsRect().width()));
}

void PixelReadoutLabel::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    updateText();
}

void PixelReadoutLabel::hideEvent(QHideEvent* event)
{
    clearSample();
    QLabel::hideEvent(event);
}

void PixelReadoutLabel::changeEvent(QEvent* event)
{
    QLabel::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        updateText();
}
