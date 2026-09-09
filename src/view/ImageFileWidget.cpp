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
};

struct ImageFileWidget::SavedPreview {
    QString                 key;
    PreviewState            preview;
    GraphicsView::ViewState view;
    QByteArray              geometry;
};

struct ImageFileWidget::SavedDocument {
    std::vector<SavedPreview> previews;
    QString                   activeKey;
    bool                      previewTabbed = true;
};

struct ImageFileWidget::RefreshTransaction {
    ~RefreshTransaction() { delete prepared.widget; }

    std::unique_ptr<OpenEXRImage> image;
    SavedDocument                 saved;
    PreparedPreview               prepared;
    QString                       gateKey;
    bool                          stateApplied = false;
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
    widget->setModel(model);
}


ImageFileWidget::ImageFileWidget(const QString& filename, QWidget* parent)
  : QWidget(parent)
  , m_img(nullptr)
  , m_openedFolder(QDir::homePath())
  , m_rgbPreviewMode(RGBFramebufferModel::Preview_Exposure)
  , m_previewTabbed(true)
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
  , m_previewTabbed(true)
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


void ImageFileWidget::clearImage()
{
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

    delete m_img;
    m_img = nullptr;
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
    state.previewTabbed = m_previewTabbed;
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
        if (auto* rgb = qobject_cast<RGBFramebufferWidget*>(window->widget()))
            preview.preview = rgb->previewState();
        if (
          auto* scalar
          = qobject_cast<YFramebufferWidget*>(window->widget()))
            preview.preview = scalar->previewState();
        if (auto* view = window->findChild<GraphicsView*>())
            preview.view = view->viewState();
        preview.geometry = window->saveGeometry();
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
    if (m_isStream || m_documentState != DocumentReady || m_refresh) return;
    std::unique_ptr<RefreshTransaction> transaction(new RefreshTransaction);
    try {
        transaction->image.reset(new OpenEXRImage(m_openedFilename, nullptr));
    } catch (const std::exception& error) {
        showLoadError(QString::fromUtf8(error.what()));
        return;
    }
    transaction->saved = captureDocumentState();
    QAbstractItemModel* layers = transaction->image->getLayerModel();
    QModelIndex gate = findLayerIndexByKey(
      layers,
      QModelIndex(),
      transaction->saved.activeKey);
    if (!gate.isValid()) {
        for (const SavedPreview& state : transaction->saved.previews) {
            gate = findLayerIndexByKey(layers, QModelIndex(), state.key);
            if (gate.isValid()) break;
        }
    }
    const LayerItem* gateItem = gate.isValid()
                                  ? static_cast<LayerItem*>(gate.internalPointer())
                                  : transaction->image->getLayerModel()
                                      ->defaultDisplayLayer();
    if (!gateItem) {
        showLoadError(tr("The refreshed file has no displayable layers."));
        return;
    }
    try {
        transaction->gateKey  = layerKey(gateItem);
        transaction->prepared = createPreview(gateItem, transaction->image.get());
    } catch (const std::exception& error) {
        showLoadError(QString::fromUtf8(error.what()));
        return;
    }

    m_refresh = std::move(transaction);
    FramebufferModel* gateModel = m_refresh->prepared.model;
    QWidget*          gateWidget = m_refresh->prepared.widget;
    const auto savedGate = std::find_if(
      m_refresh->saved.previews.begin(),
      m_refresh->saved.previews.end(),
      [this](const SavedPreview& state) {
          return state.key == m_refresh->gateKey;
      });
    if (savedGate == m_refresh->saved.previews.end()) {
        m_refresh->stateApplied = true;
    } else {
        const QString gateKey = m_refresh->gateKey;
        connect(
          gateModel,
          &FramebufferModel::imageLoaded,
          this,
          [this, gateWidget, gateKey] {
              if (!m_refresh || m_refresh->gateKey != gateKey) return;
              const auto state = std::find_if(
                m_refresh->saved.previews.begin(),
                m_refresh->saved.previews.end(),
                [&gateKey](const SavedPreview& saved) {
                    return saved.key == gateKey;
                });
              if (state == m_refresh->saved.previews.end()) return;
              restorePreview(gateWidget, *state);
              m_refresh->stateApplied = true;
          });
    }
    connect(
      gateModel,
      &FramebufferModel::imageChanged,
      this,
      [this, gateModel] {
          if (
            m_refresh && m_refresh->prepared.model == gateModel
            && m_refresh->stateApplied && gateModel->isPreviewReady())
              commitRefresh();
      });
    emit refreshInProgressChanged(true);
}


