#include "ExrPreview.h"
#include "ComSupport.h"
#include <util/ColorTransform.h>
#include <model/framebuffer/ToneMapping.h>
#include <OpenEXR/ImfIO.h>
#include <OpenEXR/ImfMultiPartInputFile.h>
#include <OpenEXR/ImfInputPart.h>
#include <OpenEXR/ImfTiledInputPart.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfTileDescription.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
constexpr uint64_t MaxPixels = 16000000;
constexpr uint64_t BufferBudget = 256ull * 1024 * 1024;
constexpr uint64_t HeaderBudget = 16ull * 1024 * 1024;

struct Stopped : std::runtime_error { Stopped() : runtime_error("Preview cancelled or time limit exceeded.") {} };
struct Budget {
    ShellPreview::Cancellation cancel;
    std::chrono::steady_clock::time_point deadline;
    void check() const
    {
        if ((cancel && cancel->load()) || std::chrono::steady_clock::now() >= deadline)
            throw Stopped();
    }
};

class Stream : public Imf::IStream {
public:
    Stream(::IStream* input, const Budget& work) : Imf::IStream("Shell stream"), input(input), work(work)
    {
        STATSTG stat = {};
        if (FAILED(input->Stat(&stat, STATFLAG_NONAME))) throw std::runtime_error("Cannot inspect the input stream.");
        size = stat.cbSize.QuadPart;
        seekg(0);
    }
    bool read(char* bytes, int count) override
    {
        work.check();
        if (count < 0 || uint64_t(count) > size - position) throw std::runtime_error("Truncated EXR file.");
        ULONG received = 0;
        if (FAILED(input->Read(bytes, ULONG(count), &received)) || received != ULONG(count))
            throw std::runtime_error("Cannot read EXR data.");
        position += received;
        work.check();
        return position < size;
    }
    uint64_t tellg() override { return position; }
    void seekg(uint64_t target) override
    {
        work.check();
        if (target > size || target > uint64_t(INT64_MAX)) throw std::runtime_error("Invalid EXR stream offset.");
        LARGE_INTEGER offset; offset.QuadPart = LONGLONG(target);
        ULARGE_INTEGER actual = {};
        if (FAILED(input->Seek(offset, STREAM_SEEK_SET, &actual)) || actual.QuadPart != target)
            throw std::runtime_error("Cannot seek the EXR stream.");
        position = target;
    }
private:
    ComPtr<::IStream> input;
    const Budget& work;
    uint64_t size = 0, position = 0;
};

