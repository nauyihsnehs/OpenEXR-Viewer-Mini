// Opt-in, reproducible release benchmark. No timing assertions in the test suite.
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <model/framebuffer/RGBFramebufferModel.h>
#include <view/mainwindow.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfOutputFile.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>
#ifdef _WIN32
#    define NOMINMAX
#    include <windows.h>
#    include <psapi.h>
#endif

namespace
{
    bool waitUntil(const std::function<bool()>& ready)
    {
        QElapsedTimer timeout;
        timeout.start();
        while (!ready() && timeout.elapsed() < 30000) {
            QApplication::processEvents();
            QThread::msleep(1);
        }
        return ready();
    }
    qint64 privateBytes()
    {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX counters = {};
        if (
          GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters)))
            return static_cast<qint64>(counters.PrivateUsage);
#endif
        return -1;
    }
    RGBFramebufferModel* openImage(MainWindow& window, const QString& path)
    {
        window.open(path);
        auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
        auto* file = qobject_cast<ImageFileWidget*>(tabs->currentWidget());
        if (!file || !file->activeFramebufferModel()) return nullptr;
        auto* model = const_cast<RGBFramebufferModel*>(
          dynamic_cast<const RGBFramebufferModel*>(
            file->activeFramebufferModel()));
        bool       updated    = false;
        const auto connection = QObject::connect(
          model,
          &FramebufferModel::imageChanged,
          &window,
          [&updated] {
              updated = true;
          });
        const bool ready = waitUntil([&updated] {
            return updated;
        });
        QObject::disconnect(connection);
        return ready ? model : nullptr;
    }
}   // namespace

int main(int argc, char** argv)
{
    QApplication  app(argc, argv);
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(
      QSettings::IniFormat,
      QSettings::UserScope,
      directory.path());
    const QString path  = directory.filePath("benchmark.exr");
    const int     width = 2048, height = 1024;
    {
        std::vector<float> pixels(size_t(width) * height * 3);
        for (size_t i = 0; i < pixels.size(); ++i)
            pixels[i] = float(i % 4096) / 1024.f;
        Imf::Header      header(width, height);
        Imf::FrameBuffer buffer;
        const char*      names[] = {"R", "G", "B"};
        for (int c = 0; c < 3; ++c) {
            header.channels().insert(names[c], Imf::Channel(Imf::FLOAT));
            buffer.insert(
              names[c],
              Imf::Slice::Make(
                Imf::FLOAT,
                pixels.data() + c,
                header.dataWindow(),
                3 * sizeof(float),
                size_t(width) * 3 * sizeof(float)));
        }
        Imf::OutputFile output(path.toLocal8Bit().constData(), header);
        output.setFrameBuffer(buffer);
        output.writePixels(height);
    }
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    RGBFramebufferModel* model = openImage(window, path);
    if (!model) return 2;
    int changes = 0;
    QObject::connect(
      model,
      &FramebufferModel::imageChanged,
      &window,
      [&changes] {
          ++changes;
      });
    QJsonObject results;
    results["width"]  = width;
    results["height"] = height;
    for (int mode = 0; mode < 3; ++mode) {
        if (mode != 0) {
            const int before = changes;
            model->setPreviewMode(
              static_cast<RGBFramebufferModel::PreviewMode>(mode));
            if (!waitUntil([&] {
                    return changes > before;
                }))
                return 3;
        }
        std::vector<double> milliseconds;
        for (int iteration = 0; iteration < 5; ++iteration) {
            const int     before = changes;
            QElapsedTimer timer;
            timer.start();
            model->setExposure(0.1 * (iteration + 1));
            if (!waitUntil([&] {
                    return changes > before;
                }))
                return 4;
            milliseconds.push_back(timer.nsecsElapsed() / 1000000.);
        }
        std::sort(milliseconds.begin(), milliseconds.end());
        results[QString("mode_%1_median_ms").arg(mode)] = milliseconds[2];
    }
    model->setPreviewMode(RGBFramebufferModel::Preview_Exposure);
    QElapsedTimer clock;
    clock.start();
    qint64    lastTick = 0, maximumGap = 0;
    int       updates = 0, finalBefore = 0;
    const int before = changes;
    QTimer    heartbeat, adjustments;
    heartbeat.setInterval(5);
    adjustments.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, &window, [&] {
        const qint64 now = clock.elapsed();
        maximumGap       = std::max(maximumGap, now - lastTick);
        lastTick         = now;
    });
    QObject::connect(&adjustments, &QTimer::timeout, &window, [&] {
        finalBefore = changes;
        model->setExposure(double(++updates) / 20.);
        if (updates == 30) adjustments.stop();
    });
    heartbeat.start();
    adjustments.start();
    if (!waitUntil([&] {
            if (updates < 30 || changes <= before) return false;
#ifdef VIEWER_BASELINE
            return changes > finalBefore;
#else
            return model->isPreviewReady();
#endif
        }))
        return 5;
    heartbeat.stop();
    results["rapid_adjustment_total_ms"] = double(clock.elapsed());
    results["maximum_event_gap_ms"]      = double(maximumGap);
    QObject::disconnect(model, nullptr, &window, nullptr);
    QMetaObject::invokeMethod(&window, "onTabCloseRequested", Q_ARG(int, 0));
    QApplication::processEvents();
    const qint64 initialMemory = privateBytes();
    for (int i = 0; i < 8; ++i) {
        if (!openImage(window, path)) return 6;
        QMetaObject::invokeMethod(
          &window,
          "onTabCloseRequested",
          Q_ARG(int, 0));
        QApplication::processEvents();
    }
    results["eight_open_close_private_bytes_delta"]
      = double(privateBytes() - initialMemory);
    results["remaining_file_widgets"]
      = window.findChildren<ImageFileWidget*>().size();
    std::cout
      << QJsonDocument(results).toJson(QJsonDocument::Indented).constData();
    return 0;
}
