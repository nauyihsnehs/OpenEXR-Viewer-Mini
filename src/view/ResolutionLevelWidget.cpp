#include "ResolutionLevelWidget.h"
#include "ImageFileWidget.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <algorithm>

namespace {
class LevelSlider : public QSlider {
public:
    explicit LevelSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {
        setRange(0, 0);
        setSingleStep(1); setPageStep(1);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumWidth(70); setMaximumWidth(160);
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
    m_status->setMinimumWidth(0);
    row->addWidget(m_xLabel); row->addWidget(m_x); row->addWidget(m_yLabel);
    row->addWidget(m_y); row->addWidget(m_status);
    for (auto* slider : {m_x, m_y}) {
        slider->setToolTip(tr("Stored resolution level: 0 is full resolution. Drag to choose; release to load. Use arrow keys or Home/End for discrete selection."));
        connect(slider, &QSlider::valueChanged, this, [this, slider] {
            previewTarget();
            if (!slider->isSliderDown()) submit();
        });
        connect(slider, &QSlider::sliderReleased, this, &ResolutionLevelWidget::submit);
    }
    hide();
}

void ResolutionLevelWidget::setDocument(ImageFileWidget* document)
{
    if (m_document != document) {
        if (m_document) disconnect(m_document, nullptr, this, nullptr);
        m_document = document;
        if (document) {
            connect(document, &ImageFileWidget::activeFramebufferChanged, this, &ResolutionLevelWidget::sync);
            connect(document, &ImageFileWidget::documentReady, this, &ResolutionLevelWidget::sync);
            connect(document, &ImageFileWidget::refreshInProgressChanged, this, &ResolutionLevelWidget::sync);
            connect(document, &QObject::destroyed, this, [this] { m_document.clear(); sync(); });
        }
    }
    sync();
}

void ResolutionLevelWidget::sync()
{
    const bool available = m_document && m_document->isDocumentReady()
      && m_document->resolutionLevels().size() > 1;
    setVisible(available);
    const bool loading = m_document && m_document->isRefreshInProgress();
    setEnabled(available && !loading);
    if (!available) { emit presentationChanged(); return; }
    if (!loading && (m_x->isSliderDown() || m_y->isSliderDown())) return;
    const auto& levels = m_document->resolutionLevels();
    const auto current = m_document->resolutionLevel();
    m_diagonal = std::all_of(levels.begin(), levels.end(), [](ResolutionLevel l) { return l.x == l.y; });
    m_xLevels.clear(); m_yLevels.clear();
    for (const auto level : levels) {
        if (m_diagonal || level.y == current.y) m_xLevels.push_back(level.x);
        if (!m_diagonal && level.x == current.x) m_yLevels.push_back(level.y);
    }
    setLevels(m_x, m_xLevels, current.x); setLevels(m_y, m_yLevels, current.y);
    m_xLabel->setText(m_diagonal ? tr("Level") : tr("X"));
    m_x->setAccessibleName(m_diagonal ? tr("Resolution level") : tr("X resolution level"));
    m_y->setAccessibleName(tr("Y resolution level"));
    m_yLabel->setVisible(!m_diagonal); m_y->setVisible(!m_diagonal);
    m_target = current;
    QString text = m_document->resolutionLevelLabel(current);
    if (loading) text += tr(" → Loading %1").arg(m_document->resolutionLevelLabel(m_document->requestedResolutionLevel()));
    m_status->setText(text); m_status->setToolTip(text);
    emit presentationChanged();
}

void ResolutionLevelWidget::previewTarget()
{
    if (!m_document || m_xLevels.empty()) return;
    m_target.x = m_xLevels[size_t(m_x->value())];
    m_target.y = m_diagonal ? m_target.x : m_yLevels[size_t(m_y->value())];
    const QString text = tr("Target %1").arg(m_document->resolutionLevelLabel(m_target));
    m_status->setText(text); m_status->setToolTip(text);
}

void ResolutionLevelWidget::submit()
{
    if (!m_document || m_document->isRefreshInProgress()) return;
    if (m_target == m_document->resolutionLevel()) { sync(); return; }
    m_document->setResolutionLevel(m_target);
    // Includes synchronous validation failure or a request which became invalid.
    sync();
}
