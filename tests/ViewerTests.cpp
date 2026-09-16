#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFont>
#include <QGraphicsPixmapItem>
#include <QMessageBox>
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
#include <util/YColormap.h>
#include <util/ColormapModule.h>
#include <view/FileDrop.h>
#include <view/GraphicsView.h>
#include <view/ImageFileWidget.h>
#include <view/RGBFramebufferWidget.h>
#include <view/YFramebufferWidget.h>
#include <view/RangeSliderWidget.h>
#include <view/ScaleWidget.h>
#include <view/mainwindow.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfTiledOutputFile.h>
#include <OpenEXR/ImfTileDescription.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineOutputFile.h>
#include <OpenEXR/ImfDeepTiledOutputFile.h>
#include <OpenEXR/ImfPartType.h>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>

namespace
{
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
            header.channels().insert(
              channel.first,
              Imf::Channel(Imf::FLOAT, sampling, sampling));
            framebuffer.insert(
              channel.first,
              Imf::Slice::Make(
                Imf::FLOAT,
                channel.second.data(),
                window,
                sizeof(float),
                size_t(width / sampling) * sizeof(float),
                sampling,
                sampling));
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

    void deepAndMultilevelPartsRemainUnsupported()
    {
        for (int kind = 0; kind < 4; ++kind) {
            const QString path = fixture(QString("unsupported-part-%1").arg(kind));
            if (kind < 2) {
                Imf::Header header(4, 4);
                header.setTileDescription(Imf::TileDescription(
                  2, 2, kind == 0 ? Imf::MIPMAP_LEVELS : Imf::RIPMAP_LEVELS));
                writeFixture(path, 4, 4, {{"Z", std::vector<float>(16, 1.f)}},
                             0, 0, 1.f, 1, &header);
            } else {
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
            }
            OpenEXRImage source(path, nullptr);
            YFramebufferModel model("Z");
            QSignalSpy failed(&model, &FramebufferModel::loadFailed);
            model.load(source.sharedEXR(), 0);
            QTRY_COMPARE(failed.count(), 1);
            QVERIFY(!model.isImageLoaded());
            const QString expected = kind < 2 ? "Mipmap and Ripmap" : "Deep";
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
        QCOMPARE(view.scene()->items().size(), 1);
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
        writeFixture(path, 2, 2, {{"Y", {10.f, 11.f, 12.f, 13.f}}});
        widget.refresh();
        QVERIFY(widget.isRefreshInProgress());
        QTRY_VERIFY(!widget.isRefreshInProgress());
        QVERIFY(previous.isNull());
        QCOMPARE(widget.findChildren<YFramebufferWidget*>().size(), 1);
        QVERIFY(widget.findChildren<RGBFramebufferWidget*>().isEmpty());
        scalar = widget.findChild<YFramebufferWidget*>();
        QVERIFY(scalar);
        QTRY_VERIFY(scalar->framebufferModel()->isPreviewReady());
        QVERIFY(scalar->previewState().automatic);
        QVERIFY(std::abs(scalar->previewState().minimum - 10.) < 0.001);
        QVERIFY(std::abs(scalar->previewState().maximum - 13.) < 0.001);
        // A single remaining preview fills the workspace without a tab bar.
        QCOMPARE(
          widget.findChild<QMdiArea*>()->viewMode(),
          QMdiArea::SubWindowView);
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
