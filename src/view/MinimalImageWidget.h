#pragma once

#include <QWidget>
#include <QPointer>

class LoadProgressWidget;
class DepthRangeWidget;
class ProjectionControls;
class ResolutionLevelWidget;
class ImageFileWidget;
class GraphicsView;
class QLabel;
class PixelReadoutLabel;
class CropIndicator;
class NonFiniteIndicator;
class FramebufferModel;
class FramebufferSummaryLabel;

class MinimalImageWidget : public QWidget
{
  public:
    explicit MinimalImageWidget(QWidget* parent = nullptr);
    GraphicsView* view() const { return m_view; }
    int footerHeight() const;
    int minimumControlWidth() const;
    void setDocument(ImageFileWidget* document);
    void setSummary(const QString& text, const FramebufferModel* model,
                    const QString& detail = QString());

  protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void updateSummary();
    GraphicsView* m_view;
    LoadProgressWidget* m_loading;
    QWidget* m_footer;
    DepthRangeWidget* m_depthRange;
    ProjectionControls* m_projection;
    ResolutionLevelWidget* m_resolution;
    CropIndicator* m_cropIndicator;
    NonFiniteIndicator* m_nonFiniteIndicator;
    FramebufferSummaryLabel* m_summaryLabel;
    QPointer<const FramebufferModel> m_summaryModel;
    PixelReadoutLabel* m_pixelLabel;
    QString m_summaryDetail;
    bool m_summaryUpdatePending = false;
};
