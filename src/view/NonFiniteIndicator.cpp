#include "NonFiniteIndicator.h"

#include <QPainter>
#include <QPalette>
#include <QStringList>

NonFiniteIndicator::NonFiniteIndicator(QWidget* parent) : QWidget(parent)
{
    setObjectName("nonFiniteIndicator");
    setFixedSize(18, 18);
    setFocusPolicy(Qt::NoFocus);
    setAccessibleName(tr("NaN/Inf Status"));
    refresh();
}

void NonFiniteIndicator::setModel(const FramebufferModel* model)
{
    if (m_model == model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (model) {
        connect(model, &FramebufferModel::imageLoaded, this, &NonFiniteIndicator::refresh);
        connect(model, &FramebufferModel::imageChanged, this, &NonFiniteIndicator::refresh);
        connect(model, &FramebufferModel::readinessChanged, this, &NonFiniteIndicator::refresh);
        connect(model, &QObject::destroyed, this, [this] {
            m_model.clear();
            refresh();
        });
    }
    refresh();
}

void NonFiniteIndicator::refresh()
{
    m_state = Unknown;
    QStringList lines;
    // These are source statistics, so they remain valid while a preview renders.
    if (!m_model || !m_model->isImageLoaded() || m_model->isLoading()) {
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
    const QString detail = lines.join("\n");
    setToolTip(detail);
    setAccessibleDescription(detail);
    update();
}

void NonFiniteIndicator::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
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
