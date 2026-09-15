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
#include <model/framebuffer/RGBFramebufferModel.h>
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
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfTiledOutputFile.h>
#include <OpenEXR/ImfTileDescription.h>
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
      int                                              sampling = 1)
    {
        const Imath::Box2i window(
          Imath::V2i(originX, originY),
          Imath::V2i(originX + width - 1, originY + height - 1));
        Imf::Header header(window, window, aspect);
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
        Imf::OutputFile file(path.toLocal8Bit().constData(), header);
        file.setFrameBuffer(framebuffer);
        file.writePixels(height);
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
        QCOMPARE(model->getLoadedImage().pixelColor(1, 0).alpha(), 191);

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
        QCOMPARE(alpha.getRawPixels()[1], 0.75f);
        QCOMPARE(luminance.getRawPixels()[1], 0.5f);
    }

    void unsupportedPartsAndInvalidMetadata()
    {
        const QString tiledPath = fixture("tiled");
        {
            Imf::Header header(2, 2);
            header.channels().insert("Y", Imf::Channel(Imf::FLOAT));
            header.setTileDescription(Imf::TileDescription(2, 2));
            Imf::TiledOutputFile file(
              tiledPath.toLocal8Bit().constData(),
              header);
            float            pixels[] = {0.f, 1.f, 2.f, 3.f};
            Imf::FrameBuffer buffer;
            buffer.insert(
              "Y",
              Imf::Slice::Make(Imf::FLOAT, pixels, header.dataWindow()));
            file.setFrameBuffer(buffer);
            file.writeTiles(0, 0, 0, 0);
        }
        OpenEXRImage      tiled(tiledPath, nullptr);
        YFramebufferModel model("Y");
        QSignalSpy        failed(&model, &FramebufferModel::loadFailed);
        model.load(tiled.sharedEXR(), 0);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!model.isImageLoaded());
        QVERIFY(model.errorString().contains("scanline"));

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
        writeFixture(path, 2, 2, {{"Y", {0.f, 1.f, 2.f, 3.f}}});
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
        auto* rgb = widget.findChild<RGBFramebufferWidget*>();
        QTRY_VERIFY(rgb->framebufferModel()->isPreviewReady());
        auto state      = rgb->previewState();
        state.automatic = true;
        rgb->restorePreviewState(state);
        const LayerItem* depth
          = widget.sourceImage()->getLayerModel()->findChannel(0, "depth");
        QVERIFY(depth);
        widget.openLayer(depth);
        auto* scalar = widget.findChild<YFramebufferWidget*>();
        QVERIFY(scalar);
        QTRY_VERIFY(scalar->framebufferModel()->isPreviewReady());
        QCOMPARE(
          widget.findChild<QMdiArea*>()->viewMode(),
          QMdiArea::TabbedView);
        QPointer<YFramebufferWidget> previous = scalar;
        QVERIFY(QFile::rename(path, path + ".old"));
        writeFixture(path, 2, 2, {{"Y", {10.f, 11.f, 12.f, 13.f}}});
        widget.refresh();
        QVERIFY(widget.isRefreshInProgress());
        QTRY_VERIFY(!widget.isRefreshInProgress());
        QVERIFY(previous.isNull());
        QCOMPARE(widget.findChildren<YFramebufferWidget*>().size(), 0);
        rgb = widget.findChild<RGBFramebufferWidget*>();
        QVERIFY(rgb);
        QTRY_VERIFY(rgb->framebufferModel()->isPreviewReady());
        QVERIFY(rgb->previewState().automatic);
        QVERIFY(std::abs(rgb->previewState().minimum - 10.) < 0.001);
        QVERIFY(std::abs(rgb->previewState().maximum - 13.) < 0.001);
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
        auto* rgb = active->findChild<RGBFramebufferWidget*>();
        rgb->findChild<QDoubleSpinBox*>("sbExposure")->setValue(1.);
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
