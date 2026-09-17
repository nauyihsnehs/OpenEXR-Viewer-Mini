#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFont>
#include <QGraphicsPixmapItem>
#include <QMessageBox>
#include <QMenu>
#include <QStandardItemModel>
#include <QMap>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSemaphore>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>
#include <QTabBar>
#include <QtEndian>
#include <model/OpenEXRImage.h>
#include <io/ImageSave.h>
#include <model/StdIStream.h>
#include <model/framebuffer/FramebufferLoader.h>
#include <model/framebuffer/PixelDiagnostics.h>
#include <model/framebuffer/RGBFramebufferModel.h>
#include <util/AnomalyMarkers.h>
#include <util/PreviewImage.h>
#include <util/ViewMetadata.h>
#include <util/ResolutionLevels.h>
#include <OpenEXR/ImfTiledOutputPart.h>
#include <util/YColormap.h>
#include <util/ColormapModule.h>
#include <view/FileDrop.h>
#include <view/GraphicsView.h>
#include <view/CropIndicator.h>
#include <view/SaveImageDialog.h>
#include <view/ImageFileWidget.h>
#include <view/RGBFramebufferWidget.h>
#include <view/YFramebufferWidget.h>
#include <view/RangeSliderWidget.h>
#include <view/DepthRangeWidget.h>
#include <view/ScaleWidget.h>
#include <view/mainwindow.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfMultiPartOutputFile.h>
#include <OpenEXR/ImfOutputPart.h>
#include <OpenEXR/ImfStringVectorAttribute.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfTiledOutputFile.h>
#include <OpenEXR/ImfTileDescription.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineOutputFile.h>
#include <OpenEXR/ImfDeepScanLineOutputPart.h>
#include <OpenEXR/ImfDeepTiledOutputFile.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfRgbaFile.h>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>

namespace
{
    // Small point-sample fixture; file order deliberately differs from depth order.
    void writeDeepFixture(const QString& path, bool stereo = false, bool zBack = false,
                          int omittedPart = -1, float depthShift = 0.f, bool constant = false)
    {
        const int parts = stereo ? 2 : 1;
        const Imath::Box2i display(Imath::V2i(-3, -2), Imath::V2i(2, 2));
        std::vector<Imf::Header> headers;
        for (int eye = 0; eye < parts; ++eye) {
            const Imath::Box2i data(Imath::V2i(-2 + eye, -1 + eye), Imath::V2i(eye, eye));
            headers.emplace_back(display, data);
            auto& header = headers.back();
            header.setName(eye == 0 ? "rgba.left" : "rgba.right");
            header.setView(eye == 0 ? "left" : "right");
            header.setType(Imf::DEEPSCANLINE);
            header.setVersion(1);
            header.compression() = Imf::ZIPS_COMPRESSION;
            header.insert("chromaticities", Imf::ChromaticitiesAttribute(Imf::Chromaticities()));
            for (const char* name : {"R", "G", "B", "A"})
                header.channels().insert(name, Imf::Channel(Imf::HALF));
            header.channels().insert("Z", Imf::Channel(Imf::FLOAT));
            header.channels().insert("id", Imf::Channel(Imf::UINT));
            if (zBack) header.channels().insert("ZBack", Imf::Channel(Imf::FLOAT));
        }
        Imf::MultiPartOutputFile file(path.toLocal8Bit().constData(), headers.data(), parts);
        for (int eye = 0; eye < parts; ++eye) {
            if (eye == omittedPart) continue;
            std::vector<uint32_t> counts = {0, 3, 1, 2, 1, 1};
            const std::vector<size_t> offsets = {0, 0, 3, 4, 6, 7};
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const float inf = std::numeric_limits<float>::infinity();
            const float rgba[8][4] = {{.5f,.25f,0,1}, {.25f,.125f,0,.5f}, {.5f,0,.25f,.5f},
              {.25f,0,.5f,0}, {0,0,0,1}, {0,0,0,1}, {nan,0,0,1}, {.125f,.25f,.5f,.5f}};
            std::vector<float> z = {3, 1, 1, 2, nan, inf, 4, 2};
            if (constant) std::fill(z.begin(), z.end(), 7.f);
            for (auto& value : z) if (std::isfinite(value)) value += depthShift + eye * 10;
            std::vector<uint32_t> ids;
            for (uint32_t i = 0; i < 8; ++i) ids.push_back(4000000000u + i);
            std::array<std::vector<half>, 4> colors;
            for (size_t c = 0; c < 4; ++c)
                for (const auto& sample : rgba) colors[c].push_back(half(sample[c]));
            const auto window = headers[eye].dataWindow();
            Imf::DeepFrameBuffer buffer;
            buffer.insertSampleCountSlice(Imf::Slice::Make(Imf::UINT, counts.data(), window,
              sizeof(uint32_t), 3 * sizeof(uint32_t)));
            std::vector<std::vector<char*>> pointers(zBack ? 7 : 6, std::vector<char*>(6));
            const std::array<const char*, 7> names = {{"R", "G", "B", "A", "Z", "id", "ZBack"}};
            for (size_t c = 0; c < pointers.size(); ++c) {
                const auto type = c < 4 ? Imf::HALF : c == 5 ? Imf::UINT : Imf::FLOAT;
                const size_t stride = c < 4 ? sizeof(half) : sizeof(uint32_t);
                char* samples = c < 4 ? reinterpret_cast<char*>(colors[c].data())
                  : c == 5 ? reinterpret_cast<char*>(ids.data()) : reinterpret_cast<char*>(z.data());
                for (size_t p = 0; p < 6; ++p)
                    pointers[c][p] = counts[p] ? samples + offsets[p] * stride : nullptr;
                const auto slice = Imf::Slice::Make(Imf::FLOAT, pointers[c].data(), window,
                  sizeof(char*), 3 * sizeof(char*));
                buffer.insert(names[c], Imf::DeepSlice(type, slice.base, slice.xStride, slice.yStride, stride));
            }
            Imf::DeepScanLineOutputPart output(file, eye);
            output.setFrameBuffer(buffer);
            output.writePixels(2);
        }
    }

    void writeFixture(
      const QString&                                   path,
      int                                              width,
      int                                              height,
      const std::map<std::string, std::vector<float>>& channels,
      int                                              originX  = 0,
      int                                              originY  = 0,
      float                                            aspect   = 1.f,
      int                                              sampling = 1,
      const Imf::Header*                                headerTemplate = nullptr)
    {
        const Imath::Box2i window(
          Imath::V2i(originX, originY),
          Imath::V2i(originX + width - 1, originY + height - 1));
        Imf::Header header = headerTemplate ? *headerTemplate : Imf::Header(window, window, aspect);
        header.dataWindow() = window;
        header.pixelAspectRatio() = aspect;
        header.compression() = Imf::ZIP_COMPRESSION;
        Imf::FrameBuffer framebuffer;
        for (const auto& channel : channels) {
            const auto* metadata = header.channels().findChannel(channel.first);
            const int xs = metadata ? metadata->xSampling : sampling;
            const int ys = metadata ? metadata->ySampling : sampling;
            header.channels().insert(
              channel.first,
              Imf::Channel(Imf::FLOAT, xs, ys));
            framebuffer.insert(
              channel.first,
              Imf::Slice::Make(
                Imf::FLOAT,
                channel.second.data(),
                window,
                sizeof(float),
                size_t(width / xs) * sizeof(float),
                xs,
                ys));
        }
        if (header.hasTileDescription()) {
            Imf::TiledOutputFile file(path.toLocal8Bit().constData(), header);
            file.setFrameBuffer(framebuffer);
            for (int ly = 0; ly < file.numYLevels(); ++ly)
                for (int lx = 0; lx < file.numXLevels(); ++lx)
                    if (file.isValidLevel(lx, ly))
                        file.writeTiles(0, file.numXTiles(lx) - 1,
                                        0, file.numYTiles(ly) - 1, lx, ly);
        } else {
            Imf::OutputFile file(path.toLocal8Bit().constData(), header);
            file.setFrameBuffer(framebuffer);
            file.writePixels(height);
        }
    }

    // Each level stores distinct values, not a downsample of level zero.
    int writeMipFixture(const QString& path, Imf::LevelRoundingMode rounding = Imf::ROUND_DOWN,
                        bool omitLast = false)
    {
        const Imath::Box2i data(Imath::V2i(-4, -2), Imath::V2i(4, 4));
        const Imath::Box2i display(Imath::V2i(-3, -1), Imath::V2i(7, 7));
        Imf::Header header(display, data);
        header.setTileDescription(Imf::TileDescription(4, 3, Imf::MIPMAP_LEVELS, rounding));
        header.compression() = Imf::ZIP_COMPRESSION;
        header.insert("multiView", Imf::StringVectorAttribute({"left", "right"}));
        const std::vector<std::string> names = {"R", "G", "B", "right.R", "right.G", "right.B", "Z"};
        for (const auto& name : names) header.channels().insert(name, Imf::Channel(Imf::FLOAT));
        Imf::TiledOutputFile output(path.toLocal8Bit().constData(), header);
        for (int level = 0; level < output.numLevels() - (omitLast ? 1 : 0); ++level) {
            const auto window = output.dataWindowForLevel(level);
            const int width = output.levelWidth(level), height = output.levelHeight(level);
            std::vector<std::vector<float>> values(names.size(), std::vector<float>(size_t(width) * height));
            Imf::FrameBuffer buffer;
            for (size_t c = 0; c < names.size(); ++c) {
                for (size_t i = 0; i < values[c].size(); ++i)
                    values[c][i] = float(level) + float(c) / 10.f + float(i) / 1000.f;
                if (names[c] == "Z" && values[c].size() > 1)
                    values[c].back() = std::numeric_limits<float>::infinity();
                buffer.insert(names[c], Imf::Slice::Make(Imf::FLOAT, values[c].data(), window,
                              sizeof(float), size_t(width) * sizeof(float)));
            }
            output.setFrameBuffer(buffer);
            output.writeTiles(0, output.numXTiles(level) - 1, 0, output.numYTiles(level) - 1, level);
        }
        return output.numLevels();
    }

    float ripSample(ResolutionLevel level, int channel, size_t pixel)
    {
        return float(100 * level.x + 10 * level.y + channel) + float(pixel) / 1024.f;
    }

    QSize writeRipFixture(const QString& path, Imf::LevelRoundingMode rounding = Imf::ROUND_UP,
                          ResolutionLevel missing = {-1, -1}, float aspect = 1.f)
    {
        const Imath::Box2i data(Imath::V2i(-4, -2), Imath::V2i(4, 2));
        const Imath::Box2i display(Imath::V2i(-3, -1), Imath::V2i(7, 5));
        Imf::Header header(display, data);
        header.setTileDescription(Imf::TileDescription(4, 3, Imf::RIPMAP_LEVELS, rounding));
        header.pixelAspectRatio() = aspect;
        header.compression() = Imf::ZIP_COMPRESSION;
        header.insert("multiView", Imf::StringVectorAttribute({"left", "right"}));
        const std::vector<std::string> names = {"R", "G", "B", "A", "right.R", "right.G", "right.B", "right.A", "Z"};
        for (const auto& name : names) header.channels().insert(name, Imf::Channel(Imf::FLOAT));
        Imf::TiledOutputFile output(path.toLocal8Bit().constData(), header);
        for (int x = 0; x < output.numXLevels(); ++x) {
            for (int y = 0; y < output.numYLevels(); ++y) {
                const ResolutionLevel level(x, y);
                if (level == missing) continue;
                const auto window = output.dataWindowForLevel(x, y);
                const int width = output.levelWidth(x), height = output.levelHeight(y);
                std::vector<std::vector<float>> values(names.size(), std::vector<float>(size_t(width) * height));
                Imf::FrameBuffer buffer;
                for (size_t c = 0; c < names.size(); ++c) {
                    for (size_t i = 0; i < values[c].size(); ++i)
                        values[c][i] = c == 3 || c == 7 ? 0.f : ripSample(level, int(c), i);
                    if (names[c] == "Z" && values[c].size() > 1)
                        values[c].back() = std::numeric_limits<float>::infinity();
                    buffer.insert(names[c], Imf::Slice::Make(Imf::FLOAT, values[c].data(), window,
                                  sizeof(float), size_t(width) * sizeof(float)));
                }
                output.setFrameBuffer(buffer);
                output.writeTiles(0, output.numXTiles(x) - 1, 0, output.numYTiles(y) - 1, x, y);
            }
        }
        return QSize(output.numXLevels(), output.numYLevels());
    }

    struct MultipartFixture {
        std::vector<Imf::Header> headers;
        std::vector<std::map<std::string, std::vector<float>>> samples;
    };

    MultipartFixture beachballFixture()
    {
        MultipartFixture fixture;
        const Imath::Box2i display(Imath::V2i(-3, -2), Imath::V2i(2, 3));
        const Imath::Box2i right(Imath::V2i(-2, -1), Imath::V2i(0, 0));
        const Imath::Box2i left(Imath::V2i(-1, 1), Imath::V2i(1, 2));
        const Imath::Box2i both(Imath::V2i(-2, -1), Imath::V2i(1, 2));
        const std::vector<std::vector<std::string>> channels = {
          {"R", "G", "B", "A"}, {"Z"}, {"forward.u", "forward.v"}, {"whitebarmask.mask"},
          {"R", "G", "B", "A"}, {"Z"}, {"forward.u", "forward.v"},
          {"disparityL.x", "disparityL.y"}, {"disparityR.x", "disparityR.y"}, {"whitebarmask.mask"}};
        const std::array<std::string, 10> views = {"right", "left", "left", "left", "left",
                                                  "right", "right", "", "", "right"};
        for (int part = 0; part < 10; ++part) {
            Imath::Box2i window = views[part] == "left" ? left : views[part] == "right" ? right : both;
            if (part == 3 || part == 9) window.max = window.min;
            Imf::Header header(display, window);
            // Intentionally misleading names: view attributes are the source of truth.
            header.setName(part == 0 ? "left_named_part" : part == 4 ? "right_named_part"
                                                                         : "part_" + std::to_string(part));
            if (!views[part].empty()) header.setView(views[part]);
            fixture.headers.push_back(header);
            fixture.samples.emplace_back();
            const int count = (window.max.x - window.min.x + 1) * (window.max.y - window.min.y + 1);
            for (size_t c = 0; c < channels[part].size(); ++c) {
                auto& values = fixture.samples.back()[channels[part][c]];
                for (int i = 0; i < count; ++i) values.push_back(float(part * 10 + c) + i / 16.f);
            }
        }
        return fixture;
    }

    void writeMultipartFixture(const QString& path, MultipartFixture fixture, int omittedPart = -1)
    {
        for (size_t i = 0; i < fixture.headers.size(); ++i) {
            fixture.headers[i].setType(Imf::SCANLINEIMAGE);
            fixture.headers[i].compression() = Imf::ZIP_COMPRESSION;
            for (const auto& channel : fixture.samples[i])
                fixture.headers[i].channels().insert(channel.first, Imf::Channel(Imf::FLOAT));
        }
        Imf::MultiPartOutputFile file(path.toLocal8Bit().constData(), fixture.headers.data(),
                                     int(fixture.headers.size()));
        for (size_t i = 0; i < fixture.headers.size(); ++i) {
            if (int(i) == omittedPart) continue;
            const auto window = fixture.headers[i].dataWindow();
            const int width = window.max.x - window.min.x + 1;
            Imf::FrameBuffer buffer;
            for (const auto& channel : fixture.samples[i])
                buffer.insert(channel.first, Imf::Slice::Make(Imf::FLOAT, channel.second.data(),
                              window, sizeof(float), width * sizeof(float)));
            Imf::OutputPart output(file, int(i));
            output.setFrameBuffer(buffer);
            output.writePixels(window.max.y - window.min.y + 1);
        }
    }

    MultipartFixture stereoFixture()
    {
        auto fixture = beachballFixture();
        fixture.headers = {fixture.headers[4], fixture.headers[0]};
        fixture.samples.resize(2);
        fixture.headers[0].dataWindow() = Imath::Box2i(Imath::V2i(-2, -1), Imath::V2i(0, 0));
        fixture.headers[1].dataWindow() = Imath::Box2i(Imath::V2i(-1, 0), Imath::V2i(1, 1));
        const float colors[2][4] = {{.25f, .5f, .75f, 0.f}, {.9f, .125f, .5f, .25f}};
        const std::array<std::string, 4> channels = {{"R", "G", "B", "A"}};
        for (size_t eye = 0; eye < 2; ++eye) {
            fixture.samples[eye].clear();
            for (size_t c = 0; c < 4; ++c)
                fixture.samples[eye][channels[c]] = std::vector<float>(6, colors[eye][c]);
        }
        return fixture;
    }

    std::array<RGBFramebufferModel::Input, 2> stereoInputs()
    {
        return {{{0, RGBFramebufferModel::Layer_RGB, {{"R", "G", "B", "A"}}},
                 {1, RGBFramebufferModel::Layer_RGB, {{"R", "G", "B", "A"}}}}};
    }

    const LayerItem* findLayer(
      const LayerItem* item, LayerItem::LayerType type, const std::string& name)
    {
        if (item->getType() == type && item->getOriginalFullName() == name)
            return item;
        for (auto* child : item->children())
            if (const auto* match = findLayer(child, type, name)) return match;
        return nullptr;
    }

    class FileWidget: public ImageFileWidget
    {
      public:
        using ImageFileWidget::ImageFileWidget;
        using ImageFileWidget::openLayer;
    };

    class SyntheticModel: public FramebufferModel
    {
      public:
        using FramebufferModel::Decoder;
        using FramebufferModel::Renderer;
        using FramebufferModel::requestRender;
        using FramebufferModel::startLoading;
        std::string getColorInfo(int, int) const override { return ""; }
        std::vector<std::string> rawChannelNames() const override
        {
            return {"Y"};
        }
        void load()
        {
            startLoading([](const Cancellation&) {
                auto data   = std::make_shared<FramebufferData>();
                data->width = data->height = 1;
                data->dataWindow = data->displayWindow = QRect(0, 0, 1, 1);
                data->pixels                           = {0.f};
                DecodeResult result;
                result.data = data;
                return result;
            });
        }

      protected:
        void updateImage() override {}
    };

    void dismissNextError()
    {
        QTimer::singleShot(0, [] {
            for (QWidget* widget : QApplication::topLevelWidgets())
                if (auto* message = qobject_cast<QMessageBox*>(widget))
                    message->accept();
        });
    }
}   // namespace

class ViewerTests: public QObject
{
    Q_OBJECT
    QTemporaryDir m_directory;
    QString       fixture(const QString& name) const
    {
        return m_directory.filePath(name + ".exr");
    }

