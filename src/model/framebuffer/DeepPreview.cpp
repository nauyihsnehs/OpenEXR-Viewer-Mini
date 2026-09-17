#include "DeepPreview.h"
#include "PixelDiagnostics.h"
#include "ToneMapping.h"
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
    void collect(double value, double& minimum, double& maximum, bool& finite)
    {
        if (!std::isfinite(value)) return;
        if (!finite) minimum = maximum = value;
        minimum = std::min(minimum, value); maximum = std::max(maximum, value);
        finite = true;
    }
    float asFloat(double value)
    {
        const double limit = std::numeric_limits<float>::max();
        if (value > limit) return std::numeric_limits<float>::infinity();
        if (value < -limit) return -std::numeric_limits<float>::infinity();
        return float(value);
    }
    std::shared_ptr<FramebufferData> metadata(const FramebufferData& source)
    {
        // Copy metadata without duplicating the previous full-sized pixel buffers.
        auto data = std::make_shared<FramebufferData>();
        data->width = source.width; data->height = source.height;
        data->dataWindow = source.dataWindow; data->displayWindow = source.displayWindow;
        data->pixelAspect = source.pixelAspect;
        data->resolutionLevel = source.resolutionLevel; data->resolutionLevels = source.resolutionLevels;
        data->rawViews = source.rawViews; data->sourceSampling = source.sourceSampling;
        data->hasRawChromaticities = source.hasRawChromaticities;
        data->rawChromaticities = source.rawChromaticities;
        data->minimum = source.minimum; data->maximum = source.maximum;
        data->hasFiniteSamples = source.hasFiniteSamples;
        data->nanCount = source.nanCount; data->infCount = source.infCount;
        data->positiveInfCount = source.positiveInfCount; data->negativeInfCount = source.negativeInfCount;
        data->deep = source.deep; data->deepChannels = source.deepChannels;
        data->deepScalar = source.deepScalar; data->stereoChannels = source.stereoChannels;
        return data;
    }
}

DepthBounds DeepPreview::bounds(const FramebufferData& data)
{
    if (data.deep) return data.deep->bounds;
    DepthBounds result;
    for (const auto& eye : data.stereo) {
        if (!eye) continue;
        const auto range = bounds(*eye);
        if (range.finite) { result.include(range.minimum); result.include(range.maximum); }
    }
    return result;
}

DecodeResult DeepPreview::decode(std::shared_ptr<FramebufferData> data,
  const std::shared_ptr<ExrInput>& source, int part,
  const std::array<std::string, 4>& names, bool scalar, const Cancellation& cancel)
{
    data->deep = readDeepSamples(source, part, cancel);
    if (!data->deep) return {};
    data->deepChannels = names; data->deepScalar = scalar;
    for (const auto& name : names) {
        if (name.empty()) continue;
        if (name != "R" && name != "G" && name != "B" && name != "A" && name != "Z")
            throw std::runtime_error("Deep previews currently support base RGBA and Z channels only.");
        const auto* channel = data->deep->find(name);
        if (!channel) throw std::runtime_error("Missing deep channel: " + name);
    }
    // Source statistics describe every stored channel sample, including invalid Z.
    for (const auto& channel : data->deep->channels) {
        for (size_t s = 0; s < data->deep->offsets.back(); ++s) {
            if (s % 4096 == 0 && cancel->load()) return {};
            const double value = channel.value(s);
            if (std::isnan(value)) ++data->nanCount;
            else if (std::isinf(value)) {
                ++data->infCount;
                if (value > 0) ++data->positiveInfCount; else ++data->negativeInfCount;
            } else collect(value, data->minimum, data->maximum, data->hasFiniteSamples);
        }
    }
    if (const auto* chroma = data->deep->header.findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities")) {
        data->hasRawChromaticities = true; data->rawChromaticities = chroma->value();
    }
    DecodeResult result;
    result.data = compose(data, bounds(*data).clamp({}), cancel);
    return result;
}

