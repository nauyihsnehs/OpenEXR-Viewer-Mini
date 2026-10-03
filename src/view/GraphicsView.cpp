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
#include <QLayout>
#include <QLabel>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QAbstractSlider>
#include <QWheelEvent>
#include <QShowEvent>
#include <QScopedValueRollback>
#ifdef Q_OS_WIN
#include "HdrRenderer.h"
#endif

GraphicsView::GraphicsView(QWidget* parent): QGraphicsView(parent)
{
#ifdef Q_OS_WIN
    _hdrRenderer.reset(new HdrRenderer);
    _hdrSurface = new HdrSurface(viewport(), [this] { paintHdrSurface(); });
    _hdrSurface->setGeometry(viewport()->rect());
    _hdrSurface->installEventFilter(this);
    viewport()->installEventFilter(this);
    _hdrTimer.setInterval(1000);
    connect(&_hdrTimer, &QTimer::timeout, this, [this] { updateHdrOutput(); });
#endif
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
    _fovTimer.setSingleShot(true);
    _fovTimer.setInterval(150);
    connect(&_fovTimer, &QTimer::timeout, this, &GraphicsView::endProjectionGesture);
}

GraphicsView::~GraphicsView()
{
#ifdef Q_OS_WIN
    _hdrPainting = _hdrRequested = false;
    _hdrTimer.stop();
    if (_hdrWindow) _hdrWindow->removeEventFilter(this);
    viewport()->removeEventFilter(this);
    _hdrSurface->removeEventFilter(this);
    _hdrSurface->clearRenderCallback();
    _hdrSurface->hide();
    _hdrRenderer->release();
    delete _hdrSurface;
    _hdrSurface = nullptr;
#endif
}

void GraphicsView::updateViewport()
{
    viewport()->update();
#ifdef Q_OS_WIN
    // An opaque native child can suppress the parent viewport's paint event.
    if (_hdrPainting) _hdrSurface->update();
#endif
}

#ifdef Q_OS_WIN
void GraphicsView::setHdrPainting(bool enabled)
{
    const bool changed = _hdrPainting != enabled;
    const bool showing = enabled && _hdrSurface->isHidden();
    if (changed) {
        if (enabled) {
            _sdrUpdateMode = viewportUpdateMode();
            setViewportUpdateMode(FullViewportUpdate);
        } else {
            setViewportUpdateMode(_sdrUpdateMode);
        }
    }
    _hdrPainting = enabled;
    _hdrSurface->setVisible(enabled);
    // Keep the native loading/error overlay and other viewport children above HDR.
    if (showing) _hdrSurface->lower();
    if (changed || showing) updateViewport();
}
#endif

void GraphicsView::setHdrStatus(const QString& status, const QString& detail)
{
    if (_hdrStatus == status && _hdrDetail == detail) return;
    _hdrStatus = status; _hdrDetail = detail;
    emit hdrStatusChanged();
}

void GraphicsView::queueHdrProbe()
{
    if (_hdrProbePending) return;
    _hdrProbePending = true;
    QTimer::singleShot(0, this, [this] {
        _hdrProbePending = false;
        updateHdrOutput();
    });
}

void GraphicsView::retryHdrOutput()
{ updateHdrOutput(true); }

