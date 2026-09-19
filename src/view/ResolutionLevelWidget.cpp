#include "ResolutionLevelWidget.h"
#include "ImageFileWidget.h"
#include <QHBoxLayout>
#include <QApplication>
#include <QLabel>
#include <QSlider>
#include <QSignalBlocker>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>

namespace {
class LevelSlider : public QSlider {
public:
    explicit LevelSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {
        setRange(0, 0);
        setSingleStep(1); setPageStep(1);
        setFocusPolicy(Qt::StrongFocus);
        setFixedWidth(112);
    }
protected:
    // A wheel over this control must not bubble into footer/image zoom.
    void wheelEvent(QWheelEvent* event) override { event->accept(); }
};
void setLevels(QSlider* slider, const std::vector<int>& levels, int current) {
    const QSignalBlocker block(slider);
    slider->setRange(0, std::max(0, int(levels.size()) - 1));
    const auto it = std::find(levels.begin(), levels.end(), current);
    slider->setValue(it == levels.end() ? 0 : int(it - levels.begin()));
}
}

ResolutionLevelWidget::ResolutionLevelWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("resolutionLevelControls");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0); row->setSpacing(6);
    m_xLabel = new QLabel(tr("Level"), this);
    m_yLabel = new QLabel(tr("Y"), this);
    m_x = new LevelSlider(this); m_y = new LevelSlider(this);
    m_x->setObjectName("resolutionXSlider"); m_y->setObjectName("resolutionYSlider");
    m_status = new QLabel(this); m_status->setObjectName("resolutionLevelStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setFixedWidth(168);
    m_loading = new QLabel(this);
    m_loading->setObjectName("resolutionLevelLoading");
    m_loading->setFixedWidth(14);
    m_loading->setAlignment(Qt::AlignCenter);
    row->addWidget(m_xLabel); row->addWidget(m_x); row->addWidget(m_yLabel);
    row->addWidget(m_y); row->addWidget(m_status); row->addWidget(m_loading);
    m_submitTimer = new QTimer(this);
    m_submitTimer->setSingleShot(true);
    m_submitTimer->setInterval(0);
    connect(m_submitTimer, &QTimer::timeout, this, &ResolutionLevelWidget::submit);
    for (auto* slider : {m_x, m_y}) {
        slider->setToolTip(tr("Stored resolution level: 0 is full resolution. Drag to choose; release to load. Use arrow keys or Home/End for discrete selection."));
        connect(slider, &QSlider::valueChanged, this, [this, slider] {
            previewTarget();
            // QSlider can emit valueChanged before finishing mousePressEvent.
            // Wait until it has established its pressed state before loading.
            if (!slider->isSliderDown()) m_submitTimer->start();
        });
        connect(slider, &QSlider::sliderReleased, this, [this] { m_submitTimer->start(); });
    }
    hide();
}

void ResolutionLevelWidget::setDocument(ImageFileWidget* document)
{
    if (m_document != document) {
        cancelPendingInput();
        m_restoreFocus.clear(); m_disabledFocus.clear();
        m_wasLoading = false;
        if (m_document) disconnect(m_document, nullptr, this, nullptr);
        m_document = document;
        if (document) {
            connect(document, &ImageFileWidget::activeFramebufferChanged, this, &ResolutionLevelWidget::sync);
            connect(document, &ImageFileWidget::documentReady, this, [this] { cancelPendingInput(); sync(); });
            connect(document, &ImageFileWidget::refreshInProgressChanged, this, [this] {
                // Also invalidates queued input when a menu request supersedes it,
                // or when selecting the committed level cancels a transaction.
                cancelPendingInput();
                sync();
            });
            connect(document, &QObject::destroyed, this, [this] { cancelPendingInput(); m_document.clear(); sync(); });
        }
    }
    sync();
}