uint32_t little32(const unsigned char* p)
{ return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
int32_t signed32(const unsigned char* p)
{ const uint32_t n = little32(p); int32_t value; std::memcpy(&value, &n, 4); return value; }

std::string stringFrom(Stream& stream)
{
    std::string result;
    for (unsigned i = 0; i <= 255; ++i) {
        char c; stream.read(&c, 1);
        if (!c) return result;
        result += c;
    }
    throw std::runtime_error("EXR attribute name is too long.");
}

struct Inspection {
    ShellPreview::Image embedded;
    uint32_t version = 0;
    unsigned parts = 0;
    bool hasDataWindow = false;
    Imath::Box2i data;
};

// Bound header allocations before OpenEXR constructs attributes/chunk tables.
// Reading embedded previews here also lets oversized and unsupported images
// fall back without asking OpenEXR to allocate their full input structures.
Inspection inspect(Stream& stream)
{
    Inspection info;
    unsigned char bytes[16];
    stream.read(reinterpret_cast<char*>(bytes), 8);
    info.version = little32(bytes + 4);
    if (little32(bytes) != 20000630 || (info.version & 0xff) != 2
        || (info.version & ~uint32_t(0x1e00 | 0xff)))
        throw std::runtime_error("Not a supported OpenEXR file.");
    const bool multipart = (info.version & 0x1000) != 0;
    for (;;) {
        auto name = stringFrom(stream);
        if (name.empty()) {
            if (!info.parts) throw std::runtime_error("Missing EXR header.");
            break;
        }
        if (++info.parts > 256) throw std::runtime_error("Too many EXR parts for a shell preview.");
        unsigned attributes = 0;
        do {
            if (++attributes > 1024) throw std::runtime_error("Too many EXR header attributes for a shell preview.");
            const auto type = stringFrom(stream);
            stream.read(reinterpret_cast<char*>(bytes), 4);
            const uint32_t length = little32(bytes);
            const uint64_t start = stream.tellg();
            if (length > HeaderBudget || start > HeaderBudget - length)
                throw std::runtime_error("EXR header exceeds the preview limit.");
            if (info.parts == 1 && name == "dataWindow" && type == "box2i" && length == 16) {
                stream.read(reinterpret_cast<char*>(bytes), 16);
                info.data = Imath::Box2i(Imath::V2i(signed32(bytes), signed32(bytes + 4)),
                                        Imath::V2i(signed32(bytes + 8), signed32(bytes + 12)));
                info.hasDataWindow = true;
            }
            if (info.embedded.pixels.empty() && name == "preview" && type == "preview" && length >= 8) {
                stream.read(reinterpret_cast<char*>(bytes), 8);
                const uint32_t w = little32(bytes), h = little32(bytes + 4);
                const uint64_t count = uint64_t(w) * h;
                if (w && h && count <= (HeaderBudget - 8) / 4 && count * 4 + 8 == length) {
                    info.embedded.width = w; info.embedded.height = h;
                    info.embedded.pixels.resize(size_t(count) * 4);
                    stream.read(reinterpret_cast<char*>(info.embedded.pixels.data()), int(count * 4));
                    for (size_t i = 0; i < count; ++i)
                        std::swap(info.embedded.pixels[4 * i], info.embedded.pixels[4 * i + 2]);
                    info.embedded.description = std::to_wstring(w) + L" x " + std::to_wstring(h)
                        + L"  |  EXR embedded preview (part " + std::to_wstring(info.parts) + L")";
                }
            }
            stream.seekg(start + length);
            name = stringFrom(stream);
        } while (!name.empty());
        if (!multipart) break;
    }
    return info;
}

std::pair<unsigned, unsigned> outputSize(double width, double height, unsigned edge)
{
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
        throw std::runtime_error("Invalid EXR display dimensions.");
    const double scale = std::min(1., edge / std::max(width, height));
    return {unsigned(std::max(1., std::round(width * scale))),
            unsigned(std::max(1., std::round(height * scale)))};
}

// Area averaging prevents the severe aliasing of point sampling for thumbnails.
// Coordinates are pixel edges; out-of-data samples contribute transparent black.
template<class Sample> ShellPreview::Image resample(
    unsigned width, unsigned height, double left, double top, double spanX, double spanY,
    int64_t minX, int64_t minY, int64_t maxX, int64_t maxY, Sample sample, const Budget& budget)
{
    ShellPreview::Image image;
    image.width = width; image.height = height;
    image.pixels.resize(size_t(width) * height * 4);
    for (unsigned y = 0; y < height; ++y) {
        budget.check();
        const double y0 = top + double(y) * spanY / height;
        const double y1 = top + double(y + 1) * spanY / height;
        for (unsigned x = 0; x < width; ++x) {
            if (!(x % 64)) budget.check();
            const double x0 = left + double(x) * spanX / width;
            const double x1 = left + double(x + 1) * spanX / width;
            double sums[4] = {};
            const double area = (x1 - x0) * (y1 - y0);
            for (int64_t sy = std::max(minY, int64_t(std::floor(y0)));
                 sy < std::min(maxY, int64_t(std::ceil(y1))); ++sy) {
                budget.check();
                const double wy = std::min(y1, double(sy + 1)) - std::max(y0, double(sy));
                for (int64_t sx = std::max(minX, int64_t(std::floor(x0)));
                     sx < std::min(maxX, int64_t(std::ceil(x1))); ++sx) {
                    if (!(sx % 4096)) budget.check();
                    const double weight = wy * (std::min(x1, double(sx + 1)) - std::max(x0, double(sx)));
                    const auto pixel = sample(sx, sy);
                    for (unsigned c = 0; c < 4; ++c) sums[c] += weight * pixel[c];
                }
            }
            for (unsigned c = 0; c < 4; ++c)
                image.pixels[(size_t(y) * width + x) * 4 + c] = uint8_t(std::max(0., std::min(255., sums[c] / area)));
        }
    }
    return image;
}

ShellPreview::Image embeddedImage(const ShellPreview::Image& input, unsigned edge, const Budget& budget)
{
    const auto size = outputSize(input.width, input.height, edge);
    auto image = resample(size.first, size.second, 0, 0, input.width, input.height,
        0, 0, input.width, input.height, [&](int64_t x, int64_t y) {
            std::array<uint8_t, 4> value;
            std::copy_n(&input.pixels[(size_t(y) * input.width + size_t(x)) * 4], 4, value.begin());
            return value;
        }, budget);
    image.description = input.description;
    return image;
}

ShellPreview::Image decode(Stream& stream, const Inspection& info, unsigned edge, const Budget& budget)
{
    if (info.parts != 1 || (info.version & (0x800 | 0x1000)) || !info.hasDataWindow)
        throw std::runtime_error("This EXR format requires an embedded preview.");
    const int64_t w = int64_t(info.data.max.x) - info.data.min.x + 1;
    const int64_t h = int64_t(info.data.max.y) - info.data.min.y + 1;
    if (w <= 0 || h <= 0 || w > int64_t(MaxPixels) || h > int64_t(MaxPixels) || uint64_t(w) * h > MaxPixels)
        throw std::runtime_error("Image exceeds the 16 million pixel preview limit.");
    stream.seekg(0);
    // Zero extra decoder threads; never reconstruct missing chunk offset tables.
    Imf::MultiPartInputFile file(stream, 0, false);
    if (file.parts() != 1) throw std::runtime_error("Multipart decoding is not supported here.");
    const auto& header = file.header(0);
    const bool tiled = header.type() == Imf::TILEDIMAGE;
    if (header.type() != Imf::SCANLINEIMAGE && !tiled)
        throw std::runtime_error("Deep EXR requires an embedded preview.");
    if (tiled && header.tileDescription().mode != Imf::ONE_LEVEL)
        throw std::runtime_error("Multiresolution EXR requires an embedded preview.");
    if (tiled && uint64_t(header.tileDescription().xSize) * header.tileDescription().ySize > 1048576)
        throw std::runtime_error("Tiles exceed the shell preview working-set limit.");
    for (auto channel = header.channels().begin(); channel != header.channels().end(); ++channel) {
        const std::string name = channel.name();
        if (name != "R" && name != "G" && name != "B" && name != "A")
            throw std::runtime_error("Layered or auxiliary-channel EXR requires an embedded preview.");
    }
    for (const char* name : {"R", "G", "B", "A"}) {
        const auto* channel = header.channels().findChannel(name);
        if ((!channel && name[0] != 'A') || (channel && (channel->xSampling != 1 || channel->ySampling != 1)))
            throw std::runtime_error("Preview requires full-resolution R, G and B channels.");
    }
    const auto data = header.dataWindow(), display = header.displayWindow();
    const int64_t dw = int64_t(display.max.x) - display.min.x + 1;
    const int64_t dh = int64_t(display.max.y) - display.min.y + 1;
    const double aspect = header.pixelAspectRatio();
    if (data != info.data || !std::isfinite(aspect) || aspect <= 0 || dw <= 0 || dh <= 0)
        throw std::runtime_error("Invalid EXR windows or pixel aspect ratio.");
    const auto size = outputSize(double(dw) * aspect, double(dh), edge);
    const uint64_t count = uint64_t(w) * h;
    if (count * 4 * sizeof(float) + uint64_t(size.first) * size.second * 4
        + info.embedded.pixels.size() + HeaderBudget > BufferBudget)
        throw std::runtime_error("Image exceeds the shell preview memory budget.");
    std::vector<std::array<float, 4>> pixels(size_t(count), {{0, 0, 0, 1}});
    Imf::FrameBuffer buffer;
    unsigned component = 0;
    for (const char* name : {"R", "G", "B", "A"}) {
        if (header.channels().findChannel(name))
            buffer.insert(name, Imf::Slice::Make(Imf::FLOAT, &pixels[0][component], data.min,
                w, h, sizeof(pixels[0]), size_t(w) * sizeof(pixels[0])));
        ++component;
    }
    if (tiled) {
        Imf::TiledInputPart part(file, 0);
        part.setFrameBuffer(buffer);
        for (int y = 0; y < part.numYTiles(); ++y)
            for (int x = 0; x < part.numXTiles(); ++x) { budget.check(); part.readTile(x, y); }
    } else {
        Imf::InputPart part(file, 0);
        part.setFrameBuffer(buffer);
        for (int64_t y = data.min.y; y <= data.max.y; y += 32) {
            budget.check();
            part.readPixels(int(y), int(std::min<int64_t>(data.max.y, y + 31)));
        }
    }
    Imf::Chromaticities source, standard;
    if (const auto* attribute = header.findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities"))
        source = attribute->value();
    const bool same = source.red == standard.red && source.green == standard.green
        && source.blue == standard.blue && source.white == standard.white;
    Imath::M44d conversion;
    if (!same) {
        conversion = Imath::M44d(Imf::RGBtoXYZ(source, 1.f)) * Imath::M44d(Imf::XYZtoRGB(standard, 1.f));
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c)
            if (!std::isfinite(conversion[r][c])) throw std::runtime_error("Invalid EXR chromaticities.");
    }
    // Convert once in-place; do not repeat pow/matrix work for each resized sample.
    for (size_t i = 0; i < pixels.size(); ++i) {
        if (!(i % 4096)) budget.check();
        auto& p = pixels[i];
        Imath::V3d rgb(p[0], p[1], p[2]);
        if (!same) rgb = rgb * conversion;
        p[0] = ToneMapping::toByte(ColorTransform::to_sRGB(rgb.z));
        p[1] = ToneMapping::toByte(ColorTransform::to_sRGB(rgb.y));
        p[2] = ToneMapping::toByte(ColorTransform::to_sRGB(rgb.x));
        p[3] = 255;
    }
    auto image = resample(size.first, size.second, display.min.x, display.min.y, double(dw), double(dh),
        data.min.x, data.min.y, int64_t(data.max.x) + 1, int64_t(data.max.y) + 1,
        [&](int64_t x, int64_t y) { return pixels[size_t(y - data.min.y) * size_t(w) + size_t(x - data.min.x)]; }, budget);
    image.description = std::to_wstring(dw) + L" x " + std::to_wstring(dh)
        + (tiled ? L"  |  Tiled" : L"  |  Scanline")
        + (header.channels().findChannel("A") ? L" RGBA" : L" RGB") + L"  |  SDR, 0 EV";
    return image;
}
}

