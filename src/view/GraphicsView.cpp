/**
 * Copyright (c) 2021 Alban Fichet <alban dot fichet at gmx dot fr>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *  * Neither the name of the organization(s) nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "GraphicsView.h"
#include "FileDrop.h"
#include <QDragEnterEvent>
#include <QGraphicsPixmapItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <cmath>

GraphicsView::GraphicsView(QWidget* parent): QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    _imageItem = scene()->addPixmap(QPixmap());
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setTransformationAnchor(NoAnchor);
    setResizeAnchor(AnchorViewCenter);
    _checkerboard = QPixmap(32, 32);
    _checkerboard.fill(QColor(125, 125, 125));
    QPainter painter(&_checkerboard);
    painter.fillRect(16, 0, 16, 16, QColor(100, 100, 100));
    painter.fillRect(0, 16, 16, 16, QColor(100, 100, 100));
}

void GraphicsView::setModel(const FramebufferModel* model)
{
    if (_model) disconnect(_model, nullptr, this, nullptr);
    _model = model;
    _imageItem->setPixmap(QPixmap());
    _dataWindow = _displayWindow = QRectF();
    if (!model) return;
    connect(
      model,
      &FramebufferModel::imageChanged,
      this,
      &GraphicsView::onImageChanged);
    connect(
      model,
      &FramebufferModel::imageLoaded,
      this,
      &GraphicsView::onImageLoaded);
    if (model->isImageLoaded()) onImageLoaded();
    if (!model->getLoadedImage().isNull()) onImageChanged();
}

void GraphicsView::onImageLoaded()
{
    if (!_model || !_model->isImageLoaded()) return;
    const QRect dataWindow = _model->getDataWindow();
    const QRect display    = _model->getDisplayWindow();
    const qreal aspect     = _model->pixelAspectRatio();
    _imageItem->setTransform(QTransform::fromScale(aspect, 1.));
    _imageItem->setTransformationMode(
      aspect == 1. ? Qt::FastTransformation : Qt::SmoothTransformation);
    _dataWindow
      = QRectF(0., 0., dataWindow.width() * aspect, dataWindow.height());
    _displayWindow = QRectF(
      (qreal(display.x()) - dataWindow.x()) * aspect,
      qreal(display.y()) - dataWindow.y(),
      display.width() * aspect,
      display.height());
    scene()->setSceneRect(_dataWindow.united(_displayWindow));
    if (_restorePending) {
        _restorePending = false;
        restoreViewState(_pendingState);
    } else
        autoscale();
}

void GraphicsView::onImageChanged()
{
    if (_model)
        _imageItem->setPixmap(QPixmap::fromImage(_model->getLoadedImage()));
}

void GraphicsView::setZoomLevel(double zoom)
{
    if (!_model || !_model->isImageLoaded() || !std::isfinite(zoom)) return;
    const QPointF center = mapToScene(viewport()->rect().center());
    _zoomLevel           = qBound(0.01, zoom, 64.);
    _autoscale           = false;
    setTransform(QTransform::fromScale(_zoomLevel, _zoomLevel));
    centerOn(center);
    emit zoomLevelChanged(_zoomLevel);
}
void GraphicsView::zoomIn()
{
    setZoomLevel(_zoomLevel * 1.1);
}
void GraphicsView::zoomOut()
{
    setZoomLevel(_zoomLevel / 1.1);
}
void GraphicsView::autoscale()
{
    if (!_model || !_model->isImageLoaded() || _displayWindow.isEmpty()) return;
    fitInView(_displayWindow, Qt::KeepAspectRatio);
    _zoomLevel = transform().m11();
    _autoscale = true;
    emit zoomLevelChanged(_zoomLevel);
}

GraphicsView::ViewState GraphicsView::viewState() const
{
    ViewState state;
    state.zoom          = _zoomLevel;
    state.fit           = _autoscale;
    state.center        = mapToScene(viewport()->rect().center());
    state.dataWindow    = _showDataWindow;
    state.displayWindow = _showDisplayWindow;
    return state;
}
void GraphicsView::restoreViewState(const ViewState& state)
{
    if (!_model || !_model->isImageLoaded()) {
        _pendingState   = state;
        _restorePending = true;
        return;
    }
    showDataWindow(state.dataWindow);
    showDisplayWindow(state.displayWindow);
    if (state.fit)
        autoscale();
    else {
        setZoomLevel(state.zoom);
        centerOn(state.center);
    }
}
void GraphicsView::showDisplayWindow(bool show)
{
    _showDisplayWindow = show;
    viewport()->update();
}
void GraphicsView::showDataWindow(bool show)
{
    _showDataWindow = show;
    viewport()->update();
}

void GraphicsView::wheelEvent(QWheelEvent* event)
{
    if (!_model || !_model->isImageLoaded()) {
        event->ignore();
        return;
    }
    const double steps = event->angleDelta().y() != 0
                           ? event->angleDelta().y() / 120.
                           : event->pixelDelta().y() / 40.;
    if (steps == 0.) {
        event->ignore();
        return;
    }
    if (event->modifiers() & Qt::ControlModifier)
        emit controlWheel(steps);
    else {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        const QPoint position = event->position().toPoint();
#else
        const QPoint position = event->pos();
#endif
        const QPointF before = mapToScene(position);
        setZoomLevel(_zoomLevel * std::pow(1.1, qBound(-100., steps, 100.)));
        centerOn(
          mapToScene(viewport()->rect().center()) + before
          - mapToScene(position));
    }
    event->accept();
}
void GraphicsView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (_autoscale) autoscale();
}
void GraphicsView::keyPressEvent(QKeyEvent* event)
{
    if (
      event->modifiers() == Qt::NoModifier
      || event->modifiers() == Qt::ShiftModifier) {
        switch (event->key()) {
            case Qt::Key_Plus:
            case Qt::Key_Equal:
                zoomIn();
                break;
            case Qt::Key_Minus:
                zoomOut();
                break;
            case Qt::Key_0:
                autoscale();
                break;
            case Qt::Key_1:
                setZoomLevel(1.);
                break;
            default:
                QGraphicsView::keyPressEvent(event);
                return;
        }
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}
void GraphicsView::mousePressEvent(QMouseEvent* event)
{
    if (
      _model && _model->isImageLoaded()
      && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        setFocus(Qt::MouseFocusReason);
        _dragging  = true;
        _startDrag = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else
        QGraphicsView::mousePressEvent(event);
}
void GraphicsView::mouseMoveEvent(QMouseEvent* event)
{
    if (!_model || !_model->isImageLoaded()) return;
    if (_dragging && (event->buttons() & (Qt::LeftButton | Qt::MiddleButton))) {
        const QPoint delta = event->pos() - _startDrag;
        horizontalScrollBar()->setValue(
          horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        _startDrag = event->pos();
    } else {
        const QPointF pixel
          = _imageItem->mapFromScene(mapToScene(event->pos()));
        if (
          pixel.x() < 0 || pixel.y() < 0 || pixel.x() >= _model->width()
          || pixel.y() >= _model->height())
            emit queryPixelInfo(-1, -1);
        else
            emit queryPixelInfo(
              int(std::floor(pixel.x())),
              int(std::floor(pixel.y())));
    }
}
void GraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
    if (
      event->button() == Qt::LeftButton
      || event->button() == Qt::MiddleButton) {
        _dragging = false;
        unsetCursor();
    }
    QGraphicsView::mouseReleaseEvent(event);
}
void GraphicsView::leaveEvent(QEvent* event)
{
    emit queryPixelInfo(-1, -1);
    QGraphicsView::leaveEvent(event);
}
void GraphicsView::dragEnterEvent(QDragEnterEvent* event)
{
    if (!localExrFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}
void GraphicsView::dragMoveEvent(QDragMoveEvent* event)
{
    if (!localExrFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}
void GraphicsView::dropEvent(QDropEvent* event)
{
    const QStringList files = localExrFiles(event->mimeData());
    if (files.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    for (const QString& file : files)
        emit openFileOnDropEvent(file);
}
void GraphicsView::drawBackground(QPainter* painter, const QRectF&)
{
    painter->save();
    painter->resetTransform();
    painter->drawTiledPixmap(viewport()->rect(), _checkerboard);
    painter->restore();
}
void GraphicsView::drawForeground(QPainter* painter, const QRectF& rect)
{
    if (!_model || !_model->isImageLoaded()) return;
    painter->save();
    if (_showDisplayWindow) {
        QPainterPath outside, inside;
        outside.addRect(rect);
        inside.addRect(_displayWindow);
        painter->fillPath(outside.subtracted(inside), QColor(0, 0, 0, 150));
    }
    QPen pen;
    pen.setCosmetic(true);
    painter->setBrush(Qt::NoBrush);
    if (_showDataWindow) {
        pen.setColor(Qt::red);
        painter->setPen(pen);
        painter->drawRect(_dataWindow);
    }
    if (_showDisplayWindow) {
        pen.setColor(Qt::black);
        painter->setPen(pen);
        painter->drawRect(_displayWindow);
    }
    painter->restore();
}
void GraphicsView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    viewport()->update();   // The checkerboard is anchored to viewport pixels.
}
