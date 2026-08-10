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
#include <QVBoxLayout>
#include <QDir>
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

#include <vector>

#include "GraphicsView.h"
#include "FramebufferInfo.h"
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
    clearImage();
}


void ImageFileWidget::clearImage()
{
    const QList<QMdiSubWindow*> windows = m_mdiArea->subWindowList();

    for (QMdiSubWindow* window : windows) {
        m_mdiArea->removeSubWindow(window);
        delete window;
    }

    m_attributesTreeView->setModel(nullptr);
    m_layersTreeView->setModel(nullptr);

    delete m_img;
    m_img = nullptr;
}



void ImageFileWidget::refresh()
{
    // TODO:
    // Better refresh handling: keep all window open, close those with no valid
    // layer...
    if (!m_isStream) {
        open(m_openedFilename);
    }
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

    m_splitterImageView  = new QSplitter(this);
    m_splitterProperties = new QSplitter(Qt::Vertical, m_splitterImageView);

    m_attributesTreeView = new QTreeView(m_splitterProperties);
    m_attributesTreeView->setAlternatingRowColors(true);
    m_attributesTreeView->setExpandsOnDoubleClick(false);
    m_attributesTreeView->setIndentation(32);

    m_layersTreeView = new QTreeView(m_splitterProperties);
    m_layersTreeView->setUniformRowHeights(true);
    m_layersTreeView->setAlternatingRowColors(true);
    m_layersTreeView->setExpandsOnDoubleClick(false);
    m_layersTreeView->setIndentation(32);

    m_mdiArea = new QMdiArea(m_splitterImageView);
    m_mdiArea->setViewMode(QMdiArea::TabbedView);
    m_mdiArea->setTabsMovable(true);
    m_mdiArea->setTabsClosable(true);
    m_mdiArea->setDocumentMode(true);
    m_mdiArea->setBackground(QBrush(QColor(80, 80, 80)));
    syncTabbedPreviewPresentation();

    connect(
      m_mdiArea,
      SIGNAL(subWindowActivated(QMdiSubWindow*)),
      this,
      SLOT(onActiveSubWindowChanged(QMdiSubWindow*)));

    m_splitterProperties->addWidget(m_attributesTreeView);
    m_splitterProperties->addWidget(m_layersTreeView);

    m_splitterImageView->addWidget(m_splitterProperties);
    m_splitterImageView->addWidget(m_mdiArea);

    layout->addWidget(m_splitterImageView);

    setLayout(layout);
}


bool ImageFileWidget::eventFilter(QObject* watched, QEvent* event)
{
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
}


