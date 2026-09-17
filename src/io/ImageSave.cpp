#include "ImageSave.h"
#include <util/MipLevels.h>
#include <limits>
#include <util/PreviewImage.h>

#include <io/ImageSavePlan.h>
#include <model/OpenEXRImage.h>
#include <model/framebuffer/FramebufferModel.h>
#include <model/framebuffer/ToneMapping.h>
#include <util/ColorTransform.h>

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputPart.h>
#include <OpenEXR/ImfMultiPartOutputFile.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfOutputPart.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfTiledInputPart.h>
#include <OpenEXR/ImfTileDescription.h>

#include <Imath/ImathBox.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <model/framebuffer/FramebufferLoader.h>
#include <QImageWriter>
#include <QObject>
#include <QRect>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <vector>
#include <utility>

namespace
{
    struct ChannelData {
        std::string        name;
        std::string        sourceName;
        int                xSampling;
        int                ySampling;
        int                width;
        int                height;
        std::vector<float> pixels;
    };

    struct PartData {
        std::string              name;
        int                      width;
        int                      height;
        float                    pixelAspect;
        Imath::Box2i             dataWindow;
        Imath::Box2i             displayWindow;
        bool                     hasChromaticities = false;
        Imf::Chromaticities       chromaticities;
        ViewMetadata             views;
        std::vector<ChannelData> channels;
    };

    ImageSave::Result result(
      ImageSave::Status  status,
      const QString&     message,
      const QStringList& paths = QStringList())
    {
        ImageSave::Result r;
        r.status  = status;
        r.message = message;
        r.paths   = paths;
        return r;
    }

    QByteArray nativePath(const QString& path)
    {
        return QDir::toNativeSeparators(path).toLocal8Bit();
    }


    QByteArray writerFormat(ImageSave::Format format)
    {
        if (format == ImageSave::FormatJpeg) return "jpg";
        return "png";
    }

    Imf::Compression exrCompression(ImageSave::ExrCompression compression)
    {
        switch (compression) {
            case ImageSave::CompressionNone:
                return Imf::Compression::NO_COMPRESSION;
            case ImageSave::CompressionRle:
                return Imf::Compression::RLE_COMPRESSION;
            case ImageSave::CompressionPiz:
                return Imf::Compression::PIZ_COMPRESSION;
            case ImageSave::CompressionDwaa:
                return Imf::Compression::DWAA_COMPRESSION;
            case ImageSave::CompressionDwab:
                return Imf::Compression::DWAB_COMPRESSION;
            case ImageSave::CompressionZip:
            default:
                return Imf::Compression::ZIP_COMPRESSION;
        }
    }

    Imf::PixelType exrPixelType(ImageSave::ExrPixelType type)
    {
        if (type == ImageSave::PixelHalf) return Imf::PixelType::HALF;
        return Imf::PixelType::FLOAT;
    }

    bool colorChannelName(const std::string& name)
    {
        std::string  leaf = name;
        const size_t dot  = leaf.find_last_of('.');
        if (dot != std::string::npos) leaf = leaf.substr(dot + 1);

        return leaf == "R" || leaf == "G" || leaf == "B" || leaf == "A"
               || leaf == "Y" || leaf == "RY" || leaf == "BY";
    }

    std::vector<int> selectedChannelIndexes(
      const std::vector<std::string>& names, ImageSave::ChannelScope scope)
    {
        std::vector<int> indexes;

        for (int i = 0; i < static_cast<int>(names.size()); i++) {
            if (scope == ImageSave::ChannelsAll || colorChannelName(names[i])) {
                indexes.push_back(i);
            }
        }

        return indexes;
    }

    int sampledSize(int size, int sampling)
    {
        return (size + sampling - 1) / sampling;
    }

    Imath::Box2i boxForSize(int width, int height)
    {
        return Imath::Box2i(
          Imath::V2i(0, 0),
          Imath::V2i(width - 1, height - 1));
    }

