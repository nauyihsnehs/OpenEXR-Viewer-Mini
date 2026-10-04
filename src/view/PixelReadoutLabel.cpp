#include "PixelReadoutLabel.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QPalette>
#include <cmath>
#include <model/framebuffer/PixelDiagnostics.h>

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
    m_sample = m_model->pixelReadout(x, y);
    const QString detail = QString::fromStdString(m_model->getColorInfo(x, y));
    setToolTip(detail.isEmpty() ? QString() : tr("Last sampled pixel\n%1").arg(detail));
    setAccessibleDescription(detail);
    update();
}

void PixelReadoutLabel::clearSample()
{
    m_sample = FramebufferModel::PixelReadout();
    setToolTip(QString());
    setAccessibleDescription(QString());
    clear();
    update();
}

void PixelReadoutLabel::paintEvent(QPaintEvent* event)
{
    QLabel::paintEvent(event);
    if (!m_sample.valid) return;
    QPainter painter(this);
    painter.setClipRect(contentsRect());
    const QFont regular = font();
    QFont bold = regular;
    bold.setBold(true);
    const QFontMetrics normalMetrics(regular), boldMetrics(bold);
    const auto textWidth = [&](const QString& text) {
        return qMax(normalMetrics.horizontalAdvance(text), boldMetrics.horizontalAdvance(text));
    };
    int digitWidth = 0;
    for (int digit = 0; digit < 10; ++digit) digitWidth = qMax(digitWidth, textWidth(QString::number(digit)));
    const int valueWidth = qMax(9 * digitWidth + textWidth(QStringLiteral("-.")), textWidth(QStringLiteral("-9.99e-99"))) + 2;
    const int gap = 12;
    const QRect source = m_model ? m_model->getDataWindow() : QRect();
    const auto coordinateWidth = [&](int low, int high) {
        int width = qMax(textWidth(QString::number(low)), textWidth(QString::number(high)));
        if (m_sample.interpolated) width = qMax(width, textWidth(QStringLiteral("-9.9999e+09")));
        return width + 2;
    };
    const int xWidth = coordinateWidth(source.left(), source.right());
    const int yWidth = coordinateWidth(source.top(), source.bottom());
    const QString opening = m_sample.interpolated ? QString::fromUtf8("≈(") : QStringLiteral("(");
    const int openingWidth = textWidth(opening), commaWidth = textWidth(QStringLiteral(", "));
    const int closingWidth = textWidth(QStringLiteral(")"));
    int total = openingWidth + xWidth + commaWidth + yWidth + closingWidth;
    for (const auto& group : m_sample.groups) {
        if (!group.label.isEmpty()) total += gap + textWidth(group.label);
        for (const auto& value : group.values) {
            const bool rgb = value.role == FramebufferModel::PixelValue::Red
              || value.role == FramebufferModel::PixelValue::Green || value.role == FramebufferModel::PixelValue::Blue;
            total += gap + valueWidth + (rgb ? 0 : textWidth(value.name + " "));
        }
    }
    int left = contentsRect().right() + 1 - total;
    const QColor neutral = palette().color(QPalette::WindowText);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const auto draw = [&](const QString& text, int width, const QColor& color, bool emphasized) {
        painter.save();
        painter.setFont(emphasized ? bold : regular);
        painter.setPen(color);
        const QRect cell(left, contentsRect().top(), width, contentsRect().height());
        painter.setClipRect(cell, Qt::IntersectClip);
        painter.drawText(cell,
                         Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, text);
        painter.restore();
        left += width;
    };
    draw(opening, openingWidth, neutral, false);
    draw(QString::number(m_sample.position.x(), m_sample.interpolated ? 'g' : 'f', m_sample.interpolated ? 5 : 0),
         xWidth, neutral, false);
    draw(QStringLiteral(", "), commaWidth, neutral, false);
    draw(QString::number(m_sample.position.y(), m_sample.interpolated ? 'g' : 'f', m_sample.interpolated ? 5 : 0),
         yWidth, neutral, false);
    draw(QStringLiteral(")"), closingWidth, neutral, false);
    for (const auto& group : m_sample.groups) {
        if (!group.label.isEmpty()) {
            left += gap;
            draw(group.label, textWidth(group.label), neutral, false);
        }
        for (const auto& value : group.values) {
            using Value = FramebufferModel::PixelValue;
            left += gap;
            QColor color = neutral;
            if (value.role == Value::Red) color = QColor(dark ? "#f28b82" : "#b83232");
            else if (value.role == Value::Green) color = QColor(dark ? "#81c995" : "#24733b");
            else if (value.role == Value::Blue) color = QColor(dark ? "#8ab4f8" : "#285ab8");
            else draw(value.name + " ", textWidth(value.name + " "), neutral, false);
            const QString text = value.available
              ? QString::fromStdString(PixelDiagnostics::compactSampleText(value.value)) : QString::fromUtf8("—");
            draw(text, valueWidth, color, value.available && std::isfinite(value.value) && value.value > 1.);
        }
    }
}

void PixelReadoutLabel::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    update();
}

void PixelReadoutLabel::hideEvent(QHideEvent* event)
{
    clearSample();
    QLabel::hideEvent(event);
}

void PixelReadoutLabel::changeEvent(QEvent* event)
{
    QLabel::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        update();
}