std::shared_ptr<const FramebufferData> DeepPreview::compose(
  const std::shared_ptr<const FramebufferData>& source, DepthRange range, const Cancellation& cancel)
{
    if (!source->hasDeep()) return source;
    if (source->depthRange == range && (!source->pixels.empty() || source->stereo[0])) return source;
    auto data = metadata(*source);
    data->depthRange = range;
    if (source->stereo[0]) {
        for (size_t i = 0; i < 2; ++i) {
            data->stereo[i] = compose(source->stereo[i], range, cancel);
            if (!data->stereo[i] || cancel->load()) return {};
            const auto& eye = *data->stereo[i];
            if (eye.hasFiniteLuminance) {
                collect(eye.luminanceMin, data->luminanceMin, data->luminanceMax, data->hasFiniteLuminance);
                collect(eye.luminanceMax, data->luminanceMin, data->luminanceMax, data->hasFiniteLuminance);
            }
            for (auto region : eye.anomalyRegions) {
                region.bounds.translate(eye.dataWindow.topLeft() - data->dataWindow.topLeft());
                data->anomalyRegions.push_back(region);
            }
        }
        return data;
    }
    const auto& deep = *data->deep;
    const auto* z = deep.find("Z");
    const auto* alpha = deep.find("A");
    const size_t pixels = deep.counts.size();
    const int components = data->deepScalar ? 1 : 4;
    std::array<const DeepSamples::Channel*, 4> channels = {{nullptr, nullptr, nullptr, nullptr}};
    for (size_t c = 0; c < channels.size(); ++c)
        if (!data->deepChannels[c].empty()) channels[c] = deep.find(data->deepChannels[c]);
    const bool depthOnly = data->deepScalar && data->deepChannels[0] == "Z";
    const std::string scalarName = data->deepChannels[0];
    const bool scalarColor = data->deepScalar
      && (scalarName == "R" || scalarName == "G" || scalarName == "B")
      && deep.find("R") && deep.find("G") && deep.find("B");
    const int scalarComponent = scalarName == "G" ? 1 : scalarName == "B" ? 2 : 0;
    if (scalarColor) channels = {{deep.find("R"), deep.find("G"), deep.find("B"), alpha}};
    const int compositeComponents = scalarColor ? 4 : components;
    data->pixels.resize(pixels * components, 0.f);
    data->deepCoverage.resize(pixels, 0);
    std::vector<uint8_t> flags(pixels, 0);
    const Imf::Chromaticities standard;
    const auto& chroma = data->rawChromaticities;
    const bool convert = (!data->deepScalar || scalarColor) && data->hasRawChromaticities && chroma != standard;
    const Imath::M44d conversion = convert
      ? Imath::M44d(Imf::RGBtoXYZ(chroma, 1.f)) * Imath::M44d(Imf::XYZtoRGB(standard, 1.f))
      : Imath::M44d();
    for (size_t p = 0; p < pixels; ++p) {
        if (p % size_t(data->width) == 0 && cancel->load()) return {};
        double sum[4] = {}, accumulatedAlpha = 0.;
        for (size_t j = 0; j < deep.counts[p]; ++j) {
            if (j % 4096 == 0 && cancel->load()) return {};
            const size_t s = deep.offsets[p] + deep.order[deep.offsets[p] + j];
            const double depth = z->value(s);
            if (!std::isfinite(depth)) {
                if (depthOnly) flags[p] |= PixelDiagnostics::classify(asFloat(depth));
                continue;
            }
            if (depth < range.minimum || depth > range.maximum) continue;
            for (const auto* channel : channels)
                if (channel) flags[p] |= PixelDiagnostics::classify(asFloat(channel->value(s)));
            if (!data->deepCoverage[p] && depthOnly) sum[0] = depth;
            data->deepCoverage[p] = 1;
            if (depthOnly || accumulatedAlpha >= 1.) continue;
            const double transmittance = 1. - accumulatedAlpha;
            for (int c = 0; c < compositeComponents; ++c)
                if (channels[c]) sum[c] += transmittance * channels[c]->value(s);
            accumulatedAlpha += transmittance * alpha->value(s);
        }
        if (!data->deepCoverage[p]) continue;
        if (!data->deepScalar && !channels[3]) sum[3] = accumulatedAlpha;
        if (convert) {
            const Imath::V3d rgb = Imath::V3d(sum[0], sum[1], sum[2]) * conversion;
            sum[0] = rgb.x; sum[1] = rgb.y; sum[2] = rgb.z;
        }
        for (int c = 0; c < components; ++c)
            data->pixels[p * components + c] = asFloat(sum[scalarColor ? scalarComponent : c]);
        if (data->deepScalar)
            collect(data->pixels[p], data->displayMinimum, data->displayMaximum, data->hasFiniteDisplay);
        else
            collect(ToneMapping::luminance(data->pixels[4 * p], data->pixels[4 * p + 1], data->pixels[4 * p + 2]),
              data->luminanceMin, data->luminanceMax, data->hasFiniteLuminance);
    }
    data->anomalyRegions = PixelDiagnostics::connectedRegions(flags, data->width, data->height, cancel);
    return cancel->load() ? nullptr : data;
}