    Imath::Box2i boxFromRect(const QRect& rect, int width, int height)
    {
        if (!rect.isValid()) return boxForSize(width, height);

        return Imath::Box2i(
          Imath::V2i(rect.x(), rect.y()),
          Imath::V2i(
            rect.x() + rect.width() - 1,
            rect.y() + rect.height() - 1));
    }

    QString safeName(const std::string& name)
    {
        QString result = QString::fromStdString(name);
        result.replace(" ", "_");
        result.replace("/", "_");
        result.replace("\\", "_");
        if (result.isEmpty()) result = "part";
        return result;
    }

    PartData
    activePart(const FramebufferModel* model, ImageSave::ChannelScope scope)
    {
        PartData part;
        part.name        = "active";
        part.views       = model->rawViews();
        part.width       = model->width();
        part.height      = model->height();
        part.pixelAspect = model->pixelAspectRatio();
        part.dataWindow
          = boxFromRect(model->getDataWindow(), part.width, part.height);
        part.displayWindow
          = boxFromRect(model->getDisplayWindow(), part.width, part.height);
        if (const auto* chromaticities = model->rawChromaticities()) {
            part.hasChromaticities = true;
            part.chromaticities = *chromaticities;
        }

        const std::vector<std::string> names = model->rawChannelNames();
        const std::vector<int> indexes = selectedChannelIndexes(names, scope);
        const std::vector<int> components = model->rawChannelComponents();
        const auto sampling = model->rawChannelSampling();
        const std::vector<float>& raw  = model->getRawPixels();
        const int rawCount = model->rawPixelStride();

        part.channels.reserve(indexes.size());

        for (int srcIndex : indexes) {
            ChannelData channel;
            channel.name       = names[srcIndex];
            channel.sourceName = channel.name;
            channel.xSampling = sampling[srcIndex].x();
            channel.ySampling = sampling[srcIndex].y();
            const auto firstSample = [](int minimum, int step) {
                return (int64_t(minimum) / step + (minimum % step > 0 ? 1 : 0)) * step;
            };
            const int64_t firstX = firstSample(part.dataWindow.min.x, channel.xSampling);
            const int64_t firstY = firstSample(part.dataWindow.min.y, channel.ySampling);
            channel.width = int((int64_t(part.dataWindow.max.x) - firstX) / channel.xSampling + 1);
            channel.height = int((int64_t(part.dataWindow.max.y) - firstY) / channel.ySampling + 1);
            channel.pixels.resize(size_t(channel.width) * channel.height);
            for (int y = 0; y < channel.height; ++y)
                for (int x = 0; x < channel.width; ++x) {
                    const size_t localX = size_t(firstX + int64_t(x) * channel.xSampling - part.dataWindow.min.x);
                    const size_t localY = size_t(firstY + int64_t(y) * channel.ySampling - part.dataWindow.min.y);
                    channel.pixels[size_t(y) * channel.width + x]
                      = raw[(localY * part.width + localX) * rawCount + components[srcIndex]];
                }

            part.channels.push_back(channel);
        }

        return part;
    }

    PartData readSourcePart(
      OpenEXRImage*           image,
      int                     partIndex,
      ImageSave::ChannelScope scope,
      bool                    prefixNames, int level)
    {
        Imf::MultiPartInputFile& file = image->getEXR();
        const Imf::Header&       header     = file.header(partIndex);
        const auto geometry = MipLevels::query(image->sharedEXR(), partIndex, level);
        const auto dataWindow = geometry.data;
        const bool tiled = geometry.tiled;

        PartData part;
        const int64_t width = int64_t(dataWindow.max.x) - dataWindow.min.x + 1;
        const int64_t height = int64_t(dataWindow.max.y) - dataWindow.min.y + 1;
        if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / 4
            || height > std::numeric_limits<int>::max() / 4
            || width * height > std::numeric_limits<int>::max() / 4)
            throw std::runtime_error("The selected mip level is too large to export safely.");
        part.width = int(width);
        part.views         = ViewMetadata::read(header);
        part.height = int(height);
        part.pixelAspect   = header.pixelAspectRatio();
        part.dataWindow    = dataWindow;
        part.displayWindow = geometry.display;
        if (const auto* attribute
            = header.findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities")) {
            part.hasChromaticities = true;
            part.chromaticities = attribute->value();
        }

