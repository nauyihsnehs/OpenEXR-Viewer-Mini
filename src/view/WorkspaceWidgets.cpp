#include "WorkspaceWidgets.h"
#include "ViewerIcons.h"

#include <QAction>
#include <QBoxLayout>
#include <QEvent>
#include <QFocusEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QKeySequence>
#include <QPainter>
#include <QPalette>
#include <QPolygonF>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QResizeEvent>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QVariantAnimation>

namespace {

class WelcomeMark : public QWidget
{
  public:
    explicit WelcomeMark(QWidget* parent) : QWidget(parent)
    {
        setFixedSize(64, 64);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

  protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
        painter.drawRoundedRect(QRectF(8, 10, 48, 44), 6, 6);
        painter.drawEllipse(QPointF(23, 24), 4, 4);
        QPolygonF mountains;
        mountains << QPointF(13, 46) << QPointF(28, 32)
                  << QPointF(36, 39) << QPointF(43, 30) << QPointF(52, 42);
        painter.drawPolyline(mountains);
    }
};

class WelcomePage : public QWidget
{
  public:
    WelcomePage(QAction* openAction, QWidget* parent) : QWidget(parent)
    {
        setObjectName("welcomePage");
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 24, 24, 24);
        layout->addStretch();
        auto* content = new QWidget(this);
        content->setObjectName("welcomeContent");
        auto* items = new QVBoxLayout(content);
        items->setSpacing(16);
        items->addWidget(new WelcomeMark(content), 0, Qt::AlignHCenter);
        auto* title = new QLabel(tr("OpenEXR Viewer"), content);
        title->setObjectName("welcomeTitle");
        items->addWidget(title, 0, Qt::AlignHCenter);
        auto* hint = new QLabel(tr("Drop EXR files here"), content);
        hint->setObjectName("welcomeHint");
        hint->setWordWrap(true);
        hint->setAlignment(Qt::AlignCenter);
        items->addWidget(hint);
        auto* open = new QPushButton(content);
        open->setObjectName("welcomeOpenButton");
        ViewerIcons::setupButton(open, ViewerIcons::Open, tr("Open Image"),
                                 openAction->toolTip(), 40, 22);
        open->setCursor(Qt::PointingHandCursor);
        connect(open, &QPushButton::clicked, openAction, &QAction::trigger);
        items->addWidget(open, 0, Qt::AlignHCenter);
        auto* shortcut = new QLabel(openAction->shortcut().toString(QKeySequence::NativeText), content);
        shortcut->setObjectName("welcomeHint");
        shortcut->setWordWrap(true);
        shortcut->setAlignment(Qt::AlignCenter);
        items->addWidget(shortcut);
        layout->addWidget(content, 0, Qt::AlignHCenter);
        layout->addStretch();

        auto* opacity = new QGraphicsOpacityEffect(content);
        content->setGraphicsEffect(opacity);
        m_fade = new QPropertyAnimation(opacity, "opacity", this);
        m_fade->setDuration(180);
        m_fade->setStartValue(0.);
        m_fade->setEndValue(1.);
        m_fade->setEasingCurve(QEasingCurve::OutCubic);
    }

  protected:
    void showEvent(QShowEvent*) override { m_fade->start(); }
    void hideEvent(QHideEvent*) override { m_fade->stop(); }

  private:
    QPropertyAnimation* m_fade;
};

