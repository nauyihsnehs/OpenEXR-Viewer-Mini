#pragma once

#include <QPointer>
#include <QToolButton>
#include <model/framebuffer/FramebufferModel.h>

class NonFiniteIndicator : public QToolButton
{
    Q_OBJECT

  public:
    explicit NonFiniteIndicator(QWidget* parent = nullptr);
    void setModel(FramebufferModel* model);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    void refresh();
    enum State { Unknown, Finite, NonFinite };
    State m_state = Unknown;
    QPointer<FramebufferModel> m_model;
};
