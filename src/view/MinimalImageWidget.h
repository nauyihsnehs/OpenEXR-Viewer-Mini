#pragma once

#include <QWidget>

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
    QLabel* m_summaryLabel;
    PixelReadoutLabel* m_pixelLabel;
    QString m_summary;
    QString m_summaryDetail;
};