ShellPreview::Result ShellPreview::render(IStream* input, unsigned maxEdge, bool thumbnail,
                                         const Cancellation& cancel) noexcept
{
    Result result;
    try {
        if (!input || !maxEdge) throw std::runtime_error("Invalid preview request.");
        const unsigned edge = std::min(maxEdge, 2048u);
        Budget budget {cancel, std::chrono::steady_clock::now() + std::chrono::seconds(thumbnail ? 2 : 5)};
        Stream stream(input, budget);
        const auto info = inspect(stream);
        if (thumbnail && !info.embedded.pixels.empty()) result.image = embeddedImage(info.embedded, edge, budget);
        else {
            try { result.image = decode(stream, info, edge, budget); }
            catch (const Stopped&) { throw; }
            catch (const std::exception&) {
                if (info.embedded.pixels.empty()) throw;
                result.image = embeddedImage(info.embedded, edge, budget);
            }
        }
    } catch (const std::exception& error) {
        // Decoder messages are diagnostic ASCII/UTF-8; do not expose file content as UI markup.
        try { const std::string message = error.what(); result.error.assign(message.begin(), message.end()); } catch (...) {}
    } catch (...) {
        try { result.error = L"Cannot preview this EXR file."; } catch (...) {}
    }
    return result;
}

HBITMAP ShellPreview::bitmap(const Image& image)
{
    if (!image.width || !image.height || image.pixels.size() != size_t(image.width) * image.height * 4) return nullptr;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = LONG(image.width); info.bmiHeader.biHeight = -LONG(image.height);
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* output = nullptr;
    const HBITMAP handle = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &output, nullptr, 0);
    if (handle) std::memcpy(output, image.pixels.data(), image.pixels.size());
    return handle;
}
