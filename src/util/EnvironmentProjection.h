#pragma once

#include <model/framebuffer/FramebufferData.h>
#include <QImage>
#include <QSize>
#include <QRegion>
#include <functional>

namespace EnvironmentProjection {
enum Type { Source = -1, LatLong, Cube, Perspective, Sphere };
struct State {
    Type type = Source;
    double yaw = 0., pitch = 0., fieldOfView = 90.; // Degrees, +Z forward / +Y up.
    bool operator==(const State& other) const {
        return type == other.type && yaw == other.yaw && pitch == other.pitch
               && fieldOfView == other.fieldOfView;
    }
};
struct SourceInfo {
    int envmap = -1;
    QSize size;
    QString unavailableReason;
    bool available() const { return envmap >= 0 && unavailableReason.isEmpty(); }
};
using ColorMapper = std::function<QImage(const FramebufferData&, const Cancellation&)>;
// Immutable render inputs; export snapshots use the committed state and complete flag.
struct Snapshot {
    std::shared_ptr<const FramebufferData> source;
    State state;
    int stride = 0;
    std::vector<std::string> names;
    std::vector<int> components;
    ColorMapper mapColors;
    bool markers = false;
    bool complete = true;
    // Transient rendering hints, never persisted in PreviewState or export snapshots.
    bool interactive = false;
    std::shared_ptr<const FramebufferData> cachedProjection;
    State cachedState;
};
QString name(Type type);
SourceInfo describe(const FramebufferData& source);
State resolve(State state, const FramebufferData& source);
QSize defaultSize(const FramebufferData& source, Type type);
void validateSize(QSize size, Type type);
QRegion coverage(const FramebufferData& frame);
// Null means outside the sphere. Source coordinates include the source window offset.
bool sourcePosition(const FramebufferData& source, State state, QSize size,
                    int x, int y, QPointF& position);
std::shared_ptr<const FramebufferData> project(const Snapshot& snapshot, State state,
                                             QSize size, const Cancellation& cancel, int threads = 1);
}
