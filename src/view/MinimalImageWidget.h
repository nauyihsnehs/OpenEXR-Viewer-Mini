#pragma once

#include <QWidget>

class DepthRangeWidget;
class GraphicsView;
class QLabel;
class CropIndicator;
class FramebufferModel;

class MinimalImageWidget : public QWidget
{
  public:
    explicit MinimalImageWidget(QWidget* parent = nullptr);
    GraphicsView* view() const { return m_view; }
    int footerHeight() const;
    void setSummary(const QString& text, const FramebufferModel* model);
    void setPixelInfo(const QString& text);

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    void updateSummary();
    GraphicsView* m_view;
    QWidget* m_footer;
    DepthRangeWidget* m_depthRange;
    CropIndicator* m_cropIndicator;
    QLabel* m_summaryLabel;
    QLabel* m_pixelLabel;
    QString m_summary;
    QString m_pixelInfo;
};
