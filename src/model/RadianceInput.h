#pragma once

#include <OpenEXR/ImfHeader.h>
#include <model/framebuffer/FramebufferData.h>
#include <model/LoadProgress.h>
#include <QFile>
#include <QStringList>
#include <memory>
#include <mutex>

// One open file snapshot, shared by color/scalar previews and original export.
class RadianceInput
{
  public:
    static std::shared_ptr<RadianceInput> open(
      const std::shared_ptr<QFile>& file, const Cancellation& cancel = {});
    const Imf::Header& header() const { return m_header; }
    const QStringList& headerLines() const { return m_headerLines; }
    const QString& resolutionLine() const { return m_resolutionLine; }
    // Interleaved source RGB, normalized to top-left image coordinates.
    std::shared_ptr<const std::vector<float>> pixels(
      const Cancellation& cancel, const Progress& progress = {});

  private:
    RadianceInput(const std::shared_ptr<QFile>& file, const Cancellation& cancel);
    struct Axis { char name = 'X'; bool positive = true; int length = 0; };
    std::shared_ptr<QFile> m_file;
    Imf::Header m_header;
    QStringList m_headerLines;
    QString m_resolutionLine;
    Axis m_axes[2];
    qint64 m_pixelOffset = 0;
    std::timed_mutex m_mutex;
    std::shared_ptr<const std::vector<float>> m_pixels;
};