void ImageFileWidget::syncTabbedPreviewPresentation()
{
    if (!m_previewTabbed) return;

    QList<QMdiSubWindow*> subWindows = m_mdiArea->subWindowList();

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


void ImageFileWidget::openLayer(const LayerItem* item)
{
    if (
      !item || item->getType() == LayerItem::GROUP
      || item->getType() == LayerItem::PART
      || item->getType() == LayerItem::N_LAYERTYPES) {
        return;
    }

    const QString title  = getTitle(item);
    const int     partId = item->getPart();
    const QString layerKey
      = QString("%1:%2:%3")
          .arg(partId)
          .arg(static_cast<int>(item->getType()))
          .arg(QString::fromStdString(item->getOriginalFullName()));
    const QString          pixelType = pixelTypeName(item);
    const Imf::Compression compression
      = m_img->getEXR().header(partId).compression();

    // Check if the window already exists
    for (auto& w : m_mdiArea->subWindowList()) {
        if (w->property("layerKey").toString() == layerKey) {
            m_mdiArea->setActiveSubWindow(w);
            w->setFocus();
            syncTabbedPreviewPresentation();
            syncActiveLayerSelection();
            emit activeFramebufferChanged();
            return;
        }
    }

    // If the window does not exist yet, create it
    YFramebufferWidget*   graphicViewBW = nullptr;
    YFramebufferModel*    imageModelBW  = nullptr;
    RGBFramebufferWidget* graphicView   = nullptr;
    RGBFramebufferModel*  imageModel    = nullptr;

    QMdiSubWindow* subWindow = nullptr;

    switch (item->getType()) {
        case LayerItem::RGB:
        case LayerItem::RGBA:
            graphicView = new RGBFramebufferWidget(m_mdiArea);
            imageModel  = new RGBFramebufferModel(
              item->getOriginalFullName(),
              RGBFramebufferModel::Layer_RGB,
              graphicView);

            graphicView->setPreviewMode(m_rgbPreviewMode);
            configureFramebuffer(graphicView, imageModel, this);

            imageModel->load(
              m_img->getEXR(),
              item->getPart(),
              item->getType() == LayerItem::RGBA);

            subWindow = m_mdiArea->addSubWindow(graphicView);

            break;

        case LayerItem::YCA:
        case LayerItem::YC:
            graphicView = new RGBFramebufferWidget(m_mdiArea);
            imageModel  = new RGBFramebufferModel(
              item->getOriginalFullName(),
              RGBFramebufferModel::Layer_YC,
              graphicView);

            graphicView->setPreviewMode(m_rgbPreviewMode);
            configureFramebuffer(graphicView, imageModel, this);

            imageModel->load(
              m_img->getEXR(),
              item->getPart(),
              item->getType() == LayerItem::YCA);

            subWindow = m_mdiArea->addSubWindow(graphicView);
            break;

        case LayerItem::R:
        case LayerItem::G:
        case LayerItem::B:
        case LayerItem::Y:
        case LayerItem::YA:
            graphicView = new RGBFramebufferWidget(m_mdiArea);
            imageModel  = new RGBFramebufferModel(
              item->getOriginalFullName(),
              RGBFramebufferModel::Layer_Y,
              graphicView);

            graphicView->setPreviewMode(m_rgbPreviewMode);
            configureFramebuffer(graphicView, imageModel, this);

            imageModel->load(
              m_img->getEXR(),
              item->getPart(),
              item->getType() == LayerItem::YA);

            subWindow = m_mdiArea->addSubWindow(graphicView);
            break;


        case LayerItem::A:
        case LayerItem::RY:
        case LayerItem::BY:
        case LayerItem::GENERAL:
            graphicViewBW = new YFramebufferWidget(m_mdiArea);
            imageModelBW  = new YFramebufferModel(
              item->getOriginalFullName(),
              graphicViewBW);

            configureFramebuffer(graphicViewBW, imageModelBW, this);

            imageModelBW->load(m_img->getEXR(), item->getPart());

            subWindow = m_mdiArea->addSubWindow(graphicViewBW);
            break;

        case LayerItem::PART:
        case LayerItem::GROUP:
        case LayerItem::N_LAYERTYPES:
            break;
    }

    if (subWindow) {
        subWindow->setWindowTitle(title);
        subWindow->setProperty("layerKey", layerKey);
        subWindow->setProperty("pixelType", pixelType);
        subWindow->setProperty(
          "compressionShort",
          compressionShortName(compression));
        subWindow->setProperty(
          "compression",
          compressionDescription(compression));
        connect(
          subWindow,
          SIGNAL(destroyed(QObject*)),
          this,
          SLOT(onSubWindowDestroyed()));

        if (m_previewTabbed) {
            subWindow->showMaximized();
        } else {
            setSubWindowFrameVisible(subWindow, true);
            subWindow->resize(800, 600);
            subWindow->show();
        }

        syncTabbedPreviewPresentation();
        syncActiveLayerSelection();
        emit activeFramebufferChanged();
    }
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

    for (QMdiSubWindow* subWindow : m_mdiArea->subWindowList()) {
        RGBFramebufferWidget* rgbWidget
          = qobject_cast<RGBFramebufferWidget*>(subWindow->widget());

        if (rgbWidget) rgbWidget->setPreviewMode(mode);
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


void ImageFileWidget::setAttributesVisible(bool visible)
{
    m_attributesTreeView->setVisible(visible);
    updatePropertiesVisibility();
}


void ImageFileWidget::setLayersVisible(bool visible)
{
    m_layersTreeView->setVisible(visible);
    updatePropertiesVisibility();
}


void ImageFileWidget::updatePropertiesVisibility()
{
    const bool showProperties
      = !m_attributesTreeView->isHidden() || !m_layersTreeView->isHidden();

    m_splitterProperties->setVisible(showProperties);
}


QModelIndex ImageFileWidget::activeLayerIndex() const
{
    QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow();

    if (!subWindow) return QModelIndex();

    return findLayerIndexByTitle(QModelIndex(), subWindow->windowTitle());
}


QModelIndex ImageFileWidget::findLayerIndexByTitle(
  const QModelIndex& parent, const QString& title) const
{
    QAbstractItemModel* model = m_layersTreeView->model();

    if (!model) return QModelIndex();

    for (int row = 0; row < model->rowCount(parent); row++) {
        QModelIndex index = model->index(row, LayerModel::LAYER, parent);
        LayerItem*  item  = static_cast<LayerItem*>(index.internalPointer());

        if (item && getTitle(item) == title) return index;

        QModelIndex child = findLayerIndexByTitle(index, title);
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


void ImageFileWidget::afterOpen()
{
    m_attributesTreeView->setModel(m_img->getHeaderModel());
    m_attributesTreeView->expandAll();
    m_attributesTreeView->resizeColumnToContents(0);

    m_layersTreeView->setModel(m_img->getLayerModel());
    m_layersTreeView->expandAll();
    m_layersTreeView->resizeColumnToContents(0);

    openDefaultLayer();
}


void ImageFileWidget::openDefaultLayer()
{
    const LayerItem* layer = m_img->getLayerModel()->defaultDisplayLayer();
    if (layer) openLayer(layer);
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
