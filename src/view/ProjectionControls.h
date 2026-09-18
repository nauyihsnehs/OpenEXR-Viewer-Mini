#pragma once
#include <QWidget>
#include <QPointer>

class FramebufferModel;
class QLabel;
class QToolButton;

// The same committed orientation and reset action in color, scalar and minimal views.
class ProjectionControls : public QWidget {
    Q_OBJECT
public:
    explicit ProjectionControls(QWidget* parent = nullptr);
    void setModel(const FramebufferModel* model);
private:
    void refresh();
    QPointer<const FramebufferModel> m_model;
    QLabel* m_angles;
    QToolButton* m_reset;
};
