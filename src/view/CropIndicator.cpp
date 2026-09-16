#include "CropIndicator.h"
#include <model/framebuffer/FramebufferModel.h>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

CropIndicator::CropIndicator(QWidget* parent) : QWidget(parent)
{
    setObjectName("cropIndicator");
    setFixedSize(16, 16);
    setFocusPolicy(Qt::NoFocus);
    setAccessibleName(tr("Data outside display window"));
    hide();
}

void CropIndicator::setModel(const FramebufferModel* model)
{
    QString text;
    if (model && model->isImageLoaded()) {
        const QRect data = model->getDataWindow();
        const QRect display = model->getDisplayWindow();
        QStringList directions;
        if (data.left() < display.left()) directions << tr("Left");
        if (data.right() > display.right()) directions << tr("Right");
        if (data.top() < display.top()) directions << tr("Top");
        if (data.bottom() > display.bottom()) directions << tr("Bottom");
        if (!directions.isEmpty()) {
            text = tr("Data outside display window") + "\n"
                 + tr("Directions: %1").arg(directions.join(", ")) + "\n";
            bool visible = false;
            const QRectF localDisplay = QRectF(display).translated(-QPointF(data.topLeft()));
            for (const QRect& area : model->pixelCoverage())
                visible |= QRectF(area).intersects(localDisplay);
            if (!visible)
                text += tr("All source data is outside the display window.") + "\n";
            text += tr("The preview is cropped. Original EXR exports retain this data.");
        }
    }
    setToolTip(text);
    setAccessibleDescription(text);
    setVisible(!text.isEmpty());
}

void CropIndicator::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(QPalette::WindowText), 1.4));
    QPainterPath path;
    path.moveTo(4., 1.);
    path.lineTo(4., 12.);
    path.lineTo(15., 12.);
    path.moveTo(1., 4.);
    path.lineTo(12., 4.);
    path.lineTo(12., 15.);
    painter.drawPath(path);
}
