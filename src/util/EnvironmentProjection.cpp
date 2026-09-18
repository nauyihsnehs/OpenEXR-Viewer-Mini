#include "EnvironmentProjection.h"
#include <model/framebuffer/PixelDiagnostics.h>
#include <OpenEXR/ImfEnvmap.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
constexpr double pi = 3.14159265358979323846;
Imath::Box2i window(QSize size) { return {{0, 0}, {size.width() - 1, size.height() - 1}}; }

// OpenEXR face coordinates are not strip coordinates: use its face rotations.
Imath::V2f inFace(Imf::CubeMapFace face, int n, double x, double y)
{
    switch (face) {
    case Imf::CUBEFACE_POS_X: return {float(n - 1 - y), float(x)};
    case Imf::CUBEFACE_NEG_X: return {float(n - 1 - y), float(n - 1 - x)};
    case Imf::CUBEFACE_POS_Y: return {float(x), float(n - 1 - y)};
    case Imf::CUBEFACE_NEG_Y: return {float(x), float(y)};
    case Imf::CUBEFACE_POS_Z: return {float(n - 1 - x), float(n - 1 - y)};
    default: return {float(x), float(n - 1 - y)};
    }
}

struct DirectionMapper {
    EnvironmentProjection::State state;
    QSize size;
    Imath::Box2i targetWindow;
    double dxScale, dyScale, lens, verticalLens, cp, sp, cy, sy;
    DirectionMapper(EnvironmentProjection::State parameters, QSize dimensions)
      : state(parameters), size(dimensions), targetWindow(window(dimensions)),
        dxScale(2. / dimensions.width()), dyScale(2. / dimensions.height()),
        lens(std::tan(parameters.fieldOfView * pi / 360.)),
        verticalLens(lens * dimensions.height() / dimensions.width()),
        cp(std::cos(parameters.pitch * pi / 180.)), sp(std::sin(parameters.pitch * pi / 180.)),
        cy(std::cos(parameters.yaw * pi / 180.)), sy(std::sin(parameters.yaw * pi / 180.)) {}
    bool operator()(int x, int y, Imath::V3f& dir) const {
        using namespace EnvironmentProjection;
        if (state.type == LatLong) {
            dir = Imf::LatLongMap::direction(targetWindow, Imath::V2f(float(x), float(y)));
            return true;
        }
        if (state.type == Cube) {
            const int n = size.width();
            const auto face = Imf::CubeMapFace(y / n);
            dir = Imf::CubeMap::direction(face, targetWindow, inFace(face, n, x, y % n));
            return true;
        }
        // Screen right is -X when looking toward +Z with +Y up (OpenEXR convention).
        double dx = 1. - dxScale * (x + .5);
        double dy = 1. - dyScale * (y + .5);
        double dz;
        if (state.type == Sphere) {
            const double r2 = dx * dx + dy * dy;
            if (r2 > 1.) return false;
            dz = std::sqrt(std::max(0., 1. - r2));
        } else {
            dx *= lens;
            dy *= verticalLens;
            dz = 1.;
        }
        const double py = dy * cp + dz * sp;
        const double pz = dz * cp - dy * sp;
        dir = Imath::V3f(float(dx * cy + pz * sy), float(py),
                         float(pz * cy - dx * sy));
        return true;
    }
};

Imath::V2f location(const FramebufferData& source, const Imath::V3f& dir)
{
    const auto dw = window(QSize(source.width, source.height));
    if (source.envmap == Imf::ENVMAP_LATLONG)
        return Imf::LatLongMap::pixelPosition(dw, dir);
    Imf::CubeMapFace face;
    Imath::V2f p;
    Imf::CubeMap::faceAndPixelPosition(dir, dw, face, p);
    return Imf::CubeMap::pixelPosition(face, dw, p);
}

// A bilinear footprint never crosses unrelated rows of the cube strip. Shared
// edge/corner samples are averaged across their adjoining faces, so both sides
// of a seam use the same endpoint. Zero-weight taps cannot propagate NaN/Inf.
struct Sampler {
    const EnvironmentProjection::Snapshot& snapshot;
    const FramebufferData& source;
    double values[4] = {};
    uint8_t flags = 0;
    bool classify;
    Sampler(const EnvironmentProjection::Snapshot& s, const FramebufferData& data, bool diagnostics)
      : snapshot(s), source(data), classify(diagnostics) {}