void GraphicsView::updateHdrOutput(bool retry)
{
    const bool requested = _model && bool(_model->hdrPreview());
#ifdef Q_OS_WIN
    if (_hdrProbing) return;
    QScopedValueRollback<bool> probing(_hdrProbing, true);
    if (!requested) {
        _hdrTimer.stop();
        _hdrRequested = false;
        setHdrPainting(false);
        _hdrRenderer->release();
        setHdrStatus(QString(), QString());
        return;
    }
    if (!isVisible() || window()->isMinimized()) {
        _hdrTimer.stop();
        return;
    }
    if (_hdrWindow != window()) {
        if (_hdrWindow) _hdrWindow->removeEventFilter(this);
        _hdrWindow = window();
        _hdrWindow->installEventFilter(this);
    }
    _hdrSurface->setGeometry(viewport()->rect());
    const bool displayChanged = _hdrRenderer->refresh(_hdrSurface->winId(), retry || !_hdrRequested);
    _hdrRequested = true;
    setHdrPainting(_hdrRenderer->available());
    setHdrStatus(_hdrRenderer->statusText(), _hdrRenderer->statusDetail());
    if (!_hdrTimer.isActive()) _hdrTimer.start();
    // White-level changes can require a new GPU frame even when status stays enabled.
    if (displayChanged) updateViewport();
#else
    Q_UNUSED(retry);
    _hdrRequested = requested;
    setHdrStatus(requested ? tr("HDR unavailable — SDR preview") : QString(),
                 requested ? tr("Native HDR display is supported on Windows only.") : QString());
#endif
}

void GraphicsView::paintEvent(QPaintEvent* event)
{
    QGraphicsView::paintEvent(event);
#ifdef Q_OS_WIN
    if (_hdrPainting) _hdrSurface->update();
#endif
}

#ifdef Q_OS_WIN
void GraphicsView::paintHdrSurface()
{
    if (!_hdrPainting || !_hdrSurface->isVisible()) return;
    if (!_model || !_model->hdrPreview()) { queueHdrProbe(); return; }
    const auto frame = _model->hdrPreview();
    const qreal dpr = _hdrSurface->devicePixelRatioF();
    const QSize size(qRound(_hdrSurface->width() * dpr), qRound(_hdrSurface->height() * dpr));
    if (size.isEmpty()) return;
    const QTransform imageToViewport = _imageItem->deviceTransform(viewportTransform());
    const auto geometry = PreviewImage::Geometry(*_model);
    QImage markers;
    if (_model->highlightNonFinite()) {
        markers = QImage(size, QImage::Format_ARGB32_Premultiplied);
        if (!markers.isNull()) {
            markers.setDevicePixelRatio(dpr);
            markers.fill(Qt::transparent);
            QPainter painter(&markers);
            const QRectF clip = imageToViewport.mapRect(geometry.visiblePixels)
              .intersected(QRectF(viewport()->rect()));
            AnomalyMarkers::draw(painter, *_model, imageToViewport, clip);
        }
    }
    if (!_hdrRenderer->render(_hdrSurface->winId(), size, dpr, *frame, imageToViewport,
      geometry.visiblePixels, _imageItem->transformationMode() == Qt::SmoothTransformation,
      _checkerboard, markers, _model->highlightNonFinite())) {
        // Hide the failed native surface after this paint event, revealing SDR.
        queueHdrProbe();
    }
}
#endif

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
        updateViewport();
    }
}

void GraphicsView::setModel(const FramebufferModel* model)
{
    endProjectionGesture();
    if (_model) disconnect(_model, nullptr, this, nullptr);
    _model = model;
    updateHdrOutput();
    _dragging = _rightClick = false;
    unsetCursor();
    const bool completed = model && model->isPreviewReady() && !model->getLoadedImage().isNull();
    if (!completed) {
        _imageItem->setPixmap(QPixmap());
        _displayWindow = QRectF();
        _displayClip->setRect(_displayWindow);
        updateViewport();
        emit queryPixelInfo(-1, -1);
    }
    if (!model) return;
    connect(model, &FramebufferModel::anomalyMarkersChanged, this,
            [this] { updateViewport(); });
    connect(model, &FramebufferModel::readinessChanged, this,
            [this] { updateHdrOutput(); updateViewport(); });
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
    if (completed) onImageChanged();
    else if (model->isImageLoaded()) onImageLoaded();
}

