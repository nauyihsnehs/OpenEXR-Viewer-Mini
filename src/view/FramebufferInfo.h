#pragma once

#include <QString>
#include <QLabel>
#include <QEvent>
#include <QFontMetricsF>
#include <QPalette>
#include <QPainter>
#include <QtMath>

#include <model/framebuffer/FramebufferModel.h>
#include <model/framebuffer/PixelDiagnostics.h>

inline QString
framebufferDatasetValueText(const FramebufferModel* model, bool minValue)
{
    if (!model || !model->hasFiniteSamples()) return "n/a";

    const double value
      = minValue ? model->getDatasetMin() : model->getDatasetMax();

    return QString::number(value, 'g', 6);
}


inline QString framebufferSizeText(const FramebufferModel* model)
{
    if (!model || !model->isImageLoaded()) return "n/a";

    return QString("%1 x %2").arg(model->width()).arg(model->height());
}


inline QString framebufferSummaryText(const FramebufferModel* model, bool compact = true)
{
    if (model) {
        if (!model->errorString().isEmpty())
            return compact ? "Error" : "Error: " + model->errorString();
        if (model->isLoading()) return "Loading...";
        if (model->isImageLoaded() && !model->isPreviewReady())
            return "Rendering...";
    }
    if (compact) {
        if (!model || !model->isImageLoaded()) return QString();
        const QSize canvas = model->isProjected() ? model->projectionCanvasSize()
                                                 : model->previewDisplayWindow().size();
        const QString maximum = model->hasFiniteSamples()
          ? QString::fromStdString(PixelDiagnostics::compactSampleText(model->getDatasetMax()))
          : QString::fromUtf8("—");
        return QString("%1×%2  Max %3").arg(canvas.width()).arg(canvas.height()).arg(maximum);
    }
    const QString level = model && model->resolutionLevelCount() > 1
      ? QString("Level %1 | ").arg(QString::fromStdString(model->resolutionLevel().toString())) : QString();
    QString environment;
    if (model && model->rawEnvmap() >= 0) {
        const QSize canvas = model->isProjected() ? model->projectionCanvasSize() : model->previewDisplayWindow().size();
        environment = QString("Source %1 | Display %2 %3×%4 | ")
          .arg(EnvironmentProjection::name(EnvironmentProjection::Type(model->rawEnvmap())))
          .arg(EnvironmentProjection::name(model->projectionState().type))
          .arg(canvas.width()).arg(canvas.height());
        if (!model->environmentSource().available()) environment += model->environmentSource().unavailableReason + " | ";
    }
    QString maximumLabel = "   Max ";
    if (model && model->isDerivedPreview())
        maximumLabel = "   Two-eye source max ";
    else if (model && model->hasDeepSamples())
        maximumLabel = "   Deep source max ";
    return environment + level + "Source size " + framebufferSizeText(model)
           + maximumLabel + framebufferDatasetValueText(model, false);
}

inline QString framebufferSummaryToolTip(const FramebufferModel* model)
{
    QString detail = framebufferSummaryText(model, false);
    if (model && model->isImageLoaded()) {
        detail += "\nMax: maximum finite source channel sample, before display color transforms.";
        detail += "\nClamp uses display-linear RGB instead, excluding alpha; color conversion and Deep composition can change its maximum.";
        if (model->hasDeepSamples())
            detail += "\nDeep statistics include all stored samples, before Depth Range filtering.";
        if (model->isDerivedPreview())
            detail += "\nStereo statistics combine both eyes.";
    }
    return detail;
}

inline bool framebufferSummaryMetricsChanged(QEvent::Type type)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (type == QEvent::DevicePixelRatioChange) return true;
#endif
    return type == QEvent::FontChange || type == QEvent::ApplicationFontChange
           || type == QEvent::StyleChange || type == QEvent::ContentsRectChange
           || type == QEvent::ScreenChangeInternal;
}

// Separate painted regions keep resolution, source Max and HDR status readable
// without requiring a minimum footer width. Full information stays in the tooltip.
class FramebufferSummaryLabel : public QLabel
{
  public:
    explicit FramebufferSummaryLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    }
    void setSummary(const FramebufferModel* model, const QString& status = QString())
    {
        QString dimensions = framebufferSummaryText(model);
        QString maximum;
        if (model && model->isImageLoaded() && model->isPreviewReady() && model->errorString().isEmpty()) {
            const QSize canvas = model->isProjected() ? model->projectionCanvasSize() : model->previewDisplayWindow().size();
            dimensions = QString("%1×%2").arg(canvas.width()).arg(canvas.height());
            maximum = tr("Max %1").arg(model->hasFiniteSamples()
              ? QString::fromStdString(PixelDiagnostics::compactSampleText(model->getDatasetMax())) : QString::fromUtf8("—"));
        }
        const bool changed = m_dimensions != dimensions || m_maximum != maximum || m_status != status;
        m_dimensions = dimensions; m_maximum = maximum; m_status = status;
        setToolTip(framebufferSummaryToolTip(model));
        setAccessibleDescription(m_dimensions + " " + m_maximum + " " + m_status);
        if (changed) { updateGeometry(); update(); }
    }
    QSize sizeHint() const override
    {
        const QFontMetricsF metrics(font(), this);
        int width = 0;
        for (const auto& text : {m_dimensions, m_maximum, m_status}) {
            if (text.isEmpty()) continue;
            if (width) width += s_regionSpacing;
            width += regionWidth(text, metrics);
        }
        const QMargins margins = contentsMargins();
        return QSize(width + margins.left() + margins.right(),
                     qCeil(metrics.height()) + margins.top() + margins.bottom());
    }
    QSize minimumSizeHint() const override
    {
        return QSize(0, sizeHint().height());
    }
  protected:
    bool event(QEvent* event) override
    {
        const bool handled = QLabel::event(event);
        if (framebufferSummaryMetricsChanged(event->type())) {
            updateGeometry();
            update();
        }
        return handled;
    }
    void paintEvent(QPaintEvent* event) override
    {
        QLabel::paintEvent(event);
        QPainter painter(this);
        painter.setClipRect(contentsRect());
        painter.setPen(palette().color(QPalette::WindowText));
        painter.setFont(font());
        const QFontMetricsF metrics(font(), this);
        int left = contentsRect().left();
        for (const auto& text : {m_dimensions, m_maximum, m_status}) {
            if (text.isEmpty()) continue;
            if (left != contentsRect().left()) left += s_regionSpacing;
            const int available = qMax(0, contentsRect().right() + 1 - left);
            const int naturalWidth = regionWidth(text, metrics);
            const int width = qMin(available, naturalWidth);
            const int textWidth = qMax(0, width - 2 * s_textPadding);
            if (textWidth == 0) break;
            // Integer metrics can round down below the elision engine's width.
            // Only elide when the layout actually gave this region less space.
            const QString displayed = width >= naturalWidth ? text
              : metrics.elidedText(text, Qt::ElideRight, textWidth);
            painter.drawText(QRectF(left + s_textPadding, contentsRect().top(),
                                    textWidth, contentsRect().height()),
              Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, displayed);
            left += width;
        }
    }
  private:
    static constexpr int s_regionSpacing = 12;
    static constexpr int s_textPadding = 1;
    static int regionWidth(const QString& text, const QFontMetricsF& metrics)
    {
        return qCeil(metrics.horizontalAdvance(text)) + 2 * s_textPadding;
    }
    QString m_dimensions, m_maximum, m_status;
};