class ButtonHover : public QObject
{
  public:
    explicit ButtonHover(QToolButton* button) : QObject(button), m_button(button)
    {
        m_animation = new QVariantAnimation(this);
        m_animation->setDuration(120);
        m_animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_animation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& value) {
                    m_amount = value.toReal();
                    updateColor();
                });
        button->installEventFilter(this);
        button->setProperty("keyboardFocus", false);
        updateColor();
    }

  protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        switch (event->type()) {
            case QEvent::FocusIn: {
                const auto reason = static_cast<QFocusEvent*>(event)->reason();
                if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason
                    || reason == Qt::ShortcutFocusReason)
                    setKeyboardFocus(true);
                else if (reason != Qt::PopupFocusReason
                         && reason != Qt::ActiveWindowFocusReason)
                    setKeyboardFocus(false);
                break;
            }
            case QEvent::MouseButtonPress:
                setKeyboardFocus(false);
                break;
            case QEvent::Enter:
            case QEvent::Leave:
                m_animation->stop();
                m_animation->setStartValue(m_amount);
                m_animation->setEndValue(
                  event->type() == QEvent::Enter && m_button->isEnabled() ? 1. : 0.);
                m_animation->start();
                break;
            case QEvent::Hide:
            case QEvent::EnabledChange:
                m_animation->stop();
                m_amount = 0.;
                updateColor();
                break;
            default:
                break;
        }
        return false;
    }

  private:
    void setKeyboardFocus(bool visible)
    {
        if (m_button->property("keyboardFocus").toBool() == visible) return;
        m_button->setProperty("keyboardFocus", visible);
        m_button->style()->unpolish(m_button);
        m_button->style()->polish(m_button);
        m_button->update();
    }

    void updateColor()
    {
        // Alpha interpolation works in both themes without copying theme colors.
        const int alpha = qRound(26 * m_amount);
        m_button->setStyleSheet(QStringLiteral(
          "QToolButton { background-color: rgba(115,150,190,%1); }"
          "QToolButton:checked, QToolButton:pressed {"
          " background-color: rgba(115,150,190,48); }"
          "QToolButton:disabled { background-color: transparent; }").arg(alpha));
    }

    QToolButton* m_button;
    QVariantAnimation* m_animation;
    qreal m_amount = 0.;
};

class PreviewControls : public QScrollArea
{
  public:
    PreviewControls(QLayout* controls, QWidget* parent) : QScrollArea(parent)
    {
        setObjectName("previewControlsScroll");
        viewport()->setObjectName("previewControlsViewport");
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        auto* content = new QWidget;
        content->setObjectName("previewControls");
        controls->setContentsMargins(8, 4, 8, 4);
        controls->setSpacing(8);
        controls->setSizeConstraint(QLayout::SetMinAndMaxSize);
        content->setLayout(controls);
        setWidget(content);
        content->installEventFilter(this);
        connect(horizontalScrollBar(), &QScrollBar::rangeChanged, this,
                [this](int, int) { updateHeight(); });
        updateHeight();
    }

  protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QScrollArea::resizeEvent(event);
        updateHeight();
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == widget() && event->type() == QEvent::LayoutRequest)
            updateHeight();
        return QScrollArea::eventFilter(watched, event);
    }

  private:
    void updateHeight()
    {
        if (!widget()) return;
        const bool overflow = horizontalScrollBar()->maximum()
                              > horizontalScrollBar()->minimum();
        const int height = widget()->sizeHint().height()
                           + (overflow ? horizontalScrollBar()->sizeHint().height() : 0);
        if (minimumHeight() != height || maximumHeight() != height)
            setFixedHeight(height);
    }
};

} // namespace

QWidget* createWelcomePage(QAction* openAction, QWidget* parent)
{
    return new WelcomePage(openAction, parent);
}

void animateToolbarButtons(QToolBar* toolbar)
{
    for (QAction* action : toolbar->actions()) {
        auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(action));
        if (!button) continue;
        // Keep keyboard activation after clicks, but only paint a keyboard focus ring.
        button->setFocusPolicy(Qt::StrongFocus);
        new ButtonHover(button);
    }
}

void wrapPreviewControls(QBoxLayout* layout)
{
    QLayoutItem* item = layout->takeAt(0);
    QLayout* controls = item->layout();
    // A layout is its own QLayoutItem; ownership moves to the scroll content.
    controls->setParent(nullptr);
    layout->insertWidget(0, new PreviewControls(controls, layout->parentWidget()));
    layout->setContentsMargins(0, 0, 0, 8);
    layout->setSpacing(0);
}
