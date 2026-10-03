#include "NonFiniteIndicator.h"

#include <QPalette>
#include <QSignalBlocker>
#include <QStringList>
#include <QStyleOptionToolButton>
#include <QStylePainter>

NonFiniteIndicator::NonFiniteIndicator(QWidget* parent) : QToolButton(parent)
{
    setObjectName("nonFiniteIndicator");
    setFixedSize(24, 24);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setAutoRaise(true);
    setCheckable(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Mark NaN/Inf"));
    connect(this, &QToolButton::toggled, this, [this](bool enabled) {
        if (m_model) m_model->setHighlightNonFinite(enabled);
        refresh();
    });
    refresh();
}

void NonFiniteIndicator::setModel(FramebufferModel* model)
{
    if (m_model == model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (model) {
        connect(model, &FramebufferModel::imageLoaded, this, &NonFiniteIndicator::refresh);
        connect(model, &FramebufferModel::imageChanged, this, &NonFiniteIndicator::refresh);
        connect(model, &FramebufferModel::readinessChanged, this, &NonFiniteIndicator::refresh);
        connect(model, &FramebufferModel::anomalyMarkersChanged, this, &NonFiniteIndicator::refresh);
        connect(model, &QObject::destroyed, this, [this] {
            m_model.clear();
            refresh();
        });
    }
    refresh();
}

void NonFiniteIndicator::refresh()
{
    // With no model, retain pending preview settings until the next binding.
    if (m_model) {
        const QSignalBlocker blocker(this);
        setChecked(m_model->highlightNonFinite());
    }
    const bool available = m_model && m_model->isImageLoaded() && !m_model->isLoading();
    setEnabled(available);
    m_state = Unknown;
    QStringList lines;
    // These are source statistics, so they remain valid while a preview renders.
    if (!available) {
        lines << tr("NaN/Inf: source statistics unavailable.");
    } else {
        const auto nan = m_model->getDatasetNaNCount();
        const auto positive = m_model->getDatasetPositiveInfCount();
        const auto negative = m_model->getDatasetNegativeInfCount();
        m_state = nan || positive || negative ? NonFinite : Finite;
        lines << (m_state == NonFinite ? tr("NaN/Inf detected") : tr("No NaN/Inf"));
        lines << tr("NaN: %1").arg(QString::number(nan));
        lines << tr("+Inf: %1").arg(QString::number(positive));
        lines << tr("-Inf: %1").arg(QString::number(negative));
        lines << tr("Counts are source channel samples, before display color transforms.");
        if (m_model->hasDeepSamples())
            lines << tr("Deep: all stored samples, before Depth Range filtering.");
        if (m_model->isDerivedPreview())
            lines << tr("Stereo: both eyes combined.");
    }
    lines << (isChecked() ? tr("Markers: on.") : tr("Markers: off."));
    lines << tr("Click or press Space to toggle position markers.");
    if (!available) lines << tr("Available when source statistics are loaded.");
    const QString explanation = tr(
      "Locate isolated anomalies with circles and connected areas with outlines. "
      "Markers stay visible when zoomed out. Outlines may include normal pixels; "
      "use the pixel readout for exact values.");
    const QString priority = tr(
      "Priority: NaN, +Inf, -Inf. Finite negative and HDR values are not marked.");
    const QString detail = lines.join("\n");
    setToolTip("<b>" + tr("Mark NaN/Inf").toHtmlEscaped() + "</b><br/>"
      + detail.toHtmlEscaped().replace("\n", "<br/>")
      + "<br/><br/>" + explanation.toHtmlEscaped() + "<br/>"
      + tr("<span style=\"color:#ff00ff\">&#9632;</span> NaN: magenta<br/>"
           "<span style=\"color:#00ffff\">&#9632;</span> +Inf: cyan<br/>"
           "<span style=\"color:#ffff00\">&#9632;</span> -Inf: yellow<br/>")
      + priority.toHtmlEscaped());
    setAccessibleDescription(detail + "\n" + explanation + "\n"
      + tr("Magenta: NaN; cyan: positive infinity; yellow: negative infinity.")
      + "\n" + priority);
    update();
}

void NonFiniteIndicator::paintEvent(QPaintEvent*)
{
    QStyleOptionToolButton option;
    initStyleOption(&option);
    option.text.clear();
    option.icon = QIcon();
    QStylePainter painter(this);
    painter.drawComplexControl(QStyle::CC_ToolButton, option);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate((width() - 18.) / 2., (height() - 18.) / 2.);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor gray = dark ? QColor(116, 123, 133) : QColor(139, 145, 154);
    const QColor color = m_state == NonFinite ? QColor(220, 62, 62) : gray;
    painter.setPen(QPen(color, 1.3));
    painter.setBrush(Qt::NoBrush);
    if (m_state != Unknown) painter.setBrush(color);
    painter.drawEllipse(QRectF(3, 3, 12, 12));
    if (m_state == NonFinite) {
        QPen mark(Qt::white, 1.5);
        mark.setCapStyle(Qt::RoundCap);
        painter.setPen(mark);
        painter.drawLine(QPointF(9, 6), QPointF(9, 9.5));
        painter.drawPoint(QPointF(9, 12));
    }
}
