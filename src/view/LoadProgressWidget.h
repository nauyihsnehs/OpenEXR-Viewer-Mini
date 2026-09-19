#pragma once

#include <QPointer>
#include <QElapsedTimer>
#include <QWidget>

class ImageFileWidget;
class QLabel;
class QProgressBar;
class QPlainTextEdit;
class QTimer;

// A small overlay; the same document state drives complete and minimal views.
class LoadProgressWidget : public QWidget
{
  public:
    explicit LoadProgressWidget(QWidget* parent);
    void setDocument(ImageFileWidget* document);
  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
  private:
    void sync();
    void place();
    void resetDelay();
    QPointer<ImageFileWidget> m_document;
    QLabel* m_label;
    QProgressBar* m_bar;
    QPlainTextEdit* m_error;
    QTimer* m_timer;
    QTimer* m_idleCheck;
    QElapsedTimer m_wait;
};
