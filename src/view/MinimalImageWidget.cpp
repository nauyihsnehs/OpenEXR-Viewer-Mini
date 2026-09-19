#include "MinimalImageWidget.h"
#include "GraphicsView.h"
#include "LoadProgressWidget.h"
#include "CropIndicator.h"
#include "NonFiniteIndicator.h"
#include "DepthRangeWidget.h"
#include "ProjectionControls.h"
#include "ResolutionLevelWidget.h"
#include "PixelReadoutLabel.h"

#include <QLabel>
#include <QFontMetrics>
#include <QVBoxLayout>

MinimalImageWidget::MinimalImageWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("minimalImageWidget");
    setMinimumSize(1, 1);
    auto* items = new QVBoxLayout(this);
    items->setContentsMargins(0, 0, 0, 0);
    items->setSpacing(0);
    m_view = new GraphicsView(this);
    m_view->setImageWindowMode();
    m_loading = new LoadProgressWidget(m_view->viewport());
    items->addWidget(m_view, 1);
    m_footer = new QWidget(this);
    m_footer->setObjectName("minimalImageFooter");
    m_footer->setFixedHeight(fontMetrics().height() + 12);
    m_footer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_cropIndicator = new CropIndicator(m_footer);
    m_nonFiniteIndicator = new NonFiniteIndicator(m_footer);
    m_summaryLabel = new QLabel(m_footer);
    m_summaryLabel->setObjectName("minimalImageInfo");
    m_summaryLabel->setTextFormat(Qt::PlainText);
    m_pixelLabel = new PixelReadoutLabel(m_footer);
    m_pixelLabel->setObjectName("minimalPixelInfo");
    m_pixelLabel->setTextFormat(Qt::PlainText);
    m_pixelLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_view, &GraphicsView::queryPixelInfo, m_pixelLabel, &PixelReadoutLabel::queryPixel);
    m_depthRange = new DepthRangeWidget(m_footer);
    m_projection = new ProjectionControls(m_footer);
    m_resolution = new ResolutionLevelWidget(m_footer);
    connect(m_resolution, &ResolutionLevelWidget::presentationChanged, this, &MinimalImageWidget::updateSummary);
    m_view->watchOutsideZoom(m_footer);
    items->addWidget(m_footer);
}

void MinimalImageWidget::setDocument(ImageFileWidget* document)
{
    m_resolution->setDocument(document);
    m_loading->setDocument(document);
}

int MinimalImageWidget::minimumControlWidth() const
{
    return m_resolution->isHidden() ? 1 : m_resolution->width() + 16;
}

int MinimalImageWidget::footerHeight() const
{
    return m_footer->height();
}

void MinimalImageWidget::setSummary(const QString& text, const FramebufferModel* model,
                                   const QString& detail)
{
    m_cropIndicator->setModel(model);
    m_nonFiniteIndicator->setModel(model);
    m_pixelLabel->setModel(model);
    // The preview owns a mutable model; the minimal window edits the same range.
    m_depthRange->setModel(const_cast<FramebufferModel*>(model));
    m_projection->setModel(model);
    m_summary = text;
    m_summaryDetail = detail.isEmpty() ? text : detail;
    m_summaryLabel->setToolTip(m_summaryDetail);
    updateSummary();
}

void MinimalImageWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateSummary();
}

void MinimalImageWidget::updateSummary()
{
    const int rowHeight = fontMetrics().height() + 12;
    int top = rowHeight;
    const int depthHeight = m_depthRange->isHidden() ? 0 : m_depthRange->sizeHint().height();
    m_depthRange->setGeometry(0, top, width(), depthHeight); top += depthHeight;
    const int projectionHeight = m_projection->isHidden() ? 0 : 28;
    m_projection->setGeometry(8, top, qMax(0, width() - 16), projectionHeight); top += projectionHeight;
    const int resolutionHeight = m_resolution->isHidden() ? 0 : qMax(28, m_resolution->sizeHint().height());
    m_resolution->setGeometry(8, top, qMax(0, width() - 16), resolutionHeight);
    m_footer->setFixedHeight(top + resolutionHeight);
    const int left = m_cropIndicator->isHidden() ? 8 : 32;
    m_cropIndicator->move(8, (rowHeight - m_cropIndicator->height()) / 2);
    const int available = qMax(0, width() - left - 8);
    const int indicatorSpace = available >= m_nonFiniteIndicator->width() + 6
      ? m_nonFiniteIndicator->width() + 6 : 0;
    const int summary = qMin(available - indicatorSpace,
      m_summaryLabel->fontMetrics().boundingRect(m_summary).width() + 2);
    const int gap = qMin(8, available - summary - indicatorSpace);
    const int pixels = available - summary - indicatorSpace - gap;
    m_summaryLabel->setGeometry(left, 0, summary, rowHeight);
    m_nonFiniteIndicator->move(left + summary + 6,
                              (rowHeight - m_nonFiniteIndicator->height()) / 2);
    m_nonFiniteIndicator->setVisible(indicatorSpace > 0);
    m_pixelLabel->setGeometry(left + summary + indicatorSpace + gap, 0, pixels, rowHeight);
    m_summaryLabel->setVisible(summary > 0);
    m_pixelLabel->setVisible(pixels > 0);
    m_summaryLabel->setText(m_summaryLabel->fontMetrics().elidedText(
      m_summary, Qt::ElideRight, summary));
    m_footer->setToolTip(m_summaryDetail);
}
