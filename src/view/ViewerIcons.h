#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QAbstractButton;

namespace ViewerIcons {
enum Kind { Open, Export, Exposure, ToneMapping, FalseColor, Inspector,
            AutoRange, ColorScale, Hdr, File, Part, Layer, Group, Channel,
            Preview, Stereo, Search, Copy, Close };

// An explicit ink color lets custom delegates match their row palette.
// Otherwise the current application palette is read on every paint.
QIcon icon(Kind kind, const QColor& ink = QColor());
void setupButton(QAbstractButton* button, Kind kind, const QString& name,
                 const QString& toolTip, int buttonSize = 28, int iconSize = 16);
}