void ImageFileWidget::commitRefresh()
{
    if (!m_refresh) return;
    std::unique_ptr<RefreshTransaction> transaction = std::move(m_refresh);
    PreparedPreview gatePreview = transaction->prepared;
    transaction->prepared.widget = nullptr;

    clearImage();
    m_img = transaction->image.release();
    m_img->setParent(this);
    m_previewTabbed = transaction->saved.previewTabbed;
    afterOpen(false);

    QPointer<QMdiSubWindow> restoredActive;
    QPointer<QMdiSubWindow> gateWindow;
    bool                    gateInstalled = false;
    for (const SavedPreview& state : transaction->saved.previews) {
        const QModelIndex index = findLayerIndexByKey(
          m_layersTreeView->model(),
          QModelIndex(),
          state.key);
        if (!index.isValid()) continue;

        FramebufferModel* model  = nullptr;
        QMdiSubWindow*    window = nullptr;
        if (state.key == transaction->gateKey) {
            model         = gatePreview.model;
            window        = installPreview(gatePreview);
            gateWindow    = window;
            gateInstalled = true;
        } else {
            try {
                model = openLayer(
                  static_cast<LayerItem*>(index.internalPointer()));
            } catch (const std::exception& error) {
                showLoadError(QString::fromUtf8(error.what()));
                continue;
            }
            window = m_mdiArea->activeSubWindow();
            if (auto* view = window ? window->findChild<GraphicsView*>()
                                    : nullptr)
                view->restoreViewState(state.view);
            if (model && window) {
                const auto restore = [this, window, state] {
                    restorePreview(window->widget(), state);
                };
                if (model->isImageLoaded())
                    restore();
                else
                    connect(
                      model,
                      &FramebufferModel::imageLoaded,
                      window,
                      restore);
            }
        }
        if (!window) continue;
        if (!m_previewTabbed) window->restoreGeometry(state.geometry);
        if (state.key == transaction->saved.activeKey)
            restoredActive = window;
    }

    if (!gateInstalled) gateWindow = installPreview(gatePreview);
    if (restoredActive)
        m_mdiArea->setActiveSubWindow(restoredActive);
    else if (gateWindow)
        m_mdiArea->setActiveSubWindow(gateWindow);
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


void ImageFileWidget::setTabbed()
{
    m_previewTabbed = true;
    syncTabbedPreviewPresentation();
}


void ImageFileWidget::setCascade()
{
    m_previewTabbed = false;
    m_mdiArea->setViewMode(QMdiArea::SubWindowView);
    for (QMdiSubWindow* subWindow : m_mdiArea->subWindowList()) {
        setSubWindowFrameVisible(subWindow, true);
        subWindow->show();
    }
    m_mdiArea->cascadeSubWindows();
}


void ImageFileWidget::setTiled()
{
    m_previewTabbed = false;
    m_mdiArea->setViewMode(QMdiArea::SubWindowView);
    for (QMdiSubWindow* subWindow : m_mdiArea->subWindowList()) {
        setSubWindowFrameVisible(subWindow, true);
        subWindow->show();
    }
    m_mdiArea->tileSubWindows();
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
    if (!m_previewTabbed) return;

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


ImageFileWidget::PreparedPreview ImageFileWidget::createPreview(
  const LayerItem* item, OpenEXRImage* source)
{
    if (
      !item || !source || item->getType() == LayerItem::GROUP
      || item->getType() == LayerItem::PART
      || item->getType() == LayerItem::N_LAYERTYPES) {
        throw std::runtime_error("The selected layer cannot be displayed.");
    }

    PreparedPreview preview;
    preview.title     = getTitle(item);
    preview.key       = layerKey(item);
    preview.pixelType = pixelTypeName(item);
    const int partId  = item->getPart();
    const Imf::Compression compression
      = source->getEXR().header(partId).compression();
    preview.compressionShort = compressionShortName(compression);
    preview.compression      = compressionDescription(compression);

    const auto type   = item->getType();
    const bool scalar = type == LayerItem::A || type == LayerItem::RY
                        || type == LayerItem::BY || type == LayerItem::GENERAL;
    if (scalar) {
        std::unique_ptr<YFramebufferWidget> widget(
          new YFramebufferWidget(this));
        auto* model
          = new YFramebufferModel(item->getOriginalFullName(), widget.get());
        configureFramebuffer(widget.get(), model, this);
        model->load(source->sharedEXR(), partId);
        preview.widget = widget.release();
        preview.model  = model;
        return preview;
    }

    const auto layout = type == LayerItem::RGB || type == LayerItem::RGBA
                          ? RGBFramebufferModel::Layer_RGB
                        : type == LayerItem::YC || type == LayerItem::YCA
                          ? RGBFramebufferModel::Layer_YC
                          : RGBFramebufferModel::Layer_Y;
    std::array<std::string, 4> channels;
    auto channelName = [item](LayerItem::LayerType channel) {
        const LayerItem* child = item->child(channel);
        return child ? child->getOriginalFullName() : std::string();
    };
    if (layout == RGBFramebufferModel::Layer_RGB)
        channels = {
          {channelName(LayerItem::R),
           channelName(LayerItem::G),
           channelName(LayerItem::B),
           channelName(LayerItem::A)}};
    else if (layout == RGBFramebufferModel::Layer_YC)
        channels = {
          {channelName(LayerItem::Y),
           channelName(LayerItem::RY),
           channelName(LayerItem::BY),
           channelName(LayerItem::A)}};
    else
        channels = {
          {type == LayerItem::YA ? channelName(LayerItem::Y)
                                 : item->getOriginalFullName(),
           "",
           "",
           channelName(LayerItem::A)}};
    std::unique_ptr<RGBFramebufferWidget> widget(
      new RGBFramebufferWidget(this));
    auto* model = new RGBFramebufferModel(
      item->getOriginalFullName(),
      layout,
      widget.get());
    widget->setPreviewMode(m_rgbPreviewMode);
    configureFramebuffer(widget.get(), model, this);
    model->load(source->sharedEXR(), partId, channels);
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
    subWindow->setProperty("layerKey", preview.key);
    subWindow->setProperty("pixelType", preview.pixelType);
    subWindow->setProperty("compressionShort", preview.compressionShort);
    subWindow->setProperty("compression", preview.compression);
    const QString key = preview.key;
    connect(subWindow, &QObject::destroyed, this, [this, key] {
        m_previewOrder.removeAll(key);
        onSubWindowDestroyed();
    });
    if (m_previewTabbed) {
        subWindow->showMaximized();
    } else {
        setSubWindowFrameVisible(subWindow, true);
        subWindow->resize(800, 600);
        subWindow->show();
    }
    m_mdiArea->setActiveSubWindow(subWindow);
    syncTabbedPreviewPresentation();
    syncActiveLayerSelection();
    emit activeFramebufferChanged();
    return subWindow;
}


FramebufferModel* ImageFileWidget::openLayer(const LayerItem* item)
{
    if (
      !item || item->getType() == LayerItem::GROUP
      || item->getType() == LayerItem::PART
      || item->getType() == LayerItem::N_LAYERTYPES)
        return nullptr;

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

    PreparedPreview preview = createPreview(item, m_img);
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


bool ImageFileWidget::hasActiveFramebuffer() const
{
    return activeGraphicsView() != nullptr;
}


void ImageFileWidget::setRgbPreviewMode(RGBFramebufferModel::PreviewMode mode)
{
    m_rgbPreviewMode = mode;

    const auto applyMode = [mode](QWidget* widget) {
        if (auto* rgb = qobject_cast<RGBFramebufferWidget*>(widget))
            rgb->setPreviewMode(mode);
    };
    if (m_initialPrepared) applyMode(m_initialPrepared->widget);
    if (m_refresh) applyMode(m_refresh->prepared.widget);

    for (QMdiSubWindow* subWindow : m_mdiArea->subWindowList()) {
        applyMode(subWindow->widget());
    }
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
    lines << "Min value: " + framebufferDatasetValueText(model, true);
    lines << "Max value: " + framebufferDatasetValueText(model, false);

    if (loaded) {
        lines << "NaN count: " + QString::number(model->getDatasetNaNCount());
        lines << "Inf count: " + QString::number(model->getDatasetInfCount());
    } else {
        lines << "NaN count: n/a";
        lines << "Inf count: n/a";
    }

    return lines.join("\n");
}


QString ImageFileWidget::activeFramebufferStatusToolTip() const
{
    return framebufferStatusToolTip(m_mdiArea->activeSubWindow());
}


QString ImageFileWidget::activeLayerTitleText() const
{
    QModelIndex index = activeLayerIndex();
    QString     title = layerTitleText(index).trimmed();

    if (title.isEmpty()) {
        QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow();
        if (subWindow) title = cleanLayerTitle(subWindow->windowTitle());
    }

    if (title == "RGB") return QString();

    return title;
}


void ImageFileWidget::setDataWindowVisible(bool visible)
{
    GraphicsView* view = activeGraphicsView();

    if (view) view->showDataWindow(visible);
}


void ImageFileWidget::setDisplayWindowVisible(bool visible)
{
    GraphicsView* view = activeGraphicsView();

    if (view) view->showDisplayWindow(visible);
}


bool ImageFileWidget::isDataWindowVisible() const
{
    GraphicsView* view = activeGraphicsView();

    return view && view->isDataWindowVisible();
}


bool ImageFileWidget::isDisplayWindowVisible() const
{
    GraphicsView* view = activeGraphicsView();

    return view && view->isDisplayWindowVisible();
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
        m_initialPrepared.reset(new PreparedPreview(createPreview(layer, m_img)));
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
      && m_refresh->prepared.model == failedModel) {
        const QString message = msg;
        QTimer::singleShot(0, this, [this, message] {
            if (m_refresh) abortRefresh(message);
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


void ImageFileWidget::onActiveSubWindowChanged(QMdiSubWindow*)
{
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