    void tap(int x, int y, double weight) {
        if (weight <= 0.) return;
        const size_t p = size_t(y) * source.width + x;
        const auto& raw = source.sourcePixels.empty() ? source.pixels : source.sourcePixels;
        for (int c = 0; c < snapshot.stride; ++c)
            values[c] += weight * source.pixels[p * snapshot.stride + c];
        if (classify) for (int c : snapshot.components)
            flags |= PixelDiagnostics::classify(raw[p * snapshot.stride + c]);
    }
    void endpoint(int x, int y, double weight) {
        if (weight <= 0.) return;
        if (source.envmap == Imf::ENVMAP_LATLONG) {
            // Poles and longitude endpoints represent repeated directions.
            if (source.height > 1 && (y == 0 || y == source.height - 1)) { tap(0, y, weight); return; }
            if (source.width > 1 && (x == 0 || x == source.width - 1)) {
                tap(0, y, weight * .5); tap(source.width - 1, y, weight * .5);
            } else tap(x, y, weight);
            return;
        }
        const int n = source.width, fy = y % n;
        if (n == 1 || (x > 0 && x < n - 1 && fy > 0 && fy < n - 1)) {
            tap(x, y, weight); return;
        }
        const auto face = Imf::CubeMapFace(y / n);
        const auto dw = window(QSize(n, 6 * n));
        const auto dir = Imf::CubeMap::direction(face, dw, inFace(face, n, x, fy));
        const int count = int(std::abs(dir.x) == 1.f) + int(std::abs(dir.y) == 1.f) + int(std::abs(dir.z) == 1.f);
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(dir[axis]) != 1.f) continue;
            const auto adjacent = Imf::CubeMapFace(2 * axis + (dir[axis] < 0.f ? 1 : 0));
            const Imath::V2f uv = axis == 0 ? Imath::V2f(dir.y, dir.z)
                                 : axis == 1 ? Imath::V2f(dir.x, dir.z) : Imath::V2f(dir.x, dir.y);
            const auto p = Imf::CubeMap::pixelPosition(adjacent, dw, (uv + Imath::V2f(1.f)) * (.5f * (n - 1)));
            tap(int(std::round(p.x)), int(std::round(p.y)), weight / count);
        }
    }
    void sample(const Imath::V3f& dir) {
        const auto p = location(source, dir);
        const double px = std::max(0., std::min(double(source.width - 1), double(p.x)));
        const double py = std::max(0., std::min(double(source.height - 1), double(p.y)));
        const int x = int(std::floor(px)), y = int(std::floor(py));
        const double fx = px - x, fy = py - y;
        const int right = std::min(x + 1, source.width - 1);
        const int bottom = source.envmap == Imf::ENVMAP_CUBE
          ? std::min(y + 1, (y / source.width + 1) * source.width - 1)
          : std::min(y + 1, source.height - 1);
        endpoint(x, y, (1. - fx) * (1. - fy));
        endpoint(right, y, fx * (1. - fy));
        endpoint(x, bottom, (1. - fx) * fy);
        endpoint(right, bottom, fx * fy);
    }
};
}

QString EnvironmentProjection::name(Type type)
{
    switch (type) {
    case LatLong: return "LatLong";
    case Cube: return "Cube";
    case Perspective: return "Pers-view";
    case Sphere: return "Sphere";
    default: return "Source";
    }
}

EnvironmentProjection::SourceInfo EnvironmentProjection::describe(const FramebufferData& source)
{
    SourceInfo info;
    info.envmap = source.envmap;
    info.size = QSize(source.width, source.height);
    if (source.envmap < 0 || source.envmap > 1 || !source.completeEnvironment
        || source.width <= 0 || source.height <= 0
        || source.hasDeep() || source.stereo[0])
        info.unavailableReason = "Projection requires a complete LatLong or Cube environment source.";
    else if (source.envmap == Cube && int64_t(source.height) != 6LL * source.width)
        info.unavailableReason = "This level has fewer than six complete square faces; native layout and original export only.";
    return info;
}

EnvironmentProjection::State EnvironmentProjection::resolve(State state, const FramebufferData& source)
{
    if (state.type == Source) state.type = Type(source.envmap);
    return state;
}

QSize EnvironmentProjection::defaultSize(const FramebufferData& source, Type type)
{
    const int n = source.envmap == Cube ? source.width : std::max(1, int(std::round(source.width / 4.)));
    if (n > std::numeric_limits<int>::max() / 6) throw std::runtime_error("Projection dimensions are too large.");
    if (type == LatLong) return QSize(4 * n, 2 * n);
    if (type == Cube) return QSize(n, 6 * n);
    return QSize(2 * n, 2 * n);
}

