#include "FramebufferLoader.h"
#include <util/ResolutionLevels.h>
#include "ToneMapping.h"
#include "PixelDiagnostics.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputPart.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfRgbaYca.h>
#include <OpenEXR/ImfTiledInputPart.h>
#include <OpenEXR/ImfTileDescription.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
    int dimension(int minimum, int maximum)
    {
        const int64_t size = int64_t(maximum) - minimum + 1;
        if (size <= 0 || size > std::numeric_limits<int>::max())
            throw std::runtime_error("Invalid image dimensions.");
        return static_cast<int>(size);
    }

    int64_t floorDivide(int64_t value, int divisor)
    {
        const int64_t quotient = value / divisor;
        return quotient - (value % divisor < 0 ? 1 : 0);
    }

    struct Channel {
        int                samplingX = 1;
        int                samplingY = 1;
        int64_t            firstX    = 0;
        int64_t            firstY    = 0;
        int                width     = 0;
        int                height    = 0;
        std::vector<float> pixels;

        float at(int64_t x, int64_t y) const
        {
            const int sx = static_cast<int>(std::max<int64_t>(
              0,
              std::min<int64_t>(
                width - 1,
                floorDivide(x, samplingX) - firstX)));
            const int sy = static_cast<int>(std::max<int64_t>(
              0,
              std::min<int64_t>(
                height - 1,
                floorDivide(y, samplingY) - firstY)));
            return pixels[size_t(sy) * width + sx];
        }
    };

    Channel allocateChannel(
      const Imf::Header&  header,
      const std::string&  name,
      const Imath::Box2i& window,
      Imf::FrameBuffer&   buffer)
    {
        const Imf::Channel* metadata = header.channels().findChannel(name);
        if (!metadata) throw std::runtime_error("Missing channel: " + name);
        Channel channel;
        channel.samplingX = metadata->xSampling;
        channel.samplingY = metadata->ySampling;
        if (channel.samplingX <= 0 || channel.samplingY <= 0)
            throw std::runtime_error("Invalid channel sampling.");
        channel.firstX
          = -floorDivide(-int64_t(window.min.x), channel.samplingX);
        channel.firstY
          = -floorDivide(-int64_t(window.min.y), channel.samplingY);
        const int64_t width
          = floorDivide(window.max.x, channel.samplingX) - channel.firstX + 1;
        const int64_t height
          = floorDivide(window.max.y, channel.samplingY) - channel.firstY + 1;
        if (width <= 0 || height <= 0)
            throw std::runtime_error(
              "Channel has no samples in the data window.");
        channel.width  = static_cast<int>(width);
        channel.height = static_cast<int>(height);
        channel.pixels.resize(size_t(width) * size_t(height));
        // Slice::Make uses the first sampled coordinate as its buffer origin.
        const Imath::V2i origin(
          static_cast<int>(channel.firstX * channel.samplingX),
          static_cast<int>(channel.firstY * channel.samplingY));
        buffer.insert(
          name,
          Imf::Slice::Make(
            Imf::FLOAT,
            channel.pixels.data(),
            origin,
            int64_t(window.max.x) - origin.x + 1,
            int64_t(window.max.y) - origin.y + 1,
            sizeof(float),
            size_t(channel.width) * sizeof(float),
            channel.samplingX,
            channel.samplingY));
        return channel;
    }

    void collect(double value, double& minimum, double& maximum, bool& finite)
    {
        if (!std::isfinite(value)) return;
        if (!finite) minimum = maximum = value;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        finite  = true;
    }
}   // namespace

