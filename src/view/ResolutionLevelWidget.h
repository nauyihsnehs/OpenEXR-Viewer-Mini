#pragma once
#include <QWidget>
#include <QPointer>
#include <util/ResolutionLevel.h>
#include <vector>

class ImageFileWidget;
class QLabel;
class QSlider;

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
    void submit();
    QPointer<ImageFileWidget> m_document;
    QSlider* m_x;
    QSlider* m_y;
    QLabel* m_xLabel;
    QLabel* m_yLabel;
    QLabel* m_status;
    std::vector<int> m_xLevels, m_yLevels;
    bool m_diagonal = true;
    ResolutionLevel m_target;
};
