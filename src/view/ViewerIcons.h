#pragma once

#include <QIcon>
#include <QString>

class QAbstractButton;

namespace ViewerIcons {
enum Kind { Open, Export, Exposure, ToneMapping, FalseColor, Layers, Attributes,
            AutoRange, ColorScale, Hdr };

QIcon icon(Kind kind);
void setupButton(QAbstractButton* button, Kind kind, const QString& name,
                 const QString& toolTip, int buttonSize = 28, int iconSize = 16);
}