void GraphicsView::onImageLoaded()
{
    if (!_model || !_model->isImageLoaded()) return;
    const PreviewImage::Geometry geometry(*_model);
    const qreal aspect = geometry.imageToScene.m11();
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
    if (_restorePending && _model->isPreviewReady()) {
        _restorePending = false;
        restoreViewState(_pendingState);
    } else
        autoscale();
}

void GraphicsView::onImageChanged()
{
    if (_model) {
        const PreviewImage::Geometry geometry(*_model);
        if (_restorePending || _displayWindow != geometry.sceneWindow()) {
            auto state = viewState();
            const bool pending = _restorePending;
            if (!_displayWindow.isEmpty()) {
                const QPointF relative((state.center.x() - _displayWindow.x()) / _displayWindow.width(),
                                       (state.center.y() - _displayWindow.y()) / _displayWindow.height());
                const auto next = geometry.sceneWindow();
                state.center = next.topLeft() + QPointF(relative.x() * next.width(), relative.y() * next.height());
            }
            onImageLoaded();
            if (!pending && !state.fit && !_imageWindow) restoreViewState(state);
        }
        _imageItem->setTransform(geometry.imageToScene);
        _imageItem->setTransformationMode(geometry.imageToScene.isIdentity() ? Qt::FastTransformation : Qt::SmoothTransformation);
        _imageItem->setPixmap(QPixmap::fromImage(_model->getLoadedImage()));
    }
    updateHdrOutput();
    if (_hdrPainting) updateViewport();
    refreshPixelInfo();
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
    updateViewport();
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
    updateViewport();
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
    updateViewport();
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
    if (!_model || !_model->isPreviewReady()) {
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

namespace {
double wheelSteps(const QWheelEvent* event) {
    return event->angleDelta().y() != 0 ? event->angleDelta().y() / 120. : event->pixelDelta().y() / 40.;
}
QPoint wheelPosition(const QWheelEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}
}

void GraphicsView::zoomWheel(double steps, const QPoint& anchor)
{
    if (_imageWindow) {
        emit imageWindowZoomRequested(_zoomLevel * std::pow(1.1, qBound(-100., steps, 100.)));
        return;
    }
    const QPointF before = mapToScene(anchor);
    setZoomLevel(_zoomLevel * std::pow(1.1, qBound(-100., steps, 100.)));
    centerOn(mapToScene(viewport()->rect().center()) + before - mapToScene(anchor));
}

void GraphicsView::watchOutsideZoom(QWidget* area, QLayout* region)
{
    _wheelArea = area; _wheelRegion = region;
    area->installEventFilter(this);
    for (auto* label : area->findChildren<QLabel*>()) label->installEventFilter(this);
}

bool GraphicsView::eventFilter(QObject* object, QEvent* event)
{
#ifdef Q_OS_WIN
    if (object == viewport() && event->type() == QEvent::Resize) {
        _hdrSurface->setGeometry(viewport()->rect());
        if (_hdrRequested) queueHdrProbe();
        updateViewport();
    }
    const bool displayObject = object == _hdrWindow.data() || object == viewport() || object == _hdrSurface;
    bool dprChanged = event->type() == QEvent::ScreenChangeInternal;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    dprChanged = dprChanged || event->type() == QEvent::DevicePixelRatioChange;
#endif
    if (displayObject && dprChanged) updateViewport();
    if (displayObject
        && (event->type() == QEvent::Move || event->type() == QEvent::Show
            || event->type() == QEvent::WinIdChange || dprChanged
            || event->type() == QEvent::WindowStateChange)
        && (_hdrRequested || (_model && _model->hdrPreview())))
        queueHdrProbe();
#endif
    if (event->type() != QEvent::Wheel || !_wheelArea || !_model || !_model->isImageLoaded())
        return QGraphicsView::eventFilter(object, event);
    auto* widget = qobject_cast<QWidget*>(object);
    if (!widget || (widget != _wheelArea && !_wheelArea->isAncestorOf(widget))) return false;
    auto* wheel = static_cast<QWheelEvent*>(event);
    const QPoint point = widget->mapTo(_wheelArea, wheelPosition(wheel));
    if (_wheelRegion && !_wheelRegion->geometry().contains(point)) return false;
    // Ignored wheel events can bubble from buttons/inputs to the parent area.
    for (auto* child = _wheelArea->childAt(point); child && child != _wheelArea; child = child->parentWidget())
        if (qobject_cast<QAbstractButton*>(child) || qobject_cast<QAbstractSpinBox*>(child)
            || qobject_cast<QComboBox*>(child) || qobject_cast<QAbstractSlider*>(child)) return false;
    const double steps = wheelSteps(wheel);
    if (!steps) return false;
    if (wheel->modifiers() & Qt::ControlModifier) emit controlWheel(steps);
    else zoomWheel(steps, viewport()->rect().center());
    wheel->accept();
    return true;
}

void GraphicsView::wheelEvent(QWheelEvent* event)
{
    if (!_model || !_model->isImageLoaded()) { event->ignore(); return; }
    const double steps = wheelSteps(event);
    if (!steps) { event->ignore(); return; }
    const QPoint position = wheelPosition(event);
    if (event->modifiers() & Qt::ControlModifier) emit controlWheel(steps);
    else if (imageContains(position) && _model->environmentSource().available()
             && _model->requestedProjectionState().type == EnvironmentProjection::Perspective) {
        auto state = _model->requestedProjectionState();
        state.fieldOfView -= steps * 5.;
        const_cast<FramebufferModel*>(_model.data())->updateProjectionInteraction(state);
        if (!_projectionDragging) _fovTimer.start();
    } else zoomWheel(steps, position);
    event->accept();
}

void GraphicsView::endProjectionGesture()
{
    _fovTimer.stop();
    _projectionDragging = false;
    if (_model) const_cast<FramebufferModel*>(_model.data())->endProjectionInteraction();
}

void GraphicsView::focusOutEvent(QFocusEvent* event)
{
    endProjectionGesture();
    QGraphicsView::focusOutEvent(event);
}

void GraphicsView::hideEvent(QHideEvent* event)
{
#ifdef Q_OS_WIN
    _hdrTimer.stop();
    _hdrSurface->hide();
#endif
    endProjectionGesture();
    QGraphicsView::hideEvent(event);
}

void GraphicsView::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);
    updateHdrOutput();
}