DecodeResult FramebufferLoader::decode(
  const std::shared_ptr<ExrInput>& source,
  int                                             partId,
  Layout                                          layout,
  const std::array<std::string, 4>&               names,
  const Cancellation&                             cancel, ResolutionLevel level)
{
    const auto file = source ? source->file : nullptr;
    if (!file || partId < 0 || partId >= file->parts())
        throw std::runtime_error("Invalid image part.");
    const Imf::Header& header = file->header(partId);
    if (
      names[0].empty()
      || ((layout == RGB || layout == Chroma) && (names[1].empty() || names[2].empty())))
        throw std::runtime_error("Missing image channels.");
    const auto geometry = ResolutionLevels::query(source, partId, level);
    const bool tiled = geometry.tiled;
    bool subsampledChroma = false;
    if (layout == Chroma) {
        const auto* y = header.channels().findChannel(names[0]);
        const auto* ry = header.channels().findChannel(names[1]);
        const auto* by = header.channels().findChannel(names[2]);
        const auto* a = names[3].empty() ? nullptr : header.channels().findChannel(names[3]);
        if (!y || !ry || !by || (!names[3].empty() && !a))
            throw std::runtime_error("Missing YC channels.");
        const bool full = ry->xSampling == 1 && ry->ySampling == 1
                          && by->xSampling == 1 && by->ySampling == 1;
        subsampledChroma = ry->xSampling == 2 && ry->ySampling == 2
                           && by->xSampling == 2 && by->ySampling == 2;
        if (y->xSampling != 1 || y->ySampling != 1
            || (a && (a->xSampling != 1 || a->ySampling != 1))
            || (!full && !subsampledChroma))
            throw std::runtime_error("Unsupported YC sampling: use full-resolution Y/A and matching 1x1 or 2x2 RY/BY.");
    }
    const Imath::Box2i window  = geometry.data;
    const Imath::Box2i display = geometry.display;
    auto               data    = std::make_shared<FramebufferData>();
    data->resolutionLevel = level;
    data->resolutionLevels = geometry.levels;
    data->rawViews             = ViewMetadata::read(header);
    data->width                = dimension(window.min.x, window.max.x);
    data->height               = dimension(window.min.y, window.max.y);
    data->pixelAspect          = header.pixelAspectRatio();
    if (!std::isfinite(data->pixelAspect) || data->pixelAspect <= 0.f)
        throw std::runtime_error("Invalid pixel aspect ratio.");
    const size_t count = size_t(data->width) * size_t(data->height);
    // Keep dimensions representable by Qt image strides and existing consumers.
    if (count > size_t(std::numeric_limits<int>::max()) / 4)
        throw std::runtime_error("The image is too large to display safely.");
    data->dataWindow
      = QRect(window.min.x, window.min.y, data->width, data->height);
    data->displayWindow = QRect(
      display.min.x,
      display.min.y,
      dimension(display.min.x, display.max.x),
      dimension(display.min.y, display.max.y));

    std::array<Channel, 4> channels;
    Imf::FrameBuffer       buffer;
    for (size_t i = 0; i < names.size(); ++i) {
        if (!names[i].empty()) {
            channels[i] = allocateChannel(header, names[i], window, buffer);
            data->sourceSampling[i] = QPoint(channels[i].samplingX, channels[i].samplingY);
        }
        if (cancel->load()) return DecodeResult();
    }
    if (tiled) {
        const auto makePart = [&] {
            const std::lock_guard<std::mutex> lock(source->mutex);
            return Imf::TiledInputPart(*file, partId);
        };
        Imf::TiledInputPart part = makePart();
        for (int y = 0; y < part.numYTiles(level.y); ++y) {
            if (cancel->load()) return DecodeResult();
            {
                const std::lock_guard<std::mutex> lock(source->mutex);
                part.setFrameBuffer(buffer);
                part.readTiles(0, part.numXTiles(level.x) - 1, y, y, level.x, level.y);
            }
            if (cancel->load()) return DecodeResult();
        }
    } else {
        const auto makePart = [&] {
            const std::lock_guard<std::mutex> lock(source->mutex);
            return Imf::InputPart(*file, partId);
        };
        Imf::InputPart part = makePart();
        for (int64_t y = window.min.y; y <= window.max.y; y += 32) {
            if (cancel->load()) return DecodeResult();
            const std::lock_guard<std::mutex> lock(source->mutex);
            part.setFrameBuffer(buffer);
            part.readPixels(
              static_cast<int>(y),
              static_cast<int>(std::min<int64_t>(window.max.y, y + 31)));
        }
    }
    // Count stored channel samples, not resampled pixels or synthesized RGBA.
    for (const Channel& channel : channels) {
        if (channel.pixels.empty()) continue;
        for (size_t i = 0; i < channel.pixels.size(); ++i) {
            if (i % size_t(channel.width) == 0 && cancel->load())
                return DecodeResult();
            const float value = channel.pixels[i];
            switch (PixelDiagnostics::classify(value)) {
                case FramebufferData::NaN: ++data->nanCount; break;
                case FramebufferData::PositiveInf:
                    ++data->positiveInfCount;
                    ++data->infCount;
                    break;
                case FramebufferData::NegativeInf:
                    ++data->negativeInfCount;
                    ++data->infCount;
                    break;
                default:
                    collect(value, data->minimum, data->maximum,
                            data->hasFiniteSamples);
                    break;
            }
        }
    }
    const int components = layout == Scalar ? 1 : 4;
    data->pixels.resize(count * components);
    std::vector<uint8_t> nonFiniteFlags;
    if (data->nanCount || data->infCount) nonFiniteFlags.resize(count, 0);
    for (int y = 0; y < data->height; ++y) {
        if (cancel->load()) return DecodeResult();
        for (int x = 0; x < data->width; ++x) {
            float* pixel
              = &data->pixels[(size_t(y) * data->width + x) * components];
            const int64_t sx = int64_t(window.min.x) + x;
            const int64_t sy = int64_t(window.min.y) + y;
            if (!nonFiniteFlags.empty()) {
                uint8_t& flags = nonFiniteFlags[size_t(y) * data->width + x];
                for (size_t c = 0; c < channels.size(); ++c)
                    if (!names[c].empty())
                        flags |= PixelDiagnostics::classify(channels[c].at(sx, sy));
            }
            pixel[0]         = channels[0].at(sx, sy);
            if (layout == Scalar) continue;
            pixel[1] = layout == Luminance ? pixel[0] : channels[1].at(sx, sy);
            pixel[2] = layout == Luminance ? pixel[0] : channels[2].at(sx, sy);
            pixel[3] = names[3].empty() ? 1.f : channels[3].at(sx, sy);
        }
    }

    data->anomalyRegions = PixelDiagnostics::connectedRegions(
      nonFiniteFlags, data->width, data->height, cancel);
    if (cancel->load()) return DecodeResult();
    std::vector<uint8_t>().swap(nonFiniteFlags);

    Imf::Chromaticities chromaticities;
    const auto*         attribute
      = header.findTypedAttribute<Imf::ChromaticitiesAttribute>(
        "chromaticities");
    if (attribute) {
        chromaticities = attribute->value();
        data->hasRawChromaticities = true;
        data->rawChromaticities = chromaticities;
    }
    if (layout == Chroma) {
        data->sourcePixels = data->pixels;
        const Imath::V3f weights = Imf::RgbaYca::computeYw(chromaticities);
        std::vector<Imf::Rgba> rgba(count);
        for (size_t i = 0; i < count; ++i) {
            if (i % size_t(data->width) == 0 && cancel->load()) return DecodeResult();
            rgba[i].r = data->pixels[4 * i + 1];   // RY
            rgba[i].g = data->pixels[4 * i];       // Y
            rgba[i].b = data->pixels[4 * i + 2];   // BY
            rgba[i].a = data->pixels[4 * i + 3];
        }
        // Match RgbaInputFile's separable reconstruction and edge extension.
        const int padding = Imf::RgbaYca::N2;
        std::vector<Imf::Rgba> lineBuffer(data->width + 2 * padding);
        if (subsampledChroma) {
            const int lastSampleX = int((channels[1].firstX + channels[1].width - 1) * 2 - window.min.x);
            for (int y = 0; y < data->height; ++y) {
                if (cancel->load()) return DecodeResult();
                if ((int64_t(window.min.y) + y) % 2 != 0) continue;
                Imf::Rgba* line = rgba.data() + size_t(y) * data->width;
                std::copy(line, line + data->width, lineBuffer.begin() + padding);
                std::fill(lineBuffer.begin(), lineBuffer.begin() + padding, line[0]);
                std::fill(lineBuffer.begin() + padding + data->width, lineBuffer.end(), line[lastSampleX]);
                Imf::RgbaYca::reconstructChromaHoriz(data->width, lineBuffer.data(), line);
            }
        }
        // Saturation correction also needs reconstructed RGB just outside the image.
        std::vector<Imf::Rgba> converted(count + 2 * size_t(data->width));
        const int lastSampleY = int((channels[1].firstY + channels[1].height - 1)
                                   * channels[1].samplingY - window.min.y);
        for (int y = subsampledChroma ? -1 : 0;
             y < data->height + (subsampledChroma ? 1 : 0); ++y) {
            if (cancel->load()) return DecodeResult();
            const int sourceY = y < 0 ? 0 : y >= data->height ? lastSampleY : y;
            const Imf::Rgba* line = rgba.data() + size_t(sourceY) * data->width;
            if (subsampledChroma && (int64_t(window.min.y) + y) % 2 != 0) {
                const Imf::Rgba* lines[Imf::RgbaYca::N];
                for (int i = 0; i < Imf::RgbaYca::N; ++i) {
                    const int row = y + i - padding;
                    const int clamped = row < 0 ? 0 : row >= data->height ? lastSampleY : row;
                    lines[i] = rgba.data() + size_t(clamped) * data->width;
                }
                Imf::RgbaYca::reconstructChromaVert(data->width, lines, lineBuffer.data());
                line = lineBuffer.data();
            }
            Imf::RgbaYca::YCAtoRGBA(weights, data->width, line,
                                   converted.data() + size_t(y + 1) * data->width);
        }
        std::vector<Imf::Rgba> corrected(data->width);
        for (int y = 0; y < data->height; ++y) {
            if (cancel->load()) return DecodeResult();
            const Imf::Rgba* lines[] = {
              converted.data() + size_t(y) * data->width,
              converted.data() + size_t(y + 1) * data->width,
              converted.data() + size_t(y + 2) * data->width};
            if (subsampledChroma)
                Imf::RgbaYca::fixSaturation(weights, data->width, lines, corrected.data());
            const Imf::Rgba* output = subsampledChroma ? corrected.data() : lines[1];
            for (int x = 0; x < data->width; ++x) {
                float* pixel = &data->pixels[4 * (size_t(y) * data->width + x)];
                pixel[0]     = output[x].r;
                pixel[1]     = output[x].g;
                pixel[2]     = output[x].b;
            }
        }
    }
    const Imf::Chromaticities standard;
    const bool standardChromaticities
      = chromaticities.red == standard.red && chromaticities.green == standard.green
        && chromaticities.blue == standard.blue && chromaticities.white == standard.white;
    // An approximate identity matrix still mixes NaN/Inf into other channels.
    if ((layout == RGB || layout == Chroma) && !standardChromaticities) {
        if (layout == RGB) data->sourcePixels = data->pixels;
        const Imath::M44d conversion
          = Imath::M44d(Imf::RGBtoXYZ(chromaticities, 1.f))
            * Imath::M44d(Imf::XYZtoRGB(standard, 1.f));
        for (size_t i = 0; i < count; ++i) {
            if (i % size_t(data->width) == 0 && cancel->load())
                return DecodeResult();
            float*           pixel = &data->pixels[4 * i];
            const Imath::V3d rgb
              = Imath::V3d(pixel[0], pixel[1], pixel[2]) * conversion;
            pixel[0] = static_cast<float>(rgb.x);
            pixel[1] = static_cast<float>(rgb.y);
            pixel[2] = static_cast<float>(rgb.z);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (i % size_t(data->width) == 0 && cancel->load())
            return DecodeResult();
        const float* pixel = &data->pixels[components * i];
        if (layout != Scalar)
            collect(
              ToneMapping::luminance(pixel[0], pixel[1], pixel[2]),
              data->luminanceMin,
              data->luminanceMax,
              data->hasFiniteLuminance);
    }
    DecodeResult result;
    result.data = data;
    return result;
}
