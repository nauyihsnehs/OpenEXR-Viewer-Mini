#include "MinimalImageWidget.h"
#include "GraphicsView.h"
#include "CropIndicator.h"
#include "DepthRangeWidget.h"

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
    items->addWidget(m_view, 1);
    m_footer = new QWidget(this);
    m_footer->setObjectName("minimalImageFooter");
    m_footer->setFixedHeight(fontMetrics().height() + 12);
    m_footer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_cropIndicator = new CropIndicator(m_footer);
    m_summaryLabel = new QLabel(m_footer);
    m_summaryLabel->setObjectName("minimalImageInfo");
    m_summaryLabel->setTextFormat(Qt::PlainText);
    m_pixelLabel = new QLabel(m_footer);
    m_pixelLabel->setObjectName("minimalPixelInfo");
    m_pixelLabel->setTextFormat(Qt::PlainText);
    m_pixelLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_depthRange = new DepthRangeWidget(m_footer);
    items->addWidget(m_footer);
}

int MinimalImageWidget::footerHeight() const
{
    return m_footer->height();
}

void MinimalImageWidget::setSummary(const QString& text, const FramebufferModel* model)
{
    m_cropIndicator->setModel(model);
    // The preview owns a mutable model; the minimal window edits the same range.
    m_depthRange->setModel(const_cast<FramebufferModel*>(model));
    m_footer->setFixedHeight(fontMetrics().height() + 12
      + (model && model->hasDeepSamples() ? m_depthRange->sizeHint().height() : 0));
    m_summary = text;
    m_summaryLabel->setToolTip(text);
    updateSummary();
}

void MinimalImageWidget::setPixelInfo(const QString& text)
{
    m_pixelInfo = text;
    m_pixelLabel->setToolTip(text);
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
    m_depthRange->setGeometry(0, rowHeight, width(), qMax(0, footerHeight() - rowHeight));
    const int left = m_cropIndicator->isHidden() ? 8 : 32;
    m_cropIndicator->move(8, (rowHeight - m_cropIndicator->height()) / 2);
    const int available = qMax(0, width() - left - 8);
    const int pixels = m_pixelInfo.isEmpty() ? 0 : qMin(
      available, m_pixelLabel->fontMetrics().boundingRect(m_pixelInfo).width() + 2);
    const int gap = pixels > 0 && pixels < available ? qMin(8, available - pixels) : 0;
    const int summary = available - pixels - gap;
    m_summaryLabel->setGeometry(left, 0, summary, rowHeight);
    m_pixelLabel->setGeometry(left + summary + gap, 0, pixels, rowHeight);
    m_summaryLabel->setVisible(summary > 0);
    m_pixelLabel->setVisible(pixels > 0);
    m_summaryLabel->setText(m_summaryLabel->fontMetrics().elidedText(
      m_summary, Qt::ElideRight, summary));
    m_pixelLabel->setText(m_pixelLabel->fontMetrics().elidedText(
      m_pixelInfo, Qt::ElideRight, pixels));
    m_footer->setToolTip(m_pixelInfo.isEmpty() ? m_summary : m_summary + "\n" + m_pixelInfo);
    m_pixelLabel->setToolTip(m_footer->toolTip());
}
