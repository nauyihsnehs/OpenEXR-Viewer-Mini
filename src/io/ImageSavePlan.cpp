#include "ImageSavePlan.h"

#include <QFileInfo>

#include <cmath>

namespace
{
    QString uniquePath(const QString& path)
    {
        QFileInfo     info(path);
        const QString base = info.path() + "/" + info.completeBaseName();
        const QString suffix
          = info.suffix().isEmpty() ? "" : "." + info.suffix();

        for (int i = 1; i < 10000; i++) {
            const QString candidate = QString("%1_%2%3")
                                        .arg(base)
                                        .arg(i, 3, 10, QChar('0'))
                                        .arg(suffix);
            if (!QFileInfo::exists(candidate)) return candidate;
        }

        return path;
    }


    int bracketCount(const ImageSave::Options& options)
    {
        int count = options.bracketCount;
        if (count < 3) count = 3;
        if (count > 9) count = 9;
        if (count % 2 == 0) count++;
        if (count > 9) count = 9;

        return count;
    }


    QString evToken(double ev)
    {
        const bool negative = ev < -0.0005;
        QString    value    = QString::number(std::fabs(ev), 'f', 3);

        while (value.contains('.') && value.endsWith('0'))
            value.chop(1);
        if (value.endsWith('.')) value.chop(1);

        value.replace('.', 'p');
        return QString("%1%2").arg(negative ? "-" : "+").arg(value);
    }


    QString bracketOutputPath(const QString& path, double ev)
    {
        QFileInfo info(path);
        return info.path() + "/" + info.completeBaseName() + "_ev" + evToken(ev)
               + "." + info.suffix();
    }


    bool hasExistingPath(const QStringList& paths)
    {
        for (const QString& path : paths) {
            if (QFileInfo::exists(path)) return true;
        }

        return false;
    }
}   // namespace


namespace ImageSavePlan
{
    QString normalizedPath(const QString& path, ImageSave::Format format)
    {
        QFileInfo info(path);
        return info.path() + "/" + info.completeBaseName() + "."
               + ImageSave::extension(format);
    }


    std::vector<double> bracketExposureValues(const ImageSave::Options& options)
    {
        const int    count = bracketCount(options);
        const double step
          = options.bracketStepEv <= 0. ? 2. : options.bracketStepEv;
        const double start = options.bracketCenterEv - step * (count / 2);
        std::vector<double> values;

        values.reserve(count);
        for (int i = 0; i < count; i++) {
            values.push_back(start + step * i);
        }

        return values;
    }


    QStringList outputPaths(const ImageSave::Options& options)
    {
        const QString path = normalizedPath(options.path, options.format);

        if (options.target != ImageSave::TargetHdrBracketedImages) {
            return QStringList() << path;
        }

        QStringList               paths;
        const std::vector<double> values = bracketExposureValues(options);

        for (double ev : values) {
            paths << bracketOutputPath(path, ev);
        }

        return paths;
    }


    QString uniqueOutputPath(const ImageSave::Options& options)
    {
        const QString path = normalizedPath(options.path, options.format);

        if (options.target != ImageSave::TargetHdrBracketedImages) {
            return uniquePath(path);
        }

        QFileInfo     info(path);
        const QString base = info.path() + "/" + info.completeBaseName();
        const QString suffix
          = info.suffix().isEmpty() ? "" : "." + info.suffix();

        for (int i = 1; i < 10000; i++) {
            ImageSave::Options candidate = options;
            candidate.path               = QString("%1_%2%3")
                               .arg(base)
                               .arg(i, 3, 10, QChar('0'))
                               .arg(suffix);

            if (!hasExistingPath(outputPaths(candidate))) {
                return candidate.path;
            }
        }

        return path;
    }
}   // namespace ImageSavePlan
