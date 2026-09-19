#include "ViewerIcons.h"

#include <QAbstractButton>
#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPolygonF>
#include <QToolButton>
#include <QVariant>

namespace {
class IconEngine : public QIconEngine
{
  public:
    explicit IconEngine(ViewerIcons::Kind kind) : m_kind(kind) {}
    QIconEngine* clone() const override { return new IconEngine(m_kind); }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
               QIcon::State) override
    {
        // Read the current palette on every draw, including after a theme change.
        const QPalette palette = QApplication::palette();
        const QColor color = palette.color(mode == QIcon::Disabled
          ? QPalette::Disabled : QPalette::Active, QPalette::ButtonText);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const qreal edge = qMin(rect.width(), rect.height());
        painter->translate(rect.x() + (rect.width() - edge) / 2.,
                           rect.y() + (rect.height() - edge) / 2.);
        painter->scale(edge / 20., edge / 20.);
        QPen pen(color, 1.5);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        QPainterPath path;
        using namespace ViewerIcons;
        switch (m_kind) {
            case Open:
                path.moveTo(2, 16); path.lineTo(2, 4); path.lineTo(8, 4);
                path.lineTo(10, 6); path.lineTo(17, 6); path.lineTo(17, 8);
                painter->drawPath(path);
                painter->drawPolygon(QPolygonF() << QPointF(2, 16) << QPointF(5, 9)
                  << QPointF(18, 9) << QPointF(15, 16));
                break;
            case Export:
                path.moveTo(8, 4); path.lineTo(3, 4); path.lineTo(3, 17);
                path.lineTo(16, 17); path.lineTo(16, 12);
                painter->drawPath(path);
                painter->drawLine(QPointF(9, 11), QPointF(17, 3));
                painter->drawLine(QPointF(11, 3), QPointF(17, 3));
                painter->drawLine(QPointF(17, 3), QPointF(17, 9));
                break;
            case Exposure:
                painter->drawEllipse(QRectF(6, 6, 8, 8));
                painter->translate(10, 10);
                for (int i = 0; i < 8; ++i) {
                    painter->drawLine(QPointF(0, -7), QPointF(0, -9));
                    painter->rotate(45);
                }
                break;
            case ToneMapping:
                path.moveTo(3, 3); path.lineTo(3, 17); path.lineTo(17, 17);
                painter->drawPath(path);
                path = QPainterPath(QPointF(5, 14));
                path.cubicTo(12, 14, 8, 5, 17, 5);
                painter->drawPath(path);
                break;
            case FalseColor: {
                const QColor colors[] = {QColor(79, 117, 211), QColor(58, 178, 159),
                                         QColor(225, 185, 65), QColor(213, 92, 78)};
                for (int i = 0; i < 4; ++i) {
                    const QColor band = mode == QIcon::Disabled ? color : colors[i];
                    painter->fillRect(QRectF(2 + i * 4.2, 4, 3, 12), band);
                }
                break;
            }
            case Layers:
                painter->drawPolygon(QPolygonF() << QPointF(2, 6) << QPointF(10, 2)
                  << QPointF(18, 6) << QPointF(10, 10));
                for (int y : {10, 14})
                    painter->drawPolyline(QPolygonF() << QPointF(2, y)
                      << QPointF(10, y + 4) << QPointF(18, y));
                break;
            case Attributes:
                painter->drawRoundedRect(QRectF(2, 2, 16, 16), 2, 2);
                for (int y : {6, 10, 14}) {
                    painter->drawPoint(QPointF(5, y));
                    painter->drawLine(QPointF(8, y), QPointF(15, y));
                }
                break;
            case AutoRange:
                path.moveTo(5, 3); path.lineTo(2, 3); path.lineTo(2, 17); path.lineTo(5, 17);
                path.moveTo(15, 3); path.lineTo(18, 3); path.lineTo(18, 17); path.lineTo(15, 17);
                painter->drawPath(path);
                painter->drawPolygon(QPolygonF() << QPointF(10, 5) << QPointF(11.5, 8.5)
                  << QPointF(15, 10) << QPointF(11.5, 11.5) << QPointF(10, 15)
                  << QPointF(8.5, 11.5) << QPointF(5, 10) << QPointF(8.5, 8.5));
                break;
            case ColorScale:
                painter->drawRect(QRectF(4, 2, 7, 16));
                for (int i = 0; i < 4; ++i) {
                    QColor band = color;
                    band.setAlphaF(0.2 + i * 0.2);
                    painter->fillRect(QRectF(4.75, 2.75 + i * 3.65, 5.5, 3.65), band);
                }
                for (int y : {3, 10, 17})
                    painter->drawLine(QPointF(14, y), QPointF(17, y));
                break;
        }
        painter->restore();
    }

  private:
    ViewerIcons::Kind m_kind;
};
}

QIcon ViewerIcons::icon(Kind kind)
{
    return QIcon(new IconEngine(kind));
}

void ViewerIcons::setupButton(QAbstractButton* button, Kind kind, const QString& name,
                              const QString& toolTip, int buttonSize, int iconSize)
{
    button->setText(QString());
    button->setIcon(icon(kind));
    button->setIconSize(QSize(iconSize, iconSize));
    button->setFixedSize(buttonSize, buttonSize);
    button->setProperty("compactIcon", true);
    button->setAccessibleName(name);
    button->setToolTip(toolTip);
    button->setFocusPolicy(Qt::StrongFocus);
    if (auto* tool = qobject_cast<QToolButton*>(button))
        tool->setToolButtonStyle(Qt::ToolButtonIconOnly);
}
