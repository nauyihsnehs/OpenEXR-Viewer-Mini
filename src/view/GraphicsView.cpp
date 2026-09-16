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
#include <util/AnomalyMarkers.h>
#include <util/PreviewImage.h>
#include <QDragEnterEvent>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QEvent>
#include <QPalette>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <cmath>

GraphicsView::GraphicsView(QWidget* parent): QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    _displayClip = scene()->addRect(QRectF(), QPen(Qt::NoPen), QBrush(Qt::NoBrush));
    _displayClip->setFlag(QGraphicsItem::ItemClipsChildrenToShape);
    _imageItem = new QGraphicsPixmapItem(_displayClip);
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setTransformationAnchor(NoAnchor);
    setResizeAnchor(AnchorViewCenter);
    updateCheckerboard();
}

void GraphicsView::updateCheckerboard()
{
    const QColor background = palette().color(QPalette::Window);
    const QColor alternate = background.lightness() < 128
                               ? background.lighter(120) : background.darker(104);
    _checkerboard = QPixmap(32, 32);
    _checkerboard.fill(background);
    QPainter painter(&_checkerboard);
    painter.fillRect(16, 0, 16, 16, alternate);
    painter.fillRect(0, 16, 16, 16, alternate);
}

void GraphicsView::changeEvent(QEvent* event)
{
    QGraphicsView::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        updateCheckerboard();
        viewport()->update();
    }
}

void GraphicsView::setModel(const FramebufferModel* model)
{
    if (_model) disconnect(_model, nullptr, this, nullptr);
    _model = model;
    _dragging = _rightClick = false;
    unsetCursor();
    _imageItem->setPixmap(QPixmap());
    _displayWindow = QRectF();
    _displayClip->setRect(_displayWindow);
    viewport()->update();
    emit queryPixelInfo(-1, -1);
    if (!model) return;
    connect(model, &FramebufferModel::anomalyMarkersChanged, this,
            [this] { viewport()->update(); });
    connect(model, &FramebufferModel::readinessChanged, this,
            [this] { viewport()->update(); });
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
    const PreviewImage::Geometry geometry(*_model);
    const qreal aspect     = _model->pixelAspectRatio();
    _imageItem->setTransform(geometry.imageToScene);
    _imageItem->setTransformationMode(
      aspect == 1. ? Qt::FastTransformation : Qt::SmoothTransformation);
    _displayWindow = geometry.sceneWindow();
    _displayClip->setRect(_displayWindow);
    scene()->setSceneRect(_displayWindow);
    if (_imageWindow) {
        applyImageWindowZoom(_zoomLevel);
        return;
    }
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
    if (_imageWindow) {
        emit imageWindowZoomRequested(zoom);
        return;
    }
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
    if (_imageWindow) {
        emit imageWindowZoomRequested(0.);
        return;
    }
    if (!_model || !_model->isImageLoaded() || _displayWindow.isEmpty()) return;
    fitInView(_displayWindow, Qt::KeepAspectRatio);
    _zoomLevel = transform().m11();
    _autoscale = true;
    emit zoomLevelChanged(_zoomLevel);
}

void GraphicsView::setImageWindowMode()
{
    _imageWindow = true;
    _autoscale = false;
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setMinimumSize(1, 1);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
}

void GraphicsView::applyImageWindowZoom(double zoom)
{
    if (!_imageWindow || !std::isfinite(zoom) || zoom <= 0.) return;
    _zoomLevel = zoom;
    setTransform(QTransform::fromScale(zoom, zoom));
    centerOn(_displayWindow.center());
    emit zoomLevelChanged(zoom);
    refreshPixelInfo();
}

