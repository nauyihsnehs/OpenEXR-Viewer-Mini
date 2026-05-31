#pragma once

#include <QString>
#include <QStringList>

class FramebufferModel;
class OpenEXRImage;

namespace ImageSave
{
    enum Target
    {
        TargetPreview,
        TargetActiveOriginal,
        TargetLayeredOriginal,
        TargetHdrBracketedImages,
    };

    enum Format
    {
        FormatPng,
        FormatJpeg,
        FormatExr,
        FormatHdr,
    };

    enum ExrCompression
    {
        CompressionNone,
        CompressionRle,
        CompressionZip,
        CompressionPiz,
        CompressionDwaa,
        CompressionDwab,
    };

    enum ExrPixelType
    {
        PixelFloat,
        PixelHalf,
    };

    enum ChannelScope
    {
        ChannelsAll,
        ChannelsRgb,
    };

    enum MetadataPolicy
    {
        MetadataBasic,
        MetadataNone,
    };

    enum MultipartPolicy
    {
        MultipartPreserve,
        MultipartFlatten,
    };

    enum ConflictPolicy
    {
        ConflictAsk,
        ConflictOverwrite,
        ConflictRename,
        ConflictCancel,
    };

    enum Status
    {
        StatusSaved,
        StatusFailed,
        StatusConflict,
        StatusCancelled,
    };

    struct Options
    {
        Target target;
        Format format;
        QString path;

        int maxWidth;
        int quality;

        ExrCompression compression;
        ExrPixelType pixelType;
        ChannelScope channelScope;
        MetadataPolicy metadata;
        MultipartPolicy multipart;
        ConflictPolicy conflict;

        int bracketCount;
        double bracketStepEv;
        double bracketCenterEv;
    };

    struct Source
    {
        const FramebufferModel* activeModel;
        OpenEXRImage* sourceImage;
    };

    struct Result
    {
        Status status;
        QString message;
        QStringList paths;
    };

    QString extension(Format format);
    QString filter(Format format);
    QStringList outputPaths(const Source& source, const Options& options);
    Result save(const Source& source, const Options& options);
}