void ResolutionLevelWidget::sync()
{
    const QSignalBlocker blockX(m_x), blockY(m_y);
    const bool available = m_document && m_document->isDocumentReady()
      && m_document->resolutionLevels().size() > 1;
    const bool loading = m_document && m_document->isRefreshInProgress();
    if (!available || loading || m_wasLoading) cancelPendingInput();
    m_wasLoading = loading;
    setVisible(available);
    setEnabled(available);
    m_loading->setText(loading ? QStringLiteral("…") : QString());
    if (!available) {
        m_x->setEnabled(false); m_y->setEnabled(false);
        emit presentationChanged();
        return;
    }
    const bool preserveTarget = !loading
      && (m_x->isSliderDown() || m_y->isSliderDown() || m_submitTimer->isActive());
    const auto& levels = m_document->resolutionLevels();
    const auto current = m_document->resolutionLevel();
    const auto selected = loading ? m_document->requestedResolutionLevel() : preserveTarget ? m_target : current;
    if (!preserveTarget) {
        m_diagonal = std::all_of(levels.begin(), levels.end(), [](ResolutionLevel l) { return l.x == l.y; });
        m_xLevels.clear(); m_yLevels.clear();
        for (const auto level : levels) {
            if (m_diagonal || level.y == selected.y) m_xLevels.push_back(level.x);
            if (!m_diagonal && level.x == selected.x) m_yLevels.push_back(level.y);
        }
        setLevels(m_x, m_xLevels, selected.x); setLevels(m_y, m_yLevels, selected.y);
    }
    m_xLabel->setText(m_diagonal ? tr("Level") : tr("X"));
    m_x->setAccessibleName(m_diagonal ? tr("Resolution level") : tr("X resolution level"));
    m_y->setAccessibleName(tr("Y resolution level"));
    m_yLabel->setVisible(!m_diagonal); m_y->setVisible(!m_diagonal);
    m_xLabel->setFixedWidth(fontMetrics().boundingRect(tr("Level")).width());
    m_yLabel->setFixedWidth(fontMetrics().boundingRect(tr("Y")).width());
    setFixedWidth(layout()->sizeHint().width());
    const bool captureFocus = loading && !m_restoreFocus && (m_x->hasFocus() || m_y->hasFocus());
    if (captureFocus) m_restoreFocus = QApplication::focusWidget();
    m_x->setEnabled(!loading && m_xLevels.size() > 1);
    m_y->setEnabled(!loading && m_yLevels.size() > 1);
    if (captureFocus) m_disabledFocus = QApplication::focusWidget();
    if (!loading && m_restoreFocus) {
        if (!QApplication::focusWidget() || QApplication::focusWidget() == m_disabledFocus)
            m_restoreFocus->setFocus(Qt::OtherFocusReason);
        m_restoreFocus.clear(); m_disabledFocus.clear();
    }
    m_target = selected;
    const QString text = preserveTarget ? tr("Target %1").arg(m_document->resolutionLevelLabel(selected))
                                        : m_document->resolutionLevelLabel(selected);
    const QString detail = loading ? m_document->resolutionLevelLabel(current) + tr(" → Loading %1").arg(text) : text;
    m_loading->setToolTip(detail);
    setStatus(text, detail);
    emit presentationChanged();
}

void ResolutionLevelWidget::previewTarget()
{
    if (!m_document || m_document->isRefreshInProgress() || m_xLevels.empty()) return;
    m_target.x = m_xLevels[size_t(m_x->value())];
    m_target.y = m_diagonal ? m_target.x : m_yLevels[size_t(m_y->value())];
    const QString text = tr("Target %1").arg(m_document->resolutionLevelLabel(m_target));
    setStatus(text, text);
}

void ResolutionLevelWidget::setStatus(const QString& text, const QString& detail)
{
    m_status->setText(m_status->fontMetrics().elidedText(text, Qt::ElideRight, m_status->width()));
    m_status->setToolTip(detail);
}

void ResolutionLevelWidget::cancelPendingInput()
{
    m_submitTimer->stop();
    const QSignalBlocker blockX(m_x), blockY(m_y);
    m_x->setSliderDown(false); m_y->setSliderDown(false);
}

void ResolutionLevelWidget::submit()
{
    if (!m_document || !m_document->isDocumentReady() || m_document->isRefreshInProgress()) return;
    if (m_x->isSliderDown() || m_y->isSliderDown()) return; // Release will queue the final target.
    const auto& levels = m_document->resolutionLevels();
    if (m_target != m_document->resolutionLevel()
        && std::binary_search(levels.begin(), levels.end(), m_target))
        m_document->setResolutionLevel(m_target);
    // Includes synchronous validation failure or a request which became invalid.
    sync();
}