  private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(
          QSettings::IniFormat,
          QSettings::UserScope,
          m_directory.path());
#ifdef _WIN32
        QApplication::setFont(QFont("Segoe UI", 9));
#endif
    }

    void anomalyRegionsJoinDiagonalsAndKeepSourceCounts()
    {
        const auto nan = uint8_t(FramebufferData::NaN);
        const auto positive = uint8_t(FramebufferData::PositiveInf);
        const auto negative = uint8_t(FramebufferData::NegativeInf);
        const std::vector<uint8_t> mask = {
          nan, 0, positive, 0, 0,
          0, nan, 0, 0, 0,
          0, 0, 0, 0, 0,
          0, 0, 0, 0, negative};
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        const auto regions = PixelDiagnostics::connectedRegions(mask, 5, 4, cancel);
        QCOMPARE(regions.size(), size_t(2));
        uint64_t pixels = 0;
        for (const auto& region : regions) {
            pixels += region.pixelCount;
            if (region.flags & nan) {
                QCOMPARE(region.bounds, QRect(0, 0, 3, 2));
                QCOMPARE(region.pixelCount, uint64_t(3));
                QCOMPARE(region.flags, uint8_t(nan | positive));
            } else {
                QCOMPARE(region.bounds, QRect(4, 3, 1, 1));
                QCOMPARE(region.flags, negative);
            }
        }
        QCOMPARE(pixels, uint64_t(4));
        cancel->store(true);
        QVERIFY(PixelDiagnostics::connectedRegions(mask, 5, 4, cancel).empty());
    }

    void anomalyMarkersStayReadableWithoutRerenderingPixels()
    {
        const QString path = fixture("anomaly-markers");
        std::vector<float> samples(128 * 128, 0.25f);
        samples[64 * 128 + 64] = std::numeric_limits<float>::quiet_NaN();
        writeFixture(path, 128, 128, {{"V", samples}});
        OpenEXRImage source(path, nullptr);
        YFramebufferModel model("V");
        model.load(source.sharedEXR(), 0);
        QTRY_VERIFY(model.isPreviewReady());
        QCOMPARE(model.getDatasetNaNCount(), uint64_t(1));
        QCOMPARE(model.anomalyRegions().size(), size_t(1));
        const QImage base = model.getLoadedImage();
        QSignalSpy rendered(&model, &FramebufferModel::imageChanged);
        QSignalSpy readiness(&model, &FramebufferModel::readinessChanged);
        QSignalSpy markers(&model, &FramebufferModel::anomalyMarkersChanged);
        model.setHighlightNonFinite(true);
        QVERIFY(model.isPreviewReady());
        QCOMPARE(markers.count(), 1);
        QCOMPARE(rendered.count(), 0);
        QCOMPARE(readiness.count(), 0);
        for (int size : {32, 128}) {
            const QImage resized = base.scaled(size, size, Qt::IgnoreAspectRatio,
                                               Qt::SmoothTransformation);
            QImage marked = resized;
            AnomalyMarkers::composite(marked, model);
            QRect changed;
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    if (marked.pixel(x, y) != resized.pixel(x, y))
                        changed = changed.united(QRect(x, y, 1, 1));
            QVERIFY(changed.width() >= 12 && changed.width() <= 18);
            QVERIFY(changed.height() >= 12 && changed.height() <= 18);
            QCOMPARE(marked.pixel(size / 2, size / 2), resized.pixel(size / 2, size / 2));
        }
        QCOMPARE(model.getLoadedImage(), base);
        QVERIFY(std::isnan(model.getRawPixels()[64 * 128 + 64]));
        model.setHighlightNonFinite(false);
        QImage unmarked = base;
        AnomalyMarkers::composite(unmarked, model);
        QCOMPARE(unmarked, base);
    }

    void overlappingMarkersMergeAndRemainVisibleOverTransparency()
    {
        SyntheticModel model;
        model.startLoading([](const Cancellation&) {
            auto data = std::make_shared<FramebufferData>();
            data->width = data->height = 64;
            data->dataWindow = data->displayWindow = QRect(0, 0, 64, 64);
            FramebufferData::AnomalyRegion first, second;
            first.bounds = QRect(20, 20, 1, 1);
            first.pixelCount = 1;
            first.flags = FramebufferData::PositiveInf;
            second.bounds = QRect(24, 20, 1, 1);
            second.pixelCount = 1;
            second.flags = FramebufferData::NaN;
            data->anomalyRegions = {first, second};
            DecodeResult result;
            result.data = data;
            return result;
        });
        QTRY_VERIFY(model.isImageLoaded());
        model.setHighlightNonFinite(true);
        QImage image(64, 64, QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        AnomalyMarkers::composite(image, model);
        bool opaqueMagenta = false;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() == 0) continue;
                // NaN wins when the disconnected source points' screen markers merge.
                QCOMPARE(pixel.green(), 0);
                if (pixel.alpha() == 255 && pixel.red() > 0 && pixel.blue() > 0)
                    opaqueMagenta = true;
            }
        QVERIFY(opaqueMagenta);
        QCOMPARE(image.pixelColor(22, 20).alpha(), 0); // No interior fill.
    }

    void newestRenderWinsWithoutBlocking()
    {
        SyntheticModel model;
        model.load();
        QTRY_VERIFY(model.isImageLoaded());
        auto started = std::make_shared<QSemaphore>();
        auto release = std::make_shared<QSemaphore>();
        // This job deliberately ignores cancellation to verify generation checks.
        model.requestRender([started, release](const Cancellation&) {
            started->release();
            release->acquire();
            QImage image(1, 1, QImage::Format_RGB32);
            image.fill(Qt::red);
            return image;
        });
        QTRY_VERIFY(started->available());
        QSignalSpy changes(&model, &FramebufferModel::imageChanged);
        bool       guiThread = false;
        connect(&model, &FramebufferModel::imageChanged, &model, [&] {
            guiThread
              = QThread::currentThread() == QApplication::instance()->thread();
        });
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 100; ++i) {
            model.requestRender([i](const Cancellation&) {
                QImage image(1, 1, QImage::Format_RGB32);
                image.fill(QColor(i, 0, 0));
                return image;
            });
        }
        const qint64 elapsed = timer.elapsed();
        release->release();
        QVERIFY2(
          elapsed < 100,
          "Parameter changes must not wait for a worker.");
        QVERIFY(!model.isPreviewReady());
        QTRY_VERIFY(model.isPreviewReady());
        QCOMPARE(changes.count(), 1);
        QCOMPARE(model.getLoadedImage().pixelColor(0, 0), QColor(99, 0, 0));
        QVERIFY(guiThread);
    }

    void destructionDoesNotWaitForDecodeOrRender()
    {
        for (bool decoding : {false, true}) {
            auto  started  = std::make_shared<QSemaphore>();
            auto  release  = std::make_shared<QSemaphore>();
            auto  finished = std::make_shared<QSemaphore>();
            auto* model    = new SyntheticModel;
            if (decoding)
                model->startLoading(
                  [started, release, finished](const Cancellation&) {
                      started->release();
                      release->acquire();
                      finished->release();
                      return DecodeResult();
                  });
            else {
                model->load();
                QTRY_VERIFY(model->isImageLoaded());
                model->requestRender(
                  [started, release, finished](const Cancellation&) {
                      started->release();
                      release->acquire();
                      finished->release();
                      return QImage();
                  });
            }
            QTRY_VERIFY(started->available());
            QElapsedTimer timer;
            timer.start();
            delete model;
            const qint64 elapsed = timer.elapsed();
            release->release();
            QVERIFY(elapsed < 100);
            QTRY_VERIFY(finished->available());
        }
    }

    void nestedLuminanceAlphaAndSharedSourceLifetime()
    {
        const QString path = fixture("ya");
        writeFixture(
          path,
          2,
          1,
          {{"beauty.Y", {0.2f, 0.8f}}, {"beauty.A", {0.25f, 0.75f}}});
        FileWidget widget(path);
        widget.show();
        const auto* model = widget.activeFramebufferModel();
        QVERIFY(model);
        QTRY_VERIFY(model->isPreviewReady());
        QCOMPARE(model->getRawPixels()[3], 0.25f);
        QCOMPARE(model->getRawPixels()[7], 0.75f);
        QVERIFY(std::abs(model->getRawPixels()[0] - 0.2f) < 0.00001f);
        QCOMPARE(model->getLoadedImage().pixelColor(1, 0).alpha(), 255);

        RGBFramebufferModel detached("beauty.", RGBFramebufferModel::Layer_Y);
        {
            OpenEXRImage source(path, nullptr);
            detached.load(
              source.sharedEXR(),
              0,
              {{"beauty.Y", "", "", "beauty.A"}});
        }
        QTRY_VERIFY(detached.isPreviewReady());
        QCOMPARE(detached.getAlphaInfo(0, 0), 0.25f);
    }

    void sampledChannelsUseMetadataAndOriginalCoordinates()
    {
        const QString path = fixture("sampled");
        writeFixture(
          path,
          4,
          4,
          {{"nested.depth", {1.f, 2.f, 3.f, 4.f}}},
          -4,
          6,
          2.f,
          2);
        OpenEXRImage      source(path, nullptr);
        YFramebufferModel model("nested.depth");
        model.load(source.sharedEXR(), 0);
        QTRY_VERIFY(model.isPreviewReady());
        QCOMPARE(model.getDataWindow(), QRect(-4, 6, 4, 4));
        QCOMPARE(model.pixelAspectRatio(), 2.f);
        QCOMPARE(model.getRawPixels().size(), size_t(16));
        QCOMPARE(model.getRawPixels()[0], 1.f);
        QCOMPARE(model.getRawPixels()[3], 2.f);
        QCOMPARE(model.getRawPixels()[12], 3.f);
        QCOMPARE(model.getRawPixels()[15], 4.f);
        QVERIFY(model.getColorInfo(0, 0).find("x: -4 y: 6") == 0);
    }

    void oneRowChromaAndConcurrentLayers()
    {
        const QString path = fixture("chroma");
        writeFixture(
          path,
          2,
          1,
          {{"light.Y", {0.25f, 0.5f}},
           {"light.RY", {0.f, 0.f}},
           {"light.BY", {0.f, 0.f}},
           {"light.A", {0.125f, 0.75f}}});
        OpenEXRImage        source(path, nullptr);
        RGBFramebufferModel rgb("light.", RGBFramebufferModel::Layer_YC);
        YFramebufferModel   alpha("light.A"), luminance("light.Y");
        rgb.load(
          source.sharedEXR(),
          0,
          {{"light.Y", "light.RY", "light.BY", "light.A"}});
        alpha.load(source.sharedEXR(), 0);
        luminance.load(source.sharedEXR(), 0);
        QTRY_VERIFY(
          rgb.isPreviewReady() && alpha.isPreviewReady()
          && luminance.isPreviewReady());
        QVERIFY(std::abs(rgb.getRedInfo(0, 0) - 0.25f) < 0.002f);
        QCOMPARE(rgb.getAlphaInfo(0, 0), 0.125f);
        QCOMPARE(rgb.getLoadedImage().pixelColor(0, 0).alpha(), 255);
        QCOMPARE(alpha.getRawPixels()[1], 0.75f);
        QCOMPARE(luminance.getRawPixels()[1], 0.5f);
        QVERIFY(rgb.rawChannelNames() == std::vector<std::string>({"light.Y", "light.RY", "light.BY", "light.A"}));
        QCOMPARE(rgb.getRawPixels()[1], 0.f); // RY, not reconstructed green.
        QCOMPARE(rgb.getRawPixels()[2], 0.f);
    }

    void ycReconstructionMatchesOfficialReader()
    {
        for (int size : {2, 8, 32}) {
            const int origin = -2;
            const int offset = -origin * (size + 1);
            std::vector<Imf::Rgba> pixels(size * size + offset);
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x) {
                    auto& p = pixels[y * size + x];
                    p.r = x < size / 2 ? 0.8f : 0.1f;
                    p.g = y < size / 2 ? 0.2f : 0.7f;
                    p.b = (x + y) % 3 ? 0.05f : 0.6f;
                    p.a = 0.5f;
                }
            const QString path = fixture(QString("official-yca-%1").arg(size));
            const Imath::Box2i window(Imath::V2i(origin), Imath::V2i(origin + size - 1));
            {
                Imf::Header header(window, window);
                Imf::RgbaOutputFile file(path.toLocal8Bit().constData(), header, Imf::WRITE_YCA);
                file.setFrameBuffer(pixels.data() + offset, 1, size);
                file.writePixels(size);
            }
            std::vector<Imf::Rgba> reference(size * size + offset);
            {
                Imf::RgbaInputFile file(path.toLocal8Bit().constData());
                file.setFrameBuffer(reference.data() + offset, 1, size);
                file.readPixels(origin, origin + size - 1);
            }
            OpenEXRImage source(path, nullptr);
            RGBFramebufferModel model("", RGBFramebufferModel::Layer_YC);
            model.load(source.sharedEXR(), 0, {{"Y", "RY", "BY", "A"}});
            QTRY_VERIFY(model.isPreviewReady());
            for (int i = 0; i < size * size; ++i) {
                const auto& display = model.getDisplayPixels();
                QVERIFY(std::abs(display[4 * i] - float(reference[i].r)) < 0.002f);
                QVERIFY(std::abs(display[4 * i + 1] - float(reference[i].g)) < 0.002f);
                QVERIFY(std::abs(display[4 * i + 2] - float(reference[i].b)) < 0.002f);
                QCOMPARE(model.getRawPixels()[4 * i + 3], 0.5f);
            }
        }
    }

    void ycSourceSamplingReadoutsAndExports()
    {
        Imf::Header header(4, 4);
        header.channels().insert("beauty.RY", Imf::Channel(Imf::FLOAT, 2, 2));
        header.channels().insert("beauty.BY", Imf::Channel(Imf::FLOAT, 2, 2));
        const Imf::Chromaticities xyz(Imath::V2f(1, 0), Imath::V2f(0, 1),
                                      Imath::V2f(0, 0), Imath::V2f(1.f / 3.f));
        header.insert("chromaticities", Imf::ChromaticitiesAttribute(xyz));
        const QString path = fixture("yc-source");
        writeFixture(path, 4, 4,
          {{"beauty.Y", std::vector<float>(16, 0.5f)},
           {"beauty.RY", {0.125f, -0.25f, 0.5f, 1.f}},
           {"beauty.BY", {-0.5f, 0.f, 0.25f, 0.5f}},
           {"beauty.A", std::vector<float>(16, 0.f)}}, -2, -2, 1.f, 1, &header);
        OpenEXRImage source(path, nullptr);
        RGBFramebufferModel model("beauty.", RGBFramebufferModel::Layer_YC);
        YFramebufferModel ry("beauty.RY");
        model.load(source.sharedEXR(), 0, {{"beauty.Y", "beauty.RY", "beauty.BY", "beauty.A"}});
        ry.load(source.sharedEXR(), 0);
        QTRY_VERIFY(model.isPreviewReady() && ry.isPreviewReady());
        QCOMPARE(model.rawChannelSampling()[1], QPoint(2, 2));
        for (int i = 0; i < 16; ++i)
            QCOMPARE(model.getRawPixels()[4 * i + 1], ry.getRawPixels()[i]);
        QVERIFY(model.getColorInfo(1, 1).find("beauty.RY: 0.125 @ (-2, -2)") != std::string::npos);
        QVERIFY(ry.getColorInfo(1, 1).find("beauty.RY: 0.125 @ (-2, -2)") != std::string::npos);
        QVERIFY(model.rawChromaticities() && *model.rawChromaticities() == xyz);
        for (int kind = 0; kind < 3; ++kind) {
            ImageSave::Source input;
            input.activeModel = kind == 1 ? static_cast<FramebufferModel*>(&ry) : &model;
            input.sourceImage = &source;
            ImageSave::Options options;
            options.target = kind == 2 ? ImageSave::TargetLayeredOriginal : ImageSave::TargetActiveOriginal;
            options.format = ImageSave::FormatExr;
            options.pixelType = ImageSave::PixelFloat;
            options.channelScope = ImageSave::ChannelsRgb;
            options.path = fixture(QString("yc-roundtrip-%1").arg(kind));
            QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
            OpenEXRImage exported(options.path, nullptr);
            const auto& h = exported.getEXR().header(0);
            const auto* channel = h.channels().findChannel("beauty.RY");
            QVERIFY(channel);
            QCOMPARE(channel->xSampling, 2);
            QCOMPARE(channel->ySampling, 2);
            QVERIFY(!h.channels().findChannel("R"));
            if (kind != 1) QVERIFY(h.channels().findChannel("beauty.Y"));
            const auto* chroma = h.findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities");
            QVERIFY(chroma && chroma->value() == xyz);
            YFramebufferModel result("beauty.RY");
            result.load(exported.sharedEXR(), 0);
            QTRY_VERIFY(result.isPreviewReady());
            QVERIFY(result.getRawPixels() == ry.getRawPixels());
            if (kind != 1) {
                RGBFramebufferModel colors("beauty.", RGBFramebufferModel::Layer_YC);
                colors.load(exported.sharedEXR(), 0, {{"beauty.Y", "beauty.RY", "beauty.BY", "beauty.A"}});
                QTRY_VERIFY(colors.isPreviewReady());
                QVERIFY(colors.getRawPixels() == model.getRawPixels());
            }
        }
    }

    void ycDiagnosticsCountStoredSamples()
    {
        Imf::Header header(4, 4);
        header.channels().insert("RY", Imf::Channel(Imf::FLOAT, 2, 2));
        header.channels().insert("BY", Imf::Channel(Imf::FLOAT, 2, 2));
        const QString path = fixture("yc-source-anomalies");
        writeFixture(path, 4, 4, {{"Y", std::vector<float>(16, 0.5f)},
          {"RY", {std::numeric_limits<float>::infinity(), 0.f, 0.f, 0.f}},
          {"BY", {0.f, 0.f, 0.f, std::numeric_limits<float>::quiet_NaN()}}},
          0, 0, 1.f, 1, &header);
        OpenEXRImage source(path, nullptr);
        RGBFramebufferModel model("", RGBFramebufferModel::Layer_YC);
        model.load(source.sharedEXR(), 0, {{"Y", "RY", "BY", ""}});
        QTRY_VERIFY(model.isPreviewReady());
        QCOMPARE(model.getDatasetNaNCount(), uint64_t(1));
        QCOMPARE(model.getDatasetPositiveInfCount(), uint64_t(1));
        QCOMPARE(model.getDatasetMax(), 0.5);
        QCOMPARE(model.anomalyRegions().size(), size_t(1));
        QCOMPARE(model.anomalyRegions()[0].pixelCount, uint64_t(8));
        QVERIFY(std::isinf(model.getRawPixels()[1]));
        QVERIFY(std::isnan(model.getRawPixels()[4 * 15 + 2]));
    }

    void xyzDisplayRetainsSourceEncoding()
    {
        const Imf::Chromaticities xyz(Imath::V2f(1, 0), Imath::V2f(0, 1),
                                      Imath::V2f(0, 0), Imath::V2f(1.f / 3.f));
        const Imath::V3f rgb(0.25f, 0.5f, 0.125f);
        const Imath::V3f value = rgb * Imf::RGBtoXYZ(Imf::Chromaticities(), 1.f);
        Imf::Header header(1, 1);
        header.insert("chromaticities", Imf::ChromaticitiesAttribute(xyz));
        const QString path = fixture("xyz-display");
        writeFixture(path, 1, 1, {{"R", {value.x}}, {"G", {value.y}}, {"B", {value.z}}},
                     0, 0, 1.f, 1, &header);
        OpenEXRImage source(path, nullptr);
        RGBFramebufferModel model("");
        model.load(source.sharedEXR(), 0, {{"R", "G", "B", ""}});
        QTRY_VERIFY(model.isPreviewReady());
        QCOMPARE(model.getRedInfo(0, 0), value.x);
        for (int c = 0; c < 3; ++c)
            QVERIFY(std::abs(model.getDisplayPixels()[c] - rgb[c]) < 0.00001f);
    }

    void ycDisplayExportsAndUnsupportedSampling()
    {
        const QString path = fixture("yc-display-exports");
        writeFixture(path, 1, 1, {{"Y", {0.25f}}, {"RY", {1.f}}, {"BY", {-0.5f}}});
        OpenEXRImage source(path, nullptr);
        RGBFramebufferModel model("", RGBFramebufferModel::Layer_YC);
        model.load(source.sharedEXR(), 0, {{"Y", "RY", "BY", ""}});
        QTRY_VERIFY(model.isPreviewReady());
        QCOMPARE(model.rawChannelNames().size(), size_t(3));
        QCOMPARE(model.getRedInfo(0, 0), 0.5f);
        ImageSave::Source input;
        input.activeModel = &model;
        ImageSave::Options options;
        options.target = ImageSave::TargetHdrBracketedImages;
        options.path = m_directory.filePath("yc-bracket.png");
        const auto bracket = ImageSave::save(input, options);
        QCOMPARE(bracket.status, ImageSave::StatusSaved);
        QCOMPARE(QImage(bracket.paths[1]).pixelColor(0, 0), model.getLoadedImage().pixelColor(0, 0));
        options.target = ImageSave::TargetActiveOriginal;
        options.format = ImageSave::FormatHdr;
        options.path = m_directory.filePath("yc-display.hdr");
        QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
        QFile hdr(options.path);
        QVERIFY(hdr.open(QIODevice::ReadOnly));
        const QByteArray encoded = hdr.readAll().right(4);
        QCOMPARE(encoded.size(), 4);
        const double scale = std::ldexp(1., int(static_cast<unsigned char>(encoded[3])) - 136);
        for (int c = 0; c < 3; ++c)
            QVERIFY(std::abs(static_cast<unsigned char>(encoded[c]) * scale - model.getDisplayPixels()[c]) <= scale);

        Imf::Header header(4, 2);
        header.channels().insert("RY", Imf::Channel(Imf::FLOAT, 2, 1));
        header.channels().insert("BY", Imf::Channel(Imf::FLOAT, 2, 1));
        const QString invalid = fixture("yc-unsupported-sampling");
        writeFixture(invalid, 4, 2, {{"Y", std::vector<float>(8, 0.5f)},
                     {"RY", std::vector<float>(4, 0.f)}, {"BY", std::vector<float>(4, 0.f)}},
                     0, 0, 1.f, 1, &header);
        OpenEXRImage unsupported(invalid, nullptr);
        QCOMPARE(unsupported.getLayerModel()->defaultDisplayLayer()->getType(), LayerItem::Y);
        FileWidget widget(invalid);
        widget.show();
        QTRY_VERIFY(widget.activeFramebufferModel() && widget.activeFramebufferModel()->isPreviewReady());
        QCOMPARE(widget.activeFramebufferModel()->rawChannelNames().front(), std::string("Y"));
        RGBFramebufferModel combined("", RGBFramebufferModel::Layer_YC);
        YFramebufferModel scalar("RY");
        QSignalSpy failed(&combined, &FramebufferModel::loadFailed);
        combined.load(unsupported.sharedEXR(), 0, {{"Y", "RY", "BY", ""}});
        scalar.load(unsupported.sharedEXR(), 0);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(combined.errorString().contains("Unsupported YC sampling"));
        QVERIFY(!combined.isImageLoaded());
        QTRY_VERIFY(scalar.isPreviewReady());
        const auto cancel = std::make_shared<std::atomic_bool>(true);
        QVERIFY(!FramebufferLoader::decode(source.sharedEXR(), 0, FramebufferLoader::Chroma,
                                          {{"Y", "RY", "BY", ""}}, cancel).data);
    }

    void premultipliedColorsUseOpaqueBlackPreview()
    {
        const QString path = fixture("premultiplied");
        writeFixture(path, 3, 1,
          {{"R", {0.25f, 0.25f, 0.25f}}, {"G", {0.5f, 0.5f, 0.5f}},
           {"B", {0.75f, 0.75f, 0.75f}}, {"A", {0.f, 0.25f, 1.f}}});
        OpenEXRImage source(path, nullptr);
        RGBFramebufferModel model("");
        model.load(source.sharedEXR(), 0, {{"R", "G", "B", "A"}});
        QTRY_VERIFY(model.isPreviewReady());
        // Missing/standard chromaticities need no second pixel buffer.
        QVERIFY(&model.getRawPixels() == &model.getDisplayPixels());
        QCOMPARE(model.getAlphaInfo(0, 0), 0.f);
        QCOMPARE(model.getAlphaInfo(1, 0), 0.25f);
        for (int x = 0; x < 3; ++x)
            QCOMPARE(model.getLoadedImage().pixelColor(x, 0), QColor(136, 187, 224));

        ImageSave::Source input;
        input.activeModel = &model;
        ImageSave::Options options;
        options.path = m_directory.filePath("black-preview.png");
        QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
        const QImage exported(options.path);
        for (int x = 0; x < 3; ++x)
            QCOMPARE(exported.pixelColor(x, 0), model.getLoadedImage().pixelColor(x, 0));

        for (auto mode : {RGBFramebufferModel::Preview_ToneMapping,
                          RGBFramebufferModel::Preview_FalseColor}) {
            model.setPreviewMode(mode);
            QTRY_VERIFY(model.isPreviewReady());
            for (int x = 0; x < 3; ++x) {
                QCOMPARE(model.getLoadedImage().pixelColor(x, 0).alpha(), 255);
                QCOMPARE(model.getLoadedImage().pixelColor(x, 0),
                         model.getLoadedImage().pixelColor(2, 0));
            }
        }
    }

    void gamutConversionPreservesRawValuesAndExrMetadata()
    {
        const Imf::Chromaticities ap0(
          Imath::V2f(0.7347f, 0.2653f), Imath::V2f(0.f, 1.f),
          Imath::V2f(0.0001f, -0.077f), Imath::V2f(0.32168f, 0.33767f));
        Imf::Header header(2, 1);
        header.insert("chromaticities", Imf::ChromaticitiesAttribute(ap0));
        const QString path = fixture("ap0");
        writeFixture(path, 2, 1,
          {{"R", {0.25f, -0.125f}}, {"G", {0.125f, 0.25f}},
           {"B", {0.0625f, 0.5f}}, {"A", {0.f, 0.5f}}},
          0, 0, 1.f, 1, &header);
        OpenEXRImage source(path, nullptr);
        RGBFramebufferModel model("");
        YFramebufferModel red("R");
        model.load(source.sharedEXR(), 0, {{"R", "G", "B", "A"}});
        red.load(source.sharedEXR(), 0);
        QTRY_VERIFY(model.isPreviewReady() && red.isPreviewReady());
        const std::vector<float> original = {
          0.25f, 0.125f, 0.0625f, 0.f, -0.125f, 0.25f, 0.5f, 0.5f};
        QVERIFY(model.getRawPixels() == original);
        QVERIFY(model.getDisplayPixels() != original);
        QCOMPARE(model.getRedInfo(1, 0), red.getRawPixels()[1]);
        QCOMPARE(model.getDatasetMin(), -0.125);
        QVERIFY(model.anomalyRegions().empty());
        QVERIFY(model.rawChromaticities() && *model.rawChromaticities() == ap0);
        ImageSave::Source input;
        input.activeModel = &model;
        input.sourceImage = &source;
        for (auto target : {ImageSave::TargetActiveOriginal, ImageSave::TargetLayeredOriginal}) {
            for (auto metadata : {ImageSave::MetadataBasic, ImageSave::MetadataNone}) {
                ImageSave::Options options;
                options.target = target;
                options.format = ImageSave::FormatExr;
                options.pixelType = ImageSave::PixelFloat;
                options.metadata = metadata;
                options.path = fixture(QString("raw-%1-%2").arg(int(target)).arg(int(metadata)));
                QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
                OpenEXRImage exported(options.path, nullptr);
                const auto* attribute = exported.getEXR().header(0)
                  .findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities");
                if (metadata == ImageSave::MetadataBasic) {
                    QVERIFY(attribute);
                    QVERIFY(attribute->value() == ap0);
                } else {
                    QVERIFY(!attribute);
                }
                RGBFramebufferModel roundTrip("");
                roundTrip.load(exported.sharedEXR(), 0, {{"R", "G", "B", "A"}});
                QTRY_VERIFY(roundTrip.isPreviewReady());
                QVERIFY(roundTrip.getRawPixels() == original);
            }
        }
        ImageSave::Options bracket;
        bracket.target = ImageSave::TargetHdrBracketedImages;
        bracket.bracketCount = 3;
        bracket.path = m_directory.filePath("ap0-bracket.png");
        const auto saved = ImageSave::save(input, bracket);
        QCOMPARE(saved.status, ImageSave::StatusSaved);
        QCOMPARE(saved.paths.size(), 3);
        const QImage displayExport(saved.paths[1]); // Middle exposure is 0 EV.
        QCOMPARE(displayExport.pixelColor(0, 0), model.getLoadedImage().pixelColor(0, 0));
    }

    void negativeOriginAndDecreasingScanlinesKeepLocalIndexing()
    {
        Imf::Header header(1, 1); // Display window smaller than the data window.
        header.lineOrder() = Imf::DECREASING_Y;
        const QString path = fixture("negative-decreasing");
        writeFixture(path, 2, 2,
          {{"R", {1.f, 2.f, 3.f, 4.f}}, {"G", {0.f, 0.f, 0.f, 0.f}},
           {"B", {0.f, 0.f, 0.f, 0.f}}, {"Z", {5.f, 6.f, 7.f, 8.f}}},
          -1, -1, 1.f, 1, &header);
        OpenEXRImage source(path, nullptr);
        QCOMPARE(source.getEXR().header(0).lineOrder(), Imf::DECREASING_Y);
        RGBFramebufferModel rgb("");
        YFramebufferModel depth("Z");
        rgb.load(source.sharedEXR(), 0, {{"R", "G", "B", ""}});
        depth.load(source.sharedEXR(), 0);
        QTRY_VERIFY(rgb.isPreviewReady() && depth.isPreviewReady());
        QCOMPARE(rgb.getDataWindow(), QRect(-1, -1, 2, 2));
        QCOMPARE(rgb.getDisplayWindow(), QRect(0, 0, 1, 1));
        QCOMPARE(rgb.getRedInfo(0, 0), 1.f);
        QCOMPARE(rgb.getRedInfo(1, 1), 4.f);
        QCOMPARE(depth.getRawPixels()[0], 5.f);
        QCOMPARE(depth.getRawPixels()[3], 8.f);
        QVERIFY(rgb.getColorInfo(0, 0).find("x: -1 y: -1") == 0);
        QVERIFY(depth.getColorInfo(1, 1).find("x: 0 y: 0") == 0);
    }

    void tiledScalarEdgesAndSmallImages()
    {
        for (int tileWidth : {3, 16}) {
            Imf::Header header(5, 3);
            header.setTileDescription(Imf::TileDescription(tileWidth, 4));
            std::vector<float> values(15);
            for (size_t i = 0; i < values.size(); ++i) values[i] = float(i) + 0.5f;
            const QString path = fixture(QString("tiled-scalar-%1").arg(tileWidth));
            writeFixture(path, 5, 3, {{"V", values}}, 0, 0, 1.f, 1, &header);
            OpenEXRImage source(path, nullptr);
            YFramebufferModel model("V");
            model.load(source.sharedEXR(), 0);
            QTRY_VERIFY(model.isPreviewReady());
            QVERIFY(model.getRawPixels() == values);
            QCOMPARE(model.getDatasetMin(), 0.5);
            QCOMPARE(model.getDatasetMax(), 14.5);
        }
    }

    void tiledColorDepthCoordinatesAndScanlineExports()
    {
        const Imf::Chromaticities gamut(
          Imath::V2f(0.62955f, 0.341f), Imath::V2f(0.2867f, 0.6108f),
          Imath::V2f(0.1489f, 0.07125f), Imath::V2f(0.3155f, 0.33165f));
        Imf::Header header(2, 2);
        header.setTileDescription(Imf::TileDescription(3, 2));
        header.insert("chromaticities", Imf::ChromaticitiesAttribute(gamut));
        std::vector<float> red(15), depth(15, 2.f), alpha(15, 0.f);
        for (size_t i = 0; i < red.size(); ++i) red[i] = float(i + 1) / 32.f;
        for (int i : {7, 8, 12}) depth[i] = std::numeric_limits<float>::quiet_NaN();
        const QString path = fixture("tiled-color-depth");
        writeFixture(path, 5, 3,
          {{"R", red}, {"G", std::vector<float>(15, 0.125f)},
           {"B", std::vector<float>(15, 0.0625f)}, {"A", alpha}, {"Z", depth}},
          -2, -1, 1.f, 1, &header);
        OpenEXRImage source(path, nullptr);
        RGBFramebufferModel rgba(""), rgb("");
        YFramebufferModel z("Z");
        rgba.load(source.sharedEXR(), 0, {{"R", "G", "B", "A"}});
        rgb.load(source.sharedEXR(), 0, {{"R", "G", "B", ""}});
        z.load(source.sharedEXR(), 0);
        QTRY_VERIFY(rgba.isPreviewReady() && rgb.isPreviewReady() && z.isPreviewReady());
        QCOMPARE(rgba.getDataWindow(), QRect(-2, -1, 5, 3));
        QCOMPARE(rgba.getDisplayWindow(), QRect(0, 0, 2, 2));
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 5; ++x) {
                QCOMPARE(rgba.getRedInfo(x, y), red[y * 5 + x]);
                QCOMPARE(rgb.getRedInfo(x, y), red[y * 5 + x]);
                QCOMPARE(rgba.getLoadedImage().pixelColor(x, y), rgb.getLoadedImage().pixelColor(x, y));
            }
        QVERIFY(rgba.getLoadedImage().pixelColor(4, 2).red() > 0);
        QCOMPARE(rgba.getLoadedImage().pixelColor(4, 2).alpha(), 255);
        QVERIFY(rgba.getRawPixels() != rgba.getDisplayPixels());
        QVERIFY(rgba.getColorInfo(0, 0).find("x: -2 y: -1") == 0);
        QVERIFY(z.getColorInfo(4, 2).find("x: 2 y: 1") == 0);
        QCOMPARE(z.getDatasetNaNCount(), uint64_t(3));
        QCOMPARE(z.anomalyRegions().size(), size_t(1));
        QCOMPARE(z.anomalyRegions()[0].bounds, QRect(2, 1, 2, 2));
        QCOMPARE(z.anomalyRegions()[0].pixelCount, uint64_t(3));

        ImageSave::Source input;
        input.activeModel = &rgba;
        input.sourceImage = &source;
        for (auto target : {ImageSave::TargetActiveOriginal, ImageSave::TargetLayeredOriginal}) {
            for (auto metadata : {ImageSave::MetadataBasic, ImageSave::MetadataNone}) {
                ImageSave::Options options;
                options.target = target;
                options.format = ImageSave::FormatExr;
                options.pixelType = ImageSave::PixelFloat;
                options.metadata = metadata;
                options.path = fixture(QString("tiled-export-%1-%2").arg(int(target)).arg(int(metadata)));
                QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
                OpenEXRImage exported(options.path, nullptr);
                const auto& outputHeader = exported.getEXR().header(0);
                QVERIFY(!outputHeader.hasTileDescription());
                QVERIFY(outputHeader.type() == Imf::SCANLINEIMAGE);
                QVERIFY(outputHeader.dataWindow() == source.getEXR().header(0).dataWindow());
                QVERIFY(outputHeader.displayWindow() == header.displayWindow());
                const auto* chroma = outputHeader.findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities");
                if (metadata == ImageSave::MetadataBasic) {
                    QVERIFY(chroma && chroma->value() == gamut);
                } else {
                    QVERIFY(!chroma);
                }
                RGBFramebufferModel roundTrip("");
                roundTrip.load(exported.sharedEXR(), 0, {{"R", "G", "B", "A"}});
                QTRY_VERIFY(roundTrip.isPreviewReady());
                QVERIFY(roundTrip.getRawPixels() == rgba.getRawPixels());
                if (target == ImageSave::TargetLayeredOriginal) {
                    YFramebufferModel exportedZ("Z");
                    exportedZ.load(exported.sharedEXR(), 0);
                    QTRY_VERIFY(exportedZ.isPreviewReady());
                    QCOMPARE(exportedZ.getRawPixels()[14], 2.f);
                    QCOMPARE(exportedZ.getDatasetNaNCount(), uint64_t(3));
                }
            }
        }
        const auto cancel = std::make_shared<std::atomic_bool>(true);
        QVERIFY(!FramebufferLoader::decode(source.sharedEXR(), 0, FramebufferLoader::Scalar,
                                          {{"Z", "", "", ""}}, cancel).data);
    }

    void tiledMissingDataDoesNotPublishPartialPreview()
    {
        Imf::Header header(5, 3);
        header.setTileDescription(Imf::TileDescription(3, 2));
        const QString path = fixture("truncated-tiles");
        writeFixture(path, 5, 3, {{"Y", std::vector<float>(15, 0.5f)}},
                     0, 0, 1.f, 1, &header);
        QFile damaged(path);
        QVERIFY(damaged.open(QIODevice::ReadWrite));
        QVERIFY(damaged.resize(damaged.size() - 8)); // Truncate the final tile payload.
        damaged.close();
        try {
            OpenEXRImage source(path, nullptr);
            YFramebufferModel model("Y");
            QSignalSpy failed(&model, &FramebufferModel::loadFailed);
            QSignalSpy loaded(&model, &FramebufferModel::imageLoaded);
            model.load(source.sharedEXR(), 0);
            QTRY_COMPARE(failed.count(), 1);
            QCOMPARE(loaded.count(), 0);
            QVERIFY(!model.isImageLoaded() && !model.isPreviewReady());
            QVERIFY(model.getLoadedImage().isNull());
            ImageSave::Source input;
            input.sourceImage = &source;
            ImageSave::Options options;
            options.target = ImageSave::TargetLayeredOriginal;
            options.format = ImageSave::FormatExr;
            options.path = fixture("truncated-export");
            QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusFailed);
            QVERIFY(!QFile::exists(options.path));
        } catch (const std::exception& error) {
            QVERIFY(*error.what()); // The library may reject truncated input on open.
        }
    }

    void pureLuminanceViewsAndDefaultOrder()
    {
        for (const auto& order : {std::vector<std::string>{"left", "right"},
                                 std::vector<std::string>{"right", "left"},
                                 std::vector<std::string>{"center", "left", "right"}}) {
            const QString path = fixture("luminance-views-" + QString::fromStdString(order[0]));
            Imf::Header header(2, 2);
            header.insert("multiView", Imf::StringVectorAttribute(order));
            std::map<std::string, std::vector<float>> samples;
            for (const auto& view : order)
                samples[view == order[0] ? "Y" : view + ".Y"] = std::vector<float>(4, view == "left" ? .25f : .5f);
            writeFixture(path, 2, 2, samples, 0, 0, 1.f, 1, &header);
            FileWidget widget(path);
            QTRY_VERIFY(widget.isDocumentReady());
            QCOMPARE(widget.activeFramebufferModel()->rawChannelNames()[0], std::string("Y"));
            QVERIFY(qobject_cast<YFramebufferWidget*>(widget.activePreviewWidget()));
            const auto pair = widget.sourceImage()->getLayerModel()->stereoLayers();
            QVERIFY(pair.anaglyphError.isEmpty());
            QCOMPARE(pair.eyes[0]->getType(), LayerItem::Y);
            widget.setStereoMode(ImageFileWidget::StereoLeft);
            QTRY_VERIFY(widget.activeFramebufferModel()->isPreviewReady());
            QCOMPARE(widget.activeFramebufferModel()->getRawPixels()[0], .25f);
            widget.setStereoMode(ImageFileWidget::StereoRight);
            QTRY_VERIFY(widget.activeFramebufferModel()->isPreviewReady());
            QCOMPARE(widget.activeFramebufferModel()->getRawPixels()[0], .5f);
            widget.setStereoMode(ImageFileWidget::StereoAnaglyph);
            QTRY_COMPARE(widget.stereoMode(), ImageFileWidget::StereoAnaglyph);
            RGBFramebufferModel left("", RGBFramebufferModel::Layer_Y), right("", RGBFramebufferModel::Layer_Y);
            left.load(widget.sourceImage()->sharedEXR(), 0, {{pair.eyes[0]->getOriginalFullName(), "", "", ""}});
            right.load(widget.sourceImage()->sharedEXR(), 0, {{pair.eyes[1]->getOriginalFullName(), "", "", ""}});
            QTRY_VERIFY(left.isPreviewReady() && right.isPreviewReady());
            const auto* stereo = widget.activeFramebufferModel();
            const auto pixel = stereo->getLoadedImage().pixelColor(0, 0);
            QCOMPARE(pixel.red(), left.getLoadedImage().pixelColor(0, 0).red());
            QCOMPARE(pixel.green(), right.getLoadedImage().pixelColor(0, 0).green());
            QCOMPARE(pixel.blue(), right.getLoadedImage().pixelColor(0, 0).blue());
            QCOMPARE(stereo->getDatasetMin(), .25);
            QCOMPARE(stereo->getDatasetMax(), .5);
            QVERIFY(stereo->getColorInfo(0, 0).find("Left:") != std::string::npos);
            widget.setStereoMode(ImageFileWidget::StereoDefault);
            QCOMPARE(widget.activeFramebufferModel()->rawChannelNames()[0], std::string("Y"));
        }
    }

    void resolutionLevelsReadStoredSamplesAndExportSelectedLevel()
    {
        for (auto rounding : {Imf::ROUND_DOWN, Imf::ROUND_UP}) {
            const QString path = fixture(QString("mip-%1").arg(int(rounding)));
            const int count = writeMipFixture(path, rounding);
            OpenEXRImage source(path, nullptr);
            QCOMPARE(int(ResolutionLevels::commonLevels(source.sharedEXR()).size()), count);
            int width = 9, height = 7, displayWidth = 11, displayHeight = 9;
            for (int level = 0; level < count; ++level) {
                RGBFramebufferModel rgb("");
                YFramebufferModel scalar("Z");
                rgb.load(source.sharedEXR(), 0, {{"R", "G", "B", ""}}, {level, level});
                scalar.load(source.sharedEXR(), 0, {level, level});
                QTRY_VERIFY(rgb.isPreviewReady() && scalar.isPreviewReady());
                QCOMPARE(rgb.resolutionLevel(), ResolutionLevel(level, level));
                QCOMPARE(rgb.resolutionLevelCount(), size_t(count));
                QCOMPARE(rgb.getDataWindow(), QRect(-4, -2, width, height));
                QCOMPARE(rgb.getDisplayWindow(), QRect(-3, -1, displayWidth, displayHeight));
                const int last = width * height - 1;
                QCOMPARE(rgb.getRawPixels()[size_t(last) * 4], float(level) + float(last) / 1000.f);
                QCOMPARE(rgb.getRawPixels()[0], float(level));
                QCOMPARE(scalar.getDatasetMin(), double(float(level) + .6f));
                QCOMPARE(scalar.getDatasetPositiveInfCount(), uint64_t(last > 0 ? 1 : 0));
                if (last > 0) QCOMPARE(scalar.anomalyRegions().back().bounds, QRect(width - 1, height - 1, 1, 1));
                QVERIFY(rgb.getColorInfo(0, 0).find("Level " + ResolutionLevel(level, level).toString()) != std::string::npos);
                QCOMPARE(PreviewImage::render(rgb).size(), QSize(displayWidth, displayHeight));
                if (width == 1 && height == 1)
                    QCOMPARE(PreviewImage::render(rgb).pixelColor(0, 0).alpha(), 0); // No window intersection.
                for (auto target : {ImageSave::TargetActiveOriginal, ImageSave::TargetLayeredOriginal}) {
                    ImageSave::Source input;
                    input.sourceImage = &source;
                    input.activeModel = &rgb;
                    input.resolutionLevel = {level, level};
                    ImageSave::Options options;
                    options.target = target;
                    options.format = ImageSave::FormatExr;
                    options.pixelType = ImageSave::PixelFloat;
                    options.compression = ImageSave::CompressionZip;
                    options.path = fixture(QString("mip-out-%1-%2-%3").arg(int(rounding)).arg(level).arg(int(target)));
                    QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
                    OpenEXRImage reopened(options.path, nullptr);
                    const auto& out = reopened.getEXR().header(0);
                    QVERIFY(!out.hasTileDescription());
                    QCOMPARE(out.displayWindow(), ResolutionLevels::query(source.sharedEXR(), 0, {level, level}).display);
                    QCOMPARE(ViewMetadata::read(out).multiView, std::vector<std::string>({"left", "right"}));
                    RGBFramebufferModel roundtrip("");
                    roundtrip.load(reopened.sharedEXR(), 0, {{"R", "G", "B", ""}});
                    QTRY_VERIFY(roundtrip.isPreviewReady());
                    QVERIFY(roundtrip.getRawPixels() == rgb.getRawPixels());
                    QCOMPARE(roundtrip.getDataWindow(), rgb.getDataWindow());
                }
                if (level == 1) {
                    ImageSave::Source input;
                    input.activeModel = &rgb; input.sourceImage = &source; input.resolutionLevel = {level, level};
                    ImageSave::Options options;
                    options.path = m_directory.filePath(QString("mip-preview-%1.png").arg(int(rounding)));
                    QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
                    QCOMPARE(QImage(options.path), PreviewImage::render(rgb));
                    options.target = ImageSave::TargetActiveOriginal;
                    options.format = ImageSave::FormatHdr;
                    options.path = m_directory.filePath(QString("mip-hdr-%1.hdr").arg(int(rounding)));
                    QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
                    QFile hdr(options.path);
                    QVERIFY(hdr.open(QIODevice::ReadOnly));
                    QVERIFY(hdr.readAll().contains(QString("-Y %1 +X %2\n").arg(height).arg(width).toLatin1()));
                    options.target = ImageSave::TargetHdrBracketedImages;
                    options.format = ImageSave::FormatPng;
                    options.bracketCount = 1;
                    options.path = m_directory.filePath(QString("mip-bracket-%1.png").arg(int(rounding)));
                    const auto bracket = ImageSave::save(input, options);
                    QCOMPARE(bracket.status, ImageSave::StatusSaved);
                    QCOMPARE(QImage(bracket.paths.front()).size(), QSize(width, height));
                }
                const auto shrink = [rounding](int n) { return std::max(1, (n + (rounding == Imf::ROUND_UP ? 1 : 0)) / 2); };
                width = shrink(width); height = shrink(height);
                displayWidth = shrink(displayWidth); displayHeight = shrink(displayHeight);
            }
            for (int invalid : {-1, count}) {
                YFramebufferModel model("Z");
                model.load(source.sharedEXR(), 0, {invalid, invalid});
                QTRY_VERIFY(!model.isLoading());
                QVERIFY(!model.isImageLoaded() && !model.errorString().isEmpty());
            }
            const auto cancelled = std::make_shared<std::atomic_bool>(true);
            QVERIFY(!FramebufferLoader::decode(source.sharedEXR(), 0, FramebufferLoader::Scalar,
                       {{"Z", "", "", ""}}, cancelled, {1, 1}).data);
        }
    }

    void resolutionDocumentSwitchIsAtomicAndPreservesParameters_data()
    {
        QTest::addColumn<bool>("ripmap");
        QTest::newRow("mipmap") << false;
        QTest::newRow("ripmap") << true;
    }

    void resolutionDocumentSwitchIsAtomicAndPreservesParameters()
    {
        QFETCH(bool, ripmap);
        const QString path = fixture(ripmap ? "rip-document" : "mip-document");
        if (ripmap) writeRipFixture(path);
        else writeMipFixture(path);
        const ResolutionLevel pending = ripmap ? ResolutionLevel(1, 0) : ResolutionLevel(1, 1);
        const ResolutionLevel selected = ripmap ? ResolutionLevel(0, 2) : ResolutionLevel(2, 2);
        FileWidget widget(path), other(path);
        widget.resize(900, 650);
        widget.show();
        QTRY_VERIFY(widget.isDocumentReady() && other.isDocumentReady());
        auto* rgb = qobject_cast<RGBFramebufferWidget*>(widget.activePreviewWidget());
        QVERIFY(rgb);
        auto color = rgb->previewState();
        color.exposure = -2.; color.highlightNonFinite = true;
        color.minimum = -.5; color.maximum = 8.; color.automatic = false;
        rgb->restorePreviewState(color);
        widget.activeGraphicsView()->setZoomLevel(2.);
        widget.setStereoMode(ImageFileWidget::StereoRight);
        QTRY_VERIFY(widget.activeFramebufferModel()->isPreviewReady());
        auto* z = widget.sourceImage()->getLayerModel()->findChannel(0, "Z");
        widget.openLayer(z);
        QTRY_VERIFY(widget.activeFramebufferModel()->isPreviewReady());
        auto* scalar = qobject_cast<YFramebufferWidget*>(widget.activePreviewWidget());
        auto scalarState = scalar->previewState();
        scalarState.automatic = true;
        scalar->restorePreviewState(scalarState);
        widget.setStereoMode(ImageFileWidget::StereoAnaglyph);
        QTRY_COMPARE(widget.stereoMode(), ImageFileWidget::StereoAnaglyph);
        const auto* oldModel = widget.activeFramebufferModel();
        widget.setResolutionLevel(pending);
        QCOMPARE(widget.resolutionLevel(), ResolutionLevel(0, 0));
        QCOMPARE(widget.activeFramebufferModel(), oldModel);
        widget.setResolutionLevel(selected); // Supersede an in-flight request.
        QTRY_VERIFY(!widget.isRefreshInProgress());
        QCOMPARE(widget.resolutionLevel(), selected);
        QCOMPARE(other.resolutionLevel(), ResolutionLevel(0, 0));
        QCOMPARE(widget.stereoMode(), ImageFileWidget::StereoAnaglyph);
        for (auto* page : widget.findChildren<RGBFramebufferWidget*>()) {
            QCOMPARE(page->framebufferModel()->resolutionLevel(), selected);
            QVERIFY(page->framebufferModel()->isPreviewReady());
        }
        scalar = widget.findChild<YFramebufferWidget*>();
        QCOMPARE(scalar->framebufferModel()->resolutionLevel(), selected);
        QVERIFY(scalar->previewState().automatic);
        QCOMPARE(scalar->previewState().minimum, scalar->framebufferModel()->getDatasetMin());
        widget.setStereoMode(ImageFileWidget::StereoLeft);
        rgb = qobject_cast<RGBFramebufferWidget*>(widget.activePreviewWidget());
        QCOMPARE(rgb->previewState().exposure, -2.);
        QCOMPARE(rgb->previewState().minimum, -.5);
        QCOMPARE(rgb->previewState().maximum, 8.);
        QVERIFY(rgb->previewState().highlightNonFinite);
        QCOMPARE(widget.activeGraphicsView()->viewState().zoom, 2.);
        widget.setResolutionLevel(pending);
        widget.setResolutionLevel(selected); // Re-select committed level to cancel.
        QVERIFY(!widget.isRefreshInProgress());
        QCOMPARE(widget.activeFramebufferModel()->resolutionLevel(), selected);
        widget.refresh();
        QTRY_VERIFY(!widget.isRefreshInProgress());
        QCOMPARE(widget.resolutionLevel(), selected);
        QCOMPARE(widget.stereoMode(), ImageFileWidget::StereoLeft);
        // Missing pairs must retain the snapshot, even if both axis indices still exist.
        const auto* oldSource = widget.sourceImage();
        const auto* oldPreview = widget.activeFramebufferModel();
        QVERIFY(QFile::rename(path, path + ".old"));
        if (ripmap) writeMipFixture(path);
        else writeFixture(path, 1, 1, {{"Y", {1.f}}});
        dismissNextError();
        widget.refresh();
        QCOMPARE(widget.sourceImage(), oldSource);
        QCOMPARE(widget.activeFramebufferModel(), oldPreview);
        QCOMPARE(widget.resolutionLevel(), selected);
    }

    void mipMenuMinimalAndSaveNotice()
    {
        const QString path = fixture("mip-ui");
        writeMipFixture(path);
        MainWindow window;
        window.resize(900, 650);
        window.show();
        window.open(path);
        auto* document = window.findChild<ImageFileWidget*>();
        QTRY_VERIFY(document && document->isDocumentReady());
        auto* menu = window.findChild<QMenu*>("menu_ResolutionLevel");
        QVERIFY(menu && menu->isEnabled());
        auto* action = window.findChild<QAction*>("action_ResolutionLevel1_1");
        QVERIFY(action && action->text().contains("5 × 4")); // Current display canvas.
        action->trigger();
        QTRY_VERIFY(!document->isRefreshInProgress());
        QCOMPARE(document->resolutionLevel(), ResolutionLevel(1, 1));
        QVERIFY(action->isChecked());
        window.findChild<QAction*>("action_CopyImageFullResolution")->trigger();
        QCOMPARE(QApplication::clipboard()->image().size(), QSize(5, 4));
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleMinimalView"));
        auto* footer = window.centralWidget()->findChild<QWidget*>("minimalImageFooter");
        QVERIFY(footer && footer->toolTip().contains("Level (1, 1)"));
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleMinimalView"));
        QCOMPARE(document->resolutionLevel(), ResolutionLevel(1, 1));
        SaveImageDialog dialog(fixture("mip-save"));
        dialog.setResolutionLevelInfo({1, 1}, true);
        auto* notice = dialog.findChild<QLabel*>("resolutionLevelLabel");
        QVERIFY(notice && !notice->isHidden());
        QVERIFY(notice->text().contains("selected level only"));
        dialog.setResolutionLevelInfo({}, false);
        QVERIFY(notice->isHidden());
    }

    void incompleteMipDoesNotPublishAndCommonLevelsAreRestricted()
    {
        const QString path = fixture("missing-mip");
        const int count = writeMipFixture(path, Imf::ROUND_DOWN, true);
        OpenEXRImage source(path, nullptr);
        YFramebufferModel model("Z");
        QSignalSpy loaded(&model, &FramebufferModel::imageLoaded);
        model.load(source.sharedEXR(), 0, {count - 1, count - 1});
        QTRY_VERIFY(!model.isLoading());
        QCOMPARE(loaded.count(), 0);
        QVERIFY(!model.isPreviewReady() && !model.errorString().isEmpty());
        ImageSave::Source input;
        input.sourceImage = &source; input.resolutionLevel = {count - 1, count - 1};
        ImageSave::Options options;
        options.target = ImageSave::TargetLayeredOriginal;
        options.format = ImageSave::FormatExr;
        options.path = fixture("missing-mip-export");
        QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusFailed);
        QVERIFY(!QFile::exists(options.path));
        FileWidget widget(path);
        QTRY_VERIFY(widget.isDocumentReady());
        const auto* committed = widget.activeFramebufferModel();
        QTimer closeError;
        connect(&closeError, &QTimer::timeout, &widget, [] {
            for (auto* top : QApplication::topLevelWidgets())
                if (auto* message = qobject_cast<QMessageBox*>(top)) message->accept();
        });
        closeError.start(10);
        widget.setResolutionLevel({count - 1, count - 1});
        QTRY_VERIFY(!widget.isRefreshInProgress());
        closeError.stop();
        QCOMPARE(widget.resolutionLevel(), ResolutionLevel(0, 0));
        QCOMPARE(widget.activeFramebufferModel(), committed);
        QVERIFY(committed->isPreviewReady());

        const QString mixedPath = fixture("mixed-levels");
        Imf::Header headers[] = {Imf::Header(4, 4), Imf::Header(4, 4)};
        for (int part = 0; part < 2; ++part) {
            headers[part].setName(std::to_string(part));
            headers[part].setType(part == 0 ? Imf::TILEDIMAGE : Imf::SCANLINEIMAGE);
            headers[part].channels().insert("Y", Imf::Channel(Imf::FLOAT));
        }
        headers[0].setTileDescription(Imf::TileDescription(2, 2, Imf::MIPMAP_LEVELS));
        {
            Imf::MultiPartOutputFile output(mixedPath.toLocal8Bit().constData(), headers, 2);
            Imf::TiledOutputPart tiled(output, 0);
            float samples[16] = {};
            for (int level = 0; level < tiled.numLevels(); ++level) {
                Imf::FrameBuffer buffer;
                buffer.insert("Y", Imf::Slice::Make(Imf::FLOAT, samples, tiled.dataWindowForLevel(level),
                              sizeof(float), size_t(tiled.levelWidth(level)) * sizeof(float)));
                tiled.setFrameBuffer(buffer);
                tiled.writeTiles(0, tiled.numXTiles(level) - 1, 0, tiled.numYTiles(level) - 1, level);
            }
            Imf::OutputPart scanline(output, 1);
            Imf::FrameBuffer buffer;
            buffer.insert("Y", Imf::Slice::Make(Imf::FLOAT, samples, headers[1].dataWindow(), sizeof(float), 4 * sizeof(float)));
            scanline.setFrameBuffer(buffer); scanline.writePixels(4);
        }
        OpenEXRImage mixed(mixedPath, nullptr);
        QVERIFY(ResolutionLevels::commonLevels(mixed.sharedEXR()) == std::vector<ResolutionLevel>({{0, 0}}));
        Imf::Header left(9, 7), right(9, 7);
        left.setTileDescription(Imf::TileDescription(4, 3, Imf::MIPMAP_LEVELS, Imf::ROUND_DOWN));
        right.setTileDescription(Imf::TileDescription(4, 3, Imf::MIPMAP_LEVELS, Imf::ROUND_UP));
        QVERIFY(ViewMetadata::stereoGeometryMatches(left, right));
        QVERIFY(!ViewMetadata::stereoGeometryMatches(left, right, {1, 1}));
    }

    void ripmapReadsBothAxesAndExportsSelectedPair()
    {
        const std::array<RGBFramebufferModel::Input, 2> eyes = {{
          {0, RGBFramebufferModel::Layer_RGB, {{"R", "G", "B", "A"}}},
          {0, RGBFramebufferModel::Layer_RGB, {{"right.R", "right.G", "right.B", "right.A"}}}}};
        for (auto rounding : {Imf::ROUND_DOWN, Imf::ROUND_UP}) {
            const QString path = fixture(QString("rip-%1").arg(int(rounding)));
            const float aspect = rounding == Imf::ROUND_DOWN ? 1.5f : 1.f;
            const QSize counts = writeRipFixture(path, rounding, {-1, -1}, aspect);
            OpenEXRImage source(path, nullptr);
            const auto levels = ResolutionLevels::commonLevels(source.sharedEXR());
            QCOMPARE(int(levels.size()), counts.width() * counts.height());
            const auto dimension = [rounding](int size, int level) {
                const int divisor = 1 << level;
                return std::max(1, (size + (rounding == Imf::ROUND_UP ? divisor - 1 : 0)) / divisor);
            };
            for (const auto level : levels) {
                const int width = dimension(9, level.x), height = dimension(5, level.y);
                RGBFramebufferModel rgb(""), stereo("");
                YFramebufferModel z("Z");
                rgb.load(source.sharedEXR(), 0, {{"R", "G", "B", "A"}}, level);
                z.load(source.sharedEXR(), 0, level);
                stereo.loadStereo(source.sharedEXR(), eyes, level);
                QTRY_VERIFY(rgb.isPreviewReady() && z.isPreviewReady() && stereo.isPreviewReady());
                QCOMPARE(rgb.resolutionLevel(), level);
                QCOMPARE(stereo.resolutionLevel(), level);
                QCOMPARE(rgb.getDataWindow(), QRect(-4, -2, width, height));
                QCOMPARE(rgb.getDisplayWindow(), QRect(-3, -1, dimension(11, level.x), dimension(7, level.y)));
                QCOMPARE(rgb.pixelAspectRatio(), aspect);
                const size_t last = size_t(width) * height - 1;
                for (size_t pixel : {size_t(0), size_t(width - 1), size_t(width) * (height - 1), last}) {
                    QCOMPARE(rgb.getRawPixels()[4 * pixel], ripSample(level, 0, pixel));
                    QCOMPARE(rgb.getRawPixels()[4 * pixel + 2], ripSample(level, 2, pixel));
                    QCOMPARE(rgb.getRawPixels()[4 * pixel + 3], 0.f);
                }
                QCOMPARE(rgb.getLoadedImage().pixelColor(0, 0).alpha(), 255);
                QCOMPARE(z.getDatasetMin(), double(ripSample(level, 8, 0)));
                QCOMPARE(z.getDatasetPositiveInfCount(), uint64_t(last > 0 ? 1 : 0));
                if (last > 0) QCOMPARE(z.anomalyRegions().back().bounds, QRect(width - 1, height - 1, 1, 1));
                QVERIFY(rgb.getColorInfo(0, 0).find("Level " + level.toString()) != std::string::npos);
                QVERIFY(rgb.getColorInfo(0, 0).find("x: -4 y: -2") != std::string::npos);
                QVERIFY(stereo.getColorInfo(0, 0).find("right.R:") != std::string::npos);
                const QSize outputSize(int(std::lround(dimension(11, level.x) * aspect)), dimension(7, level.y));
                QCOMPARE(PreviewImage::render(rgb).size(), outputSize);
                if (width == 1 || height == 1)
                    QCOMPARE(PreviewImage::render(rgb).pixelColor(0, 0).alpha(), 0);
                const QColor combined = stereo.getLoadedImage().pixelColor(0, 0);
                QCOMPARE(combined.red(), rgb.getLoadedImage().pixelColor(0, 0).red());

                if (level != ResolutionLevel(1, 0) && level != ResolutionLevel(0, 1)
                    && level != ResolutionLevel(counts.width() - 1, counts.height() - 1)) continue;
                ImageSave::Source input;
                input.activeModel = &rgb; input.sourceImage = &source; input.resolutionLevel = level;
                ImageSave::Options preview;
                preview.path = m_directory.filePath(QString("rip-preview-%1-%2-%3.png")
                  .arg(int(rounding)).arg(level.x).arg(level.y));
                QCOMPARE(ImageSave::save(input, preview).status, ImageSave::StatusSaved);
                QCOMPARE(QImage(preview.path), PreviewImage::render(rgb));
                ImageSave::Options hdr;
                hdr.target = ImageSave::TargetActiveOriginal; hdr.format = ImageSave::FormatHdr;
                hdr.path = preview.path + ".hdr";
                QCOMPARE(ImageSave::save(input, hdr).status, ImageSave::StatusSaved);
                QFile hdrFile(hdr.path);
                QVERIFY(hdrFile.open(QIODevice::ReadOnly));
                QVERIFY(hdrFile.readAll().contains(QString("-Y %1 +X %2\n").arg(height).arg(width).toLatin1()));
                for (auto target : {ImageSave::TargetActiveOriginal, ImageSave::TargetLayeredOriginal}) {
                    ImageSave::Options options;
                    options.target = target; options.format = ImageSave::FormatExr;
                    options.pixelType = ImageSave::PixelFloat; options.compression = ImageSave::CompressionZip;
                    options.path = fixture(QString("rip-out-%1-%2-%3-%4").arg(int(rounding)).arg(level.x).arg(level.y).arg(int(target)));
                    QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
                    OpenEXRImage reopened(options.path, nullptr);
                    const auto& header = reopened.getEXR().header(0);
                    QVERIFY(!header.hasTileDescription());
                    QVERIFY(!header.hasType() || header.type() == Imf::SCANLINEIMAGE);
                    QCOMPARE(header.displayWindow(), ResolutionLevels::query(source.sharedEXR(), 0, level).display);
                    QCOMPARE(header.pixelAspectRatio(), aspect);
                    QCOMPARE(ViewMetadata::read(header).multiView, std::vector<std::string>({"left", "right"}));
                    RGBFramebufferModel copy("");
                    copy.load(reopened.sharedEXR(), 0, {{"R", "G", "B", "A"}});
                    QTRY_VERIFY(copy.isPreviewReady());
                    QVERIFY(copy.getRawPixels() == rgb.getRawPixels());
                    QCOMPARE(copy.getDataWindow(), rgb.getDataWindow());
                }
            }
            for (const auto invalid : {ResolutionLevel(-1, 0), ResolutionLevel(0, -1),
                                      ResolutionLevel(counts.width(), 0), ResolutionLevel(0, counts.height())}) {
                YFramebufferModel z("Z");
                z.load(source.sharedEXR(), 0, invalid);
                QTRY_VERIFY(!z.isLoading());
                QVERIFY(!z.isImageLoaded() && !z.errorString().isEmpty());
            }
            const auto cancel = std::make_shared<std::atomic_bool>(true);
            QVERIFY(!FramebufferLoader::decode(source.sharedEXR(), 0, FramebufferLoader::Scalar,
                        {{"Z", "", "", ""}}, cancel, {1, 0}).data);
        }
    }

    void ripmapDocumentAndMenuUseWholePairs()
    {
        const QString path = fixture("rip-ui");
        const auto counts = writeRipFixture(path);
        MainWindow window;
        window.resize(900, 650); window.show(); window.open(path);
        auto* document = window.findChild<ImageFileWidget*>();
        QTRY_VERIFY(document && document->isDocumentReady());
        FileWidget other(path);
        QTRY_VERIFY(other.isDocumentReady());
        auto* menu = window.findChild<QMenu*>("menu_ResolutionLevel");
        QVERIFY(menu && menu->isEnabled());
        QCOMPARE(menu->actions().size(), counts.width());
        auto* x1 = menu->findChild<QMenu*>("menu_ResolutionX1");
        QVERIFY(x1);
        QCOMPARE(x1->actions().size(), counts.height());
        auto* action10 = menu->findChild<QAction*>("action_ResolutionLevel1_0");
        auto* action01 = menu->findChild<QAction*>("action_ResolutionLevel0_1");
        QVERIFY(action10 && action01);
        QVERIFY(action10->text().contains("6 × 7"));
        x1->menuAction()->trigger(); // Entering X alone must not start a load.
        QVERIFY(!document->isRefreshInProgress());
        QCOMPARE(document->resolutionLevel(), ResolutionLevel());
        document->setStereoMode(ImageFileWidget::StereoRight);
        QTRY_VERIFY(document->activeFramebufferModel()->isPreviewReady());
        document->setStereoMode(ImageFileWidget::StereoAnaglyph);
        QTRY_COMPARE(document->stereoMode(), ImageFileWidget::StereoAnaglyph);
        const auto* old = document->activeFramebufferModel();
        action10->trigger();
        QCOMPARE(document->activeFramebufferModel(), old);
        action01->trigger();
        QTRY_VERIFY(!document->isRefreshInProgress());
        QCOMPARE(document->resolutionLevel(), ResolutionLevel(0, 1));
        QCOMPARE(other.resolutionLevel(), ResolutionLevel());
        QVERIFY(action01->isChecked() && !action10->isChecked());
        for (auto* page : document->findChildren<RGBFramebufferWidget*>())
            QCOMPARE(page->framebufferModel()->resolutionLevel(), ResolutionLevel(0, 1));
        window.findChild<QAction*>("action_CopyImageFullResolution")->trigger();
        QCOMPARE(QApplication::clipboard()->image().size(), QSize(11, 4));
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleMinimalView"));
        auto* footer = window.centralWidget()->findChild<QWidget*>("minimalImageFooter");
        QVERIFY(footer && footer->toolTip().contains("Level (0, 1)"));
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleMinimalView"));
        QCOMPARE(document->resolutionLevel(), ResolutionLevel(0, 1));
        document->refresh();
        QTRY_VERIFY(!document->isRefreshInProgress());
        QCOMPARE(document->resolutionLevel(), ResolutionLevel(0, 1));
        QCOMPARE(document->stereoMode(), ImageFileWidget::StereoAnaglyph);
        SaveImageDialog dialog(fixture("rip-dialog"));
        dialog.setResolutionLevelInfo({0, 1}, true);
        QVERIFY(dialog.findChild<QLabel*>("resolutionLevelLabel")->text().contains("(0, 1)"));
        document->setResolutionLevel({1, 0});
        document->setResolutionLevel({0, 1}); // Cancel back to the committed pair.
        QVERIFY(!document->isRefreshInProgress());
        QCOMPARE(document->activeFramebufferModel()->resolutionLevel(), ResolutionLevel(0, 1));
    }

    void ripmapMissingTilesRetainCommittedDocument()
    {
        const QString path = fixture("rip-incomplete");
        writeRipFixture(path, Imf::ROUND_UP, {1, 0});
        FileWidget widget(path);
        QTRY_VERIFY(widget.isDocumentReady());
        const auto* original = widget.activeFramebufferModel();
        QTimer closeError;
        connect(&closeError, &QTimer::timeout, &widget, [] {
            for (auto* top : QApplication::topLevelWidgets())
                if (auto* message = qobject_cast<QMessageBox*>(top)) message->accept();
        });
        closeError.start(10);
        widget.setResolutionLevel({1, 0});
        QTRY_VERIFY(!widget.isRefreshInProgress());
        closeError.stop();
        QCOMPARE(widget.activeFramebufferModel(), original);
        QCOMPARE(widget.resolutionLevel(), ResolutionLevel());
        ImageSave::Source input;
        input.activeModel = original; input.sourceImage = widget.sourceImage(); input.resolutionLevel = {1, 0};
        ImageSave::Options options;
        options.target = ImageSave::TargetLayeredOriginal; options.format = ImageSave::FormatExr;
        options.path = fixture("rip-incomplete-out");
        QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusFailed);
        QVERIFY(!QFile::exists(options.path));
    }

    void mixedResolutionPartsUseActualLevelIntersection()
    {
        for (auto secondMode : {Imf::ONE_LEVEL, Imf::MIPMAP_LEVELS, Imf::RIPMAP_LEVELS}) {
            const QString path = fixture(QString("mixed-rip-%1").arg(int(secondMode)));
            Imf::Header headers[] = {Imf::Header(9, 5), Imf::Header(9, 5)};
            for (int part = 0; part < 2; ++part) {
                headers[part].setName(std::to_string(part)); headers[part].setType(Imf::TILEDIMAGE);
                headers[part].channels().insert("Y", Imf::Channel(Imf::FLOAT));
                headers[part].setTileDescription(Imf::TileDescription(4, 3,
                  part == 0 ? Imf::RIPMAP_LEVELS : secondMode, Imf::ROUND_UP));
            }
            {
                Imf::MultiPartOutputFile output(path.toLocal8Bit().constData(), headers, 2);
                for (int part = 0; part < 2; ++part) {
                    Imf::TiledOutputPart tiled(output, part);
                    for (int x = 0; x < tiled.numXLevels(); ++x) {
                        for (int y = 0; y < tiled.numYLevels(); ++y) {
                            if (!tiled.isValidLevel(x, y)) continue;
                            const auto data = tiled.dataWindowForLevel(x, y);
                            std::vector<float> values(size_t(tiled.levelWidth(x)) * tiled.levelHeight(y), float(100 * x + y));
                            Imf::FrameBuffer buffer;
                            buffer.insert("Y", Imf::Slice::Make(Imf::FLOAT, values.data(), data,
                              sizeof(float), size_t(tiled.levelWidth(x)) * sizeof(float)));
                            tiled.setFrameBuffer(buffer);
                            tiled.writeTiles(0, tiled.numXTiles(x) - 1, 0, tiled.numYTiles(y) - 1, x, y);
                        }
                    }
                }
            }
            OpenEXRImage source(path, nullptr);
            const auto levels = ResolutionLevels::commonLevels(source.sharedEXR());
            const int expected = secondMode == Imf::ONE_LEVEL ? 1 : secondMode == Imf::MIPMAP_LEVELS ? 4 : 20;
            QCOMPARE(int(levels.size()), expected);
            if (secondMode == Imf::MIPMAP_LEVELS)
                for (const auto level : levels) QCOMPARE(level.x, level.y);
            FileWidget widget(path);
            QTRY_VERIFY(widget.isDocumentReady());
            QVERIFY(widget.resolutionLevels() == levels);
            if (secondMode != Imf::RIPMAP_LEVELS) {
                widget.setResolutionLevel({1, 0});
                QVERIFY(!widget.isRefreshInProgress());
                QCOMPARE(widget.resolutionLevel(), ResolutionLevel());
            }
        }
    }

    void deepRangeCompositesAndReadouts()
    {
        const QString path = fixture("deep-range");
        writeDeepFixture(path);
        OpenEXRImage source(path, nullptr);
        RGBFramebufferModel rgb("", RGBFramebufferModel::Layer_RGB);
        YFramebufferModel red("R"), alpha("A"), depth("Z");
        rgb.load(source.sharedEXR(), 0, {{"R", "G", "B", "A"}});
        red.load(source.sharedEXR(), 0); alpha.load(source.sharedEXR(), 0); depth.load(source.sharedEXR(), 0);
        QTRY_VERIFY(rgb.isPreviewReady() && red.isPreviewReady() && alpha.isPreviewReady() && depth.isPreviewReady());
        QVERIFY(rgb.deepSamples() == red.deepSamples() && red.deepSamples() == depth.deepSamples());
        QCOMPARE(rgb.depthBounds().minimum, 1.); QCOMPARE(rgb.depthBounds().maximum, 4.);
        QCOMPARE(rgb.getRedInfo(1, 0), .625f);
        QCOMPARE(rgb.getGreenInfo(1, 0), .1875f);
        QCOMPARE(rgb.getBlueInfo(1, 0), .125f);
        QCOMPARE(rgb.getAlphaInfo(1, 0), 1.f);
        QCOMPARE(red.getDisplayPixels()[1], rgb.getRedInfo(1, 0));
        QCOMPARE(alpha.getDisplayPixels()[1], 1.f);
        QCOMPARE(depth.getDisplayPixels()[1], 1.f);
        QCOMPARE(rgb.getLoadedImage().pixelColor(0, 0).alpha(), 0);
        QCOMPARE(rgb.getLoadedImage().pixelColor(2, 0).alpha(), 255); // Zero-alpha emission.
        QVERIFY(rgb.getLoadedImage().pixelColor(2, 0).red() > 0);
        QVERIFY(QString::fromStdString(rgb.getColorInfo(1, 0)).contains("x: -1 y: -1 | Composite"));
        QVERIFY(QString::fromStdString(red.getColorInfo(0, 0)).contains("No samples"));
        QCOMPARE(rgb.getDatasetNaNCount(), uint64_t(2)); // One R and one Z, across the whole part.
        QCOMPARE(rgb.getDatasetPositiveInfCount(), uint64_t(1));
        QVERIFY(!depth.anomalyRegions().empty());
        const auto native = rgb.deepSamples();
        DepthRange range; range.full = false; range.minimum = range.maximum = 1.;
        rgb.setDepthRange(range); red.setDepthRange(range); alpha.setDepthRange(range); depth.setDepthRange(range);
        QCOMPARE(rgb.getRedInfo(1, 0), .625f); // Until atomic completion, both image and readout remain old.
        QTRY_VERIFY(rgb.isPreviewReady() && red.isPreviewReady() && alpha.isPreviewReady() && depth.isPreviewReady());
        QCOMPARE(rgb.getRedInfo(1, 0), .5f);
        QCOMPARE(rgb.getAlphaInfo(1, 0), .75f);
        QCOMPARE(red.getDisplayPixels()[1], .5f);
        QCOMPARE(alpha.getDisplayPixels()[1], .75f);
        QCOMPARE(depth.getDisplayPixels()[1], 1.f);
        QCOMPARE(rgb.getLoadedImage().pixelColor(2, 0).alpha(), 0);
        QCOMPARE(rgb.getDatasetNaNCount(), uint64_t(2));
        QVERIFY(rgb.anomalyRegions().empty());
        range.minimum = range.maximum = 2.5;
        rgb.setDepthRange(range);
        QTRY_VERIFY(rgb.isPreviewReady());
        for (int y = 0; y < rgb.height(); ++y)
            for (int x = 0; x < rgb.width(); ++x) QCOMPARE(rgb.getLoadedImage().pixelColor(x, y).alpha(), 0);
        QVERIFY(!rgb.hasFiniteLuminanceSamples());
        const auto empty = PreviewImage::render(rgb);
        QCOMPARE(empty.size(), QSize(6, 5));
        QCOMPARE(PreviewImage::render(rgb, 0, Qt::white).pixelColor(2, 2), QColor(Qt::white));
        // Several unprocessed requests must leave only the last range visible.
        for (double z : {1., 3., 4., 2.}) { range.minimum = range.maximum = z; rgb.setDepthRange(range); }
        QTRY_VERIFY(rgb.isPreviewReady());
        QCOMPARE(rgb.getLoadedImage().pixelColor(1, 0).alpha(), 0);
        QCOMPARE(rgb.getRedInfo(2, 0), .25f);
        QVERIFY(rgb.deepSamples() == native);
        red.setAutomaticRange(true); red.setDepthRange(range);
        QTRY_VERIFY(red.isPreviewReady());
        QCOMPARE(red.displayMinimum(), .125);
        QCOMPARE(red.displayMaximum(), .25);
        QCOMPARE(native->find("Z")->value(0), 3.); // Sorting did not mutate stored order.
        rgb.setDepthRange(DepthRange());
        QTRY_VERIFY(rgb.isPreviewReady());
        QVERIFY(rgb.depthRange().full);
        QCOMPARE(rgb.getRedInfo(1, 0), .625f);
    }

    void deepRawExportsPreserveNativeSamples()
    {
        const QString path = fixture("deep-export");
        writeDeepFixture(path, true);
        OpenEXRImage source(path, nullptr);
        YFramebufferModel red("R"), depth("Z");
        red.load(source.sharedEXR(), 0); depth.load(source.sharedEXR(), 0);
        QTRY_VERIFY(red.isPreviewReady() && depth.isPreviewReady());
        DepthRange range; range.minimum = range.maximum = 2.5; range.full = false;
        red.setDepthRange(range);
        QTRY_VERIFY(red.isPreviewReady());
        const auto original = red.deepSamples();
        ImageSave::Source input; input.sourceImage = &source; input.activeModel = &red;
        for (bool whole : {false, true}) for (bool basic : {false, true}) {
            ImageSave::Options options;
            options.target = whole ? ImageSave::TargetLayeredOriginal : ImageSave::TargetActiveOriginal;
            options.format = ImageSave::FormatExr;
            options.pixelType = ImageSave::PixelFloat;
            options.compression = ImageSave::CompressionDwaa; // Deep ignores lossy/converted settings.
            options.metadata = basic ? ImageSave::MetadataBasic : ImageSave::MetadataNone;
            options.path = fixture(QString("deep-roundtrip-%1-%2").arg(whole).arg(basic));
            QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
            OpenEXRImage output(options.path, nullptr);
            QCOMPARE(output.getEXR().parts(), whole ? 2 : 1);
            for (int part = 0; part < output.getEXR().parts(); ++part) {
                const auto& header = output.getEXR().header(part);
                QCOMPARE(header.type(), Imf::DEEPSCANLINE);
                QCOMPARE(header.compression(), Imf::ZIPS_COMPRESSION);
                QCOMPARE(header.channels().findChannel("R")->type, Imf::HALF);
                QVERIFY(header.channels().findChannel("A") && header.channels().findChannel("Z"));
                QCOMPARE(header.hasView(), basic);
                QCOMPARE(header.findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities") != nullptr, basic);
                if (basic) QCOMPARE(header.view(), std::string(part == 0 ? "left" : "right"));
                QVERIFY(header.dataWindow() == source.getEXR().header(part).dataWindow());
                QVERIFY(header.displayWindow() == source.getEXR().header(part).displayWindow());
                const auto cancel = std::make_shared<std::atomic_bool>(false);
                const auto native = readDeepSamples(source.sharedEXR(), part, cancel);
                const auto saved = readDeepSamples(output.sharedEXR(), part, cancel);
                QVERIFY(saved->counts == native->counts);
                QVERIFY(saved->offsets == native->offsets);
                for (const auto& channel : saved->channels) {
                    const auto* before = native->find(channel.name);
                    QCOMPARE(channel.type, before->type);
                    const int bytes = int(saved->offsets.back() * channel.stride());
                    QCOMPARE(QByteArray(static_cast<const char*>(channel.buffer()), bytes),
                             QByteArray(static_cast<const char*>(before->buffer()), bytes));
                }
                QCOMPARE(saved->channels.size(), size_t(whole ? 6 : 3));
            }
        }
        ImageSave::Options invalid;
        invalid.target = ImageSave::TargetLayeredOriginal; invalid.format = ImageSave::FormatExr;
        invalid.multipart = ImageSave::MultipartFlatten; invalid.path = fixture("deep-flatten");
        QVERIFY(ImageSave::save(input, invalid).message.contains("Preserve multipart"));
        QVERIFY(!QFile::exists(invalid.path));
        invalid.target = ImageSave::TargetActiveOriginal; invalid.channelScope = ImageSave::ChannelsRgb;
        invalid.path = fixture("deep-z-color-only"); input.activeModel = &depth;
        QCOMPARE(ImageSave::save(input, invalid).status, ImageSave::StatusFailed);
        QVERIFY(!QFile::exists(invalid.path));
        input.activeModel = &red;
        invalid.path = fixture("deep-r-color-only");
        QCOMPARE(ImageSave::save(input, invalid).status, ImageSave::StatusSaved);
        invalid.target = ImageSave::TargetPreview; invalid.format = ImageSave::FormatPng;
        invalid.path = m_directory.filePath("deep-empty.png");
        QCOMPARE(ImageSave::save(input, invalid).status, ImageSave::StatusSaved);
        const QImage png(invalid.path);
        QCOMPARE(png.pixelColor(2, 2).alpha(), 0);
        invalid.format = ImageSave::FormatJpeg; invalid.jpegBackground = ImageSave::BackgroundWhite;
        invalid.path = m_directory.filePath("deep-empty.jpg");
        QCOMPARE(ImageSave::save(input, invalid).status, ImageSave::StatusSaved);
        const QImage jpeg(invalid.path);
        QVERIFY(jpeg.pixelColor(2, 2).red() >= 254);
    }

    void deepControlsStereoAndRefresh()
    {
        const QString path = fixture("deep-controls");
        writeDeepFixture(path, true);
        MainWindow window; window.resize(800, 600); window.show(); window.open(path);
        QTRY_VERIFY(window.findChild<ImageFileWidget*>());
        auto* document = window.findChild<ImageFileWidget*>();
        QTRY_VERIFY(document->activeFramebufferModel() && document->activeFramebufferModel()->isPreviewReady());
        auto* page = qobject_cast<RGBFramebufferWidget*>(document->activePreviewWidget());
        QVERIFY(page && page->findChild<DepthRangeWidget*>());
        auto state = page->previewState();
        state.depth.minimum = 1.; state.depth.maximum = 2.; state.depth.full = false;
        state.automatic = true; state.highlightNonFinite = true;
        page->restorePreviewState(state);
        QTRY_VERIFY(document->activeFramebufferModel()->isPreviewReady());
        page->resetCurrentMode();
        QVERIFY(page->previewState().depth == state.depth);
        document->setStereoMode(ImageFileWidget::StereoAnaglyph);
        QTRY_VERIFY(document->stereoMode() == ImageFileWidget::StereoAnaglyph);
        const auto* model = document->activeFramebufferModel();
        QVERIFY(model->depthRange() == state.depth);
        QVERIFY(QString::fromStdString(model->getColorInfo(2, 1)).contains("Right: No samples"));
        window.findChild<QAction*>("action_CopyImageFullResolution")->trigger();
        QCOMPARE(QApplication::clipboard()->image(), PreviewImage::render(*model));
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleMinimalView"));
        auto* control = window.centralWidget()->findChild<DepthRangeWidget*>();
        QVERIFY(control && control->isVisible());
        auto* slider = control->findChild<RangeSliderWidget*>();
        QTest::mouseDClick(slider, Qt::LeftButton, Qt::NoModifier, slider->rect().center());
        QTRY_VERIFY(model->isPreviewReady());
        QVERIFY(model->depthRange().full);
        QCOMPARE(model->depthRange().maximum, 14.);
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleMinimalView"));
        document->refresh();
        QTRY_VERIFY(!document->isRefreshInProgress());
        QVERIFY(document->activeFramebufferModel()->depthRange().full);
        QCOMPARE(document->activeFramebufferModel()->depthRange().maximum, 14.);
        SaveImageDialog dialog(fixture("deep-dialog"));
        dialog.setDeepSourceInfo(true, true); dialog.setSourceState(true, true);
        auto* targets = dialog.findChild<QComboBox*>("targetCombo");
        targets->setCurrentIndex(targets->findData(ImageSave::TargetLayeredOriginal));
        QVERIFY(dialog.findChild<QComboBox*>("compressionCombo")->isHidden());
        QVERIFY(dialog.findChild<QComboBox*>("pixelTypeCombo")->isHidden());
        QVERIFY(!dialog.findChild<QLabel*>("deepInfoLabel")->isHidden());
        QCOMPARE(dialog.options().multipart, ImageSave::MultipartPreserve);
        targets->setCurrentIndex(targets->findData(ImageSave::TargetPreview));
        QVERIFY(dialog.findChild<QLabel*>("deepInfoLabel")->isHidden());
    }

    void deepCancellationAndUnsupportedVolumes()
    {
        const QString path = fixture("deep-cancel"); writeDeepFixture(path);
        OpenEXRImage source(path, nullptr);
        const auto cancel = std::make_shared<std::atomic_bool>(true);
        QVERIFY(!readDeepSamples(source.sharedEXR(), 0, cancel));
        QVERIFY(source.sharedEXR()->deepParts.empty());
        QVERIFY_EXCEPTION_THROWN(ResolutionLevels::query(source.sharedEXR(), 0, {1, 1}), std::runtime_error);
        const QString volume = fixture("deep-zback"); writeDeepFixture(volume, false, true);
        OpenEXRImage volumeSource(volume, nullptr);
        YFramebufferModel depth("Z"); QSignalSpy failed(&depth, &FramebufferModel::loadFailed);
        depth.load(volumeSource.sharedEXR(), 0);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(depth.errorString().contains("ZBack"));
        QVERIFY(!depth.isImageLoaded() && depth.getLoadedImage().isNull());
        const QString broken = fixture("deep-missing-eye"); writeDeepFixture(broken, true, false, 1);
        OpenEXRImage incomplete(broken, nullptr);
        RGBFramebufferModel stereo("", RGBFramebufferModel::Layer_RGB);
        QSignalSpy stereoFailed(&stereo, &FramebufferModel::loadFailed);
        stereo.loadStereo(incomplete.sharedEXR(), stereoInputs());
        QTRY_COMPARE(stereoFailed.count(), 1);
        QVERIFY(!stereo.isImageLoaded() && stereo.getLoadedImage().isNull());
    }

    void deepConstantAndRefreshRangePolicy()
    {
        const QString constantPath = fixture("deep-constant");
        writeDeepFixture(constantPath, false, false, -1, 0.f, true);
        FileWidget constant(constantPath);
        QTRY_VERIFY(constant.activeFramebufferModel() && constant.activeFramebufferModel()->isPreviewReady());
        QCOMPARE(constant.activeFramebufferModel()->depthBounds().minimum, 7.);
        QCOMPARE(constant.activeFramebufferModel()->depthBounds().maximum, 7.);
        QVERIFY(!constant.findChild<DepthRangeWidget*>()->findChild<RangeSliderWidget*>()->isEnabled());
        const QString invalidDepthPath = fixture("deep-no-finite-depth");
        writeDeepFixture(invalidDepthPath, false, false, -1, std::numeric_limits<float>::quiet_NaN());
        FileWidget invalidDepth(invalidDepthPath);
        QTRY_VERIFY(invalidDepth.activeFramebufferModel() && invalidDepth.activeFramebufferModel()->isPreviewReady());
        const auto* invalidModel = invalidDepth.activeFramebufferModel();
        QVERIFY(!invalidModel->depthBounds().finite);
        QVERIFY(!invalidDepth.findChild<DepthRangeWidget*>()->findChild<RangeSliderWidget*>()->isEnabled());
        for (int y = 0; y < invalidModel->height(); ++y)
            for (int x = 0; x < invalidModel->width(); ++x)
                QCOMPARE(invalidModel->getLoadedImage().pixelColor(x, y).alpha(), 0);
        const QString path = fixture("deep-refresh-range"); writeDeepFixture(path);
        FileWidget widget(path);
        QTRY_VERIFY(widget.activeFramebufferModel() && widget.activeFramebufferModel()->isPreviewReady());
        auto* page = qobject_cast<RGBFramebufferWidget*>(widget.activePreviewWidget());
        auto state = page->previewState();
        state.depth.minimum = 1.; state.depth.maximum = 2.; state.depth.full = false;
        page->restorePreviewState(state);
        QTRY_VERIFY(widget.activeFramebufferModel()->isPreviewReady());
        QVERIFY(QFile::rename(path, path + ".old"));
        writeDeepFixture(path, false, false, -1, 2.f);
        widget.refresh(); QTRY_VERIFY(!widget.isRefreshInProgress());
        QCOMPARE(widget.activeFramebufferModel()->depthRange().minimum, 3.);
        QCOMPARE(widget.activeFramebufferModel()->depthRange().maximum, 3.);
        QVERIFY(!widget.activeFramebufferModel()->depthRange().full);
        page = qobject_cast<RGBFramebufferWidget*>(widget.activePreviewWidget());
        state = page->previewState(); state.depth.full = true; page->restorePreviewState(state);
        QTRY_VERIFY(widget.activeFramebufferModel()->isPreviewReady());
        QVERIFY(QFile::rename(path, path + ".second"));
        writeDeepFixture(path, false, false, -1, 4.f);
        widget.refresh(); QTRY_VERIFY(!widget.isRefreshInProgress());
        QCOMPARE(widget.activeFramebufferModel()->depthRange().minimum, 5.);
        QCOMPARE(widget.activeFramebufferModel()->depthRange().maximum, 8.);
        const auto* committed = widget.activeFramebufferModel();
        const QImage image = committed->getLoadedImage();
        QVERIFY(QFile::rename(path, path + ".third"));
        writeDeepFixture(path, false, true);
        dismissNextError(); widget.refresh(); QTRY_VERIFY(!widget.isRefreshInProgress());
        QCOMPARE(widget.activeFramebufferModel(), committed);
        QCOMPARE(widget.activeFramebufferModel()->getLoadedImage(), image);
    }

    void deepTiledAndMissingDependenciesRemainUnsupported()
    {
        for (int kind = 2; kind < 4; ++kind) {
            const QString path = fixture(QString("unsupported-part-%1").arg(kind));
            Imf::Header header(1, 1);
            header.setType(kind == 2 ? Imf::DEEPSCANLINE : Imf::DEEPTILE);
            header.compression() = Imf::ZIPS_COMPRESSION;
            header.channels().insert("Z", Imf::Channel(Imf::FLOAT));
            unsigned int count = 1;
            float value = 1.f;
            float* sample = &value;
            Imf::DeepFrameBuffer buffer;
            buffer.insertSampleCountSlice(Imf::Slice(
              Imf::UINT, reinterpret_cast<char*>(&count), sizeof(count), sizeof(count)));
            buffer.insert("Z", Imf::DeepSlice(
              Imf::FLOAT, reinterpret_cast<char*>(&sample), sizeof(sample),
              sizeof(sample), sizeof(float)));
            if (kind == 2) {
                Imf::DeepScanLineOutputFile file(path.toLocal8Bit().constData(), header);
                file.setFrameBuffer(buffer);
                file.writePixels(1);
            } else {
                header.setTileDescription(Imf::TileDescription(1, 1));
                Imf::DeepTiledOutputFile file(path.toLocal8Bit().constData(), header);
                file.setFrameBuffer(buffer);
                file.writeTile(0, 0, 0, 0);
            }

            OpenEXRImage source(path, nullptr);
            YFramebufferModel model("Z");
            QSignalSpy failed(&model, &FramebufferModel::loadFailed);
            model.load(source.sharedEXR(), 0);
            QTRY_COMPARE(failed.count(), 1);
            QVERIFY(!model.isImageLoaded());
            const QString expected = "Deep";
            QVERIFY(model.errorString().contains(expected));
            ImageSave::Source input;
            input.sourceImage = &source;
            ImageSave::Options options;
            options.target = ImageSave::TargetLayeredOriginal;
            options.format = ImageSave::FormatExr;
            options.path = fixture(QString("unsupported-export-%1").arg(kind));
            const auto saved = ImageSave::save(input, options);
            QCOMPARE(saved.status, ImageSave::StatusFailed);
            QVERIFY(saved.message.contains(expected));
            QVERIFY(!QFile::exists(options.path));
        }
    }

    void invalidMetadata()
    {
        const QString path = fixture("invalid");
        writeFixture(path, 2, 2, {{"Y", {0.f, 1.f, 2.f, 3.f}}});
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QByteArray       bytes = file.readAll();
        const QByteArray attribute("pixelAspectRatio\0float\0", 23);
        const int valueOffset = bytes.indexOf(attribute) + attribute.size() + 4;
        QVERIFY(valueOffset > attribute.size() + 4);
        const float invalid = std::numeric_limits<float>::quiet_NaN();
        bytes.replace(
          valueOffset,
          sizeof(float),
          reinterpret_cast<const char*>(&invalid),
          sizeof(float));
        QVERIFY(file.seek(0));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        try {
            OpenEXRImage      invalidSource(path, nullptr);
            YFramebufferModel invalidModel("Y");
            QSignalSpy error(&invalidModel, &FramebufferModel::loadFailed);
            invalidModel.load(invalidSource.sharedEXR(), 0);
            QTRY_COMPARE(error.count(), 1);
            QVERIFY(!invalidModel.isImageLoaded());
        } catch (const std::exception& error) {
            QVERIFY(
              *error
                 .what());   // OpenEXR may reject the header before decoding.
        }
    }

    void invalidDimensionsAreRejectedBeforeAllocation()
    {
        const QString path = fixture("dimensions");
        writeFixture(path, 2, 2, {{"Y", {0.f, 1.f, 2.f, 3.f}}});
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QByteArray       bytes = file.readAll();
        const QByteArray attribute("dataWindow\0box2i\0", 17);
        const int offset = bytes.indexOf(attribute) + attribute.size() + 4;
        QVERIFY(offset > attribute.size() + 4);
        const qint32 maximum = qToLittleEndian<qint32>(100000);
        bytes
          .replace(offset + 8, 4, reinterpret_cast<const char*>(&maximum), 4);
        bytes
          .replace(offset + 12, 4, reinterpret_cast<const char*>(&maximum), 4);
        QVERIFY(file.seek(0));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        try {
            OpenEXRImage      image(path, nullptr);
            YFramebufferModel model("Y");
            QSignalSpy        failed(&model, &FramebufferModel::loadFailed);
            model.load(image.sharedEXR(), 0);
            QTRY_COMPARE(failed.count(), 1);
            QVERIFY(model.getRawPixels().empty());
        } catch (const std::exception& error) {
            QVERIFY(*error.what());
        }
    }

    void finiteRangesAndStreams()
    {
        for (int map = 0; map < ColormapModule::N_MAPS; ++map) {
            std::unique_ptr<Colormap> colormap(
              ColormapModule::create(static_cast<ColormapModule::Map>(map)));
            for (float value :
                 {0.f,
                  std::numeric_limits<float>::quiet_NaN(),
                  std::numeric_limits<float>::infinity()}) {
                float rgb[3];
                colormap->getRGBValue(value, 1.f, 1.f, rgb);
                for (float channel : rgb)
                    QVERIFY(std::isfinite(channel));
            }
        }
        std::istringstream input("abcd");
        StdIStream         stream(input);
        char               bytes[4];
        QVERIFY(!stream.read(bytes, 4));
        QCOMPARE(stream.tellg(), uint64_t(4));
        QVERIFY_EXCEPTION_THROWN(stream.read(bytes, 1), std::runtime_error);
        stream.seekg(0);
        QVERIFY(stream.read(bytes, 1));
        QVERIFY_EXCEPTION_THROWN(
          stream.seekg(std::numeric_limits<uint64_t>::max()),
          std::runtime_error);

        const QString path = fixture("nonfinite");
        writeFixture(
          path,
          4,
          1,
          {{"depth",
            {2.f,
             2.f,
             std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity()}}});
        OpenEXRImage      source(path, nullptr);
        YFramebufferModel model("depth");
        model.load(source.sharedEXR(), 0);
        QTRY_VERIFY(model.isPreviewReady());
        QCOMPARE(model.getDatasetNaNCount(), uint64_t(1));
        QCOMPARE(model.getDatasetInfCount(), uint64_t(1));
        model.setRange(2., 2.);
        QTRY_VERIFY(model.isPreviewReady());
        QCOMPARE(model.getLoadedImage().pixelColor(0, 0), QColor(Qt::black));
    }

    void truncatedInputFailsCleanly()
    {
        const QString path = fixture("truncated");
        writeFixture(path, 8, 8, {{"Y", std::vector<float>(64, 0.5f)}});
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(12));
        file.close();
        QVERIFY_EXCEPTION_THROWN(OpenEXRImage(path, nullptr), std::exception);
        MainWindow window;
        dismissNextError();
        window.open(path);
        QCOMPARE(window.findChildren<ImageFileWidget*>().size(), 0);
    }

    void rangeSliderIgnoresOtherButtonsAndUnchangedValues()
    {
        RangeSliderWidget slider;
        slider.resize(180, 24);
        QSignalSpy changes(&slider, &RangeSliderWidget::rangeChanged);
        QTest::mouseClick(
          &slider,
          Qt::RightButton,
          Qt::NoModifier,
          QPoint(40, 12));
        QCOMPARE(changes.count(), 0);
        QTest::mouseClick(
          &slider,
          Qt::LeftButton,
          Qt::NoModifier,
          QPoint(8, 12));
        QCOMPARE(changes.count(), 0);
        QTest::mouseClick(
          &slider,
          Qt::LeftButton,
          Qt::NoModifier,
          QPoint(40, 12));
        QCOMPARE(changes.count(), 1);
    }

    void canvasZoomCoordinatesAndItemReuse()
    {
        const QString path = fixture("canvas");
        writeFixture(
          path,
          200,
          100,
          {{"Y", std::vector<float>(20000, 0.4f)}},
          -20,
          10,
          2.f);
        OpenEXRImage        source(path, nullptr);
        RGBFramebufferModel model("Y", RGBFramebufferModel::Layer_Y);
        GraphicsView        view;
        view.resize(480, 300);
        view.setModel(&model);
        view.show();
        model.load(source.sharedEXR(), 0, {{"Y", "", "", ""}});
        QTRY_VERIFY(model.isPreviewReady());
        auto* item = qgraphicsitem_cast<QGraphicsPixmapItem*>(
          view.scene()->items().first());
        QVERIFY(item);
        view.setZoomLevel(2.);
        QSignalSpy   pixels(&view, &GraphicsView::queryPixelInfo);
        const QPoint point
          = view.mapFromScene(item->mapToScene(QPointF(100.25, 50.25)));
        QTest::mouseMove(view.viewport(), point);
        QTRY_VERIFY(!pixels.isEmpty());
        QCOMPARE(pixels.last()[0].toInt(), 100);
        QCOMPARE(pixels.last()[1].toInt(), 50);
        model.setExposure(1.);
        QTRY_VERIFY(model.isPreviewReady());
        QCOMPARE(view.scene()->items().size(), 2); // Pixmap and display-window clip.
        QCOMPARE(view.scene()->items().first(), item);
        const QPointF before = view.mapToScene(point);
        QWheelEvent   wheel(
          QPointF(point),
          QPointF(view.viewport()->mapToGlobal(point)),
          QPoint(),
          QPoint(0, 60),
          Qt::NoButton,
          Qt::NoModifier,
          Qt::NoScrollPhase,
          false);
        QApplication::sendEvent(view.viewport(), &wheel);
        QVERIFY(QLineF(before, view.mapToScene(point)).length() < 1.5);
        const auto state = view.viewState();
        QVERIFY(std::abs(state.zoom - 2. * std::sqrt(1.1)) < 0.00001);
        view.resize(500, 320);
        QApplication::processEvents();
        QVERIFY(std::abs(view.viewState().zoom - state.zoom) < 0.00001);
        QVERIFY(QLineF(view.viewState().center, state.center).length() < 1.5);
        view.setZoomLevel(1000.);
        QCOMPARE(view.viewState().zoom, 64.);
        view.setZoomLevel(0.0001);
        QCOMPARE(view.viewState().zoom, 0.01);
        QTest::keyClick(&view, Qt::Key_0);
        QVERIFY(view.viewState().fit);
        QTest::keyClick(&view, Qt::Key_1);
        QCOMPARE(view.viewState().zoom, 1.);
    }

    void viewMetadataRulesAndDefaultEye()
    {
        Imf::Header header(1, 1);
        header.insert("multiView", Imf::StringVectorAttribute(std::vector<std::string>{"right", "left"}));
        auto views = ViewMetadata::read(header);
        QCOMPARE(views.defaultView(), std::string("right"));
        for (const std::string channel : {"R", "G", "B", "A", "Z"})
            QCOMPARE(views.channelView(channel), std::string("right"));
        QCOMPARE(views.channelView("left.R"), std::string("left"));
        QCOMPARE(views.channelView("nested.forward.left.u"), std::string("left"));
        QVERIFY(views.channelView("disparityL.x").empty());
        QVERIFY(views.channelView("disparityR.y").empty());
        QVERIFY(views.channelView("left.nested.u").empty()); // View must precede the channel.
        header.setView("right");
        QCOMPARE(ViewMetadata::read(header).channelView("left.R"), std::string("right"));
        Imf::Header empty(1, 1);
        empty.insert("multiView", Imf::StringVectorAttribute(std::vector<std::string>()));
        views = ViewMetadata::read(empty);
        QVERIFY(views.hasMultiView && views.channelView("R").empty());
        QVERIFY(!ViewMetadata::read(Imf::Header(1, 1)).present());

        auto data = beachballFixture();
        // The default part now contains only Z. Left RGBA precedes right RGBA.
        std::swap(data.headers[0], data.headers[5]);
        std::swap(data.samples[0], data.samples[5]);
        const QString path = fixture("default-view");
        writeMultipartFixture(path, data);
        OpenEXRImage source(path, nullptr);
        const auto* preferred = source.getLayerModel()->defaultDisplayLayer();
        QVERIFY(preferred);
        QCOMPARE(preferred->getPart(), 5);
        QCOMPARE(preferred->getType(), LayerItem::RGBA);
        QCOMPARE(source.getLayerModel()->viewLabel(preferred), QString(" [right, default]"));
    }

    void beachballSingleAndMultipartSourcesAgree()
    {
        const auto data = beachballFixture();
        const QString multipartPath = fixture("beachball-multipart");
        const QString singlePath = fixture("beachball-single");
        writeMultipartFixture(multipartPath, data);
        Imf::Header header(4, 4);
        header.displayWindow() = data.headers[0].displayWindow();
        header.insert("multiView", Imf::StringVectorAttribute(std::vector<std::string>{"right", "left"}));
        const auto singleName = [](const Imf::Header& part, const std::string& channel) {
            if (!part.hasView() || (part.view() == "right" && channel.find('.') == std::string::npos))
                return channel;
            const auto dot = channel.find_last_of('.');
            const auto offset = dot == std::string::npos ? 0 : dot + 1;
            return channel.substr(0, offset) + part.view() + "." + channel.substr(offset);
        };
        std::map<std::string, std::vector<float>> channels;
        for (size_t part = 0; part < data.headers.size(); ++part) {
            const auto window = data.headers[part].dataWindow();
            for (const auto& channel : data.samples[part]) {
                auto& pixels = channels[singleName(data.headers[part], channel.first)];
                pixels.resize(16, 0.f);
                size_t index = 0;
                for (int y = window.min.y; y <= window.max.y; ++y)
                    for (int x = window.min.x; x <= window.max.x; ++x)
                        pixels[(y + 1) * 4 + x + 2] = channel.second[index++];
            }
        }
        writeFixture(singlePath, 4, 4, channels, -2, -1, 1.f, 1, &header);
        OpenEXRImage multipart(multipartPath, nullptr), single(singlePath, nullptr);
        QCOMPARE(multipart.getEXR().parts(), 10);
        QCOMPARE(multipart.getLayerModel()->defaultDisplayLayer()->getPart(), 0);
        QCOMPARE(single.getLayerModel()->defaultDisplayLayer()->getOriginalFullName(), std::string());
        auto* layers = single.getLayerModel();
        const auto singlePair = layers->stereoLayers();
        const auto multiPair = multipart.getLayerModel()->stereoLayers();
        QVERIFY(singlePair.anaglyphError.isEmpty() && multiPair.anaglyphError.isEmpty());
        QCOMPARE(singlePair.eyes[0]->getOriginalFullName(), std::string("left."));
        QCOMPARE(singlePair.eyes[1]->getOriginalFullName(), std::string());
        QCOMPARE(multiPair.eyes[0]->getPart(), 4);
        QCOMPARE(multiPair.eyes[1]->getPart(), 0);
        QCOMPARE(layers->viewLabel(layers->findChannel(0, "left.R")), QString(" [left]"));
        QCOMPARE(layers->viewLabel(layers->findChannel(0, "disparityL.x")), QString(" [No view]"));
        QVERIFY(layers->viewLabel(layers->getRoot()).isEmpty());
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        for (size_t part = 0; part < data.headers.size(); ++part) {
            const auto window = data.headers[part].dataWindow();
            for (const auto& channel : data.samples[part]) {
                const std::string name = singleName(data.headers[part], channel.first);
                const auto multi = FramebufferLoader::decode(multipart.sharedEXR(), int(part),
                  FramebufferLoader::Scalar, {{channel.first, "", "", ""}}, cancel);
                const auto one = FramebufferLoader::decode(single.sharedEXR(), 0,
                  FramebufferLoader::Scalar, {{name, "", "", ""}}, cancel);
                QVERIFY(multi.data && one.data);
                QVERIFY(multi.data->pixels == channel.second);
                QCOMPARE(multi.data->dataWindow.topLeft(), QPoint(window.min.x, window.min.y));
                QCOMPARE(multi.data->displayWindow, one.data->displayWindow);
                QCOMPARE(multi.data->rawViews.channelView(channel.first), one.data->rawViews.channelView(name));
                size_t index = 0;
                for (int y = window.min.y; y <= window.max.y; ++y)
                    for (int x = window.min.x; x <= window.max.x; ++x)
                        QCOMPARE(multi.data->pixels[index++], one.data->pixels[(y + 1) * 4 + x + 2]);
            }
        }
        RGBFramebufferModel right(""), left("");
        right.load(multipart.sharedEXR(), 0, {{"R", "G", "B", "A"}});
        left.load(multipart.sharedEXR(), 4, {{"R", "G", "B", "A"}});
        QTRY_VERIFY(right.isPreviewReady() && left.isPreviewReady());
        QCOMPARE(right.rawViews().view, std::string("right"));
        QCOMPARE(left.rawViews().view, std::string("left"));
        QVERIFY(right.getColorInfo(0, 0).find("x: -2 y: -1") == 0);
        QVERIFY(right.getRawPixels() != left.getRawPixels());
        RGBFramebufferModel singleStereo(""), multiStereo("");
        singleStereo.loadStereo(single.sharedEXR(),
          {{{0, RGBFramebufferModel::Layer_RGB, {{"left.R", "left.G", "left.B", "left.A"}}},
            {0, RGBFramebufferModel::Layer_RGB, {{"R", "G", "B", "A"}}}}});
        multiStereo.loadStereo(multipart.sharedEXR(),
          {{{4, RGBFramebufferModel::Layer_RGB, {{"R", "G", "B", "A"}}},
            {0, RGBFramebufferModel::Layer_RGB, {{"R", "G", "B", "A"}}}}});
        QTRY_VERIFY(singleStereo.isPreviewReady() && multiStereo.isPreviewReady());
        QCOMPARE(PreviewImage::render(singleStereo, 0, Qt::black), PreviewImage::render(multiStereo, 0, Qt::black));
        cancel->store(true);
        QVERIFY(!FramebufferLoader::decode(multipart.sharedEXR(), 4, FramebufferLoader::RGB,
                                          {{"R", "G", "B", "A"}}, cancel).data);

        // A singlepart export, including the Flatten option, must retain channel names and view order.
        YFramebufferModel rawLeft("left.R");
        rawLeft.load(single.sharedEXR(), 0);
        QTRY_VERIFY(rawLeft.isPreviewReady());
        ImageSave::Source input;
        input.sourceImage = &single;
        input.activeModel = &rawLeft;
        for (auto target : {ImageSave::TargetActiveOriginal, ImageSave::TargetLayeredOriginal}) {
            for (auto metadata : {ImageSave::MetadataBasic, ImageSave::MetadataNone}) {
                ImageSave::Options options;
                options.target = target;
                options.format = ImageSave::FormatExr;
                options.pixelType = ImageSave::PixelFloat;
                options.metadata = metadata;
                options.multipart = ImageSave::MultipartFlatten;
                options.path = fixture(QString("single-views-%1-%2").arg(int(target)).arg(int(metadata)));
                QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
                OpenEXRImage exported(options.path, nullptr);
                const auto& outputHeader = exported.getEXR().header(0);
                QVERIFY(outputHeader.channels().findChannel("left.R"));
                const auto views = ViewMetadata::read(outputHeader);
                QCOMPARE(views.hasMultiView, metadata == ImageSave::MetadataBasic);
                if (views.hasMultiView) QVERIFY(views.multiView == std::vector<std::string>({"right", "left"}));
                YFramebufferModel roundTrip("left.R");
                roundTrip.load(exported.sharedEXR(), 0);
                QTRY_VERIFY(roundTrip.isPreviewReady());
                QVERIFY(roundTrip.getRawPixels() == rawLeft.getRawPixels());
            }
        }
    }

    void beachballMultipartExportsAndFilters()
    {
        const auto data = beachballFixture();
        const QString path = fixture("multipart-exports");
        writeMultipartFixture(path, data);
        OpenEXRImage source(path, nullptr);
        RGBFramebufferModel left("");
        left.load(source.sharedEXR(), 4, {{"R", "G", "B", "A"}});
        QTRY_VERIFY(left.isPreviewReady());
        ImageSave::Source input;
        input.sourceImage = &source;
        input.activeModel = &left;
        for (auto metadata : {ImageSave::MetadataBasic, ImageSave::MetadataNone}) {
            for (auto target : {ImageSave::TargetActiveOriginal, ImageSave::TargetLayeredOriginal}) {
                for (auto scope : {ImageSave::ChannelsAll, ImageSave::ChannelsRgb}) {
                    ImageSave::Options options;
                    options.target = target;
                    options.format = ImageSave::FormatExr;
                    options.pixelType = ImageSave::PixelFloat;
                    options.metadata = metadata;
                    options.channelScope = scope;
                    options.path = fixture(QString("multipart-out-%1-%2-%3")
                      .arg(int(metadata)).arg(int(target)).arg(int(scope)));
                    QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
                    OpenEXRImage output(options.path, nullptr);
                    std::vector<int> indexes = {4};
                    if (target == ImageSave::TargetLayeredOriginal)
                        indexes = scope == ImageSave::ChannelsRgb ? std::vector<int>{0, 4}
                          : std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
                    QCOMPARE(output.getEXR().parts(), int(indexes.size()));
                    for (int i = 0; i < int(indexes.size()); ++i) {
                        const auto& original = data.headers[indexes[i]];
                        const auto& header = output.getEXR().header(i);
                        QVERIFY(header.dataWindow() == original.dataWindow());
                        QVERIFY(header.displayWindow() == original.displayWindow());
                        const auto views = ViewMetadata::read(header);
                        QCOMPARE(views.hasView, metadata == ImageSave::MetadataBasic && original.hasView());
                        if (views.hasView) QCOMPARE(views.view, original.view());
                        if (indexes.size() > 1) {
                            QCOMPARE(header.type(), Imf::SCANLINEIMAGE);
                            QCOMPARE(header.name(), original.name());
                        }
                        for (const auto& channel : data.samples[indexes[i]]) {
                            QVERIFY(header.channels().findChannel(channel.first));
                            YFramebufferModel decoded(channel.first);
                            decoded.load(output.sharedEXR(), i);
                            QTRY_VERIFY(decoded.isPreviewReady());
                            QVERIFY(decoded.getRawPixels() == channel.second);
                        }
                    }
                }
            }
        }
        ImageSave::Options options;
        options.format = ImageSave::FormatExr;
        options.target = ImageSave::TargetLayeredOriginal;
        options.multipart = ImageSave::MultipartFlatten;
        options.metadata = ImageSave::MetadataNone;
        options.path = fixture("rejected-multiview-flatten");
        const auto flattened = ImageSave::save(input, options);
        QCOMPARE(flattened.status, ImageSave::StatusFailed);
        QVERIFY(flattened.message.contains("Preserve multipart"));
        QVERIFY(!QFile::exists(options.path));
        YFramebufferModel depth("Z");
        depth.load(source.sharedEXR(), 1);
        QTRY_VERIFY(depth.isPreviewReady());
        input.activeModel = &depth;
        options.target = ImageSave::TargetActiveOriginal;
        options.channelScope = ImageSave::ChannelsRgb;
        options.path = fixture("rejected-noncolor-active");
        const auto filtered = ImageSave::save(input, options);
        QCOMPARE(filtered.status, ImageSave::StatusFailed);
        QVERIFY(filtered.message.contains("No channels"));

        auto onlyDepth = data;
        onlyDepth.headers = {data.headers[1], data.headers[5]};
        onlyDepth.samples = {data.samples[1], data.samples[5]};
        const QString depthPath = fixture("only-depth-parts");
        writeMultipartFixture(depthPath, onlyDepth);
        OpenEXRImage depthSource(depthPath, nullptr);
        input.sourceImage = &depthSource;
        options.target = ImageSave::TargetLayeredOriginal;
        options.multipart = ImageSave::MultipartPreserve;
        options.path = fixture("rejected-noncolor-file");
        const auto empty = ImageSave::save(input, options);
        QCOMPARE(empty.status, ImageSave::StatusFailed);
        QVERIFY(empty.message.contains("No channels"));
        QVERIFY(!QFile::exists(options.path));
    }

    void beachballFailuresAndRefresh()
    {
        auto data = beachballFixture();
        const QString brokenPath = fixture("missing-part-data");
        writeMultipartFixture(brokenPath, data, 9);
        OpenEXRImage broken(brokenPath, nullptr);
        YFramebufferModel missing("whitebarmask.mask");
        missing.load(broken.sharedEXR(), 9);
        QTRY_VERIFY(!missing.isLoading());
        QVERIFY(!missing.isImageLoaded() && !missing.isPreviewReady());
        QVERIFY(!missing.errorString().isEmpty());

        const QString path = fixture("view-refresh");
        writeMultipartFixture(path, data);
        FileWidget widget(path);
        QTRY_VERIFY(widget.activeFramebufferModel() && widget.activeFramebufferModel()->isPreviewReady());
        QVERIFY(widget.activeLayerTitleText().contains("[right, default]"));
        const auto* left = widget.sourceImage()->getLayerModel()->findChannel(4, "R");
        QVERIFY(left);
        auto* model = widget.openLayer(left);
        QTRY_VERIFY(model->isPreviewReady());
        QVERIFY(widget.activeLayerTitleText().contains("[left]"));
        widget.refresh();
        QTRY_VERIFY(!widget.isRefreshInProgress());
        QTRY_VERIFY(widget.activeFramebufferModel()->isPreviewReady());
        QVERIFY(widget.activeLayerTitleText().contains("[left]"));
        QCOMPARE(widget.activeFramebufferModel()->rawViews().view, std::string("left"));

        MainWindow window;
        window.resize(800, 600);
        window.show();
        window.open(path);
        QTRY_VERIFY(window.findChild<ImageFileWidget*>());
        auto* document = window.findChild<ImageFileWidget*>();
        QTRY_VERIFY(document->activeFramebufferModel() && document->activeFramebufferModel()->isPreviewReady());
        const auto* right = document->activeFramebufferModel();
        const QImage expected = PreviewImage::render(*right);
        auto* copy = window.findChild<QAction*>("action_CopyImage");
        QVERIFY(copy && copy->isEnabled());
        copy->trigger();
        QCOMPARE(QApplication::clipboard()->image(), expected);
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleMinimalView"));
        auto* footer = window.centralWidget()->findChild<QWidget*>("minimalImageFooter");
        QVERIFY(footer && footer->toolTip().contains("[right, default]"));
        QCOMPARE(document->activeFramebufferModel(), right);
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleMinimalView"));
        QVERIFY(document->activeLayerTitleText().contains("[right, default]"));

        // Equal dimensions do not make differently positioned parts safe to flatten.
        data.headers.resize(2);
        data.samples.resize(2);
        for (auto& header : data.headers) header.erase("view");
        const QString shiftedPath = fixture("shifted-parts");
        writeMultipartFixture(shiftedPath, data);
        OpenEXRImage shifted(shiftedPath, nullptr);
        QVERIFY(!shifted.getLayerModel()->hasViews());
        QVERIFY(shifted.getLayerModel()->viewLabel(shifted.getLayerModel()->defaultDisplayLayer()).isEmpty());
        ImageSave::Source input;
        input.sourceImage = &shifted;
        ImageSave::Options options;
        options.format = ImageSave::FormatExr;
        options.target = ImageSave::TargetLayeredOriginal;
        options.multipart = ImageSave::MultipartFlatten;
        options.path = fixture("rejected-shifted-flatten");
        const auto output = ImageSave::save(input, options);
        QCOMPARE(output.status, ImageSave::StatusFailed);
        QVERIFY(output.message.contains("different windows"));
    }

    void stereoMappingAndSourceReadouts()
    {
        const auto fixtureData = stereoFixture();
        const QString path = fixture("stereo-colors");
        writeMultipartFixture(path, fixtureData);
        OpenEXRImage source(path, nullptr);
        RGBFramebufferModel stereo(""), left(""), right("");
        stereo.loadStereo(source.sharedEXR(), stereoInputs());
        left.load(source.sharedEXR(), 0, {{"R", "G", "B", "A"}});
        right.load(source.sharedEXR(), 1, {{"R", "G", "B", "A"}});
        QTRY_VERIFY(stereo.isPreviewReady() && left.isPreviewReady() && right.isPreviewReady());
        QVERIFY(stereo.isDerivedPreview() && stereo.getRawPixels().empty());
        QCOMPARE(stereo.getDataWindow(), QRect(-2, -1, 4, 3));
        QVERIFY(stereo.getColorInfo(0, 0).find("x: -2 y: -1 | Left:") == 0);
        QVERIFY(stereo.getColorInfo(0, 0).find("A: 0") != std::string::npos);
        QVERIFY(stereo.getColorInfo(0, 0).find("Right: no data") != std::string::npos);
        QVERIFY(stereo.getColorInfo(0, 2).empty());
        QCOMPARE(stereo.getDatasetMin(), 0.);
        QCOMPARE(stereo.getDatasetMax(), double(.9f));
        QCOMPARE(stereo.getLuminanceMin(), std::min(left.getLuminanceMin(), right.getLuminanceMin()));
        QCOMPARE(stereo.getLuminanceMax(), std::max(left.getLuminanceMax(), right.getLuminanceMax()));
        // Source alpha is not multiplied into either eye, including zero-alpha emission.
        QVERIFY(stereo.getLoadedImage().pixelColor(0, 0).red() > 0);
        QCOMPARE(stereo.getLoadedImage().pixelColor(0, 0).alpha(), 255);
        for (auto mode : {RGBFramebufferModel::Preview_Exposure, RGBFramebufferModel::Preview_ToneMapping,
                          RGBFramebufferModel::Preview_FalseColor}) {
            for (auto* model : {&stereo, &left, &right}) {
                model->setExposure(1.);
                model->setToneMappingMethod(RGBFramebufferModel::Tone_Clamp);
                model->setToneParameters(0., 1., 0., 0.);
                model->setFalseColorRange(0., 1.);
                model->setPreviewMode(mode);
            }
            QTRY_VERIFY(stereo.isPreviewReady() && left.isPreviewReady() && right.isPreviewReady());
            for (int y = 0; y < 3; ++y) {
                for (int x = 0; x < 4; ++x) {
                    const QPoint file = QPoint(x, y) + stereo.getDataWindow().topLeft();
                    const bool hasLeft = left.getDataWindow().contains(file);
                    const bool hasRight = right.getDataWindow().contains(file);
                    const auto l = hasLeft ? left.getLoadedImage().pixelColor(file - left.getDataWindow().topLeft()) : QColor(0, 0, 0);
                    const auto r = hasRight ? right.getLoadedImage().pixelColor(file - right.getDataWindow().topLeft()) : QColor(0, 0, 0);
                    QCOMPARE(stereo.getLoadedImage().pixelColor(x, y), QColor(l.red(), r.green(), r.blue(), hasLeft || hasRight ? 255 : 0));
                }
            }
        }
        const auto output = PreviewImage::render(stereo);
        QCOMPARE(output.size(), QSize(6, 6));
        QCOMPARE(output.pixelColor(1, 3).alpha(), 0); // Gap within the bounding data rectangle.
        QCOMPARE(output.pixelColor(2, 2).alpha(), 255);
    }

    void stereoLuminanceChromaAndDiagnostics()
    {
        for (auto layout : {RGBFramebufferModel::Layer_Y, RGBFramebufferModel::Layer_YC}) {
            auto data = stereoFixture();
            data.samples[1] = {{"Y", std::vector<float>(6, .5f)}, {"A", std::vector<float>(6, 0.f)}};
            auto inputs = stereoInputs();
            inputs[1].layout = layout;
            inputs[1].channels = {{"Y", "", "", "A"}};
            if (layout == RGBFramebufferModel::Layer_YC) {
                data.samples[1]["RY"] = std::vector<float>(6, 0.f);
                data.samples[1]["BY"] = std::vector<float>(6, 0.f);
                inputs[1].channels = {{"Y", "RY", "BY", "A"}};
            }
            data.samples[0]["G"][0] = std::numeric_limits<float>::quiet_NaN();
            data.samples[1]["A"][5] = std::numeric_limits<float>::infinity();
            const QString path = fixture(QString("stereo-y-%1").arg(int(layout)));
            writeMultipartFixture(path, data);
            OpenEXRImage source(path, nullptr);
            QVERIFY(source.getLayerModel()->stereoLayers().anaglyphError.isEmpty());
            RGBFramebufferModel stereo(""), right("", layout);
            stereo.loadStereo(source.sharedEXR(), inputs);
            right.load(source.sharedEXR(), 1, inputs[1].channels);
            QTRY_VERIFY(stereo.isPreviewReady() && right.isPreviewReady());
            QCOMPARE(stereo.getLoadedImage().pixelColor(3, 2).green(), right.getLoadedImage().pixelColor(2, 1).green());
            QCOMPARE(stereo.getDatasetNaNCount(), uint64_t(1));
            QCOMPARE(stereo.getDatasetPositiveInfCount(), uint64_t(1));
            QCOMPARE(stereo.anomalyRegions().size(), size_t(2));
            QCOMPARE(stereo.anomalyRegions()[0].bounds, QRect(0, 0, 1, 1));
            QCOMPARE(stereo.anomalyRegions()[1].bounds, QRect(3, 2, 1, 1));
            QVERIFY(stereo.getColorInfo(3, 2).find("Y: 0.5") != std::string::npos);
            stereo.setHighlightNonFinite(true);
            const QImage marked = PreviewImage::render(stereo);
            QCOMPARE(marked.pixelColor(1, 3).alpha(), 0); // Markers cannot fill the uncovered gap.
            QCOMPARE(marked.pixelColor(4, 1).alpha(), 0);
        }
    }

    void stereoPairAvailability()
    {
        Imf::Header leftHeader(4, 4), rightHeader(4, 4);
        QVERIFY(ViewMetadata::stereoGeometryMatches(leftHeader, rightHeader));
        rightHeader.dataWindow().min.x = -1;
        QVERIFY(ViewMetadata::stereoGeometryMatches(leftHeader, rightHeader));
        rightHeader.displayWindow().min.x = 1;
        QVERIFY(!ViewMetadata::stereoGeometryMatches(leftHeader, rightHeader));
        rightHeader.displayWindow() = leftHeader.displayWindow();
        rightHeader.pixelAspectRatio() = 1.5f;
        QVERIFY(!ViewMetadata::stereoGeometryMatches(leftHeader, rightHeader));
        for (int kind = 0; kind < 4; ++kind) {
            auto data = stereoFixture();
            if (kind == 0) data.headers[1].erase("view");
            if (kind == 1) {
                data.headers.push_back(data.headers[0]);
                data.headers.back().setName("duplicate-left");
                data.samples.push_back(data.samples[0]);
            }
            if (kind == 2) { // Other layer families must not be silently substituted.
                const auto sourceChannels = data.samples[0];
                data.samples[0].clear();
                for (const auto& channel : sourceChannels)
                    data.samples[0]["other." + channel.first] = channel.second;
            }
            if (kind == 3) for (auto& header : data.headers) header.erase("view");
            const QString path = fixture(QString("stereo-unavailable-%1").arg(kind));
            writeMultipartFixture(path, data);
            OpenEXRImage source(path, nullptr);
            const auto pair = source.getLayerModel()->stereoLayers();
            QVERIFY(!pair.anaglyphError.isEmpty());
            if (kind == 1) QVERIFY(pair.eyeErrors[0].contains("ambiguous"));
        }
    }

    void stereoIncompleteInputDoesNotPublish()
    {
        const QString path = fixture("stereo-incomplete");
        writeMultipartFixture(path, stereoFixture(), 1);
        OpenEXRImage source(path, nullptr);
        RGBFramebufferModel stereo("");
        stereo.loadStereo(source.sharedEXR(), stereoInputs());
        QTRY_VERIFY(!stereo.isLoading());
        QVERIFY(!stereo.isImageLoaded() && !stereo.isPreviewReady());
        QVERIFY(!stereo.errorString().isEmpty());
        QVERIFY(stereo.getLoadedImage().isNull());
    }

    void stereoStateRefreshAndCancellation()
    {
        const QString path = fixture("stereo-state");
        writeMultipartFixture(path, stereoFixture());
        FileWidget widget(path), other(path);
        QTRY_VERIFY(widget.activeFramebufferModel() && other.activeFramebufferModel()
                    && widget.activeFramebufferModel()->isPreviewReady() && other.activeFramebufferModel()->isPreviewReady());
        widget.setStereoMode(ImageFileWidget::StereoRight);
        QCOMPARE(widget.stereoMode(), ImageFileWidget::StereoRight);
        QCOMPARE(other.stereoMode(), ImageFileWidget::StereoDefault);
        widget.setStereoMode(ImageFileWidget::StereoLeft);
        QTRY_VERIFY(widget.activeFramebufferModel()->isPreviewReady());
        auto* color = qobject_cast<RGBFramebufferWidget*>(widget.activePreviewWidget());
        auto state = color->previewState();
        state.exposure = -1.5;
        state.highlightNonFinite = true;
        color->restorePreviewState(state);
        QTRY_VERIFY(widget.activeFramebufferModel()->isPreviewReady());
        const auto* previous = widget.activeFramebufferModel();
        widget.setStereoMode(ImageFileWidget::StereoAnaglyph);
        QCOMPARE(widget.activeFramebufferModel(), previous); // Both eyes must finish first.
        widget.setStereoMode(ImageFileWidget::StereoDefault); // Cancel before queued completion.
        QCOMPARE(widget.stereoMode(), ImageFileWidget::StereoDefault);
        widget.setStereoMode(ImageFileWidget::StereoLeft);
        widget.setStereoMode(ImageFileWidget::StereoAnaglyph);
        QTRY_VERIFY(widget.stereoMode() == ImageFileWidget::StereoAnaglyph);
        QVERIFY(widget.activeFramebufferModel()->isDerivedPreview());
        QCOMPARE(qobject_cast<RGBFramebufferWidget*>(widget.activePreviewWidget())->previewState().exposure, -1.5);
        QVERIFY(widget.activeFramebufferModel()->highlightNonFinite());
        const auto* stereo = widget.activeFramebufferModel();
        widget.setStereoMode(ImageFileWidget::StereoRight);
        widget.setStereoMode(ImageFileWidget::StereoAnaglyph);
        QCOMPARE(widget.activeFramebufferModel(), stereo);
        QCOMPARE(widget.findChildren<RGBFramebufferWidget*>().size(), 3);
        widget.refresh();
        QTRY_VERIFY(!widget.isRefreshInProgress());
        QCOMPARE(widget.stereoMode(), ImageFileWidget::StereoAnaglyph);
        QVERIFY(widget.activeFramebufferModel()->isPreviewReady());
        QCOMPARE(qobject_cast<RGBFramebufferWidget*>(widget.activePreviewWidget())->previewState().exposure, -1.5);
        auto* raw = widget.openLayer(widget.sourceImage()->getLayerModel()->findChannel(0, "R"));
        QTRY_VERIFY(raw->isPreviewReady());
        QCOMPARE(widget.stereoMode(), ImageFileWidget::StereoDefault);
        widget.setStereoMode(ImageFileWidget::StereoAnaglyph);
        const auto* oldImage = widget.sourceImage();
        const auto* oldPreview = widget.activeFramebufferModel();
        QVERIFY(QFile::rename(path, path + ".old"));
        auto missing = stereoFixture();
        missing.headers[0].erase("view");
        writeMultipartFixture(path, missing);
        dismissNextError();
        widget.refresh();
        QCOMPARE(widget.sourceImage(), oldImage);
        QCOMPARE(widget.activeFramebufferModel(), oldPreview);
        QCOMPARE(widget.stereoMode(), ImageFileWidget::StereoAnaglyph);
    }

    void stereoExportsMenuAndMinimal()
    {
        const QString path = fixture("stereo-ui");
        writeMultipartFixture(path, stereoFixture());
        MainWindow window;
        window.resize(800, 600);
        window.show();
        window.open(path);
        QTRY_VERIFY(window.findChild<ImageFileWidget*>());
        auto* document = window.findChild<ImageFileWidget*>();
        QTRY_VERIFY(document->activeFramebufferModel() && document->activeFramebufferModel()->isPreviewReady());
        QVERIFY(window.findChild<QMenu*>("menu_Stereo"));
        auto* action = window.findChild<QAction*>("action_StereoAnaglyph");
        QVERIFY(action && action->isEnabled());
        action->trigger();
        QTRY_VERIFY(document->stereoMode() == ImageFileWidget::StereoAnaglyph);
        QVERIFY(action->isChecked());
        const auto* model = document->activeFramebufferModel();
        window.findChild<QAction*>("action_CopyImageFullResolution")->trigger();
        QCOMPARE(QApplication::clipboard()->image(), PreviewImage::render(*model));
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleMinimalView"));
        auto* footer = window.centralWidget()->findChild<QWidget*>("minimalImageFooter");
        QVERIFY(footer && footer->toolTip().contains("Anaglyph 3D"));
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleMinimalView"));
        QCOMPARE(document->activeFramebufferModel(), model);
        QCOMPARE(document->stereoMode(), ImageFileWidget::StereoAnaglyph);
        SaveImageDialog dialog(fixture("stereo-dialog"));
        dialog.setSourceState(true, true, {}, true);
        auto* targets = dialog.findChild<QComboBox*>("targetCombo");
        auto* items = qobject_cast<QStandardItemModel*>(targets->model());
        QVERIFY(items);
        QVERIFY(!items->item(targets->findData(ImageSave::TargetActiveOriginal))->isEnabled());
        QVERIFY(!items->item(targets->findData(ImageSave::TargetHdrBracketedImages))->isEnabled());
        ImageSave::Source source;
        source.activeModel = model;
        source.sourceImage = document->sourceImage();
        ImageSave::Options options;
        options.path = fixture("stereo-save");
        for (auto target : {ImageSave::TargetActiveOriginal, ImageSave::TargetHdrBracketedImages}) {
            options.target = target;
            QCOMPARE(ImageSave::save(source, options).status, ImageSave::StatusFailed);
        }
        options.target = ImageSave::TargetPreview;
        options.format = ImageSave::FormatHdr;
        QCOMPARE(ImageSave::save(source, options).status, ImageSave::StatusFailed);
        options.format = ImageSave::FormatPng;
        options.path = m_directory.filePath("stereo-preview.png");
        QCOMPARE(ImageSave::save(source, options).status, ImageSave::StatusSaved);
        QCOMPARE(QImage(options.path).pixelColor(1, 3).alpha(), 0);
        options.format = ImageSave::FormatJpeg;
        options.jpegBackground = ImageSave::BackgroundWhite;
        options.path = m_directory.filePath("stereo-preview.jpg");
        QCOMPARE(ImageSave::save(source, options).status, ImageSave::StatusSaved);
        QVERIFY(QImage(options.path).pixelColor(0, 5).red() > 200);
        options.target = ImageSave::TargetLayeredOriginal;
        options.format = ImageSave::FormatExr;
        options.pixelType = ImageSave::PixelFloat;
        options.path = fixture("stereo-source-out");
        QCOMPARE(ImageSave::save(source, options).status, ImageSave::StatusSaved);
        OpenEXRImage exported(options.path, nullptr);
        QCOMPARE(exported.getEXR().parts(), 2);
        QCOMPARE(exported.getEXR().header(0).view(), std::string("left"));
    }

    void displayWindowGeometry_data()
    {
        QTest::addColumn<QRect>("data");
        QTest::addColumn<QRect>("display");
        QTest::addColumn<double>("aspect");
        QTest::addColumn<QRectF>("visible");
        QTest::addColumn<QSize>("output");
        const QRect data(0, 0, 400, 300);
        QTest::newRow("t01") << data << data << 1. << QRectF(data) << QSize(400, 300);
        QTest::newRow("t02") << data << QRect(1, 1, 400, 300) << 1.
                            << QRectF(1, 1, 399, 299) << QSize(400, 300);
        QTest::newRow("t03") << data << QRect(30, 20, 370, 280) << 1.
                            << QRectF(30, 20, 370, 280) << QSize(370, 280);
        QTest::newRow("t04") << data << QRect(0, 0, 370, 280) << 1.
                            << QRectF(0, 0, 370, 280) << QSize(370, 280);
        QTest::newRow("t05") << data << QRect(30, 20, 340, 260) << 1.
                            << QRectF(30, 20, 340, 260) << QSize(340, 260);
        QTest::newRow("t06") << data << QRect(-1, -1, 402, 302) << 1.
                            << QRectF(data) << QSize(402, 302);
        QTest::newRow("t07") << data << QRect(-40, -40, 481, 371) << 1.
                            << QRectF(data) << QSize(481, 371);
        QTest::newRow("t08") << QRect(30, 40, 400, 300) << QRect(0, 0, 501, 401) << 1.
                            << QRectF(data) << QSize(501, 401);
        QTest::newRow("t09") << data << QRect(400, 0, 200, 300) << 1.
                            << QRectF() << QSize(200, 300);
        QTest::newRow("t10") << data << QRect(-100, 0, 100, 300) << 1.
                            << QRectF() << QSize(100, 300);
        QTest::newRow("t11") << data << QRect(0, 300, 400, 200) << 1.
                            << QRectF() << QSize(400, 200);
        QTest::newRow("t12") << data << QRect(0, -100, 400, 100) << 1.
                            << QRectF() << QSize(400, 100);
        QTest::newRow("t13") << data << QRect(399, 299, 101, 101) << 1.
                            << QRectF(399, 299, 1, 1) << QSize(101, 101);
        QTest::newRow("t14") << data << QRect(-100, -100, 101, 101) << 1.
                            << QRectF(0, 0, 1, 1) << QSize(101, 101);
        QTest::newRow("t15") << data << QRect(-40, -40, 481, 371) << 1.5
                            << QRectF(data) << QSize(722, 371);
        QTest::newRow("t16") << data << QRect(-40, -40, 481, 371) << double(2.f / 3.f)
                            << QRectF(data) << QSize(321, 371);
    }

    void displayWindowGeometry()
    {
        QFETCH(QRect, data);
        QFETCH(QRect, display);
        QFETCH(double, aspect);
        QFETCH(QRectF, visible);
        QFETCH(QSize, output);
        const PreviewImage::Geometry geometry(data, display, aspect);
        QCOMPARE(geometry.visiblePixels, visible);
        QCOMPARE(geometry.outputSize(), output);
        QCOMPARE(geometry.sceneWindow().size(), QSizeF(display.width() * aspect, display.height()));
        const QTransform transform = geometry.imageToOutput(output);
        QVERIFY(QLineF(transform.map(geometry.displayPixels.topLeft()), QPointF()).length() < 1e-6);
        QVERIFY(QLineF(transform.map(geometry.displayPixels.bottomRight()),
                       QPointF(output.width(), output.height())).length() < 1e-6);
    }

    void displayWindowOutputLimits()
    {
        const PreviewImage::Geometry geometry(QRect(0, 0, 1, 1),
                                             QRect(0, 0, 1000000, 1000000), 2.);
        QVERIFY(geometry.outputSize().isEmpty());
        QCOMPARE(geometry.outputSize(100), QSize(100, 50));
        const PreviewImage::Geometry extreme(QRect(0, 0, 1, 1), QRect(0, 0, 4, 4),
                                             std::numeric_limits<float>::max());
        QVERIFY(extreme.outputSize().isEmpty());
        QCOMPARE(extreme.outputSize(100), QSize(100, 1));
    }

    void cropIndicator_data() { displayWindowGeometry_data(); }

    void cropIndicator()
    {
        QFETCH(QRect, data);
        QFETCH(QRect, display);
        SyntheticModel model;
        model.startLoading([data, display](const Cancellation&) {
            auto buffer = std::make_shared<FramebufferData>();
            buffer->width = data.width();
            buffer->height = data.height();
            buffer->dataWindow = data;
            buffer->displayWindow = display;
            DecodeResult result;
            result.data = buffer;
            return result;
        });
        QTRY_VERIFY(model.isImageLoaded());
        const QMap<QString, QString> expected = {
          {"t02", "Left, Top"}, {"t03", "Left, Top"}, {"t04", "Right, Bottom"},
          {"t05", "Left, Right, Top, Bottom"}, {"t09", "Left"}, {"t10", "Right"},
          {"t11", "Top"}, {"t12", "Bottom"}, {"t13", "Left, Top"}, {"t14", "Right, Bottom"}};
        const QString directions = expected.value(QString::fromLatin1(QTest::currentDataTag()));
        QWidget parent;
        CropIndicator indicator(&parent);
        indicator.setModel(&model);
        QCOMPARE(indicator.isHidden(), directions.isEmpty());
        QCOMPARE(indicator.size(), QSize(16, 16));
        if (!directions.isEmpty()) {
            QVERIFY(indicator.toolTip().contains("Directions: " + directions + "\n"));
            QCOMPARE(indicator.toolTip().contains("All source data"), !data.intersects(display));
            QCOMPARE(indicator.accessibleDescription(), indicator.toolTip());
        }
        indicator.setModel(nullptr);
        QVERIFY(indicator.isHidden());
        QVERIFY(indicator.toolTip().isEmpty());
        QVERIFY(indicator.accessibleDescription().isEmpty());
    }

    void jpegBackgroundCompositesAlpha()
    {
        // Include partial alpha to catch dropping alpha without compositing it.
        SyntheticModel model;
        model.load();
        QTRY_VERIFY(model.isImageLoaded());
        for (int alpha : {0, 128, 255}) {
            model.requestRender([alpha](const Cancellation&) {
                QImage image(1, 1, QImage::Format_RGBA8888);
                image.fill(QColor(255, 0, 0, alpha));
                return image;
            });
            QTRY_VERIFY(model.isPreviewReady());
            for (auto background : {ImageSave::BackgroundBlack, ImageSave::BackgroundWhite}) {
                const bool white = background == ImageSave::BackgroundWhite;
                const QColor expected(white ? 255 : alpha, white ? 255 - alpha : 0,
                                      white ? 255 - alpha : 0);
                QCOMPARE(PreviewImage::render(model, 0, white ? Qt::white : Qt::black)
                           .pixelColor(0, 0), expected);
                ImageSave::Source source;
                source.activeModel = &model;
                ImageSave::Options options;
                options.format = ImageSave::FormatJpeg;
                options.jpegBackground = background;
                options.quality = 100;
                options.path = m_directory.filePath(QString("alpha-%1-%2.jpg").arg(alpha).arg(int(background)));
                QCOMPARE(ImageSave::save(source, options).status, ImageSave::StatusSaved);
                const QImage jpeg(options.path);
                QVERIFY(!jpeg.isNull());
                const QColor actual = jpeg.pixelColor(0, 0);
                QVERIFY(std::abs(actual.red() - expected.red()) <= 3);
                QVERIFY(std::abs(actual.green() - expected.green()) <= 3);
                QVERIFY(std::abs(actual.blue() - expected.blue()) <= 3);
                QCOMPARE(actual.alpha(), 255);
                options.format = ImageSave::FormatPng;
                options.path += ".png";
                QCOMPARE(ImageSave::save(source, options).status, ImageSave::StatusSaved);
                QCOMPARE(QImage(options.path).pixelColor(0, 0).alpha(), alpha);
            }
            QCOMPARE(PreviewImage::render(model).pixelColor(0, 0).alpha(), alpha);
        }
    }

    void jpegBackgroundDialogVisibilityAndMemory()
    {
        QCOMPARE(ImageSave::Options().jpegBackground, ImageSave::BackgroundBlack);
        SaveImageDialog dialog(m_directory.filePath("background.png"));
        auto* target = dialog.findChild<QComboBox*>("targetCombo");
        auto* format = dialog.findChild<QComboBox*>("formatCombo");
        auto* background = dialog.findChild<QComboBox*>("jpegBackgroundCombo");
        QVERIFY(target && format && background);
        target->setCurrentIndex(target->findData(ImageSave::TargetPreview));
        format->setCurrentIndex(format->findData(ImageSave::FormatJpeg));
        QVERIFY(!background->isHidden());
        background->setCurrentIndex(background->findData(ImageSave::BackgroundWhite));
        format->setCurrentIndex(format->findData(ImageSave::FormatPng));
        QVERIFY(background->isHidden());
        QCOMPARE(dialog.options().jpegBackground, ImageSave::BackgroundWhite);
        target->setCurrentIndex(target->findData(ImageSave::TargetHdrBracketedImages));
        format->setCurrentIndex(format->findData(ImageSave::FormatJpeg));
        QVERIFY(background->isHidden());
        target->setCurrentIndex(target->findData(ImageSave::TargetPreview));
        QVERIFY(!background->isHidden());
        dialog.reject(); // Uses the existing session-local save-option memory.
        SaveImageDialog restored(m_directory.filePath("restored.png"));
        QCOMPARE(restored.options().jpegBackground, ImageSave::BackgroundWhite);
        QVERIFY(!restored.findChild<QComboBox*>("jpegBackgroundCombo")->isHidden());
    }

    void displayWindowPreviewAndRawExport_data()
    {
        QTest::addColumn<QRect>("display");
        QTest::newRow("crop-and-pad") << QRect(-1, 0, 4, 3);
        QTest::newRow("outside") << QRect(2, -1, 4, 3);
        QTest::newRow("lower-right-pixel") << QRect(1, 1, 3, 3);
        QTest::newRow("upper-left-pixel") << QRect(-4, -3, 3, 3);
    }

    void displayWindowPreviewAndRawExport()
    {
        QFETCH(QRect, display);
        const QRect data(-2, -1, 4, 3);
        Imf::Header header(4, 3);
        header.displayWindow() = Imath::Box2i(Imath::V2i(display.left(), display.top()),
                                              Imath::V2i(display.right(), display.bottom()));
        const QString path = fixture("display-export");
        const std::vector<float> red = {0.f, .1f, .2f, .3f, .4f, .5f, .6f, .7f, .8f, .9f, 1.f, .25f};
        writeFixture(path, 4, 3, {{"R", red}, {"G", std::vector<float>(12, .25f)},
                                 {"B", std::vector<float>(12, .5f)}, {"A", std::vector<float>(12, 0.f)}},
                     -2, -1, 1.f, 1, &header);
        OpenEXRImage source(path, nullptr);
        RGBFramebufferWidget rgbPage;
        YFramebufferWidget scalarPage;
        RGBFramebufferModel rgb("");
        YFramebufferModel scalar("R");
        rgbPage.setModel(&rgb);
        scalarPage.setModel(&scalar);
        rgb.load(source.sharedEXR(), 0, {{"R", "G", "B", "A"}});
        scalar.load(source.sharedEXR(), 0);
        QTRY_VERIFY(rgb.isPreviewReady() && scalar.isPreviewReady());
        const auto* rgbCrop = rgbPage.findChild<CropIndicator*>();
        const auto* scalarCrop = scalarPage.findChild<CropIndicator*>();
        QVERIFY(rgbCrop && scalarCrop);
        QVERIFY(!rgbCrop->isHidden() && !scalarCrop->isHidden());
        QCOMPARE(rgbCrop->toolTip(), scalarCrop->toolTip());
        for (const FramebufferModel* model : {static_cast<FramebufferModel*>(&rgb),
                                              static_cast<FramebufferModel*>(&scalar)}) {
            const QImage preview = PreviewImage::render(*model);
            QCOMPARE(preview.size(), display.size());
            for (int y = 0; y < preview.height(); ++y)
                for (int x = 0; x < preview.width(); ++x) {
                    const QPoint file = display.topLeft() + QPoint(x, y);
                    const QColor expected = data.contains(file)
                      ? model->getLoadedImage().pixelColor(file - data.topLeft()) : QColor(Qt::transparent);
                    QCOMPARE(preview.pixelColor(x, y), expected);
                }
            ImageSave::Source input;
            input.activeModel = model;
            input.sourceImage = &source;
            ImageSave::Options options;
            options.path = m_directory.filePath(model == &rgb ? "display-rgb.png" : "display-r.png");
            QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
            QCOMPARE(QImage(options.path).convertToFormat(preview.format()), preview);
            if (!data.intersects(display)) {
                options.format = ImageSave::FormatJpeg;
                options.path += ".jpg";
                QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
                const QImage jpeg(options.path);
                QCOMPARE(jpeg.size(), display.size());
                for (int y = 0; y < jpeg.height(); ++y)
                    for (int x = 0; x < jpeg.width(); ++x)
                        QCOMPARE(jpeg.pixelColor(x, y), QColor(Qt::black));
                GraphicsView emptyView;
                emptyView.resize(300, 200);
                emptyView.setModel(model);
                emptyView.show();
                QSignalSpy reset(&emptyView, &GraphicsView::resetParametersRequested);
                QSignalSpy minimal(&emptyView, &GraphicsView::minimalViewRequested);
                const QPoint center = emptyView.mapFromScene(emptyView.sceneRect().center());
                QTest::mouseClick(emptyView.viewport(), Qt::RightButton, Qt::NoModifier, center);
                QCOMPARE(reset.count(), 1);
                QTest::mouseDClick(emptyView.viewport(), Qt::LeftButton, Qt::NoModifier, center);
                QCOMPARE(minimal.count(), 1);
            }
        }
        QVERIFY(scalar.getRawPixels() == red);
        QVERIFY(scalar.getColorInfo(0, 0).find("x: -2 y: -1") == 0);
        for (auto target : {ImageSave::TargetActiveOriginal, ImageSave::TargetLayeredOriginal}) {
            ImageSave::Source input;
            input.activeModel = &rgb;
            input.sourceImage = &source;
            ImageSave::Options options;
            options.target = target;
            options.format = ImageSave::FormatExr;
            options.pixelType = ImageSave::PixelFloat;
            options.path = fixture(QString("display-raw-%1").arg(int(target)));
            QCOMPARE(ImageSave::save(input, options).status, ImageSave::StatusSaved);
            OpenEXRImage exported(options.path, nullptr);
            RGBFramebufferModel roundTrip("");
            roundTrip.load(exported.sharedEXR(), 0, {{"R", "G", "B", "A"}});
            QTRY_VERIFY(roundTrip.isPreviewReady());
            QCOMPARE(roundTrip.getDataWindow(), data);
            QCOMPARE(roundTrip.getDisplayWindow(), display);
            QVERIFY(roundTrip.getRawPixels() == rgb.getRawPixels());
        }
    }

    void displayWindowClipsMarkersBeforeLayout()
    {
        Imf::Header header(64, 64);
        header.displayWindow() = Imath::Box2i(Imath::V2i(32, 0), Imath::V2i(95, 63));
        std::vector<float> samples(64 * 64, .25f);
        samples[32 * 64 + 30] = std::numeric_limits<float>::quiet_NaN(); // Just outside crop.
        samples[32 * 64 + 48] = std::numeric_limits<float>::infinity();
        const QString path = fixture("display-markers");
        writeFixture(path, 64, 64, {{"V", samples}}, 0, 0, 2.f, 1, &header);
        OpenEXRImage source(path, nullptr);
        YFramebufferModel model("V");
        model.load(source.sharedEXR(), 0);
        QTRY_VERIFY(model.isPreviewReady());
        const QImage base = model.getLoadedImage();
        model.setHighlightNonFinite(true);
        for (int maxWidth : {0, 64}) {
            const QImage output = PreviewImage::render(model, maxWidth);
            QCOMPARE(output.size(), maxWidth ? QSize(64, 32) : QSize(128, 64));
            bool cyan = false;
            for (int y = 0; y < output.height(); ++y)
                for (int x = 0; x < output.width(); ++x) {
                    const QColor pixel = output.pixelColor(x, y);
                    if (x >= output.width() / 2) QCOMPARE(pixel.alpha(), 0);
                    QVERIFY(!(pixel.red() > pixel.green() && pixel.blue() > pixel.green()));
                    cyan |= pixel.green() > 200 && pixel.blue() > 200 && pixel.red() < 50;
                }
            QVERIFY(cyan);
        }
        QCOMPARE(model.getLoadedImage(), base);
        QCOMPARE(model.getDatasetNaNCount(), uint64_t(1));
        QVERIFY(std::isnan(model.getRawPixels()[32 * 64 + 30]));
    }

    void displayWindowViewCoordinatesCopyAndRestore()
    {
        Imf::Header header(4, 3);
        header.displayWindow() = Imath::Box2i(Imath::V2i(-1, 0), Imath::V2i(2, 2));
        const QString path = fixture("display-view");
        writeFixture(path, 4, 3, {{"Y", std::vector<float>(12, .5f)}}, -2, -1, 2.f, 1, &header);
        MainWindow window;
        window.resize(800, 600);
        window.show();
        window.open(path);
        QTRY_VERIFY(window.findChild<ImageFileWidget*>());
        auto* document = window.findChild<ImageFileWidget*>();
        QTRY_VERIFY(document->activeFramebufferModel() && document->activeFramebufferModel()->isPreviewReady());
        auto* view = document->activeGraphicsView();
        auto* model = document->activeFramebufferModel();
        QVERIFY(!document->findChild<CropIndicator*>()->isHidden());
        QCOMPARE(view->sceneRect(), QRectF(2., 1., 8., 3.));
        view->setZoomLevel(20.);
        QSignalSpy pixels(view, &GraphicsView::queryPixelInfo);
        for (const QPointF scenePoint : {QPointF(1., .5), QPointF(9., 2.5), QPointF(5., 2.5)}) {
            pixels.clear();
            QTest::mouseMove(view->viewport(), view->mapFromScene(scenePoint));
            view->refreshPixelInfo();
            QVERIFY(!pixels.isEmpty());
            QCOMPARE(pixels.last()[0].toInt(), scenePoint.x() == 5. ? 2 : -1);
            QCOMPARE(pixels.last()[1].toInt(), scenePoint.x() == 5. ? 2 : -1);
        }
        // Rendering a larger scene area must not reveal the cropped source pixels.
        QImage sceneImage(8, 3, QImage::Format_ARGB32_Premultiplied);
        sceneImage.fill(Qt::transparent);
        {
            QPainter painter(&sceneImage);
            view->scene()->render(&painter, QRectF(0, 0, 8, 3), QRectF(0, 0, 8, 3));
        }
        QCOMPARE(sceneImage.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(sceneImage.pixelColor(2, 1).alpha(), 255);
        auto* scalarPage = document->findChild<YFramebufferWidget*>();
        QVERIFY(scalarPage);
        auto previewState = scalarPage->previewState();
        previewState.highlightNonFinite = true;
        scalarPage->restorePreviewState(previewState);
        QTRY_VERIFY(model->isPreviewReady());
        auto* copy = window.findChild<QAction*>("action_CopyImage");
        QVERIFY(copy && copy->isEnabled());
        copy->trigger();
        const QImage copied = QApplication::clipboard()->image();
        QCOMPARE(copied.size(), QSize(8, 3));
        QCOMPARE(copied.pixelColor(7, 2).alpha(), 0);
        const auto state = view->viewState();
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleMinimalView"));
        auto* minimal = window.centralWidget()->findChild<GraphicsView*>();
        QVERIFY(minimal);
        const auto* minimalCrop = window.centralWidget()->findChild<CropIndicator*>();
        QVERIFY(minimalCrop && !minimalCrop->isHidden());
        QCOMPARE(minimalCrop->toolTip(), document->findChild<CropIndicator*>()->toolTip());
        QCOMPARE(minimal->sceneRect(), QRectF(2., 1., 8., 3.));
        // Padding is an interactive part of the canvas, despite having no source values.
        QSignalSpy reset(minimal, &GraphicsView::resetParametersRequested);
        QTest::mouseClick(minimal->viewport(), Qt::RightButton, Qt::NoModifier,
                          minimal->mapFromScene(QPointF(9., 2.5)));
        QCOMPARE(reset.count(), 1);
        QVERIFY(model->highlightNonFinite());
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleMinimalView"));
        QCOMPARE(view->viewState().zoom, state.zoom);
        QVERIFY(QLineF(view->viewState().center, state.center).length() < 1.5);
        document->refresh();
        QTRY_VERIFY(!document->isRefreshInProgress());
        QTRY_VERIFY(document->activeFramebufferModel()->isPreviewReady());
        QCOMPARE(document->activeGraphicsView()->sceneRect(), QRectF(2., 1., 8., 3.));
        QCOMPARE(document->activeGraphicsView()->viewState().zoom, state.zoom);
        QVERIFY(document->activeFramebufferModel()->highlightNonFinite());
        QVERIFY(!document->findChild<CropIndicator*>()->isHidden());
    }

    void refreshPreservesPreviewAndFailedRefreshKeepsSource()
    {
        const QString path = fixture("refresh");
        writeFixture(path, 2, 2,
          {{"Y", {0.f, 1.f, 2.f, 3.f}}, {"A", {1.f, 1.f, 1.f, 1.f}}});
        FileWidget widget(path);
        widget.resize(800, 500);
        widget.show();
        auto* rgb = widget.findChild<RGBFramebufferWidget*>();
        QVERIFY(rgb);
        QTRY_VERIFY(rgb->framebufferModel()->isPreviewReady());
        auto state       = rgb->previewState();
        state.exposure   = 2.4;
        state.mode       = RGBFramebufferModel::Preview_ToneMapping;
        state.toneMethod = rgb->findChild<QComboBox*>("cbToneMappingMethod")
                             ->findData(RGBFramebufferModel::Tone_Clamp);
        state.toneParameters[0] = 10.;
        state.toneParameters[1] = 20.;
        state.minimum           = -1.;
        state.maximum           = 5.;
        state.automatic         = false;
        rgb->restorePreviewState(state);
        auto* view = rgb->findChild<GraphicsView*>();
        view->setZoomLevel(2.);
        QPointer<RGBFramebufferWidget> old = rgb;
        widget.refresh();
        QVERIFY(widget.isRefreshInProgress());
        QTRY_VERIFY(!widget.isRefreshInProgress());
        QVERIFY(old.isNull());
        rgb = widget.findChild<RGBFramebufferWidget*>();
        QVERIFY(rgb);
        QTRY_VERIFY(rgb->framebufferModel()->isPreviewReady());
        QCOMPARE(rgb->previewState().exposure, 2.4);
        QCOMPARE(rgb->previewState().minimum, -1.);
        QCOMPARE(rgb->previewState().maximum, 5.);
        QCOMPARE(rgb->previewState().toneParameters[0], 10.);
        QCOMPARE(rgb->previewState().toneParameters[1], 20.);
        QCOMPARE(rgb->findChild<GraphicsView*>()->viewState().zoom, 2.);
        const OpenEXRImage* previous = widget.sourceImage();
        QVERIFY(QFile::rename(path, path + ".old"));
        dismissNextError();
        widget.refresh();
        QCOMPARE(widget.sourceImage(), previous);
        QCOMPARE(widget.findChild<RGBFramebufferWidget*>(), rgb);
    }

    void refreshRestoresLayersAndRecomputesAutomaticRange()
    {
        const QString path = fixture("layers");
        writeFixture(
          path,
          2,
          2,
          {{"Y", {0.f, 1.f, 2.f, 3.f}}, {"depth", {4.f, 5.f, 6.f, 7.f}}});
        FileWidget widget(path);
        widget.resize(900, 600);
        widget.show();
        auto* scalar = widget.findChild<YFramebufferWidget*>();
        QVERIFY(scalar);
        QTRY_VERIFY(scalar->framebufferModel()->isPreviewReady());
        auto state      = scalar->previewState();
        state.automatic = true;
        scalar->restorePreviewState(state);
        const LayerItem* depth
          = widget.sourceImage()->getLayerModel()->findChannel(0, "depth");
        QVERIFY(depth);
        auto* depthModel = widget.openLayer(depth);
        QVERIFY(depthModel);
        QTRY_VERIFY(depthModel->isPreviewReady());
        QCOMPARE(widget.findChildren<YFramebufferWidget*>().size(), 2);
        QCOMPARE(
          widget.findChild<QMdiArea*>()->viewMode(),
          QMdiArea::TabbedView);
        QPointer<FramebufferModel> previous = depthModel;
        QVERIFY(QFile::rename(path, path + ".old"));
        writeFixture(path, 2, 2, {{"Y", {10.f, 11.f, 12.f, 13.f}}, {"depth", {20.f, 21.f, 22.f, 23.f}}});
        widget.refresh();
        QVERIFY(widget.isRefreshInProgress());
        QTRY_VERIFY(!widget.isRefreshInProgress());
        QVERIFY(previous.isNull());
        QCOMPARE(widget.findChildren<YFramebufferWidget*>().size(), 2);
        QVERIFY(widget.findChildren<RGBFramebufferWidget*>().isEmpty());
        const auto pages = widget.findChildren<YFramebufferWidget*>();
        for (auto* page : pages) {
            if (page->framebufferModel()->rawChannelNames()[0] != "Y") continue;
            QVERIFY(page->previewState().automatic);
            QCOMPARE(page->previewState().minimum, 10.);
            QCOMPARE(page->previewState().maximum, 13.);
        }
        QCOMPARE(widget.findChild<QMdiArea*>()->viewMode(), QMdiArea::TabbedView);
        const auto* oldSource = widget.sourceImage();
        QVERIFY(QFile::rename(path, path + ".new"));
        writeFixture(path, 2, 2, {{"Y", {1.f, 2.f, 3.f, 4.f}}});
        dismissNextError();
        widget.refresh();
        QVERIFY(!widget.isRefreshInProgress());
        QCOMPARE(widget.sourceImage(), oldSource);
        QCOMPARE(widget.findChildren<YFramebufferWidget*>().size(), 2);
    }

    void closeAndRefreshDuringDecode()
    {
        const QString path = fixture("lifetime");
        writeFixture(
          path,
          256,
          256,
          {{"Y", std::vector<float>(256 * 256, 0.5f)}});
        for (int i = 0; i < 8; ++i) {
            FileWidget widget(path);
            widget.show();
            widget.refresh();
            // Both previous and current jobs can still be in flight at destruction.
        }
        FileWidget survivor(path);
        survivor.show();
        QTRY_VERIFY(
          survivor.activeFramebufferModel()
          && survivor.activeFramebufferModel()->isPreviewReady());
    }

    void movedLayerTabsAndKeyboardActivationSurviveRefresh()
    {
        const QString path = fixture("order");
        writeFixture(
          path,
          2,
          2,
          {{"Y", {0.f, 1.f, 2.f, 3.f}}, {"depth", {4.f, 5.f, 6.f, 7.f}}});
        FileWidget widget(path);
        widget.show();
        QTRY_VERIFY(
          widget.activeFramebufferModel()
          && widget.activeFramebufferModel()->isPreviewReady());
        QTreeView* layers = nullptr;
        for (auto* tree : widget.findChildren<QTreeView*>())
            if (tree->model() == widget.sourceImage()->getLayerModel())
                layers = tree;
        QVERIFY(layers);
        QModelIndex depth;
        for (int row = 0; row < layers->model()->rowCount(); ++row) {
            const QModelIndex index = layers->model()->index(row, 0);
            if (index.data().toString() == "depth") depth = index;
        }
        QVERIFY(depth.isValid());
        layers->setCurrentIndex(depth);
        QTest::keyClick(layers, Qt::Key_Return);
        QTRY_VERIFY(widget.activeFramebufferModel()->isPreviewReady());
        QCOMPARE(
          widget.activeFramebufferModel()->rawChannelNames().front(),
          std::string("depth"));
        auto* mdi  = widget.findChild<QMdiArea*>();
        auto* tabs = mdi->findChild<QTabBar*>();
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 2);
        tabs->moveTab(0, 1);
        const QString first  = tabs->tabText(0);
        const QString second = tabs->tabText(1);
        widget.refresh();
        QVERIFY(widget.isRefreshInProgress());
        QTRY_VERIFY(!widget.isRefreshInProgress());
        QVERIFY(widget.activeFramebufferModel()->isPreviewReady());
        tabs = mdi->findChild<QTabBar*>();
        QVERIFY(tabs);
        QCOMPARE(tabs->tabText(0), first);
        QCOMPARE(tabs->tabText(1), second);
        QCOMPARE(
          widget.activeFramebufferModel()->rawChannelNames().front(),
          std::string("depth"));
    }

    void scaleGradientKeepsBothEndpoints()
    {
        ScaleWidget scale;
        scale.resize(100, 200);
        scale.setColormap(ColormapModule::TURBO);
        const QImage image = scale.grab().toImage();
        QVERIFY(image.pixelColor(15, 12) != image.pixelColor(15, 185));
    }

    void previewExportWaitsForCurrentRender()
    {
        const QString path = fixture("export");
        writeFixture(path, 2, 1, {{"Y", {0.1f, 0.2f}}});
        OpenEXRImage        source(path, nullptr);
        RGBFramebufferModel model("Y", RGBFramebufferModel::Layer_Y);
        model.load(source.sharedEXR(), 0, {{"Y", "", "", ""}});
        QTRY_VERIFY(model.isPreviewReady());
        model.setExposure(1.);
        ImageSave::Source input;
        input.activeModel = &model;
        ImageSave::Options options;
        options.path = m_directory.filePath("preview.png");
        QCOMPARE(
          ImageSave::save(input, options).status,
          ImageSave::StatusFailed);
        QTRY_VERIFY(model.isPreviewReady());
        QCOMPARE(
          ImageSave::save(input, options).status,
          ImageSave::StatusSaved);
        QImage exported(options.path);
        QCOMPARE(
          exported.pixelColor(0, 0),
          model.getLoadedImage().pixelColor(0, 0));
    }

    void dropsDuplicateTabsAndCloseReleaseWidgets()
    {
        const QString path = fixture("tabs");
        writeFixture(path, 2, 2, {{"Y", {0.f, 1.f, 2.f, 3.f}}});
        QMimeData mime;
        mime.setUrls(
          {QUrl::fromLocalFile(path),
           QUrl("https://example.com/a.exr"),
           QUrl::fromLocalFile(m_directory.path()),
           QUrl::fromLocalFile(path)});
        QCOMPARE(localExrFiles(&mime).size(), 2);
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        QDragEnterEvent enter(
          QPoint(30, 100),
          Qt::CopyAction,
          &mime,
          Qt::LeftButton,
          Qt::NoModifier);
        QApplication::sendEvent(&window, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(
          QPointF(30, 100),
          Qt::CopyAction,
          &mime,
          Qt::LeftButton,
          Qt::NoModifier);
        QApplication::sendEvent(&window, &drop);
        auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
        QTRY_COMPARE(tabs->count(), 2);
        QCOMPARE(tabs->tabToolTip(0), path);
        auto* active = qobject_cast<ImageFileWidget*>(tabs->currentWidget());
        QTRY_VERIFY(
          active->activeFramebufferModel()
          && active->activeFramebufferModel()->isPreviewReady());
        auto* copy = window.findChild<QAction*>("action_CopyImage");
        QVERIFY(copy->isEnabled());
        auto* scalar = active->findChild<YFramebufferWidget*>();
        QVERIFY(scalar);
        scalar->findChild<QDoubleSpinBox*>("sbMaxValue")->setValue(2.);
        QVERIFY(!copy->isEnabled());
        QTRY_VERIFY(copy->isEnabled());
        QPointer<ImageFileWidget> closing = active;
        QVERIFY(
          QMetaObject::invokeMethod(
            &window,
            "onTabCloseRequested",
            Q_ARG(int, 1)));
        QVERIFY(closing.isNull());
        QCOMPARE(tabs->count(), 1);
        QVERIFY(
          QMetaObject::invokeMethod(
            &window,
            "onTabCloseRequested",
            Q_ARG(int, 0)));
        QCOMPARE(window.findChildren<ImageFileWidget*>().size(), 0);
        QVERIFY(!copy->isEnabled());
    }

    void themeScreenshots()
    {
        const QString      path = fixture("themes");
        std::vector<float> ramp(640 * 360);
        for (size_t i = 0; i < ramp.size(); ++i)
            ramp[i] = float(i % 640) / 160.f;
        writeFixture(path, 640, 360, {{"R", ramp}, {"G", ramp}, {"B", ramp}});
        MainWindow window;
        window.resize(1200, 760);
        window.open(path);
        window.show();
        auto* document = window.findChild<ImageFileWidget*>();
        QTRY_VERIFY(
          document->activeFramebufferModel()
          && document->activeFramebufferModel()->isPreviewReady());
        QDir().mkpath("test-artifacts");
        for (const QString& theme : {QString("Light"), QString("Dark")}) {
            const QByteArray slot
              = "on_action_Theme" + theme.toLatin1() + "_triggered";
            QVERIFY(QMetaObject::invokeMethod(&window, slot.constData()));
            for (const QString& mode :
                 {QString("Exposure"),
                  QString("ToneMapping"),
                  QString("FalseColor")}) {
                const QByteArray modeSlot
                  = "on_action_Mode" + mode.toLatin1() + "_triggered";
                QVERIFY(
                  QMetaObject::invokeMethod(&window, modeSlot.constData()));
                QTRY_VERIFY(
                  document->activeFramebufferModel()->isPreviewReady());
                QApplication::processEvents();
                QVERIFY(window.grab().save(
                  "test-artifacts/" + theme + "-" + mode + ".png"));
            }
        }
    }
};

QTEST_MAIN(ViewerTests)
#include "ViewerTests.moc"