GraphicsView::ViewState GraphicsView::viewState() const
{
    ViewState state;
    state.zoom   = _zoomLevel;
    state.fit    = _autoscale;
    state.center = mapToScene(viewport()->rect().center());
    return state;
}
void GraphicsView::restoreViewState(const ViewState& state)
{
    if (!_model || !_model->isImageLoaded()) {
        _pendingState   = state;
        _restorePending = true;
        return;
    }
    if (state.fit)
        autoscale();
    else {
        setZoomLevel(state.zoom);
        centerOn(state.center);
    }
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
    else if (_imageWindow)
        emit imageWindowZoomRequested(_zoomLevel * std::pow(1.1, qBound(-100., steps, 100.)));
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
    _rightClick = event->button() == Qt::RightButton
                  && event->modifiers() == Qt::NoModifier && imageContains(event->pos());
    if (_rightClick) {
        _rightPress = event->pos();
        event->accept();
        return;
    }
    if (
      _model && _model->isImageLoaded()
      && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        setFocus(Qt::MouseFocusReason);
        _dragging  = true;
        _startDrag = event->pos();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        _windowDragOffset = event->globalPosition().toPoint() - window()->pos();
#else
        _windowDragOffset = event->globalPos() - window()->pos();
#endif
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else
        QGraphicsView::mousePressEvent(event);
}

bool GraphicsView::imageContains(const QPoint& position) const
{
    return _model && _model->isImageLoaded()
           && _displayWindow.contains(mapToScene(position));
}

void GraphicsView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier
        && imageContains(event->pos())) {
        _dragging = _rightClick = false;
        unsetCursor();
        event->accept();
        emit minimalViewRequested();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void GraphicsView::contextMenuEvent(QContextMenuEvent* event)
{
    // Right clicks on the image are a direct reset gesture, not a menu request.
    event->accept();
}
void GraphicsView::mouseMoveEvent(QMouseEvent* event)
{
    if (!_model || !_model->isImageLoaded()) return;
    if (_rightClick && (event->pos() - _rightPress).manhattanLength()
                       >= QApplication::startDragDistance())
        _rightClick = false;
    if (_dragging && (event->buttons() & (Qt::LeftButton | Qt::MiddleButton))) {
        if (_imageWindow) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPoint globalPosition = event->globalPosition().toPoint();
#else
            const QPoint globalPosition = event->globalPos();
#endif
            emit imageWindowMoveRequested(globalPosition - _windowDragOffset);
            return;
        }
        const QPoint delta = event->pos() - _startDrag;
        horizontalScrollBar()->setValue(
          horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        _startDrag = event->pos();
    } else {
        queryPixelAt(event->pos());
    }
}

void GraphicsView::queryPixelAt(const QPoint& position)
{
    if (!_model || !_model->isImageLoaded() || !viewport()->isVisible()
        || !viewport()->rect().contains(position)) {
        emit queryPixelInfo(-1, -1);
        return;
    }
    const QPointF pixel = _imageItem->mapFromScene(mapToScene(position));
    const QRectF visible = PreviewImage::Geometry(*_model).visiblePixels;
    if (!std::isfinite(pixel.x()) || !std::isfinite(pixel.y())
        || visible.isEmpty() || pixel.x() < visible.left() || pixel.y() < visible.top()
        || pixel.x() >= visible.right() || pixel.y() >= visible.bottom()) {
        emit queryPixelInfo(-1, -1);
        return;
    }
    const QPoint local(int(std::floor(pixel.x())), int(std::floor(pixel.y())));
    if (!_model->pixelCoverage().contains(local)) {
        emit queryPixelInfo(-1, -1);
        return;
    }
    emit queryPixelInfo(local.x(), local.y());
}

void GraphicsView::refreshPixelInfo()
{
    queryPixelAt(viewport()->mapFromGlobal(QCursor::pos()));
}
void GraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        const bool reset = _rightClick && event->modifiers() == Qt::NoModifier
                           && imageContains(event->pos())
                           && (event->pos() - _rightPress).manhattanLength()
                                < QApplication::startDragDistance();
        _rightClick = false;
        event->accept();
        if (reset) emit resetParametersRequested();
        return;
    }
    if (
      event->button() == Qt::LeftButton
      || event->button() == Qt::MiddleButton) {
        _dragging = false;
        unsetCursor();
    }
    QGraphicsView::mouseReleaseEvent(event);
    if (_imageWindow) refreshPixelInfo();
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
void GraphicsView::drawForeground(QPainter* painter, const QRectF&)
{
    if (!_model || !_model->isImageLoaded()) return;
    const QTransform imageToViewport = _imageItem->deviceTransform(viewportTransform());
    const QRectF clip = imageToViewport.mapRect(PreviewImage::Geometry(*_model).visiblePixels)
        .intersected(QRectF(viewport()->rect()));
    painter->save();
    painter->resetTransform();
    AnomalyMarkers::draw(*painter, *_model, imageToViewport, clip);
    painter->restore();
}

void GraphicsView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    viewport()->update();   // The checkerboard is anchored to viewport pixels.
}