        if (header.hasName()) {
            part.name = header.name();
        } else {
            part.name = QString("part_%1").arg(partIndex).toStdString();
        }

        const Imf::ChannelList& channels     = header.channels();
        int                     channelCount = 0;

        for (Imf::ChannelList::ConstIterator it = channels.begin();
             it != channels.end();
             it++) {
            if (scope == ImageSave::ChannelsAll || colorChannelName(it.name())) {
                channelCount++;
            }
        }

        part.channels.reserve(channelCount);

        for (Imf::ChannelList::ConstIterator it = channels.begin();
             it != channels.end();
             it++) {
            if (scope == ImageSave::ChannelsRgb && !colorChannelName(it.name())) {
                continue;
            }

            const Imf::Channel& sourceChannel = it.channel();

            ChannelData channel;
            channel.name       = it.name();
            channel.sourceName = channel.name;
            if (prefixNames) {
                const QString prefix = QString("part_%1_%2")
                                         .arg(partIndex)
                                         .arg(safeName(part.name));
                channel.name = prefix.toStdString() + "." + channel.name;
            }
            channel.xSampling = sourceChannel.xSampling;
            channel.ySampling = sourceChannel.ySampling;
            channel.width     = sampledSize(part.width, channel.xSampling);
            channel.height    = sampledSize(part.height, channel.ySampling);
            channel.pixels.resize(channel.width * channel.height);

            part.channels.push_back(channel);
        }

        if (part.channels.empty()) return part;

        Imf::FrameBuffer framebuffer;

        for (ChannelData& channel : part.channels) {
            framebuffer.insert(
              channel.sourceName.c_str(),
              Imf::Slice::Make(
                Imf::PixelType::FLOAT,
                channel.pixels.data(),
                dataWindow,
                sizeof(float),
                channel.width * sizeof(float),
                channel.xSampling,
                channel.ySampling));
        }

        if (tiled) {
            const auto makePart = [&] {
                const std::lock_guard<std::mutex> lock(image->sharedEXR()->mutex);
                return Imf::TiledInputPart(file, partIndex);
            };
            Imf::TiledInputPart input = makePart();
            for (int y = 0; y < input.numYTiles(level); ++y) {
                const std::lock_guard<std::mutex> lock(image->sharedEXR()->mutex);
                input.setFrameBuffer(framebuffer);
                input.readTiles(0, input.numXTiles(level) - 1, y, y, level, level);
            }
        } else {
            const std::lock_guard<std::mutex> lock(
              image->sharedEXR()->mutex);
            Imf::InputPart input(file, partIndex);
            input.setFrameBuffer(framebuffer);
            input.readPixels(dataWindow.min.y, dataWindow.max.y);
        }

