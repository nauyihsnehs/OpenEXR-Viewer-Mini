#include "LoadProgressWidget.h"
#include "ImageFileWidget.h"
#include <QEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

LoadProgressWidget::LoadProgressWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("loadProgressOverlay");
    setAttribute(Qt::WA_StyledBackground);
    setAutoFillBackground(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    m_label = new QLabel(this);
    m_label->setTextFormat(Qt::PlainText);
    m_label->setWordWrap(true);
    m_bar = new QProgressBar(this);
    m_bar->setObjectName("loadProgressBar");
    m_bar->setAccessibleName(tr("Current loading stage progress"));
    m_bar->setRange(0, 0);
    m_error = new QPlainTextEdit(this);
    m_error->setObjectName("loadErrorDetails");
    m_error->setReadOnly(true);
    m_error->setAccessibleName(tr("Loading error details"));
    layout->addWidget(m_label);
    layout->addWidget(m_bar);
    layout->addWidget(m_error);
    m_error->hide();
    parent->installEventFilter(this);
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this] { sync(); });
    m_idleCheck = new QTimer(this);
    m_idleCheck->setSingleShot(true);
    m_idleCheck->setInterval(0);
    connect(m_idleCheck, &QTimer::timeout, this, [this] {
        LoadProgress::Snapshot snapshot;
        if (!m_document || !m_document->loadingProgress(snapshot)) m_wait.invalidate();
        sync();
    });
    hide();
}

void LoadProgressWidget::resetDelay()
{
    m_idleCheck->stop();
    m_wait.invalidate();
    hide();
}

void LoadProgressWidget::setDocument(ImageFileWidget* document)
{
    if (m_document == document) { sync(); return; }
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    resetDelay();
    m_document = document;
    if (document) {
        connect(document, &ImageFileWidget::activeFramebufferChanged, this, [this] { sync(); });
        connect(document, &ImageFileWidget::documentReady, this, [this] { resetDelay(); sync(); });
        connect(document, &ImageFileWidget::documentLoadFailed, this, [this] { sync(); });
        connect(document, &ImageFileWidget::refreshInProgressChanged, this, [this](bool refreshing) {
            if (!refreshing) resetDelay();
            sync();
        });
        connect(document, &QObject::destroyed, this, [this] {
            m_document.clear();
            resetDelay();
            m_timer->stop();
        });
    }
    if (document) m_timer->start(); else m_timer->stop();
    sync();
}

void LoadProgressWidget::sync()
{
    if (!m_document) { resetDelay(); m_timer->stop(); return; }
    LoadProgress::Snapshot snapshot;
    const QString error = m_document->loadingError();
    const bool busy = m_document->loadingProgress(snapshot);
    if (!busy && error.isEmpty()) {
        hide();
        // Header/decode/render handoffs can briefly report idle in the same
        // signal stack. Confirm idle next turn before forgetting elapsed time.
        if (m_wait.isValid() && !m_idleCheck->isActive()) m_idleCheck->start();
        return;
    }
    m_idleCheck->stop();
    if (!error.isEmpty()) m_wait.invalidate();
    else {
        if (!m_wait.isValid()) m_wait.start();
        if (m_wait.elapsed() < 1000) { hide(); return; }
    }
    QString label;
    if (!error.isEmpty()) label = tr("Load failed");
    else switch (snapshot.phase) {
        case LoadProgress::Opening: label = tr("Opening…"); break;
        case LoadProgress::Decoding: label = tr("Decoding…"); break;
        case LoadProgress::Processing: label = tr("Processing…"); break;
        case LoadProgress::Rendering: label = tr("Rendering…"); break;
    }
    // Distinguish successive processing steps instead of implying a total %.
    if (error.isEmpty() && !snapshot.step.isEmpty()) label += " " + snapshot.step;
    if (error.isEmpty() && !snapshot.context.isEmpty()) label += " (" + snapshot.context + ")";
    m_label->setText(label);
    m_bar->setAccessibleDescription(label);
    setAccessibleName(label);
    setToolTip(error.isEmpty() ? tr("Progress for the current stage only.") : error);
    m_bar->setVisible(error.isEmpty());
    m_error->setVisible(!error.isEmpty());
    if (m_error->toPlainText() != error) m_error->setPlainText(error);
    if (snapshot.total && error.isEmpty()) {
        m_bar->setRange(0, 100);
        m_bar->setValue(int(100. * double(std::min(snapshot.completed, snapshot.total)) / double(snapshot.total)));
    } else m_bar->setRange(0, 0);
    place(); show(); raise();
}

void LoadProgressWidget::place()
{
    const auto area = parentWidget()->rect();
    const int w = std::max(0, std::min(480, area.width() - 24));
    const int h = std::max(0, std::min(m_error->isHidden() ? 96 : 220, area.height() - 24));
    setGeometry((area.width() - w) / 2, (area.height() - h) / 2, w, h);
}

bool LoadProgressWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        place();
    return QWidget::eventFilter(watched, event);
}
