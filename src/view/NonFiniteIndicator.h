#pragma once

#include <QPointer>
#include <QWidget>
#include <model/framebuffer/FramebufferModel.h>

class NonFiniteIndicator : public QWidget
{
    Q_OBJECT

  public:
    explicit NonFiniteIndicator(QWidget* parent = nullptr);
    void setModel(const FramebufferModel* model);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    void refresh();
    enum State { Unknown, Finite, NonFinite };
    State m_state = Unknown;
    QPointer<const FramebufferModel> m_model;
};
