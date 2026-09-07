#pragma once
#include <QFileInfo>
#include <QMimeData>
#include <QStringList>
#include <QUrl>

inline QStringList localExrFiles(const QMimeData* mime)
{
    QStringList files;
    if (!mime || !mime->hasUrls()) return files;
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile()) continue;
        const QFileInfo info(url.toLocalFile());
        if (
          info.isFile()
          && info.suffix().compare("exr", Qt::CaseInsensitive) == 0)
            files.append(info.absoluteFilePath());
    }
    return files;
}
