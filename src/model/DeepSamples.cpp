#include "DeepSamples.h"
#include "ExrInput.h"
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineInputPart.h>
#include <OpenEXR/ImfPartType.h>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <new>

namespace { struct DeepReadCancelled {}; }

void DepthBounds::include(double value)
{
    if (!std::isfinite(value)) return;
    if (!finite) minimum = maximum = value;
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
    finite = true;
}

DepthRange DepthBounds::clamp(DepthRange range) const
{
    if (range.full || !finite) {
        range.minimum = minimum; range.maximum = maximum;
    } else {
        range.minimum = std::max(minimum, std::min(range.minimum, maximum));
        range.maximum = std::max(range.minimum, std::min(range.maximum, maximum));
    }
    return range;
}

const void* DeepSamples::Channel::buffer() const
{
    if (type == Imf::HALF) return halves.data();
    if (type == Imf::UINT) return integers.data();
    return floats.data();
}
double DeepSamples::Channel::value(size_t sample) const
{
    if (type == Imf::HALF) return float(halves[sample]);
    if (type == Imf::UINT) return integers[sample];
    return floats[sample];
}
void DeepSamples::Channel::resize(size_t count)
{
    if (type == Imf::HALF) halves.resize(count);
    else if (type == Imf::UINT) integers.resize(count);
    else floats.resize(count);
}
const DeepSamples::Channel* DeepSamples::find(const std::string& name) const
{
    for (const auto& channel : channels) if (channel.name == name) return &channel;
    return nullptr;
}

