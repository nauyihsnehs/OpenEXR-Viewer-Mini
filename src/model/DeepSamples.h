#pragma once
#include <model/LoadProgress.h>

#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfPixelType.h>
#include <Imath/half.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ExrInput;

struct DepthRange {
    double minimum = 0.;
    double maximum = 0.;
    bool full = true;
    bool operator==(const DepthRange& other) const
    { return minimum == other.minimum && maximum == other.maximum && full == other.full; }
};

struct DepthBounds {
    double minimum = 0.;
    double maximum = 0.;
    bool finite = false;
    void include(double value);
    DepthRange clamp(DepthRange range) const;
};

// Immutable after publication. Native samples and their file order are never edited.
struct DeepSamples {
    struct Channel {
        std::string name;
        Imf::PixelType type = Imf::FLOAT;
        std::vector<half> halves;
        std::vector<float> floats;
        std::vector<uint32_t> integers;
        size_t stride() const { return type == Imf::HALF ? sizeof(half) : sizeof(uint32_t); }
        const void* buffer() const;
        double value(size_t sample) const;
        void resize(size_t count);
    };
    Imf::Header header;
    int width = 0, height = 0;
    std::vector<uint32_t> counts;
    std::vector<size_t> offsets;
    // Relative sample indices, sorted by finite Z; nonfinite Z comes last.
    std::vector<uint32_t> order;
    std::vector<Channel> channels;
    DepthBounds bounds;
    const Channel* find(const std::string& name) const;
};

std::shared_ptr<const DeepSamples> readDeepSamples(
  const std::shared_ptr<ExrInput>& source, int part,
  const std::shared_ptr<std::atomic_bool>& cancel, const Progress& progress = {});
