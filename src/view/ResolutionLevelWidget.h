#pragma once
#include <QWidget>
#include <QPointer>
#include <util/ResolutionLevel.h>
#include <vector>

class ImageFileWidget;
class QLabel;
class QSlider;
class QTimer;

// Selects stored levels of the document, never a display scaling factor.
class ResolutionLevelWidget : public QWidget {
    Q_OBJECT
public:
    explicit ResolutionLevelWidget(QWidget* parent = nullptr);
    void setDocument(ImageFileWidget* document);
signals:
    void presentationChanged();
private:
    void sync();
    void previewTarget();
    void cancelPendingInput();
    void submit();
    void setStatus(const QString& text, const QString& detail);
    QPointer<ImageFileWidget> m_document;
    QSlider* m_x;
    QSlider* m_y;
    QLabel* m_xLabel;
    QLabel* m_yLabel;
    QLabel* m_status;
    QLabel* m_loading;
    QTimer* m_submitTimer;
    QPointer<QWidget> m_restoreFocus, m_disabledFocus;
    std::vector<int> m_xLevels, m_yLevels;
    bool m_diagonal = true;
    bool m_wasLoading = false;
    ResolutionLevel m_target;
};
