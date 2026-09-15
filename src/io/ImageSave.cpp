#include "ImageSave.h"
#include <util/AnomalyMarkers.h>

#include <io/ImageSavePlan.h>
#include <model/OpenEXRImage.h>
#include <model/framebuffer/FramebufferModel.h>
#include <model/framebuffer/ToneMapping.h>
#include <util/ColorTransform.h>

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputPart.h>
#include <OpenEXR/ImfMultiPartOutputFile.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfOutputPart.h>

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
#include <vector>

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

    bool rgbChannelName(const std::string& name)
    {
        std::string  leaf = name;
        const size_t dot  = leaf.find_last_of('.');
        if (dot != std::string::npos) leaf = leaf.substr(dot + 1);

        return leaf == "R" || leaf == "G" || leaf == "B" || leaf == "A";
    }

    std::vector<int> selectedChannelIndexes(
      const std::vector<std::string>& names, ImageSave::ChannelScope scope)
    {
        std::vector<int> indexes;

        for (int i = 0; i < static_cast<int>(names.size()); i++) {
            if (scope == ImageSave::ChannelsAll || rgbChannelName(names[i])) {
                indexes.push_back(i);
            }
        }

        if (indexes.empty()) {
            for (int i = 0; i < static_cast<int>(names.size()); i++) {
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
        part.width       = model->width();
        part.height      = model->height();
        part.pixelAspect = model->pixelAspectRatio();
        part.dataWindow
          = boxFromRect(model->getDataWindow(), part.width, part.height);
        part.displayWindow
          = boxFromRect(model->getDisplayWindow(), part.width, part.height);

        const std::vector<std::string> names = model->rawChannelNames();
        const std::vector<int> indexes = selectedChannelIndexes(names, scope);
        const std::vector<int> components = model->rawChannelComponents();
        const std::vector<float>& raw  = model->getRawPixels();
        const int rawCount = model->rawPixelStride();

        part.channels.reserve(indexes.size());

        for (int srcIndex : indexes) {
            ChannelData channel;
            channel.name       = names[srcIndex];
            channel.sourceName = channel.name;
            channel.xSampling  = 1;
            channel.ySampling  = 1;
            channel.width      = part.width;
            channel.height     = part.height;
            channel.pixels.resize(part.width * part.height);

            for (int i = 0; i < part.width * part.height; i++) {
                channel.pixels[i] = raw[size_t(i) * rawCount + components[srcIndex]];
            }

            part.channels.push_back(channel);
        }

        return part;
    }

    PartData readSourcePart(
      OpenEXRImage*           image,
      int                     partIndex,
      ImageSave::ChannelScope scope,
      bool                    prefixNames)
    {
        Imf::MultiPartInputFile& file = image->getEXR();
        Imf::InputPart           input(file, partIndex);
        const Imf::Header&       header     = input.header();
        const Imath::Box2i       dataWindow = header.dataWindow();

        PartData part;
        part.width         = dataWindow.max.x - dataWindow.min.x + 1;
        part.height        = dataWindow.max.y - dataWindow.min.y + 1;
        part.pixelAspect   = header.pixelAspectRatio();
        part.dataWindow    = dataWindow;
        part.displayWindow = header.displayWindow();

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
            if (scope == ImageSave::ChannelsAll || rgbChannelName(it.name())) {
                channelCount++;
            }
        }

        part.channels.reserve(channelCount);

        for (Imf::ChannelList::ConstIterator it = channels.begin();
             it != channels.end();
             it++) {
            if (scope == ImageSave::ChannelsRgb && !rgbChannelName(it.name())) {
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

        if (part.channels.empty() && scope == ImageSave::ChannelsRgb) {
            return readSourcePart(
              image,
              partIndex,
              ImageSave::ChannelsAll,
              prefixNames);
        }

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

        {
            const std::lock_guard<std::mutex> lock(
              image->sharedEXR()->mutex);
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
        header.compression() = exrCompression(options.compression);

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
              QObject::tr("No source layers to save."));
        }

        if (parts.size() == 1) return writeSinglePartExr(parts[0], options);

        try {
            std::vector<Imf::Header> headers;
            headers.reserve(parts.size());

            for (const PartData& part : parts) {
                headers.push_back(exrHeader(part, options));
                headers.back().setName(part.name);
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
              QObject::tr("No source layers to save."));
        }

        PartData flattened;
        flattened.name          = "flattened";
        flattened.width         = parts[0].width;
        flattened.height        = parts[0].height;
        flattened.pixelAspect   = parts[0].pixelAspect;
        flattened.dataWindow    = parts[0].dataWindow;
        flattened.displayWindow = parts[0].displayWindow;

        for (const PartData& part : parts) {
            if (
              part.width != flattened.width
              || part.height != flattened.height) {
                return result(
                  ImageSave::StatusFailed,
                  QObject::tr(
                    "Cannot flatten parts with different dimensions."));
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

    int
    findChannel(const std::vector<std::string>& names, const std::string& leaf)
    {
        for (int i = 0; i < static_cast<int>(names.size()); i++) {
            std::string  name = names[i];
            const size_t dot  = name.find_last_of('.');
            if (dot != std::string::npos) name = name.substr(dot + 1);
            if (name == leaf) return i;
        }

        return -1;
    }

    std::array<int, 3> rgbComponents(const FramebufferModel* model)
    {
        const auto names = model->rawChannelNames();
        const auto components = model->rawChannelComponents();
        std::array<int, 3> rgb = {{-1, -1, -1}};
        const char* colorNames[] = {"R", "G", "B"};
        for (int c = 0; c < 3; ++c) {
            const int index = findChannel(names, colorNames[c]);
            if (index >= 0) rgb[c] = components[index];
        }
        // Preserve single R/G/B colors, rather than treating them as luminance.
        if (rgb[0] >= 0 || rgb[1] >= 0 || rgb[2] >= 0) return rgb;
        const int y = findChannel(names, "Y");
        if (y >= 0) return {{components[y], components[y], components[y]}};
        for (int c = 0; c < 3; ++c)
            rgb[c] = components[c < int(components.size()) ? c : 0];
        return rgb;
    }

    float rgbSample(const std::vector<float>& pixels, size_t offset, int component)
    {
        return component < 0 ? 0.f : pixels[offset + component];
    }

    ImageSave::Result
    writeHdr(const FramebufferModel* model, const ImageSave::Options& options)
    {
        const std::vector<std::string> names  = model->rawChannelNames();
        const std::vector<float>&      pixels = model->getRawPixels();
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

        const auto rgb = rgbComponents(model);

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
        QImage image = model->getLoadedImage();
        if (image.isNull()) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("The active image is empty."));
        }

        if (options.maxWidth > 0 && image.width() > options.maxWidth) {
            image
              = image.scaledToWidth(options.maxWidth, Qt::SmoothTransformation);
        }

        AnomalyMarkers::composite(image, *model);
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
        const std::vector<float>&      pixels = model->getRawPixels();
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

        const auto rgb = rgbComponents(model);

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
    readLayeredParts(OpenEXRImage* image, const ImageSave::Options& options)
    {
        std::vector<PartData> parts;
        if (!image) return parts;

        Imf::MultiPartInputFile& file      = image->getEXR();
        const int                partCount = file.parts();
        parts.reserve(partCount);
        const bool prefix = options.multipart == ImageSave::MultipartFlatten;

        for (int i = 0; i < partCount; i++) {
            parts.push_back(
              readSourcePart(image, i, options.channelScope, prefix));
        }

        return parts;
    }

    ImageSave::Result
    saveLayeredOriginal(OpenEXRImage* image, const ImageSave::Options& options)
    {
        if (options.format != ImageSave::FormatExr) {
            return result(
              ImageSave::StatusFailed,
              QObject::tr("Layered Original only supports EXR."));
        }

        std::vector<PartData> parts;

        try {
            parts = readLayeredParts(image, options);
        } catch (const std::exception& e) {
            return result(
              ImageSave::StatusFailed,
              QString::fromLocal8Bit(e.what()));
        }

        if (options.multipart == ImageSave::MultipartFlatten) {
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
            return saveLayeredOriginal(source.sourceImage, options);
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
