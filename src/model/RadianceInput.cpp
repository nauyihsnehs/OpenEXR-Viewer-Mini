#include "RadianceInput.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfPartType.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace {
struct Cancelled {};
void checkCancelled(const Cancellation& cancel)
{
    if (cancel && cancel->load()) throw Cancelled();
}

QByteArray headerLine(QFile& file)
{
    // Bound both individual lines and the complete header before storing metadata.
    const QByteArray line = file.readLine(65538);
    if (line.size() > 65536 || !line.endsWith('\n'))
        throw std::runtime_error("Truncated or oversized Radiance header line.");
    QByteArray text = line;
    text.chop(1);
    if (text.endsWith('\r')) text.chop(1);
    return text;
}

std::istringstream values(const QByteArray& text)
{
    std::istringstream stream(text.toStdString());
    stream.imbue(std::locale::classic());
    return stream;
}

Imf::Chromaticities primaries(const QByteArray& text)
{
    auto stream = values(text);
    float p[8];
    for (float& value : p)
        if (!(stream >> value) || !std::isfinite(value))
            throw std::runtime_error("Invalid Radiance PRIMARIES.");
    std::string trailing;
    if (stream >> trailing)
        throw std::runtime_error("Invalid Radiance PRIMARIES.");
    for (int i = 1; i < 8; i += 2)
        if (p[i] == 0.f) throw std::runtime_error("Invalid Radiance PRIMARIES.");
    const double determinant = double(p[0]) * (p[3] - p[5])
      + double(p[2]) * (p[5] - p[1]) + double(p[4]) * (p[1] - p[3]);
    if (std::abs(determinant) < 1e-10)
        throw std::runtime_error("Singular Radiance PRIMARIES.");
    const Imf::Chromaticities result(
      Imath::V2f(p[0], p[1]), Imath::V2f(p[2], p[3]),
      Imath::V2f(p[4], p[5]), Imath::V2f(p[6], p[7]));
    const auto forward = Imf::RGBtoXYZ(result, 1.f);
    const auto inverse = Imf::XYZtoRGB(result, 1.f);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            if (!std::isfinite(forward[y][x]) || !std::isfinite(inverse[y][x]))
                throw std::runtime_error("Invalid Radiance PRIMARIES transform.");
    return result;
}

void readBytes(QFile& file, void* bytes, int count)
{
    if (file.read(static_cast<char*>(bytes), count) != count)
        throw std::runtime_error("Truncated Radiance pixel data.");
}

unsigned char readByte(QFile& file)
{
    unsigned char value;
    readBytes(file, &value, 1);
    return value;
}

using Rgbe = std::array<unsigned char, 4>;

void readScanline(QFile& file, std::vector<Rgbe>& line, const Cancellation& cancel)
{
    Rgbe pixel;
    readBytes(file, pixel.data(), 4);
    const size_t length = line.size();
    if (length >= 8 && length <= 32767
        && pixel[0] == 2 && pixel[1] == 2 && !(pixel[2] & 128)) {
        if ((size_t(pixel[2]) << 8 | pixel[3]) != length)
            throw std::runtime_error("Radiance RLE scanline width does not match the header.");
        for (int component = 0; component < 4; ++component) {
            size_t position = 0;
            while (position < length) {
                checkCancelled(cancel);
                const unsigned code = readByte(file);
                const unsigned count = code > 128 ? code - 128 : code;
                if (!count || count > length - position)
                    throw std::runtime_error("Invalid Radiance RLE packet length.");
                if (code > 128) {
                    const auto value = readByte(file);
                    for (unsigned i = 0; i < count; ++i) line[position++][component] = value;
                } else {
                    unsigned char literal[128];
                    readBytes(file, literal, int(count));
                    for (unsigned i = 0; i < count; ++i) line[position++][component] = literal[i];
                }
            }
        }
        return;
    }

    // Flat RGBE and the older (1,1,1,count) repeat encoding share this path.
    size_t position = 0;
    unsigned shift = 0;
    while (position < length) {
        checkCancelled(cancel);
        if (pixel[0] == 1 && pixel[1] == 1 && pixel[2] == 1) {
            if (!position || shift >= 32)
                throw std::runtime_error("Invalid Radiance legacy RLE repeat.");
            const uint64_t count = uint64_t(pixel[3]) << shift;
            if (count > length - position)
                throw std::runtime_error("Radiance legacy RLE exceeds the scanline.");
            const Rgbe previous = line[position - 1];
            for (uint64_t i = 0; i < count; ++i) {
                if ((i & 4095) == 0) checkCancelled(cancel);
                line[position++] = previous;
            }
            shift += 8;
        } else {
            line[position++] = pixel;
            shift = 0;
        }
        if (position < length) readBytes(file, pixel.data(), 4);
    }
}
}

std::shared_ptr<RadianceInput> RadianceInput::open(
  const std::shared_ptr<QFile>& file, const Cancellation& cancel)
{
    try { return std::shared_ptr<RadianceInput>(new RadianceInput(file, cancel)); }
    catch (const Cancelled&) { return {}; }
}

