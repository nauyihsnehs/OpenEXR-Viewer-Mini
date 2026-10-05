#pragma once

#include <QPersistentModelIndex>
#include <QStringList>
#include <QWidget>

class OpenEXRImage;
class InspectorLayerFilter;
class InspectorAttributeFilter;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QScrollArea;
class QSplitter;
class QToolButton;
class QTreeView;

// Inspecting headers never requests pixels. Activation is a separate action.
class InspectorWidget : public QWidget
{
    Q_OBJECT
  public:
    struct NavigationState {
        QString selectedObject; // Empty means the file summary.
        QString query;
        QStringList expandedObjects;
    };

    explicit InspectorWidget(QWidget* parent = nullptr);
    void setDocument(OpenEXRImage* source);
    void inspectLayer(const QModelIndex& sourceIndex);
    void setActivePreviews(const QStringList& keys, bool followSelection = false);
    NavigationState navigationState() const;
    void restoreNavigationState(const NavigationState& state);
    QByteArray splitterState() const;
    void restoreSplitterState(const QByteArray& state);

  signals:
    void layerActivated(const QModelIndex& sourceIndex);
    void closeRequested();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void inspectFile();
    void updateDetails();
    void activate(const QModelIndex& proxyIndex);
    void filterLayers(const QString& query);
    void filterAttributes(const QString& query);
    void showAttributeValue(const QModelIndex& proxyIndex);
    QStringList expandedObjects() const;
    void restoreExpandedObjects(const QStringList& keys);

    OpenEXRImage* m_source = nullptr;
    bool m_binding = false;
    bool m_searching = false;
    QStringList m_unfilteredExpansion;
    QPersistentModelIndex m_inspected;
    QPersistentModelIndex m_attributeRoot;
    InspectorLayerFilter* m_layers;
    InspectorAttributeFilter* m_attributes;
    QSplitter* m_splitter;
    QToolButton* m_file;
    QLabel* m_fileSummary;
    QLineEdit* m_layerSearch;
    QTreeView* m_navigation;
    QLabel* m_noLayers;
    QScrollArea* m_detailsScroll;
    QWidget* m_summary;
    QToolButton* m_partButton;
    QWidget* m_partSummary;
    QToolButton* m_attributesButton;
    QWidget* m_attributesBody;
    QLineEdit* m_attributeSearch;
    QTreeView* m_attributeTree;
    QLabel* m_noAttributes;
    QWidget* m_attributeTitleRow;
    QLabel* m_attributeTitle;
    QToolButton* m_copyAttribute;
    QPlainTextEdit* m_attributeValue;
};