        return part;
    }

    Imf::Header
    exrHeader(const PartData& part, const ImageSave::Options& options)
    {
        const float pixelAspect = options.metadata == ImageSave::MetadataBasic
                                    ? part.pixelAspect
                                    : 1.f;
        Imf::Header header(part.displayWindow, part.dataWindow, pixelAspect);
        if (!part.name.empty()) header.setName(part.name);
        header.compression() = exrCompression(options.compression);
        if (options.metadata == ImageSave::MetadataBasic) part.views.write(header);
        if (options.metadata == ImageSave::MetadataBasic && part.hasChromaticities)
            header.insert("chromaticities", Imf::ChromaticitiesAttribute(part.chromaticities));

        for (const ChannelData& channel : part.channels) {
            header.channels().insert(
              channel.name.c_str(),
              Imf::Channel(
                exrPixelType(options.pixelType),
                channel.xSampling,
                channel.ySampling));
        }

        return header;
    }

    Imf::FrameBuffer exrFrameBuffer(const PartData& part)
    {
        Imf::FrameBuffer framebuffer;
        for (const ChannelData& channel : part.channels) {
            framebuffer.insert(
              channel.name.c_str(),
              Imf::Slice::Make(
                Imf::PixelType::FLOAT,
                const_cast<float*>(channel.pixels.data()),
                part.dataWindow,
                sizeof(float),
                channel.width * sizeof(float),
                channel.xSampling,
                channel.ySampling));
        }

        return framebuffer;
    }

    ImageSave::Result
    writeSinglePartExr(const PartData& part, const ImageSave::Options& options)
    {
        if (part.channels.empty())
            return result(ImageSave::StatusFailed,
                          QObject::tr("No channels match the selected channel filter."));
        try {
            const QByteArray filename = nativePath(options.path);
            Imf::Header      header   = exrHeader(part, options);
            Imf::OutputFile  file(filename.constData(), header);
            Imf::FrameBuffer framebuffer = exrFrameBuffer(part);
            file.setFrameBuffer(framebuffer);
            file.writePixels(part.height);
        } catch (const std::exception& e) {
            return result(
              ImageSave::StatusFailed,
              QString::fromLocal8Bit(e.what()));
        }

        return result(
          ImageSave::StatusSaved,
          QObject::tr("Saved %1").arg(options.path),
          QStringList() << options.path);
    }

    ImageSave::Result writeMultipartExr(
      const std::vector<PartData>& parts, const ImageSave::Options& options)
    {
        if (parts.empty()) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("No channels match the selected channel filter."));
        }

        if (parts.size() == 1) return writeSinglePartExr(parts[0], options);

        try {
            std::vector<Imf::Header> headers;
            headers.reserve(parts.size());

            for (const PartData& part : parts) {
                headers.push_back(exrHeader(part, options));
                headers.back().setType(Imf::SCANLINEIMAGE);
            }

            const QByteArray         filename = nativePath(options.path);
            Imf::MultiPartOutputFile file(
              filename.constData(),
              headers.data(),
              static_cast<int>(headers.size()));

            for (int i = 0; i < static_cast<int>(parts.size()); i++) {
                Imf::OutputPart  output(file, i);
                Imf::FrameBuffer framebuffer = exrFrameBuffer(parts[i]);
                output.setFrameBuffer(framebuffer);
                output.writePixels(parts[i].height);
            }
        } catch (const std::exception& e) {
            return result(
              ImageSave::StatusFailed,
              QString::fromLocal8Bit(e.what()));
        }

        return result(
          ImageSave::StatusSaved,
          QObject::tr("Saved %1").arg(options.path),
          QStringList() << options.path);
    }

    ImageSave::Result writeFlattenedExr(
      const std::vector<PartData>& parts, const ImageSave::Options& options)
    {
        if (parts.empty()) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("No channels match the selected channel filter."));
        }

        PartData flattened;
        flattened.name          = "flattened";
        flattened.width         = parts[0].width;
        flattened.height        = parts[0].height;
        flattened.pixelAspect   = parts[0].pixelAspect;
        flattened.dataWindow    = parts[0].dataWindow;
        flattened.displayWindow = parts[0].displayWindow;
        flattened.hasChromaticities = parts[0].hasChromaticities;
        flattened.chromaticities = parts[0].chromaticities;

        for (const PartData& part : parts) {
            // A single output header cannot describe different source gamuts.
            if (options.metadata == ImageSave::MetadataBasic
                && part.chromaticities != flattened.chromaticities) {
                return result(
                  ImageSave::StatusFailed,
                  QObject::tr("Cannot flatten parts with different chromaticities. Preserve parts instead."));
            }
            flattened.hasChromaticities |= part.hasChromaticities;
            if (part.dataWindow != flattened.dataWindow
                || part.displayWindow != flattened.displayWindow
                || part.pixelAspect != flattened.pixelAspect) {
                return result(
                  ImageSave::StatusFailed,
                  QObject::tr(
                    "Cannot flatten parts with different windows or pixel aspect ratios. Preserve parts instead."));
            }

            for (const ChannelData& channel : part.channels) {
                flattened.channels.push_back(channel);
            }
        }

        return writeSinglePartExr(flattened, options);
    }

    float saneHdrValue(float value)
    {
        if (!std::isfinite(value) || value < 0.f) return 0.f;
        return value;
    }

    void rgbe(float r, float g, float b, char bytes[4])
    {
        const float maxValue = std::max(r, std::max(g, b));

        if (maxValue < 1e-32f) {
            bytes[0] = 0;
            bytes[1] = 0;
            bytes[2] = 0;
            bytes[3] = 0;
            return;
        }

        int         exponent = 0;
        const float scale = std::frexp(maxValue, &exponent) * 256.f / maxValue;

        bytes[0] = static_cast<char>(std::min(255, int(r * scale)));
        bytes[1] = static_cast<char>(std::min(255, int(g * scale)));
        bytes[2] = static_cast<char>(std::min(255, int(b * scale)));
        bytes[3] = static_cast<char>(exponent + 128);
    }

    float rgbSample(const std::vector<float>& pixels, size_t offset, int component)
    {
        return component < 0 ? 0.f : pixels[offset + component];
    }

    ImageSave::Result
    writeHdr(const FramebufferModel* model, const ImageSave::Options& options)
    {
        const std::vector<std::string> names  = model->rawChannelNames();
        const std::vector<float>&      pixels = model->getDisplayPixels();
        const int channelCount                = static_cast<int>(names.size());
        const int width                       = model->width();
        const int height                      = model->height();

        if (channelCount == 0 || pixels.empty()) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("No raw data to save."));
        }

        const uint64_t expected
          = uint64_t(width) * uint64_t(height) * uint64_t(model->rawPixelStride());
        if (pixels.size() < expected) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("The raw framebuffer data is incomplete."));
        }

        const auto rgb = model->displayRgbComponents();

        QFile file(options.path);
        if (!file.open(QFile::WriteOnly)) {
            return result(ImageSave::StatusFailed, file.errorString());
        }

        QByteArray header("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n");
        header += QString("-Y %1 +X %2\n").arg(height).arg(width).toLatin1();
        if (file.write(header) != header.size()) {
            return result(ImageSave::StatusFailed, file.errorString());
        }

        QByteArray line;
        line.resize(4 * width);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                const size_t offset = model->rawPixelStride() * (size_t(y) * width + x);
                const float r = saneHdrValue(rgbSample(pixels, offset, rgb[0]));
                const float g = saneHdrValue(rgbSample(pixels, offset, rgb[1]));
                const float b = saneHdrValue(rgbSample(pixels, offset, rgb[2]));
                rgbe(r, g, b, line.data() + 4 * x);
            }

            if (file.write(line) != line.size()) {
                return result(ImageSave::StatusFailed, file.errorString());
            }
        }

        if (!file.flush()) {
            return result(ImageSave::StatusFailed, file.errorString());
        }

        return result(
          ImageSave::StatusSaved,
          QObject::tr("Saved %1").arg(options.path),
          QStringList() << options.path);
    }

    ImageSave::Result savePreview(
      const FramebufferModel* model, const ImageSave::Options& options)
    {
        if (!model || !model->isPreviewReady())
            return result(
              ImageSave::StatusFailed,
              "The current preview is still rendering.");
        const QColor background = options.format != ImageSave::FormatJpeg ? Qt::transparent
          : options.jpegBackground == ImageSave::BackgroundWhite ? Qt::white : Qt::black;
        QImage image = PreviewImage::render(*model, options.maxWidth, background);
        if (image.isNull()) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("Unable to create the preview image. Try a smaller output size."));
        }

        if (
          options.format == ImageSave::FormatJpeg && image.hasAlphaChannel()) {
            image = image.convertToFormat(QImage::Format_RGB888);
        }

        QImageWriter writer(options.path, writerFormat(options.format));
        if (options.format == ImageSave::FormatJpeg) {
            writer.setQuality(options.quality);
        }

        if (!writer.write(image)) {
            return result(ImageSave::StatusFailed, writer.errorString());
        }

        return result(
          ImageSave::StatusSaved,
          QObject::tr("Saved %1").arg(options.path),
          QStringList() << options.path);
    }

    unsigned char bracketByte(float value, double exposureMul)
    {
        return ToneMapping::toByte(ColorTransform::to_sRGB(exposureMul * value));
    }

    ImageSave::Result saveBracketedImages(
      const FramebufferModel* model, const ImageSave::Options& options)
    {
        if (
          options.format != ImageSave::FormatPng
          && options.format != ImageSave::FormatJpeg) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("HDR Bracketed Images only supports PNG and JPEG."));
        }

        const std::vector<std::string> names  = model->rawChannelNames();
        const std::vector<float>&      pixels = model->getDisplayPixels();
        const int channelCount                = static_cast<int>(names.size());
        const int width                       = model->width();
        const int height                      = model->height();

        if (channelCount == 0 || pixels.empty() || width <= 0 || height <= 0) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("No raw data to save."));
        }

        const uint64_t expected
          = uint64_t(width) * uint64_t(height) * uint64_t(model->rawPixelStride());
        if (pixels.size() < expected) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("The raw framebuffer data is incomplete."));
        }

        const auto rgb = model->displayRgbComponents();

        const QStringList         paths = ImageSavePlan::outputPaths(options);
        const std::vector<double> values
          = ImageSavePlan::bracketExposureValues(options);

        for (int i = 0; i < static_cast<int>(values.size()); i++) {
            QImage      image(width, height, QImage::Format_RGB888);
            const double exposureMul = std::exp2(values[i]);

            for (int y = 0; y < height; y++) {
                unsigned char* line = image.scanLine(y);

                for (int x = 0; x < width; x++) {
                    const size_t offset = model->rawPixelStride() * (size_t(y) * width + x);
                    line[3 * x + 0]
                      = bracketByte(rgbSample(pixels, offset, rgb[0]), exposureMul);
                    line[3 * x + 1]
                      = bracketByte(rgbSample(pixels, offset, rgb[1]), exposureMul);
                    line[3 * x + 2]
                      = bracketByte(rgbSample(pixels, offset, rgb[2]), exposureMul);
                }
            }

            if (options.maxWidth > 0 && image.width() > options.maxWidth) {
                image = image.scaledToWidth(
                  options.maxWidth,
                  Qt::SmoothTransformation);
            }

            QImageWriter writer(paths[i], writerFormat(options.format));
            if (options.format == ImageSave::FormatJpeg) {
                writer.setQuality(options.quality);
            }

            if (!writer.write(image)) {
                return result(
                  ImageSave::StatusFailed,
                  QObject::tr("Could not save %1: %2")
                    .arg(paths[i])
                    .arg(writer.errorString()));
            }
        }

        return result(
          ImageSave::StatusSaved,
          QObject::tr("Saved %1 images.").arg(paths.size()),
          paths);
    }

    ImageSave::Result saveActiveOriginal(
      const FramebufferModel* model, const ImageSave::Options& options)
    {
        if (model->width() <= 0 || model->height() <= 0) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("The active framebuffer is empty."));
        }
        if (model->rawChannelNames().empty()) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("The active framebuffer has no channels."));
        }

        const uint64_t expected = uint64_t(model->width())
                                  * uint64_t(model->height())
                                  * uint64_t(model->rawPixelStride());

        if (model->getRawPixels().size() < expected) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("The raw framebuffer data is incomplete."));
        }

        if (options.format == ImageSave::FormatHdr)
            return writeHdr(model, options);

        return writeSinglePartExr(
          activePart(model, options.channelScope),
          options);
    }

    std::vector<PartData>
    readLayeredParts(OpenEXRImage* image, const ImageSave::Options& options, int level)
    {
        std::vector<PartData> parts;
        if (!image) return parts;

        Imf::MultiPartInputFile& file      = image->getEXR();
        const int                partCount = file.parts();
        parts.reserve(partCount);
        const bool prefix = partCount > 1 && options.multipart == ImageSave::MultipartFlatten;
        if (prefix) {
            for (int i = 0; i < partCount; ++i) {
                if (ViewMetadata::read(file.header(i)).present())
                    throw std::runtime_error(
                      "Flattening multiview parts is not supported. Use Preserve multipart instead.");
            }
        }

        for (int i = 0; i < partCount; i++) {
            PartData part = readSourcePart(image, i, options.channelScope, prefix, level);
            if (!part.channels.empty()) parts.push_back(std::move(part));
        }

        return parts;
    }

    ImageSave::Result
    saveLayeredOriginal(OpenEXRImage* image, const ImageSave::Options& options, int level)
    {
        if (options.format != ImageSave::FormatExr) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("Layered Original only supports EXR."));
        }

        std::vector<PartData> parts;

        try {
            parts = readLayeredParts(image, options, level);
        } catch (const std::exception& e) {
            return result(
              ImageSave::StatusFailed,
              QString::fromLocal8Bit(e.what()));
        }

        if (options.multipart == ImageSave::MultipartFlatten && image->getEXR().parts() > 1) {
            return writeFlattenedExr(parts, options);
        }

        return writeMultipartExr(parts, options);
    }

    ImageSave::Result saveResolved(
      const ImageSave::Source& source, const ImageSave::Options& options)
    {
        if (options.target == ImageSave::TargetPreview) {
            if (!source.activeModel) {
                return result(
                  ImageSave::StatusFailed,
                  QObject::tr("No active image."));
            }
            return savePreview(source.activeModel, options);
        }

        if (options.target == ImageSave::TargetLayeredOriginal) {
            if (!source.sourceImage) {
                return result(
                  ImageSave::StatusFailed,
                  QObject::tr("No source image."));
            }
            return saveLayeredOriginal(source.sourceImage, options, source.mipLevel);
        }

        if (options.target == ImageSave::TargetHdrBracketedImages) {
            if (!source.activeModel) {
                return result(
                  ImageSave::StatusFailed,
                  QObject::tr("No active framebuffer."));
            }
            return saveBracketedImages(source.activeModel, options);
        }

        if (!source.activeModel) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("No active framebuffer."));
        }

        return saveActiveOriginal(source.activeModel, options);
    }
}   // namespace

