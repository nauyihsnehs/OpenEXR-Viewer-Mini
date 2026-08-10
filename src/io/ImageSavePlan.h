#pragma once

#include <io/ImageSave.h>

#include <QString>
#include <QStringList>

#include <vector>

namespace ImageSavePlan
{
    QString     normalizedPath(const QString& path, ImageSave::Format format);
    QStringList outputPaths(const ImageSave::Options& options);
    QString     uniqueOutputPath(const ImageSave::Options& options);
    std::vector<double>
    bracketExposureValues(const ImageSave::Options& options);
}   // namespace ImageSavePlan