void GraphicsView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (_hdrRequested) queueHdrProbe();
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
        const auto type = _model->requestedProjectionState().type;
        _projectionDragging = event->button() == Qt::LeftButton && imageContains(event->pos())
          && _model->environmentSource().available()
          && (type == EnvironmentProjection::Perspective || type == EnvironmentProjection::Sphere);
        if (_projectionDragging) { _fovTimer.stop(); const_cast<FramebufferModel*>(_model.data())->beginProjectionInteraction(); }
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
        endProjectionGesture();
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
        const auto projection = _model->requestedProjectionState();
        if (_projectionDragging && (event->buttons() & Qt::LeftButton) && _model->environmentSource().available()
            && (projection.type == EnvironmentProjection::Perspective || projection.type == EnvironmentProjection::Sphere)) {
            auto state = projection;
            const QPoint delta = event->pos() - _startDrag;
            state.yaw += delta.x() * .3;
            state.pitch += delta.y() * .3;
            _startDrag = event->pos();
            const_cast<FramebufferModel*>(_model.data())->updateProjectionInteraction(state);
            return;
        }
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
        endProjectionGesture();
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
    if (!localImageFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}
void GraphicsView::dragMoveEvent(QDragMoveEvent* event)
{
    if (!localImageFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}
void GraphicsView::dropEvent(QDropEvent* event)
{
    const QStringList files = localImageFiles(event->mimeData());
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
    updateViewport();   // The checkerboard is anchored to viewport pixels.
}