std::shared_ptr<const DeepSamples> readDeepSamples(
  const std::shared_ptr<ExrInput>& source, int part,
  const std::shared_ptr<std::atomic_bool>& cancel) try
{
    if (!source || !source->file || part < 0 || part >= source->file->parts())
        throw std::runtime_error("Invalid deep image part.");
    // Serialize cache creation, but keep the file binding lock scoped to each read.
    std::unique_lock<std::timed_mutex> cacheLock(source->deepMutex, std::defer_lock);
    while (!cacheLock.try_lock_for(std::chrono::milliseconds(20)))
        if (cancel->load()) return {};
    if (cancel->load()) return {};
    if (auto cached = source->deepParts[part].lock()) return cached;
    auto data = std::make_shared<DeepSamples>();
    data->header = source->file->header(part);
    const auto& header = data->header;
    if (!header.hasType() || header.type() != Imf::DEEPSCANLINE)
        throw std::runtime_error("Only Deep Scanline parts are supported.");
    if (header.channels().findChannel("ZBack"))
        throw std::runtime_error("Deep parts with ZBack are not supported yet.");
    if (!header.channels().findChannel("Z") || !header.channels().findChannel("A"))
        throw std::runtime_error("Deep Scanline previews require Z and A channels.");
    const auto window = header.dataWindow();
    const int64_t width = int64_t(window.max.x) - window.min.x + 1;
    const int64_t height = int64_t(window.max.y) - window.min.y + 1;
    const uint64_t limit = uint64_t(1) << 30;
    if (width <= 0 || height <= 0 || width > INT_MAX / 4 || height > INT_MAX / 4
        || uint64_t(width) * height > limit / (sizeof(uint32_t) + sizeof(size_t)))
        throw std::runtime_error("Deep part exceeds the 1 GiB cache limit.");
    data->width = int(width); data->height = int(height);
    const size_t pixels = size_t(width) * height;
    uint64_t sampleBytes = sizeof(uint32_t); // Sorted sample indices also occupy the cache.
    for (auto it = header.channels().begin(); it != header.channels().end(); ++it) {
        const std::string name = it.name();
        if (name.substr(name.find_last_of('.') + 1) == "ZBack")
            throw std::runtime_error("Deep parts with ZBack are not supported yet.");
        if (it.channel().xSampling != 1 || it.channel().ySampling != 1)
            throw std::runtime_error("Subsampled deep channels are not supported.");
        DeepSamples::Channel channel;
        channel.name = it.name(); channel.type = it.channel().type;
        sampleBytes += channel.stride();
        data->channels.push_back(std::move(channel));
        if (data->channels.size() > limit / sizeof(void*) / uint64_t(width))
            throw std::runtime_error("Deep part exceeds the 1 GiB cache limit.");
    }
    const uint64_t fixedBytes = pixels * (sizeof(uint32_t) + sizeof(size_t))
      + sizeof(size_t) + uint64_t(width) * sizeof(void*) * data->channels.size();
    if (fixedBytes >= limit || sampleBytes > limit)
        throw std::runtime_error("Deep part exceeds the 1 GiB cache limit.");
    data->counts.resize(pixels);
    data->offsets.resize(pixels + 1);
    const auto makePart = [&] {
        const std::lock_guard<std::mutex> lock(source->mutex);
        return Imf::DeepScanLineInputPart(*source->file, part);
    };
    auto input = makePart();
    Imf::DeepFrameBuffer countsBuffer;
    countsBuffer.insertSampleCountSlice(Imf::Slice::Make(Imf::UINT, data->counts.data(), window,
      sizeof(uint32_t), size_t(width) * sizeof(uint32_t)));
    size_t total = 0;
    for (int row = 0; row < data->height; ++row) {
        if (cancel->load()) return {};
        const int y = int(int64_t(window.min.y) + row);
        {
            const std::lock_guard<std::mutex> lock(source->mutex);
            input.setFrameBuffer(countsBuffer);
            input.readPixelSampleCounts(y, y);
        }
        for (int x = 0; x < data->width; ++x) {
            if (x % 4096 == 0 && cancel->load()) return {};
            const size_t p = size_t(row) * data->width + x;
            if (data->counts[p] > (limit - fixedBytes) / sampleBytes - total)
                throw std::runtime_error("Deep part exceeds the 1 GiB cache limit.");
            total += data->counts[p];
            data->offsets[p + 1] = total;
        }
    }
    for (auto& channel : data->channels) {
        if (cancel->load()) return {};
        channel.resize(total);
    }
    std::vector<std::vector<char*>> pointers(data->channels.size(), std::vector<char*>(data->width));
    for (int row = 0; row < data->height; ++row) {
        if (cancel->load()) return {};
        const int y = int(int64_t(window.min.y) + row);
        Imf::DeepFrameBuffer buffer = countsBuffer;
        for (size_t c = 0; c < data->channels.size(); ++c) {
            const auto& channel = data->channels[c];
            for (int x = 0; x < data->width; ++x) {
                if (x % 4096 == 0 && cancel->load()) return {};
                const size_t p = size_t(row) * data->width + x;
                pointers[c][x] = data->counts[p] ? const_cast<char*>(static_cast<const char*>(channel.buffer()))
                  + data->offsets[p] * channel.stride() : nullptr;
            }
            const auto slice = Imf::Slice::Make(Imf::FLOAT, pointers[c].data(),
              Imath::V2i(window.min.x, 0), width, 1, sizeof(char*), size_t(width) * sizeof(char*));
            buffer.insert(channel.name, Imf::DeepSlice(channel.type, slice.base,
              slice.xStride, 0, channel.stride()));
        }
        const std::lock_guard<std::mutex> lock(source->mutex);
        input.setFrameBuffer(buffer);
        input.readPixelSampleCounts(y, y);
        for (int x = 0; x < data->width; ++x) {
            const size_t p = size_t(row) * data->width + x;
            if (data->counts[p] != data->offsets[p + 1] - data->offsets[p])
                throw std::runtime_error("Deep sample counts changed while reading.");
        }
        input.readPixels(y, y);
    }
    const auto* z = data->find("Z");
    data->order.resize(total);
    for (size_t p = 0; p < pixels; ++p) {
        if (cancel->load()) return {};
        const size_t first = data->offsets[p], count = data->counts[p];
        for (size_t s = 0; s < count; ++s) {
            if (s % 4096 == 0 && cancel->load()) return {};
            data->order[first + s] = uint32_t(s);
            data->bounds.include(z->value(first + s));
        }
        size_t comparisons = 0;
        std::sort(data->order.begin() + first, data->order.begin() + first + count,
          [&](uint32_t a, uint32_t b) {
              if (++comparisons % 4096 == 0 && cancel->load()) throw DeepReadCancelled();
              const double za = z->value(first + a), zb = z->value(first + b);
              const bool fa = std::isfinite(za), fb = std::isfinite(zb);
              if (fa != fb) return fa;
              if (fa && za != zb) return za < zb;
              return a < b;
          });
    }
    if (cancel->load()) return {};
    source->deepParts[part] = data;
    return data;
}
catch (const DeepReadCancelled&) { return {}; }
catch (const std::bad_alloc&) {
    throw std::runtime_error("Unable to allocate the Deep sample cache (1 GiB maximum per part).");
}
