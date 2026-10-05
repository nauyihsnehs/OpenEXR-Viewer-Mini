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
    explicit IconEngine(ViewerIcons::Kind kind, const QColor& ink)
      : m_kind(kind), m_ink(ink) {}
    QIconEngine* clone() const override { return new IconEngine(m_kind, m_ink); }

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
        const QColor color = m_ink.isValid() ? m_ink : palette.color(mode == QIcon::Disabled
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
            case Hdr:
                painter->drawRoundedRect(QRectF(2, 4, 16, 12), 2, 2);
                painter->drawLine(QPointF(5, 13), QPointF(5, 7));
                painter->drawLine(QPointF(5, 10), QPointF(8, 10));
                painter->drawLine(QPointF(8, 13), QPointF(8, 7));
                path = QPainterPath(QPointF(11, 13));
                path.lineTo(11, 7); path.cubicTo(17, 7, 17, 13, 11, 13);
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
            case Inspector:
                painter->drawRoundedRect(QRectF(2, 2, 16, 16), 2, 2);
                for (int y : {6, 10, 14}) {
                    painter->drawPoint(QPointF(5, y));
                    painter->drawLine(QPointF(8, y), QPointF(15, y));
                }
                break;
            case File:
                path.moveTo(4, 2); path.lineTo(12, 2); path.lineTo(16, 6);
                path.lineTo(16, 18); path.lineTo(4, 18); path.closeSubpath();
                path.moveTo(12, 2); path.lineTo(12, 6); path.lineTo(16, 6);
                painter->drawPath(path);
                break;
            case Part:
                painter->drawRoundedRect(QRectF(2, 3, 16, 14), 1.5, 1.5);
                path.moveTo(4, 14); path.lineTo(8, 9); path.lineTo(11, 12);
                path.lineTo(14, 8); path.lineTo(16, 14);
                painter->drawPath(path);
                painter->drawEllipse(QRectF(5, 6, 2, 2));
                break;
            case Layer:
                painter->drawPolygon(QPolygonF() << QPointF(10, 2) << QPointF(18, 7)
                  << QPointF(10, 12) << QPointF(2, 7));
                path.moveTo(2, 11); path.lineTo(10, 16); path.lineTo(18, 11);
                path.moveTo(2, 14); path.lineTo(10, 19); path.lineTo(18, 14);
                painter->drawPath(path);
                break;
            case Group:
                path.moveTo(2, 16); path.lineTo(2, 4); path.lineTo(8, 4);
                path.lineTo(10, 6); path.lineTo(18, 6); path.lineTo(18, 16);
                path.closeSubpath();
                painter->drawPath(path);
                break;
            case Channel:
                path.moveTo(2, 10); path.lineTo(5, 10); path.lineTo(8, 4);
                path.lineTo(12, 16); path.lineTo(15, 10); path.lineTo(18, 10);
                painter->drawPath(path);
                break;
            case Preview:
                path.moveTo(2, 10); path.cubicTo(6, 3, 14, 3, 18, 10);
                path.cubicTo(14, 17, 6, 17, 2, 10);
                painter->drawPath(path);
                painter->drawEllipse(QRectF(8, 8, 4, 4));
                break;
            case Stereo:
                for (qreal center : {5., 15.}) {
                    path = QPainterPath(QPointF(center - 4, 10));
                    path.cubicTo(center - 2, 5, center + 2, 5, center + 4, 10);
                    path.cubicTo(center + 2, 15, center - 2, 15, center - 4, 10);
                    painter->drawPath(path);
                    painter->drawEllipse(QRectF(center - 1, 9, 2, 2));
                }
                break;
            case Search:
                painter->drawEllipse(QRectF(3, 3, 10, 10));
                painter->drawLine(QPointF(12, 12), QPointF(17, 17));
                break;
            case Copy:
                path.moveTo(12, 4); path.lineTo(12, 2); path.lineTo(3, 2);
                path.lineTo(3, 13); path.lineTo(5, 13);
                painter->drawPath(path);
                painter->drawRoundedRect(QRectF(7, 6, 10, 12), 1, 1);
                break;
            case Close:
                painter->drawLine(QPointF(5, 5), QPointF(15, 15));
                painter->drawLine(QPointF(15, 5), QPointF(5, 15));
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
    QColor m_ink;
};
}

QIcon ViewerIcons::icon(Kind kind, const QColor& ink)
{
    return QIcon(new IconEngine(kind, ink));
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