namespace ImageSave
{
    QString extension(Format format)
    {
        switch (format) {
            case FormatJpeg:
                return "jpg";
            case FormatExr:
                return "exr";
            case FormatHdr:
                return "hdr";
            case FormatPng:
            default:
                return "png";
        }
    }

    QString filter(Format format)
    {
        switch (format) {
            case FormatJpeg:
                return QObject::tr("JPEG Image (*.jpg *.jpeg)");
            case FormatExr:
                return QObject::tr("OpenEXR Image (*.exr)");
            case FormatHdr:
                return QObject::tr("Radiance HDR Image (*.hdr)");
            case FormatPng:
            default:
                return QObject::tr("PNG Image (*.png)");
        }
    }

    QStringList outputPaths(const Source&, const Options& options)
    {
        return ImageSavePlan::outputPaths(options);
    }

    Result save(const Source& source, const Options& options)
    {
        if (source.activeModel && source.activeModel->isDerivedPreview()
            && (options.format == FormatHdr
                || (options.target != TargetPreview && options.target != TargetLayeredOriginal)))
            return result(StatusFailed, QObject::tr(
              "Anaglyph is a derived preview. Select a left/right source layer for active original, HDR or bracketed export."));
        if (options.conflict == ConflictCancel) {
            return result(StatusCancelled, QObject::tr("Save cancelled."));
        }

        Options resolved = options;
        resolved.path
          = ImageSavePlan::normalizedPath(resolved.path, resolved.format);
        QStringList paths = ImageSavePlan::outputPaths(resolved);
        QStringList conflicts;

        for (const QString& path : paths) {
            if (QFileInfo::exists(path)) conflicts << path;
        }

        if (!conflicts.isEmpty() && options.conflict == ConflictAsk) {
            return result(
              StatusConflict,
              QObject::tr("File already exists."),
              conflicts);
        }

        if (!conflicts.isEmpty() && options.conflict == ConflictRename) {
            resolved.path = ImageSavePlan::uniqueOutputPath(resolved);
        }

        return saveResolved(source, resolved);
    }
}   // namespace ImageSave
