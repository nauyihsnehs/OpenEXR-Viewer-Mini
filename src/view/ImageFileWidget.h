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

#pragma once

#include <QWidget>

#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPointer>
#include <QModelIndex>
#include <QPoint>
#include <QSplitter>
#include <QTreeView>

#include <model/OpenEXRImage.h>
#include <model/framebuffer/RGBFramebufferModel.h>

#include <memory>

class QEvent;
class QAbstractItemModel;
class FramebufferModel;
class GraphicsView;

class ImageFileWidget: public QWidget
{
    Q_OBJECT
  public:
    // Desktop opens opt into background header I/O; preserve synchronous source
    // construction for existing internal callers (pixel decoding is always async).
    explicit ImageFileWidget(
      const QString& filename, QWidget* parent = nullptr, bool asynchronous = false);

    explicit ImageFileWidget(std::istream& stream, QWidget* parent = nullptr);

    virtual ~ImageFileWidget();

    QString getOpenedFolder() const { return m_openedFolder; }
    QString getOpenedFilename() const { return m_openedFilename; }

    QByteArray getSplitterImageState() const;
    void setSplitterImageState(const QByteArray& state);

    QByteArray getSplitterPropertiesState() const
    {
        return m_splitterProperties->saveState();
    }

    void setSplitterPropertiesState(const QByteArray& state)
    {
        m_splitterProperties->restoreState(state);
    }

    bool isStream() const { return m_isStream; }

    const FramebufferModel* activeFramebufferModel() const;
    GraphicsView* activeGraphicsView() const;
    QWidget* activePreviewWidget() const;
    OpenEXRImage*           sourceImage() const { return m_img; }
    bool                    isDocumentReady() const;
    bool                    hasDocumentLoadFailed() const;
    bool                    isRefreshInProgress() const;
    bool loadingProgress(LoadProgress::Snapshot& snapshot) const;
    QString loadingError() const { return m_documentState == DocumentFailed ? m_openError : QString(); }
    QString                 activeFramebufferStatusText() const;
    QString                 activeFramebufferStatusToolTip() const;
    QString                 activeLayerTitleText() const;
    void setRgbPreviewMode(RGBFramebufferModel::PreviewMode mode);
    enum StereoMode { StereoDefault, StereoLeft, StereoRight, StereoAnaglyph };
    ResolutionLevel resolutionLevel() const { return m_resolutionLevel; }
    const std::vector<ResolutionLevel>& resolutionLevels() const { return m_resolutionLevels; }
    ResolutionLevel requestedResolutionLevel() const;
    bool hasRipmapLevels() const;
    QString resolutionLevelLabel(ResolutionLevel level) const;
    void setResolutionLevel(ResolutionLevel level);
    StereoMode stereoMode() const { return m_stereoMode; }
    QString stereoUnavailableReason(StereoMode mode) const;
    void setStereoMode(StereoMode mode);

  signals:
    void minimalViewRequested();
    void openFileOnDropEvent(const QString& filename);
    void activeFramebufferChanged();
    void documentReady();
    void documentLoadFailed(const QString& message);
    void refreshInProgressChanged(bool refreshing);
    void previewsAboutToBeReplaced();
    void previewsReplaced();


  public slots:
    void refresh();

    void setAttributesVisible(bool visible);
    void setLayersVisible(bool visible);


  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

    void setupLayout();

    static QString getTitle(const LayerItem* item);
    QString        getTitle(int partId, const std::string& layer) const;
    void           openAttribute(const HeaderItem* item);

    FramebufferModel* openLayer(const LayerItem* item);

    void open(const QString& filename);
    void open(std::istream& stream);

    void afterOpen(bool defaultLayer = true);

  private slots:
    void onAttributeDoubleClicked(const QModelIndex& index);
    void onLayerDoubleClicked(const QModelIndex& index);

    void onLoadFailed(const QString& msg);

    void onOpenFileDropEvent(const QString& filename);
    void onActiveSubWindowChanged(QMdiSubWindow* subWindow);
    void onSubWindowDestroyed();
    void onFileInfoHoverRequested(QWidget* widget, const QPoint& position);
    void onFileInfoHoverLeft();
    void syncTabbedPreviewPresentation();
    void syncAfterSubWindowDestroyed();

  private:
    enum DocumentState
    {
        DocumentPending,
        DocumentReady,
        DocumentFailed
    };
    struct PreparedPreview;
    struct SavedPreview;
    struct SavedDocument;
    struct RefreshTransaction;

    static QString layerKey(const LayerItem* item);
    PreparedPreview createPreview(
      const LayerItem* item, OpenEXRImage* source, ResolutionLevel level);
    PreparedPreview createStereoPreview(OpenEXRImage* source, ResolutionLevel level);
    void cancelStereoPreview();
    QMdiSubWindow* installPreview(PreparedPreview& preview);
    SavedDocument captureDocumentState() const;
    void          restorePreview(
               QWidget* widget, const SavedPreview& state) const;
    void trackInitialPreview(FramebufferModel* model);
    void prepareDocument(ResolutionLevel level, bool reopen, std::shared_ptr<ExrInput> input = {});
    void startInputOpen(bool reopen, ResolutionLevel level);
    void cancelInputOpen();
    void commitRefresh();
    void abortRefresh(const QString& message);
    void showLoadError(const QString& message) const;
    void           clearImage(bool keepSource = false);
    void           configurePreviewTabBar();
    void           syncActiveLayerSelection();
    void           updatePropertiesVisibility();

    const FramebufferModel* framebufferModel(QMdiSubWindow* subWindow) const;
    QString     framebufferStatusText(QMdiSubWindow* subWindow) const;
    QString     framebufferStatusToolTip(QMdiSubWindow* subWindow) const;
    QModelIndex activeLayerIndex() const;
    QModelIndex findLayerIndexByKey(
      QAbstractItemModel* model,
      const QModelIndex&  parent,
      const QString&      key) const;
    QString layerTitleText(const QModelIndex& index) const;

    QSplitter* m_splitterImageView;
    QSplitter* m_splitterProperties;
    QWidget* m_attributesPanel;
    QWidget* m_layersPanel;
    int m_propertiesWidth = 280;
    QTreeView* m_attributesTreeView;
    QTreeView* m_layersTreeView;
    QMdiArea*  m_mdiArea;

    OpenEXRImage* m_img;
    QString       m_openedFolder;
    QString       m_openedFilename;

    RGBFramebufferModel::PreviewMode m_rgbPreviewMode;
    bool                             m_isStream;
    QStringList                      m_previewOrder;
    DocumentState                    m_documentState = DocumentPending;
    std::unique_ptr<PreparedPreview> m_initialPrepared;
    std::unique_ptr<PreparedPreview> m_pendingStereo;
    ResolutionLevel m_resolutionLevel;
    std::vector<ResolutionLevel> m_resolutionLevels = {{0, 0}};
    unsigned m_preparationGeneration = 0;
    StereoMode m_stereoMode = StereoDefault;
    bool m_selectingStereo = false;
    QPointer<FramebufferModel>       m_initialPreview;
    std::unique_ptr<RefreshTransaction> m_refresh;
    bool m_opening = false;
    unsigned m_inputGeneration = 0;
    Cancellation m_openCancel;
    Progress m_openProgress;
    QString m_openError;
};
