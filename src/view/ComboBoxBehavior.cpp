#include "ComboBoxBehavior.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QList>
#include <QObject>
#include <QSizePolicy>
#include <QVariant>
#include <QWidget>

namespace
{
    static const char* s_installedProperty = "openexrViewerComboBehaviorInstalled";
    static const int s_defaultMaximumWidth = 200;


    class ComboBoxWheelFilter: public QObject
    {
      public:
        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (event->type() != QEvent::Wheel) {
                return QObject::eventFilter(watched, event);
            }

            QComboBox* combo = qobject_cast<QComboBox*>(watched);
            if (!combo) return QObject::eventFilter(watched, event);

            QAbstractItemView* view = combo->view();
            if (view && view->isVisible()) {
                return QObject::eventFilter(watched, event);
            }

            event->accept();
            return true;
        }
    };


    ComboBoxWheelFilter* wheelFilter()
    {
        static ComboBoxWheelFilter filter;
        return &filter;
    }


    void applyComboBoxBehavior(QComboBox* combo)
    {
        if (!combo) return;

        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        combo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

        if (combo->maximumWidth() == QWIDGETSIZE_MAX) {
            combo->setMaximumWidth(s_defaultMaximumWidth);
        }

        if (combo->property(s_installedProperty).toBool()) return;

        combo->installEventFilter(wheelFilter());
        combo->setProperty(s_installedProperty, true);
    }
}


void applyComboBoxBehavior(QWidget* root)
{
    if (!root) return;

    applyComboBoxBehavior(qobject_cast<QComboBox*>(root));

    const QList<QComboBox*> combos = root->findChildren<QComboBox*>();
    for (QComboBox* combo : combos) {
        applyComboBoxBehavior(combo);
    }
}