void EnvironmentProjection::validateSize(QSize size, Type type)
{
    const int64_t w = size.width(), h = size.height();
    if (w <= 0 || h <= 0 || w * h > std::numeric_limits<int>::max() / 16
        || (type == LatLong && w != 2 * h) || (type == Cube && h != 6 * w)
        || (type == Sphere && w != h) || type < LatLong || type > Sphere)
        throw std::runtime_error("Invalid or excessive projection output dimensions.");
}

bool EnvironmentProjection::sourcePosition(const FramebufferData& source, State state, QSize size,
                                            int x, int y, QPointF& position)
{
    Imath::V3f dir;
    if (!DirectionMapper(state, size)(x, y, dir)) return false;
    const auto p = location(source, dir);
    position = QPointF(p.x + source.dataWindow.x(), p.y + source.dataWindow.y());
    return true;
}

QRegion EnvironmentProjection::coverage(const FramebufferData& frame)
{
    if (frame.deepCoverage.empty()) return QRect(0, 0, frame.width, frame.height);
    QRegion covered;
    for (int y = 0; y < frame.height; ++y) {
        int first = 0;
        while (first < frame.width && !frame.covers(size_t(y) * frame.width + first)) ++first;
        int last = frame.width;
        while (last > first && !frame.covers(size_t(y) * frame.width + last - 1)) --last;
        if (last > first) covered += QRect(first, y, last - first, 1);
    }
    return covered;
}

std::shared_ptr<const FramebufferData> EnvironmentProjection::project(
  const Snapshot& snapshot, State state, QSize size, const Cancellation& cancel, int threads)
{
    if (!snapshot.source) throw std::runtime_error("No environment source.");
    if (cancel->load()) return {};
    const auto& source = *snapshot.source;
    const auto info = describe(source);
    if (!info.available()) throw std::runtime_error(info.unavailableReason.toStdString());
    state = resolve(state, source);
    validateSize(size, state.type);
    if (!std::isfinite(state.yaw) || !std::isfinite(state.pitch) || !std::isfinite(state.fieldOfView)
        || state.fieldOfView < 10. || state.fieldOfView > 150. || std::abs(state.pitch) > 90.)
        throw std::runtime_error("Invalid projection orientation or field of view.");
    if ((snapshot.stride != 1 && snapshot.stride != 4)
        || source.pixels.size() != size_t(source.width) * source.height * snapshot.stride
        || (!source.sourcePixels.empty() && source.sourcePixels.size() != source.pixels.size())
        || snapshot.names.empty() || snapshot.names.size() != snapshot.components.size()
        || (snapshot.stride == 1 && snapshot.names.size() != 1))
        throw std::runtime_error("Unsupported environment channel layout.");
    for (int component : snapshot.components)
        if (component < 0 || component >= snapshot.stride)
            throw std::runtime_error("Invalid environment channel component.");
    auto output = std::make_shared<FramebufferData>();
    output->width = size.width(); output->height = size.height();
    output->dataWindow = output->displayWindow = QRect(QPoint(), size);
    const size_t count = size_t(size.width()) * size.height();
    output->pixels.resize(count * snapshot.stride);
    const bool sphere = state.type == Sphere;
    if (sphere) output->deepCoverage.resize(count, 0);
    const bool diagnostics = source.nanCount || source.infCount || source.positiveInfCount || source.negativeInfCount;
    std::vector<uint8_t> flags(diagnostics ? count : 0, 0);
    const DirectionMapper direction(state, size);
    threads = std::max(1, std::min(4, threads));
    Q_UNUSED(threads);
#pragma omp parallel for num_threads(threads) if (count >= 65536)
    for (int y = 0; y < size.height(); ++y) {
        if (cancel->load()) continue;
        for (int x = 0; x < size.width(); ++x) {
            if ((x & 4095) == 0 && cancel->load()) break;
            Imath::V3f dir;
            if (!direction(x, y, dir)) continue;
            Sampler sampler(snapshot, source, diagnostics);
            sampler.sample(dir);
            const size_t p = size_t(y) * size.width() + x;
            for (int c = 0; c < snapshot.stride; ++c) {
                double value = sampler.values[c];
                if (std::isfinite(value)) {
                    const double limit = std::numeric_limits<float>::max();
                    value = std::max(-limit, std::min(limit, value));
                }
                output->pixels[p * snapshot.stride + c] = float(value);
            }
            if (sphere) output->deepCoverage[p] = 1;
            if (diagnostics) flags[p] = sampler.flags;
        }
    }
    if (diagnostics && !cancel->load())
        output->anomalyRegions = PixelDiagnostics::connectedRegions(flags, output->width, output->height, cancel);
    return cancel->load() ? nullptr : output;
}
