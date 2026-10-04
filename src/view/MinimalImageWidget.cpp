#include "MinimalImageWidget.h"
#include "GraphicsView.h"
#include "LoadProgressWidget.h"
#include "CropIndicator.h"
#include "NonFiniteIndicator.h"
#include "DepthRangeWidget.h"
#include "ProjectionControls.h"
#include "ResolutionLevelWidget.h"
#include "PixelReadoutLabel.h"
#include "FramebufferInfo.h"

#include <QLabel>
#include <QFontMetrics>
#include <QTimer>
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
    m_footer->setFixedHeight(qMax(24, fontMetrics().height() + 12));
    m_footer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_cropIndicator = new CropIndicator(m_footer);
    m_nonFiniteIndicator = new NonFiniteIndicator(m_footer);
    m_summaryLabel = new FramebufferSummaryLabel(m_footer);
    m_summaryLabel->setObjectName("minimalImageInfo");
    m_summaryLabel->setTextFormat(Qt::PlainText);
    m_summaryLabel->installEventFilter(this);
    m_pixelLabel = new PixelReadoutLabel(m_footer);
    m_pixelLabel->setObjectName("minimalPixelInfo");
    m_pixelLabel->setTextFormat(Qt::PlainText);
    m_pixelLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_view, &GraphicsView::queryPixelInfo, m_pixelLabel, &PixelReadoutLabel::queryPixel);
    connect(m_view, &GraphicsView::hdrStatusChanged, this, &MinimalImageWidget::updateSummary);
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
    // The preview owns a mutable model; both views edit its markers and depth range.
    auto* mutableModel = const_cast<FramebufferModel*>(model);
    m_nonFiniteIndicator->setModel(mutableModel);
    m_pixelLabel->setModel(model);
    m_depthRange->setModel(mutableModel);
    m_projection->setModel(model);
    m_summaryModel = model;
    m_summaryDetail = detail.isEmpty() ? text : detail;
    m_summaryLabel->setToolTip(m_summaryDetail);
    updateSummary();
}

void MinimalImageWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateSummary();
}

bool MinimalImageWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_summaryLabel && framebufferSummaryMetricsChanged(event->type())
        && !m_summaryUpdatePending) {
        // Run after the label has applied the new font/style/DPI metrics.
        m_summaryUpdatePending = true;
        QTimer::singleShot(0, this, [this] {
            m_summaryUpdatePending = false;
            updateSummary();
        });
    }
    return QWidget::eventFilter(watched, event);
}

void MinimalImageWidget::updateSummary()
{
    const QString status = m_view->hdrStatusText();
    m_summaryLabel->setSummary(m_summaryModel, status);
    const QString detail = status.isEmpty() ? m_summaryDetail
      : m_summaryDetail + "\n" + status + "\n" + m_view->hdrStatusDetail();
    m_summaryLabel->setToolTip(detail);
    const int rowHeight = qMax(24, fontMetrics().height() + 12);
    int top = rowHeight;
    const int depthHeight = m_depthRange->isHidden() ? 0 : m_depthRange->sizeHint().height();
    m_depthRange->setGeometry(0, top, width(), depthHeight); top += depthHeight;
    const int projectionHeight = m_projection->isHidden() ? 0 : 28;
    m_projection->setGeometry(8, top, qMax(0, width() - 16), projectionHeight); top += projectionHeight;
    const int resolutionHeight = m_resolution->isHidden() ? 0 : qMax(28, m_resolution->sizeHint().height());
    m_resolution->setGeometry(8, top, qMax(0, width() - 16), resolutionHeight);
    m_footer->setFixedHeight(top + resolutionHeight);
    const int left = m_cropIndicator->isHidden() ? 8 : 8 + m_cropIndicator->width() + 12;
    m_cropIndicator->move(8, (rowHeight - m_cropIndicator->height()) / 2);
    const int available = qMax(0, width() - left - 8);
    const int indicatorSpace = available >= m_nonFiniteIndicator->width() + 12
      ? m_nonFiniteIndicator->width() + 12 : 0;
    const int summary = qMin(available - indicatorSpace,
      m_summaryLabel->sizeHint().width());
    const int gap = qMin(12, available - summary - indicatorSpace);
    const int pixels = available - summary - indicatorSpace - gap;
    m_summaryLabel->setGeometry(left, 0, summary, rowHeight);
    m_nonFiniteIndicator->move(left + summary + 12,
                              (rowHeight - m_nonFiniteIndicator->height()) / 2);
    m_nonFiniteIndicator->setVisible(indicatorSpace > 0);
    m_pixelLabel->setGeometry(left + summary + indicatorSpace + gap, 0, pixels, rowHeight);
    m_summaryLabel->setVisible(summary > 0);
    m_pixelLabel->setVisible(pixels > 0);
    m_footer->setToolTip(detail);
}
