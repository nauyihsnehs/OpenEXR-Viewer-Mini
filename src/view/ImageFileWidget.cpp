/**
 * Copyright (c) 2021 - 2023 Alban Fichet <alban dot fichet at gmx dot fr>
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

#include "ImageFileWidget.h"
#include <util/ResolutionLevels.h>
#include <util/PreviewImage.h>

#include <QAbstractItemModel>
#include <QEvent>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QList>
#include <QLabel>
#include <QVBoxLayout>
#include <QDir>
#include <QDataStream>
#include <QIODevice>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QString>
#include <QStringList>
#include <QTabBar>
#include <QTimer>
#include <QToolTip>

#include <model/OpenEXRImage.h>
#include <model/attribute/LayerItem.h>
#include <model/attribute/LayerModel.h>
#include <model/framebuffer/FramebufferModel.h>

#include <OpenEXR/ImfHeader.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "GraphicsView.h"
#include "FramebufferInfo.h"
#include <QKeyEvent>
#include <QPointer>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include "RGBFramebufferWidget.h"
#include "YFramebufferWidget.h"

static void setSubWindowFrameVisible(QMdiSubWindow* subWindow, bool visible)
{
    if (!subWindow) return;

    Qt::WindowFlags flags   = subWindow->windowFlags();
    Qt::WindowFlags updated = flags;

    if (visible) {
        updated &= ~Qt::FramelessWindowHint;
    } else {
        updated |= Qt::FramelessWindowHint;
    }

    if (flags == updated) return;

    subWindow->setWindowFlags(updated);
}


static QString cleanLayerTitle(QString title)
{
    title = title.trimmed();

    const QString prefix      = "Layer:";
    const int     prefixIndex = title.indexOf(prefix);
    if (prefixIndex >= 0) title.remove(prefixIndex, prefix.size());

    return title.simplified();
}


static QString pixelTypeName(Imf::PixelType type)
{
    switch (type) {
        case Imf::PixelType::UINT:
            return "uint32";
        case Imf::PixelType::HALF:
            return "half";
        case Imf::PixelType::FLOAT:
            return "float";
        default:
            return "unknown";
    }
}


static void
collectPixelTypes(const LayerItem* item, std::vector<Imf::PixelType>& types)
{
    if (!item) return;

    Imf::PixelType type = item->getPixelType();

    if (type != Imf::PixelType::NUM_PIXELTYPES) {
        types.push_back(type);
        return;
    }

    for (LayerItem* child : item->children()) {
        collectPixelTypes(child, types);
    }
}


static QString pixelTypeName(const LayerItem* item)
{
    std::vector<Imf::PixelType> types;
    collectPixelTypes(item, types);

    if (types.empty()) return "unknown";

    const Imf::PixelType firstType = types.front();

    for (Imf::PixelType type : types) {
        if (type != firstType) return "mixed";
    }

    return pixelTypeName(firstType);
}


static QString compressionShortName(Imf::Compression compression)
{
    switch (compression) {
        case Imf::Compression::NO_COMPRESSION:
            return "No compression";
        case Imf::Compression::RLE_COMPRESSION:
            return "RLE";
        case Imf::Compression::ZIPS_COMPRESSION:
            return "ZIPS";
        case Imf::Compression::ZIP_COMPRESSION:
            return "ZIP";
        case Imf::Compression::PIZ_COMPRESSION:
            return "PIZ";
        case Imf::Compression::PXR24_COMPRESSION:
            return "PXR24";
        case Imf::Compression::B44_COMPRESSION:
            return "B44";
        case Imf::Compression::B44A_COMPRESSION:
            return "B44A";
        case Imf::Compression::DWAA_COMPRESSION:
            return "DWAA";
        case Imf::Compression::DWAB_COMPRESSION:
            return "DWAB";
        default:
            return QString("unknown compression type: %1")
              .arg(static_cast<int>(compression));
    }
}


static QString compressionDescription(Imf::Compression compression)
{
    switch (compression) {
        case Imf::Compression::NO_COMPRESSION:
            return "No compression";
        case Imf::Compression::RLE_COMPRESSION:
            return "RLE (lossless - run length encoding)";
        case Imf::Compression::ZIPS_COMPRESSION:
            return "ZIPS (lossless - zlib compression, one scan line at a "
                   "time)";
        case Imf::Compression::ZIP_COMPRESSION:
            return "ZIP (lossless - zlib compression, in blocks of 16 scan "
                   "lines)";
        case Imf::Compression::PIZ_COMPRESSION:
            return "PIZ (lossless - piz-based wavelet compression)";
        case Imf::Compression::PXR24_COMPRESSION:
            return "PXR24 (lossy - 24-bit float compression)";
        case Imf::Compression::B44_COMPRESSION:
            return "B44 (lossy - 4-by-4 pixel block compression)";
        case Imf::Compression::B44A_COMPRESSION:
            return "B44A (lossy - 4-by-4 pixel block compression)";
        case Imf::Compression::DWAA_COMPRESSION:
            return "DWAA (lossy - DCT based compression, in blocks of 32 "
                   "scanlines)";
        case Imf::Compression::DWAB_COMPRESSION:
            return "DWAB (lossy - DCT based compression, in blocks of 256 "
                   "scanlines)";
        default:
            return QString("unknown compression type: %1")
              .arg(static_cast<int>(compression));
    }
}


struct ImageFileWidget::PreparedPreview {
    QWidget*          widget = nullptr;
    FramebufferModel* model  = nullptr;
    QString           key;
    QString           title;
    QString           pixelType;
    QString           compressionShort;
    QString           compression;
    QStringList       stereoKeys;
    bool stateApplied = false;
};

struct ImageFileWidget::SavedPreview {
    QString                 key;
    PreviewState            preview;
    GraphicsView::ViewState view;
    QStringList             stereoKeys;
    QRectF canvas;
};

struct ImageFileWidget::SavedDocument {
    std::vector<SavedPreview> previews;
    QString                   activeKey;
    StereoMode                stereoMode = StereoDefault;
    ResolutionLevel level;
};

struct ImageFileWidget::RefreshTransaction {
    ~RefreshTransaction() { for (auto& preview : prepared) delete preview.widget; }
    std::unique_ptr<OpenEXRImage> image; // Empty when changing levels on the current source.
    SavedDocument saved;
    std::vector<PreparedPreview> prepared;
    std::vector<ResolutionLevel> levels;
};

template<typename Widget, typename Model>
void configureFramebuffer(
  Widget* widget, Model* model, ImageFileWidget* receiver)
{
    QObject::connect(
      model,
      SIGNAL(loadFailed(QString)),
      receiver,
      SLOT(onLoadFailed(QString)));
    QObject::connect(
      model,
      SIGNAL(imageLoaded()),
      receiver,
      SIGNAL(activeFramebufferChanged()));
    QObject::connect(
      widget,
      SIGNAL(openFileOnDropEvent(QString)),
      receiver,
      SLOT(onOpenFileDropEvent(QString)));
    QObject::connect(
      widget,
      SIGNAL(fileInfoHoverRequested(QWidget*, QPoint)),
      receiver,
      SLOT(onFileInfoHoverRequested(QWidget*, QPoint)));
    QObject::connect(
      widget,
      SIGNAL(fileInfoHoverLeft()),
      receiver,
      SLOT(onFileInfoHoverLeft()));

    QObject::connect(
      model,
      &FramebufferModel::readinessChanged,
      receiver,
      &ImageFileWidget::activeFramebufferChanged);
    QObject::connect(widget, &Widget::minimalViewRequested,
                     receiver, &ImageFileWidget::minimalViewRequested);
    widget->setModel(model);
}


ImageFileWidget::ImageFileWidget(const QString& filename, QWidget* parent)
  : QWidget(parent)
  , m_img(nullptr)
  , m_openedFolder(QDir::homePath())
  , m_rgbPreviewMode(RGBFramebufferModel::Preview_Exposure)
  , m_isStream(false)
{
    setupLayout();

    // clang-format off
    connect(m_attributesTreeView, SIGNAL(doubleClicked(QModelIndex)),
            this                , SLOT(onAttributeDoubleClicked(QModelIndex)));

    connect(m_layersTreeView    , SIGNAL(doubleClicked(QModelIndex)),
            this                , SLOT(onLayerDoubleClicked(QModelIndex)));
    // clang-format on

    // Open the file
    open(filename);
}


ImageFileWidget::ImageFileWidget(std::istream& stream, QWidget* parent)
  : QWidget(parent)
  , m_img(nullptr)
  , m_openedFolder(QDir::homePath())
  , m_rgbPreviewMode(RGBFramebufferModel::Preview_Exposure)
  , m_isStream(true)
{
    setupLayout();

    // clang-format off
    connect(m_attributesTreeView, SIGNAL(doubleClicked(QModelIndex)),
            this                , SLOT(onAttributeDoubleClicked(QModelIndex)));

    connect(m_layersTreeView    , SIGNAL(doubleClicked(QModelIndex)),
            this                , SLOT(onLayerDoubleClicked(QModelIndex)));
    // clang-format on


    // Open the file
    open(stream);
}


ImageFileWidget::~ImageFileWidget()
{
    m_refresh.reset();
    disconnect(m_mdiArea, nullptr, this, nullptr);
    clearImage();
}


bool ImageFileWidget::isDocumentReady() const
{
    return m_documentState == DocumentReady;
}


bool ImageFileWidget::hasDocumentLoadFailed() const
{
    return m_documentState == DocumentFailed;
}


bool ImageFileWidget::isRefreshInProgress() const
{
    return bool(m_refresh);
}


void ImageFileWidget::clearImage(bool keepSource)
{
    cancelStereoPreview();
    if (m_initialPrepared) {
        delete m_initialPrepared->widget;
        m_initialPrepared->widget = nullptr;
        m_initialPrepared.reset();
        m_initialPreview.clear();
    }
    const QSignalBlocker        blockActivation(m_mdiArea);
    const QList<QMdiSubWindow*> windows = m_mdiArea->subWindowList();

    for (QMdiSubWindow* window : windows) {
        disconnect(window, nullptr, this, nullptr);
        m_mdiArea->removeSubWindow(window);
        delete window;
    }

    m_attributesTreeView->setModel(nullptr);
    m_layersTreeView->setModel(nullptr);

    if (!keepSource) {
        delete m_img;
        m_img = nullptr;
    }
    m_previewOrder.clear();
}



QString ImageFileWidget::layerKey(const LayerItem* item)
{
    return QString("%1:%2:%3")
      .arg(item->getPart())
      .arg(int(item->getType()))
      .arg(QString::fromStdString(item->getOriginalFullName()));
}


ImageFileWidget::SavedDocument ImageFileWidget::captureDocumentState() const
{
    SavedDocument state;
    state.stereoMode = m_stereoMode;
    state.level = m_resolutionLevel;
    const QMdiSubWindow* active = m_mdiArea->activeSubWindow();
    state.activeKey
      = active ? active->property("layerKey").toString() : QString();
    auto windows = m_mdiArea->subWindowList();
    std::stable_sort(
      windows.begin(),
      windows.end(),
      [this](QMdiSubWindow* a, QMdiSubWindow* b) {
          return m_previewOrder.indexOf(a->property("layerKey").toString())
                 < m_previewOrder.indexOf(b->property("layerKey").toString());
      });
    for (auto* window : windows) {
        SavedPreview preview;
        preview.key = window->property("layerKey").toString();
        preview.stereoKeys = window->property("stereoKeys").toStringList();
        if (auto* rgb = qobject_cast<RGBFramebufferWidget*>(window->widget()))
            preview.preview = rgb->previewState();
        if (
          auto* scalar
          = qobject_cast<YFramebufferWidget*>(window->widget()))
            preview.preview = scalar->previewState();
        if (auto* view = window->findChild<GraphicsView*>())
            preview.view = view->viewState();
        const auto* model = framebufferModel(window);
        if (model && model->isImageLoaded())
            preview.canvas = PreviewImage::Geometry(*model).sceneWindow();
        state.previews.push_back(preview);
    }
    return state;
}


void ImageFileWidget::restorePreview(
  QWidget* widget, const SavedPreview& state) const
{
    if (auto* rgb = qobject_cast<RGBFramebufferWidget*>(widget))
        rgb->restorePreviewState(state.preview);
    if (auto* scalar = qobject_cast<YFramebufferWidget*>(widget))
        scalar->restorePreviewState(state.preview);
    if (auto* view = widget->findChild<GraphicsView*>())
        view->restoreViewState(state.view);
}


void ImageFileWidget::refresh()
{
    if (m_isStream || m_documentState != DocumentReady) return;
    prepareDocument(m_resolutionLevel, true);
}

bool ImageFileWidget::hasRipmapLevels() const
{
    if (!m_img) return false;
    for (int part = 0; part < m_img->getEXR().parts(); ++part) {
        const auto& header = m_img->getEXR().header(part);
        if (header.hasTileDescription() && header.tileDescription().mode == Imf::RIPMAP_LEVELS)
            return true;
    }
    return false;
}

QString ImageFileWidget::resolutionLevelLabel(ResolutionLevel level) const
{
    if (!m_img) return {};
    const LayerItem* item = nullptr;
    const auto index = activeLayerIndex();
    if (index.isValid()) item = static_cast<LayerItem*>(index.internalPointer());
    if (!item) item = m_img->getLayerModel()->defaultDisplayLayer();
    if (!item) return {};
    const auto& header = m_img->getEXR().header(item->getPart());
    const auto window = ResolutionLevels::displayWindow(header, level);
    return tr("Level %1 — %2 × %3").arg(QString::fromStdString(level.toString()))
      .arg(int64_t(window.max.x) - window.min.x + 1)
      .arg(int64_t(window.max.y) - window.min.y + 1);
}

void ImageFileWidget::setResolutionLevel(ResolutionLevel level)
{
    if (!isDocumentReady() || !std::binary_search(m_resolutionLevels.begin(), m_resolutionLevels.end(), level)) return;
    if (level == m_resolutionLevel) {
        ++m_preparationGeneration;
        m_refresh.reset();
        emit refreshInProgressChanged(false);
        return;
    }
    prepareDocument(level, false);
}

void ImageFileWidget::prepareDocument(ResolutionLevel level, bool reopen)
{
    cancelStereoPreview();
    const unsigned generation = ++m_preparationGeneration;
    m_refresh.reset();
    std::unique_ptr<RefreshTransaction> transaction(new RefreshTransaction);
    try {
        if (reopen) transaction->image.reset(new OpenEXRImage(m_openedFilename, nullptr));
        auto* source = reopen ? transaction->image.get() : m_img;
        transaction->levels = ResolutionLevels::commonLevels(source->sharedEXR());
        if (!std::binary_search(transaction->levels.begin(), transaction->levels.end(), level))
            throw std::runtime_error("The selected resolution level is no longer available; previous previews retained.");
        transaction->saved = captureDocumentState();
        transaction->saved.level = level;
        const auto mode = transaction->saved.stereoMode;
        if (mode == StereoLeft || mode == StereoRight) {
            const int eye = mode == StereoLeft ? 0 : 1;
            const auto oldPair = m_img->getLayerModel()->stereoLayers(m_resolutionLevel);
            const auto newPair = source->getLayerModel()->stereoLayers(level);
            if (!oldPair.eyes[eye] || !newPair.eyes[eye]
                || layerKey(oldPair.eyes[eye]) != layerKey(newPair.eyes[eye]))
                throw std::runtime_error("The selected stereo eye is no longer available; previous previews retained.");
        }
        for (const auto& state : transaction->saved.previews) {
            if (!state.stereoKeys.isEmpty()) {
                const auto pair = source->getLayerModel()->stereoLayers(level);
                if (!pair.anaglyphError.isEmpty()) throw std::runtime_error(pair.anaglyphError.toStdString());
                if (QStringList({layerKey(pair.eyes[0]), layerKey(pair.eyes[1])}) != state.stereoKeys)
                    throw std::runtime_error("The stereo pair has changed; previous previews retained.");
                transaction->prepared.push_back(createStereoPreview(source, level));
            } else {
                const auto index = findLayerIndexByKey(source->getLayerModel(), {}, state.key);
                if (!index.isValid()) throw std::runtime_error("An open source layer is no longer available; previous previews retained.");
                transaction->prepared.push_back(createPreview(static_cast<LayerItem*>(index.internalPointer()), source, level));
            }
        }
    } catch (const std::exception& error) {
        transaction.reset();
        emit refreshInProgressChanged(false);
        showLoadError(QString::fromUtf8(error.what()));
        return;
    }
    m_refresh = std::move(transaction);
    for (size_t i = 0; i < m_refresh->prepared.size(); ++i) {
        auto* model = m_refresh->prepared[i].model;
        connect(model, &FramebufferModel::imageLoaded, this, [this, generation, i] {
            if (!m_refresh || generation != m_preparationGeneration) return;
            auto& preview = m_refresh->prepared[i];
            auto state = m_refresh->saved.previews[i];
            const auto canvas = PreviewImage::Geometry(*preview.model).sceneWindow();
            if (!state.view.fit && !state.canvas.isEmpty()) {
                const QPointF relative((state.view.center.x() - state.canvas.x()) / state.canvas.width(),
                                       (state.view.center.y() - state.canvas.y()) / state.canvas.height());
                state.view.center = canvas.topLeft() + QPointF(relative.x() * canvas.width(), relative.y() * canvas.height());
            }
            m_refresh->saved.previews[i].view = state.view;
            restorePreview(preview.widget, state);
            preview.stateApplied = true;
        });
        connect(model, &FramebufferModel::imageChanged, this, [this, generation] {
            if (!m_refresh || generation != m_preparationGeneration) return;
            for (const auto& preview : m_refresh->prepared)
                if (!preview.stateApplied || !preview.model->isPreviewReady()) return;
            // Leave the emitting model's signal stack before replacing widgets.
            QTimer::singleShot(0, this, [this, generation] {
                if (!m_refresh || generation != m_preparationGeneration) return;
                for (const auto& preview : m_refresh->prepared)
                    if (!preview.stateApplied || !preview.model->isPreviewReady()) return;
                commitRefresh();
            });
        });
    }
    emit refreshInProgressChanged(true);
    if (m_refresh->prepared.empty()) commitRefresh();
}

void ImageFileWidget::commitRefresh()
{
    if (!m_refresh) return;
    auto transaction = std::move(m_refresh);
    clearImage(!transaction->image);
    if (transaction->image) {
        m_img = transaction->image.release();
        m_img->setParent(this);
    }
    m_resolutionLevel = transaction->saved.level;
    m_resolutionLevels = transaction->levels;
    afterOpen(false);
    QPointer<QMdiSubWindow> active;
    for (auto& preview : transaction->prepared) {
        auto* window = installPreview(preview);
        if (preview.key == transaction->saved.activeKey) active = window;
    }
    if (active) m_mdiArea->setActiveSubWindow(active);
    for (size_t i = 0; i < transaction->prepared.size(); ++i) {
        auto* view = transaction->prepared[i].model->parent()->findChild<GraphicsView*>();
        if (view) view->restoreViewState(transaction->saved.previews[i].view);
    }
    m_stereoMode = transaction->saved.stereoMode;
    syncActiveLayerSelection();
    emit activeFramebufferChanged();
    emit refreshInProgressChanged(false);
}


void ImageFileWidget::abortRefresh(const QString& message)
{
    if (!m_refresh) return;
    m_refresh.reset();
    emit refreshInProgressChanged(false);
    showLoadError(message);
}


void ImageFileWidget::setupLayout()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_splitterImageView  = new QSplitter(this);
    m_splitterProperties = new QSplitter(Qt::Vertical, m_splitterImageView);
    m_splitterProperties->setObjectName("informationSidebar");
    m_splitterProperties->setMinimumWidth(200);
    m_splitterProperties->setChildrenCollapsible(false);
    m_splitterImageView->setChildrenCollapsible(false);
    m_splitterImageView->setHandleWidth(5);
    m_splitterProperties->setHandleWidth(5);

    auto makePanel = [this](const QString& title) {
        auto* panel = new QWidget(m_splitterProperties);
        panel->setObjectName("informationPanel");
        auto* items = new QVBoxLayout(panel);
        items->setContentsMargins(8, 8, 8, 8);
        items->setSpacing(8);
        auto* heading = new QLabel(title, panel);
        heading->setObjectName("panelHeading");
        items->addWidget(heading);
        return panel;
    };
    m_layersPanel = makePanel(tr("Layers"));
    m_attributesPanel = makePanel(tr("Attributes"));

    m_attributesTreeView = new QTreeView(m_attributesPanel);
    m_attributesPanel->layout()->addWidget(m_attributesTreeView);
    m_attributesTreeView->setAlternatingRowColors(true);
    m_attributesTreeView->setExpandsOnDoubleClick(false);
    m_attributesTreeView->setIndentation(16);

    m_layersTreeView = new QTreeView(m_layersPanel);
    m_layersPanel->layout()->addWidget(m_layersTreeView);
    m_layersTreeView->setUniformRowHeights(true);
    m_layersTreeView->installEventFilter(this);
    m_layersTreeView->setAlternatingRowColors(true);
    m_layersTreeView->setExpandsOnDoubleClick(false);
    m_layersTreeView->setIndentation(16);

    m_mdiArea = new QMdiArea(m_splitterImageView);
    m_mdiArea->setViewMode(QMdiArea::TabbedView);
    m_mdiArea->setTabsMovable(true);
    m_mdiArea->setTabsClosable(true);
    m_mdiArea->setDocumentMode(true);
    m_mdiArea->setBackground(palette().brush(QPalette::Window));
    syncTabbedPreviewPresentation();

    connect(
      m_mdiArea,
      SIGNAL(subWindowActivated(QMdiSubWindow*)),
      this,
      SLOT(onActiveSubWindowChanged(QMdiSubWindow*)));

    m_splitterProperties->addWidget(m_layersPanel);
    m_splitterProperties->addWidget(m_attributesPanel);

    m_splitterImageView->addWidget(m_mdiArea);
    m_splitterImageView->addWidget(m_splitterProperties);
    m_splitterImageView->setStretchFactor(0, 1);
    m_splitterImageView->setStretchFactor(1, 0);
    m_splitterImageView->setSizes({700, m_propertiesWidth});
    m_splitterImageView->installEventFilter(this);
    connect(m_splitterImageView, &QSplitter::splitterMoved, this, [this] {
        if (m_splitterProperties->isVisible())
            m_propertiesWidth = m_splitterProperties->width();
    });

    layout->addWidget(m_splitterImageView);

    setLayout(layout);
}


bool ImageFileWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_splitterImageView && event->type() == QEvent::Show
        && !m_splitterProperties->isHidden()) {
        const int available = m_splitterImageView->width()
                              - m_splitterImageView->handleWidth();
        m_splitterImageView->setSizes(
          {qMax(1, available - m_propertiesWidth), m_propertiesWidth});
    }
    if (watched == m_layersTreeView && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            onLayerDoubleClicked(m_layersTreeView->currentIndex());
            return true;
        }
    }
    QTabBar*   tabBar = qobject_cast<QTabBar*>(watched);
    const bool previewTabRelease
      = tabBar && m_mdiArea->isAncestorOf(tabBar)
        && event->type() == QEvent::MouseButtonRelease;

    if (previewTabRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

        if (mouseEvent->button() == Qt::MiddleButton) return true;
    }

    return QWidget::eventFilter(watched, event);
}


void ImageFileWidget::configurePreviewTabBar()
{
    QTabBar* tabBar = m_mdiArea->findChild<QTabBar*>();

    if (!tabBar) return;

    tabBar->installEventFilter(this);
    if (!tabBar->property("orderConnected").toBool()) {
        tabBar->setProperty("orderConnected", true);
        connect(tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
            if (
              from >= 0 && to >= 0 && from < m_previewOrder.size()
              && to < m_previewOrder.size())
                m_previewOrder.move(from, to);
        });
    }
}


void ImageFileWidget::syncTabbedPreviewPresentation()
{
    QPointer<QMdiSubWindow> active     = m_mdiArea->activeSubWindow();
    QList<QMdiSubWindow*>   subWindows = m_mdiArea->subWindowList();

    if (subWindows.size() > 1) {
        for (QMdiSubWindow* subWindow : subWindows) {
            setSubWindowFrameVisible(subWindow, true);
        }

        if (m_mdiArea->viewMode() != QMdiArea::TabbedView) {
            m_mdiArea->setViewMode(QMdiArea::TabbedView);
        }

        m_mdiArea->setTabsMovable(true);
        m_mdiArea->setTabsClosable(true);
        configurePreviewTabBar();

        for (QMdiSubWindow* subWindow : subWindows) {
            subWindow->showMaximized();
        }
        if (active) m_mdiArea->setActiveSubWindow(active);
        return;
    }

    if (m_mdiArea->viewMode() != QMdiArea::SubWindowView) {
        m_mdiArea->setViewMode(QMdiArea::SubWindowView);
    }

    for (QMdiSubWindow* subWindow : subWindows) {
        setSubWindowFrameVisible(subWindow, false);
        subWindow->showMaximized();
    }
}


QString ImageFileWidget::getTitle(const LayerItem* item)
{
    QString layerName;

    switch (item->getType()) {
        // Color layer groups
        case LayerItem::RGB:
            layerName
              = "Layer: " + QString::fromStdString(item->getOriginalFullName())
                + "RGB";
            break;

        case LayerItem::RGBA:
            layerName
              = "Layer: " + QString::fromStdString(item->getOriginalFullName())
                + "RGBA";
            break;

        case LayerItem::YC:
            layerName
              = "Layer: " + QString::fromStdString(item->getOriginalFullName())
                + "YC";
            break;

        case LayerItem::YCA:
            layerName
              = "Layer: " + QString::fromStdString(item->getOriginalFullName())
                + "YCA";
            break;

        case LayerItem::YA:
            layerName
              = "Layer: " + QString::fromStdString(item->getOriginalFullName())
                + "YA";
            break;

        // Individual layers
        case LayerItem::R:
        case LayerItem::G:
        case LayerItem::B:
        case LayerItem::A:
        case LayerItem::Y:
        case LayerItem::RY:
        case LayerItem::BY:
        case LayerItem::GENERAL:
            layerName
              = "Layer: " + QString::fromStdString(item->getOriginalFullName());
            break;

        case LayerItem::GROUP:
        case LayerItem::PART:
            layerName = "";
            break;

        // This shall never happen
        case LayerItem::N_LAYERTYPES:
            assert(0);
            break;
    }


    // check if there is a part name

    QString partName;

    if (item->getPart() >= 0) {
        partName += tr("Part:") + " " + QString::number(item->getPart());

        if (item->hasPartName()) {
            partName
              += " (" + QString::fromStdString(item->getPartName()) + ")";
        }
    } else {
        // Single part file
        if (item->hasPartName()) {
            partName += QString::fromStdString(item->getPartName());
        }
    }

    return partName + " " + layerName;
}


void ImageFileWidget::openAttribute(const HeaderItem* item)
{
    if (item->getLayerItem() != nullptr) {
        openLayer(item->getLayerItem());
    }
}


static RGBFramebufferModel::Input colorInput(const LayerItem* item)
{
    RGBFramebufferModel::Input input;
    input.part = item->getPart();
    const auto type = item->getType();
    input.layout = type == LayerItem::RGB || type == LayerItem::RGBA
                     ? RGBFramebufferModel::Layer_RGB
                   : type == LayerItem::YC || type == LayerItem::YCA
                     ? RGBFramebufferModel::Layer_YC : RGBFramebufferModel::Layer_Y;
    const auto name = [item](LayerItem::LayerType channel) {
        const auto* child = item->getType() == channel ? item : item->child(channel);
        return child ? child->getOriginalFullName() : std::string();
    };
    if (input.layout == RGBFramebufferModel::Layer_RGB)
        input.channels = {{name(LayerItem::R), name(LayerItem::G), name(LayerItem::B), name(LayerItem::A)}};
    else if (input.layout == RGBFramebufferModel::Layer_YC)
        input.channels = {{name(LayerItem::Y), name(LayerItem::RY), name(LayerItem::BY), name(LayerItem::A)}};
    else
        input.channels = {{name(LayerItem::Y), "", "", name(LayerItem::A)}};
    return input;
}

ImageFileWidget::PreparedPreview ImageFileWidget::createStereoPreview(OpenEXRImage* source, ResolutionLevel level)
{
    const auto pair = source->getLayerModel()->stereoLayers(level);
    if (!pair.anaglyphError.isEmpty()) throw std::runtime_error(pair.anaglyphError.toStdString());
    PreparedPreview preview;
    preview.stereoKeys = {layerKey(pair.eyes[0]), layerKey(pair.eyes[1])};
    preview.key = QString("stereo:%1:%2%3").arg(preview.stereoKeys[0].size())
                    .arg(preview.stereoKeys[0], preview.stereoKeys[1]);
    preview.title = tr("Anaglyph 3D — Left red / Right cyan");
    preview.pixelType = tr("Derived preview; two-eye source statistics");
    preview.compression = tr("Left: %1; Right: %2")
      .arg(compressionDescription(source->getEXR().header(pair.eyes[0]->getPart()).compression()),
           compressionDescription(source->getEXR().header(pair.eyes[1]->getPart()).compression()));
    std::unique_ptr<RGBFramebufferWidget> widget(new RGBFramebufferWidget(this));
    auto* model = new RGBFramebufferModel("", RGBFramebufferModel::Layer_RGB, widget.get());
    widget->setPreviewMode(m_rgbPreviewMode);
    configureFramebuffer(widget.get(), model, this);
    model->loadStereo(source->sharedEXR(), {{colorInput(pair.eyes[0]), colorInput(pair.eyes[1])}}, level);
    preview.widget = widget.release();
    preview.model = model;
    return preview;
}

ImageFileWidget::PreparedPreview ImageFileWidget::createPreview(
  const LayerItem* item, OpenEXRImage* source, ResolutionLevel level)
{
    if (
      !item || !source || item->getType() == LayerItem::GROUP
      || item->getType() == LayerItem::PART
      || item->getType() == LayerItem::N_LAYERTYPES) {
        throw std::runtime_error("The selected layer cannot be displayed.");
    }

    PreparedPreview preview;
    preview.title     = getTitle(item) + source->getLayerModel()->viewLabel(item);
    preview.key       = layerKey(item);
    preview.pixelType = pixelTypeName(item);
    const int partId  = item->getPart();
    const Imf::Compression compression
      = source->getEXR().header(partId).compression();
    preview.compressionShort = compressionShortName(compression);
    preview.compression      = compressionDescription(compression);

    const auto type   = item->getType();
    const bool scalar = type == LayerItem::R || type == LayerItem::G
                        || type == LayerItem::B || type == LayerItem::A
                        || type == LayerItem::Y || type == LayerItem::RY
                        || type == LayerItem::BY || type == LayerItem::GENERAL;
    if (scalar) {
        std::unique_ptr<YFramebufferWidget> widget(
          new YFramebufferWidget(this));
        auto* model
          = new YFramebufferModel(item->getOriginalFullName(), widget.get());
        configureFramebuffer(widget.get(), model, this);
        model->load(source->sharedEXR(), partId, level);
        preview.widget = widget.release();
        preview.model  = model;
        return preview;
    }

    const auto input = colorInput(item);
    std::unique_ptr<RGBFramebufferWidget> widget(
      new RGBFramebufferWidget(this));
    auto* model = new RGBFramebufferModel(
      item->getOriginalFullName(),
      input.layout,
      widget.get());
    widget->setPreviewMode(m_rgbPreviewMode);
    configureFramebuffer(widget.get(), model, this);
    model->load(source->sharedEXR(), partId, input.channels, level);
    preview.widget = widget.release();
    preview.model  = model;
    return preview;
}


QMdiSubWindow* ImageFileWidget::installPreview(PreparedPreview& preview)
{
    if (!preview.widget || !preview.model) return nullptr;
    QWidget*       widget    = preview.widget;
    QMdiSubWindow* subWindow = m_mdiArea->addSubWindow(widget);
    preview.widget           = nullptr;
    m_previewOrder.removeAll(preview.key);
    m_previewOrder.append(preview.key);
    subWindow->setAttribute(Qt::WA_DeleteOnClose);
    subWindow->setWindowTitle(preview.title);
    const auto* model = preview.model;
    const QString title = preview.title;
    const auto updateTitle = [subWindow, model, title] {
        subWindow->setWindowTitle(model->resolutionLevelCount() > 1
          ? title + tr(" — Level %1 (%2 × %3)").arg(QString::fromStdString(model->resolutionLevel().toString())).arg(model->width()).arg(model->height()) : title);
    };
    connect(preview.model, &FramebufferModel::imageLoaded, subWindow, updateTitle);
    if (preview.model->isImageLoaded()) updateTitle();
    subWindow->setProperty("layerKey", preview.key);
    subWindow->setProperty("stereoKeys", preview.stereoKeys);
    subWindow->setProperty("pixelType", preview.pixelType);
    subWindow->setProperty("compressionShort", preview.compressionShort);
    subWindow->setProperty("compression", preview.compression);
    const QString key = preview.key;
    connect(subWindow, &QObject::destroyed, this, [this, key] {
        m_previewOrder.removeAll(key);
        onSubWindowDestroyed();
    });
    subWindow->showMaximized();
    m_mdiArea->setActiveSubWindow(subWindow);
    syncTabbedPreviewPresentation();
    syncActiveLayerSelection();
    if (!preview.stereoKeys.isEmpty()) m_stereoMode = StereoAnaglyph;
    emit activeFramebufferChanged();
    return subWindow;
}


FramebufferModel* ImageFileWidget::openLayer(const LayerItem* item)
{
    if (m_refresh) return nullptr;
    if (
      !item || item->getType() == LayerItem::GROUP
      || item->getType() == LayerItem::PART
      || item->getType() == LayerItem::N_LAYERTYPES)
        return nullptr;

    cancelStereoPreview();
    if (!m_selectingStereo) m_stereoMode = StereoDefault;

    const QString key = layerKey(item);

    // Check if the window already exists
    for (auto& w : m_mdiArea->subWindowList()) {
        if (w->property("layerKey").toString() == key) {
            const FramebufferModel* existing = framebufferModel(w);
            if (
              existing && !existing->isLoading()
              && !existing->isImageLoaded()) {
                m_mdiArea->removeSubWindow(w);
                delete w;
                break;
            }
            m_mdiArea->setActiveSubWindow(w);
            w->setFocus();
            syncTabbedPreviewPresentation();
            syncActiveLayerSelection();
            emit activeFramebufferChanged();
            return const_cast<FramebufferModel*>(existing);
        }
    }

    PreparedPreview preview = createPreview(item, m_img, m_resolutionLevel);
    FramebufferModel* model = preview.model;
    installPreview(preview);
    return model;
}


GraphicsView* ImageFileWidget::activeGraphicsView() const
{
    QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow();

    if (!subWindow) return nullptr;

    return subWindow->findChild<GraphicsView*>("graphicsView");
}


const FramebufferModel*
ImageFileWidget::framebufferModel(QMdiSubWindow* subWindow) const
{
    if (!subWindow) return nullptr;

    RGBFramebufferWidget* rgbWidget
      = qobject_cast<RGBFramebufferWidget*>(subWindow->widget());

    if (rgbWidget) return rgbWidget->framebufferModel();

    YFramebufferWidget* yWidget
      = qobject_cast<YFramebufferWidget*>(subWindow->widget());

    if (yWidget) return yWidget->framebufferModel();

    return nullptr;
}


const FramebufferModel* ImageFileWidget::activeFramebufferModel() const
{
    return framebufferModel(m_mdiArea->activeSubWindow());
}

void ImageFileWidget::cancelStereoPreview()
{
    if (!m_pendingStereo) return;
    disconnect(m_pendingStereo->model, nullptr, this, nullptr);
    delete m_pendingStereo->widget;
    m_pendingStereo.reset();
}

QString ImageFileWidget::stereoUnavailableReason(StereoMode mode) const
{
    if (!m_img || !isDocumentReady() || m_refresh) return tr("No ready document.");
    if (mode == StereoDefault) return {};
    const auto pair = m_img->getLayerModel()->stereoLayers(m_resolutionLevel);
    if (mode == StereoAnaglyph) return pair.anaglyphError;
    return pair.eyeErrors[mode == StereoLeft ? 0 : 1];
}

void ImageFileWidget::setStereoMode(StereoMode mode)
{
    if (!stereoUnavailableReason(mode).isEmpty()) return;
    cancelStereoPreview();
    const QScopedValueRollback<bool> selecting(m_selectingStereo, true);
    if (mode != StereoAnaglyph) {
        const auto pair = m_img->getLayerModel()->stereoLayers(m_resolutionLevel);
        const auto* layer = mode == StereoDefault ? m_img->getLayerModel()->defaultDisplayLayer()
                                                 : pair.eyes[mode == StereoLeft ? 0 : 1];
        openLayer(layer);
        m_stereoMode = mode;
        emit activeFramebufferChanged();
        return;
    }
    for (auto* window : m_mdiArea->subWindowList()) {
        if (window->property("stereoKeys").toStringList().isEmpty()) continue;
        m_mdiArea->setActiveSubWindow(window);
        m_stereoMode = StereoAnaglyph;
        emit activeFramebufferChanged();
        return;
    }
    try {
        m_pendingStereo.reset(new PreparedPreview(createStereoPreview(m_img, m_resolutionLevel)));
    } catch (const std::exception& error) {
        showLoadError(QString::fromUtf8(error.what()));
        return;
    }
    auto* model = m_pendingStereo->model;
    auto* page = qobject_cast<RGBFramebufferWidget*>(m_pendingStereo->widget);
    const auto* current = qobject_cast<RGBFramebufferWidget*>(activePreviewWidget());
    const bool inheritParameters = current != nullptr;
    const PreviewState parameters = current ? current->previewState() : PreviewState();
    const auto* oldModel = activeFramebufferModel();
    const QPoint oldOrigin = oldModel ? oldModel->getDataWindow().topLeft() : QPoint();
    const auto viewState = activeGraphicsView() ? activeGraphicsView()->viewState() : GraphicsView::ViewState();
    connect(model, &FramebufferModel::imageLoaded, this,
      [this, page, model, parameters, inheritParameters, oldOrigin, viewState] {
        if (inheritParameters) {
            auto state = parameters;
            state.mode = int(m_rgbPreviewMode);
            page->restorePreviewState(state);
        }
        auto state = viewState;
        const QPointF offset = QPointF(oldOrigin) - QPointF(model->getDataWindow().topLeft());
        state.center += QPointF(offset.x() * model->pixelAspectRatio(), offset.y());
        page->findChild<GraphicsView*>()->restoreViewState(state);
    });
    connect(model, &FramebufferModel::imageChanged, this, [this, model] {
        if (!m_pendingStereo || m_pendingStereo->model != model || !model->isPreviewReady()) return;
        auto prepared = std::move(m_pendingStereo);
        m_stereoMode = StereoAnaglyph;
        installPreview(*prepared);
    });
    connect(model, &FramebufferModel::loadFailed, this, [this, model] {
        // Defer destruction until all failure observers have finished.
        const QPointer<FramebufferModel> guarded(model);
        QTimer::singleShot(0, this, [this, guarded] {
            if (m_pendingStereo && m_pendingStereo->model == guarded.data()) cancelStereoPreview();
        });
    });
}


void ImageFileWidget::setRgbPreviewMode(RGBFramebufferModel::PreviewMode mode)
{
    m_rgbPreviewMode = mode;

    const auto applyMode = [mode](QWidget* widget) {
        if (auto* rgb = qobject_cast<RGBFramebufferWidget*>(widget))
            rgb->setPreviewMode(mode);
    };
    if (m_initialPrepared) applyMode(m_initialPrepared->widget);
    if (m_refresh) {
        for (auto& state : m_refresh->saved.previews) state.preview.mode = int(mode);
        for (auto& preview : m_refresh->prepared) applyMode(preview.widget);
    }
    if (m_pendingStereo) applyMode(m_pendingStereo->widget);

    for (QMdiSubWindow* subWindow : m_mdiArea->subWindowList()) {
        applyMode(subWindow->widget());
    }
}

QWidget* ImageFileWidget::activePreviewWidget() const
{
    QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow();
    return subWindow ? subWindow->widget() : nullptr;
}


QString ImageFileWidget::framebufferStatusText(QMdiSubWindow* subWindow) const
{
    const QString path = m_isStream ? tr("Stream") : m_openedFilename;

    if (!subWindow) return path;

    const FramebufferModel* model = framebufferModel(subWindow);
    const QString           layer = subWindow->windowTitle().trimmed();

    QStringList fields;
    fields << path;
    if (!layer.isEmpty()) fields << layer;
    fields << subWindow->property("pixelType").toString();
    fields << subWindow->property("compressionShort").toString();
    fields << framebufferSizeText(model);
    fields << "min " + framebufferDatasetValueText(model, true);
    fields << "max " + framebufferDatasetValueText(model, false);

    return fields.join(" | ");
}


QString ImageFileWidget::activeFramebufferStatusText() const
{
    return framebufferStatusText(m_mdiArea->activeSubWindow());
}


QString
ImageFileWidget::framebufferStatusToolTip(QMdiSubWindow* subWindow) const
{
    const QString path = m_isStream ? tr("Stream") : m_openedFilename;

    if (!subWindow) return path;

    const FramebufferModel* model  = framebufferModel(subWindow);
    const bool              loaded = model && model->isImageLoaded();
    const QString           layer  = subWindow->windowTitle().trimmed();

    QStringList lines;
    lines << "File path: " + path;
    if (!layer.isEmpty()) lines << "Layer info: " + layer;
    lines << "Pixel type: " + subWindow->property("pixelType").toString();
    lines << "Compression: " + subWindow->property("compression").toString();
    lines << "Size: " + framebufferSizeText(model);
    lines << "Source finite min: " + framebufferDatasetValueText(model, true);
    lines << "Source finite max: " + framebufferDatasetValueText(model, false);

    if (loaded) {
        lines << (model->isDerivedPreview() ? "Two-eye source channel samples (not pixels):"
                                           : "Source channel samples (not pixels):");
        lines << "NaN count: " + QString::number(model->getDatasetNaNCount());
        lines << "+Inf count: " + QString::number(model->getDatasetPositiveInfCount());
        lines << "-Inf count: " + QString::number(model->getDatasetNegativeInfCount());
    } else {
        lines << "NaN count: n/a";
        lines << "+Inf count: n/a";
        lines << "-Inf count: n/a";
    }

    return lines.join("\n");
}


QString ImageFileWidget::activeFramebufferStatusToolTip() const
{
    return framebufferStatusToolTip(m_mdiArea->activeSubWindow());
}


QString ImageFileWidget::activeLayerTitleText() const
{
    const auto* window = m_mdiArea->activeSubWindow();
    const auto* model = activeFramebufferModel();
    if (window && model && model->resolutionLevelCount() > 1)
        return window->windowTitle().trimmed();
    QString title = layerTitleText(activeLayerIndex()).trimmed();
    if (title.isEmpty() && window) title = cleanLayerTitle(window->windowTitle());
    return title == "RGB" ? QString() : title;
}


QByteArray ImageFileWidget::getSplitterImageState() const
{
    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_0);
    const int width = m_splitterProperties->isVisible()
                        ? m_splitterProperties->width() : m_propertiesWidth;
    // QSplitter's hidden pane size is not its last expanded width.
    stream << m_splitterImageView->saveState() << qint32(width);
    return state;
}

void ImageFileWidget::setSplitterImageState(const QByteArray& state)
{
    if (state.isEmpty()) return;
    QDataStream stream(state);
    stream.setVersion(QDataStream::Qt_5_0);
    QByteArray splitterState;
    qint32 width = 0;
    stream >> splitterState >> width;
    if (stream.status() != QDataStream::Ok || width < 200 || width > QWIDGETSIZE_MAX)
        return;
    if (m_splitterImageView->restoreState(splitterState)) m_propertiesWidth = width;
}

void ImageFileWidget::setAttributesVisible(bool visible)
{
    m_attributesPanel->setVisible(visible);
    updatePropertiesVisibility();
}


void ImageFileWidget::setLayersVisible(bool visible)
{
    m_layersPanel->setVisible(visible);
    updatePropertiesVisibility();
}


void ImageFileWidget::updatePropertiesVisibility()
{
    const bool showProperties
      = !m_attributesPanel->isHidden() || !m_layersPanel->isHidden();

    if (showProperties == !m_splitterProperties->isHidden()) return;
    if (!showProperties && m_splitterProperties->isVisible()) {
        const QList<int> sizes = m_splitterImageView->sizes();
        if (sizes.size() == 2 && sizes[1] > 0) m_propertiesWidth = sizes[1];
    }

    m_splitterProperties->setVisible(showProperties);
    if (showProperties) {
        const int available = m_splitterImageView->width()
                              - m_splitterImageView->handleWidth();
        m_splitterImageView->setSizes(
          {qMax(1, available - m_propertiesWidth), m_propertiesWidth});
    }
}


QModelIndex ImageFileWidget::activeLayerIndex() const
{
    QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow();

    if (!subWindow) return QModelIndex();

    return findLayerIndexByKey(
      m_layersTreeView->model(),
      QModelIndex(),
      subWindow->property("layerKey").toString());
}


QModelIndex ImageFileWidget::findLayerIndexByKey(
  QAbstractItemModel* model,
  const QModelIndex&  parent,
  const QString&      key) const
{
    if (!model) return QModelIndex();

    for (int row = 0; row < model->rowCount(parent); row++) {
        QModelIndex index = model->index(row, LayerModel::LAYER, parent);
        LayerItem*  item  = static_cast<LayerItem*>(index.internalPointer());

        if (item && layerKey(item) == key) return index;

        QModelIndex child = findLayerIndexByKey(model, index, key);
        if (child.isValid()) return child;
    }

    return QModelIndex();
}


QString ImageFileWidget::layerTitleText(const QModelIndex& index) const
{
    if (!index.isValid()) return QString();

    const QAbstractItemModel* model  = index.model();
    const QModelIndex         parent = index.parent();
    const QString             layer
      = model->data(model->index(index.row(), LayerModel::LAYER, parent))
          .toString()
          .trimmed();
    const QString type
      = model->data(model->index(index.row(), LayerModel::TYPE, parent))
          .toString()
          .trimmed();

    if (layer.isEmpty()) return type;
    if (type.isEmpty() || type == layer) return layer;

    return layer + " " + type;
}


void ImageFileWidget::syncActiveLayerSelection()
{
    QItemSelectionModel* selection = m_layersTreeView->selectionModel();

    if (!selection) return;

    QModelIndex index = activeLayerIndex();

    if (!index.isValid()) {
        selection->clear();
        return;
    }

    selection->select(
      index,
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    selection->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    m_layersTreeView->scrollTo(index);
}


void ImageFileWidget::open(const QString& filename)
{
    assert(!m_isStream);

    // Open the file
    m_openedFilename = filename;
    m_openedFolder   = QFileInfo(m_openedFilename).absolutePath();

    // Attempt opening the image
    OpenEXRImage* imageLoaded = nullptr;

    try {
        imageLoaded = new OpenEXRImage(m_openedFilename, this);
    } catch (std::exception& e) {
        onLoadFailed(e.what());

        delete imageLoaded;

        return;
    }

    // No error so far, continue normal execution
    if (m_img) clearImage();

    m_img = imageLoaded;

    afterOpen();
}


void ImageFileWidget::open(std::istream& stream)
{
    assert(m_isStream);

    // Attempt opening the image
    OpenEXRImage* imageLoaded = nullptr;

    try {
        imageLoaded = new OpenEXRImage(stream, this);
    } catch (std::exception& e) {
        onLoadFailed(e.what());

        delete imageLoaded;

        return;
    }

    // No error so far, continue normal execution
    if (m_img) clearImage();

    m_img = imageLoaded;

    afterOpen();
}


void ImageFileWidget::afterOpen(bool defaultLayer)
{
    try { m_resolutionLevels = ResolutionLevels::commonLevels(m_img->sharedEXR()); }
    catch (const std::exception&) { m_resolutionLevels = {{0, 0}}; }
    m_attributesTreeView->setModel(m_img->getHeaderModel());
    m_attributesTreeView->expandAll();
    m_attributesTreeView->resizeColumnToContents(0);

    m_layersTreeView->setModel(m_img->getLayerModel());
    m_layersTreeView->expandAll();
    m_layersTreeView->resizeColumnToContents(0);

    if (!defaultLayer) return;
    const LayerItem* layer = m_img->getLayerModel()->defaultDisplayLayer();
    if (!layer) {
        onLoadFailed(tr("The file has no displayable layers."));
        return;
    }
    try {
        m_initialPrepared.reset(new PreparedPreview(createPreview(layer, m_img, m_resolutionLevel)));
        trackInitialPreview(m_initialPrepared->model);
    } catch (const std::exception& error) {
        onLoadFailed(QString::fromUtf8(error.what()));
    }
}


void ImageFileWidget::trackInitialPreview(FramebufferModel* model)
{
    m_initialPreview = model;
    connect(
      model,
      &FramebufferModel::imageChanged,
      this,
      [this, model] {
          if (
            m_documentState != DocumentPending || m_initialPreview != model
            || !model->isPreviewReady())
              return;
          installPreview(*m_initialPrepared);
          m_initialPrepared.reset();
          m_initialPreview.clear();
          m_documentState = DocumentReady;
          emit documentReady();
      });
}


void ImageFileWidget::onAttributeDoubleClicked(const QModelIndex& index)
{
    HeaderItem* item = static_cast<HeaderItem*>(index.internalPointer());
    openAttribute(item);
}


void ImageFileWidget::onLayerDoubleClicked(const QModelIndex& index)
{
    LayerItem* item = static_cast<LayerItem*>(index.internalPointer());
    openLayer(item);
}


void ImageFileWidget::onLoadFailed(const QString& msg)
{
    std::cerr << "Loading error: " << msg.toStdString() << std::endl;
    FramebufferModel* failedModel
      = qobject_cast<FramebufferModel*>(sender());
    if (
      m_refresh && failedModel
      && std::any_of(m_refresh->prepared.begin(), m_refresh->prepared.end(),
           [failedModel](const PreparedPreview& preview) { return preview.model == failedModel; })) {
        const QString message = msg;
        const unsigned generation = m_preparationGeneration;
        QTimer::singleShot(0, this, [this, message, generation] {
            if (m_refresh && generation == m_preparationGeneration) abortRefresh(message);
        });
        return;
    }
    showLoadError(msg);
    if (
      m_documentState == DocumentPending
      && (!failedModel || m_initialPreview == failedModel)) {
        m_initialPreview.clear();
        m_documentState = DocumentFailed;
        emit documentLoadFailed(msg);
    }
}


void ImageFileWidget::showLoadError(const QString& msg) const
{
    QMessageBox msgBox;
    msgBox.setText(tr("Error while loading the framebuffer."));
    msgBox.setInformativeText(
      tr("The loading process ended with the following error:") + " " + msg);
    msgBox.exec();
}


void ImageFileWidget::onOpenFileDropEvent(const QString& filename)
{
    emit openFileOnDropEvent(filename);
}


void ImageFileWidget::onFileInfoHoverRequested(
  QWidget* widget, const QPoint& position)
{
    if (!widget) return;

    for (QMdiSubWindow* subWindow : m_mdiArea->subWindowList()) {
        if (subWindow->widget() != widget) continue;

        QToolTip::showText(
          position,
          framebufferStatusToolTip(subWindow),
          widget);
        return;
    }
}


void ImageFileWidget::onFileInfoHoverLeft()
{
    QToolTip::hideText();
}


void ImageFileWidget::onActiveSubWindowChanged(QMdiSubWindow* window)
{
    if (!m_selectingStereo) {
        cancelStereoPreview();
        m_stereoMode = window && !window->property("stereoKeys").toStringList().isEmpty()
                         ? StereoAnaglyph : StereoDefault;
    }
    syncActiveLayerSelection();
    emit activeFramebufferChanged();
}


void ImageFileWidget::onSubWindowDestroyed()
{
    QTimer::singleShot(0, this, SLOT(syncAfterSubWindowDestroyed()));
}


void ImageFileWidget::syncAfterSubWindowDestroyed()
{
    syncTabbedPreviewPresentation();
    syncActiveLayerSelection();
    emit activeFramebufferChanged();
}
