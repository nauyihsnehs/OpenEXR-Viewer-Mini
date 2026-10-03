#pragma once

#include <objidl.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ShellPreview {
struct Image {
    unsigned width = 0, height = 0;
    // Top-down BGRA, with premultiplied alpha. Real EXR data is shown over black,
    // matching the viewer; absent display-window pixels remain transparent.
    std::vector<uint8_t> pixels;
    std::wstring description;
};
struct Result {
    Image image;
    std::wstring error;
};
using Cancellation = std::shared_ptr<std::atomic_bool>;

// Owns no file path, never writes, and never initializes a Qt application.
Result render(IStream* stream, unsigned maxEdge, bool thumbnail,
              const Cancellation& cancel) noexcept;
HBITMAP bitmap(const Image& image);
}