RadianceInput::RadianceInput(const std::shared_ptr<QFile>& file, const Cancellation& cancel)
  : m_file(file)
{
    checkCancelled(cancel);
    const auto magic = headerLine(*file);
    if (magic != "#?RADIANCE" && magic != "#?RGBE")
        throw std::runtime_error("Not a Radiance RGBE image.");
    m_headerLines.append(QString::fromLatin1(magic));
    bool formatFound = false;
    bool hasPrimaries = false;
    Imf::Chromaticities chromaticities;
    double pixelAspect = 1.;
    int headerSize = magic.size();
    for (;;) {
        checkCancelled(cancel);
        const auto line = headerLine(*file);
        headerSize += line.size() + 1;
        if (headerSize > 1024 * 1024)
            throw std::runtime_error("Radiance header is too large.");
        if (line.trimmed().isEmpty()) break;
        m_headerLines.append(QString::fromLatin1(line));
        const int separator = line.indexOf('=');
        if (separator < 0) continue;
        const auto key = line.left(separator).trimmed();
        const auto value = line.mid(separator + 1).trimmed();
        if (key == "FORMAT") {
            if (value != "32-bit_rle_rgbe")
                throw std::runtime_error("Unsupported Radiance encoding; only RGBE is supported (not XYZE).");
            formatFound = true;
        } else if (key == "PRIMARIES") {
            chromaticities = primaries(value);
            hasPrimaries = true;
        } else if (key == "PIXASPECT") {
            auto stream = values(value);
            double ratio;
            std::string trailing;
            if (!(stream >> ratio) || !std::isfinite(ratio) || ratio <= 0. || (stream >> trailing))
                throw std::runtime_error("Invalid Radiance PIXASPECT.");
            pixelAspect *= ratio;
            if (!std::isfinite(pixelAspect) || pixelAspect <= 0.)
                throw std::runtime_error("Invalid cumulative Radiance PIXASPECT.");
        }
    }
    if (!formatFound) throw std::runtime_error("Missing Radiance FORMAT declaration.");
    checkCancelled(cancel);
    const auto resolution = headerLine(*file);
    m_resolutionLine = QString::fromLatin1(resolution);
    auto stream = values(resolution);
    for (auto& axis : m_axes) {
        std::string token;
        int64_t length;
        if (!(stream >> token >> length) || token.size() != 2
            || (token[0] != '+' && token[0] != '-')
            || (token[1] != 'X' && token[1] != 'Y')
            || length <= 0 || length > std::numeric_limits<int>::max() / 4)
            throw std::runtime_error("Invalid Radiance resolution or scan direction.");
        axis.name = token[1]; axis.positive = token[0] == '+'; axis.length = int(length);
    }
    std::string trailing;
    if (m_axes[0].name == m_axes[1].name || (stream >> trailing))
        throw std::runtime_error("Invalid Radiance resolution axes.");
    const int width = m_axes[m_axes[0].name == 'X' ? 0 : 1].length;
    const int height = m_axes[m_axes[0].name == 'Y' ? 0 : 1].length;
    if (uint64_t(width) * uint64_t(height) > uint64_t(std::numeric_limits<int>::max()) / 4)
        throw std::runtime_error("The Radiance image is too large to display safely.");
    // Radiance stores height/width; OpenEXR and the viewer use width/height.
    const float aspect = float(1. / pixelAspect);
    if (!std::isfinite(aspect) || aspect <= 0.f)
        throw std::runtime_error("Radiance pixel aspect ratio is outside the supported range.");
    m_header = Imf::Header(width, height);
    m_header.setType(Imf::SCANLINEIMAGE);
    m_header.pixelAspectRatio() = aspect;
    for (const auto* name : {"R", "G", "B"}) m_header.channels().insert(name, Imf::Channel(Imf::FLOAT));
    if (hasPrimaries) m_header.insert("chromaticities", Imf::ChromaticitiesAttribute(chromaticities));
    m_pixelOffset = file->pos();
}

std::shared_ptr<const std::vector<float>> RadianceInput::pixels(
  const Cancellation& cancel, const Progress& progress)
{
    try {
        std::unique_lock<std::timed_mutex> lock(m_mutex, std::defer_lock);
        while (!lock.try_lock_for(std::chrono::milliseconds(10))) checkCancelled(cancel);
        checkCancelled(cancel);
        if (m_pixels) return m_pixels;
        if (!m_file->seek(m_pixelOffset))
            throw std::runtime_error("Cannot seek to Radiance pixel data.");
        const int width = m_header.dataWindow().max.x + 1;
        const int height = m_header.dataWindow().max.y + 1;
        auto result = std::make_shared<std::vector<float>>(size_t(width) * height * 3);
        std::vector<Rgbe> line(size_t(m_axes[1].length));
        if (progress) progress->begin(LoadProgress::Decoding, m_axes[0].length);
        for (int row = 0; row < m_axes[0].length; ++row) {
            checkCancelled(cancel);
            readScanline(*m_file, line, cancel);
            for (int column = 0; column < m_axes[1].length; ++column) {
                if ((column & 4095) == 0) checkCancelled(cancel);
                const int indices[2] = {row, column};
                int x = 0, y = 0;
                for (int a = 0; a < 2; ++a) {
                    const auto& axis = m_axes[a];
                    const bool forward = axis.name == 'X' ? axis.positive : !axis.positive;
                    const int coordinate = forward ? indices[a] : axis.length - 1 - indices[a];
                    (axis.name == 'X' ? x : y) = coordinate;
                }
                const auto& pixel = line[size_t(column)];
                const float scale = pixel[3] ? std::ldexp(1.f, int(pixel[3]) - 136) : 0.f;
                const size_t offset = 3 * (size_t(y) * width + x);
                for (int c = 0; c < 3; ++c) (*result)[offset + c] = pixel[c] * scale;
            }
            if (progress) progress->advance();
        }
        checkCancelled(cancel);
        m_pixels = result;
        return m_pixels;
    } catch (const Cancelled&) { return {}; }
}
