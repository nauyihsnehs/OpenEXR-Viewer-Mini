#include "AnomalyMarkerButton.h"

#include <QCoreApplication>
#include <QStyleOptionToolButton>
#include <QStylePainter>

AnomalyMarkerButton::AnomalyMarkerButton(QWidget* parent): QToolButton(parent)
{
    setFixedSize(28, 28);
    setIconSize(QSize(16, 16));
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setCheckable(true);
    setChecked(false);
    setEnabled(false);
    setFocusPolicy(Qt::StrongFocus);
    const auto label = QCoreApplication::translate("AnomalyMarkerButton", "Mark NaN/Inf");
    setAccessibleName(label);
    setToolTip(QCoreApplication::translate("AnomalyMarkerButton",
      "<b>Mark NaN/Inf</b><br/>"
      "Locate isolated anomalies with circles and connected areas with outlines. "
      "Markers stay visible when zoomed out. Outlines may include normal pixels; "
      "use the pixel readout for exact values.<br/>"
      "<span style=\"color:#ff00ff\">&#9632;</span> NaN: magenta<br/>"
      "<span style=\"color:#00ffff\">&#9632;</span> +Inf: cyan<br/>"
      "<span style=\"color:#ffff00\">&#9632;</span> -Inf: yellow<br/>"
      "Priority: NaN, +Inf, -Inf. Finite negative and HDR values are not marked."));
    setAccessibleDescription(QCoreApplication::translate("AnomalyMarkerButton",
      "Toggle NaN and infinity position markers. Magenta: NaN; cyan: positive infinity; "
      "yellow: negative infinity."));
}

void AnomalyMarkerButton::paintEvent(QPaintEvent*)
{
    QStyleOptionToolButton option;
    initStyleOption(&option);
    option.text.clear();
    option.icon = QIcon();
    QStylePainter painter(this);
    painter.drawComplexControl(QStyle::CC_ToolButton, option);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate((width() - 16.) / 2., (height() - 16.) / 2.);
    const QColor foreground = option.palette.color(
      isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
    QPen pen(foreground, 1.3);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(3.5, 3.5, 9., 9.));
    painter.drawLine(QPointF(8., 0.7), QPointF(8., 3.));
    painter.drawLine(QPointF(8., 13.), QPointF(8., 15.3));
    painter.drawLine(QPointF(0.7, 8.), QPointF(3., 8.));
    painter.drawLine(QPointF(13., 8.), QPointF(15.3, 8.));
    painter.setPen(Qt::NoPen);
    painter.setBrush(foreground);
    painter.drawEllipse(QPointF(8., 8.), 1.3, 1.3);
}
