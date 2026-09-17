#pragma once

#include <QString>
#include <util/ResolutionLevel.h>
#include <QStringList>
#include <util/EnvironmentProjection.h>
#include <util/PreviewImage.h>

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
        TargetProjectionConversion,
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

    enum JpegBackground { BackgroundBlack, BackgroundWhite };

    struct Options {
        Target  target = TargetPreview;
        Format  format = FormatPng;
        QString path;
        EnvironmentProjection::State projection;
        QSize projectionSize; // Empty selects defaults from the captured source level.

        int maxWidth = 0;
        int quality  = 90;
        JpegBackground jpegBackground = BackgroundBlack;

        ExrCompression  compression  = CompressionZip;
        ExrPixelType    pixelType    = PixelHalf;
        ChannelScope    channelScope = ChannelsAll;
        MetadataPolicy  metadata     = MetadataBasic;
        MultipartPolicy multipart    = MultipartPreserve;
        ConflictPolicy  conflict     = ConflictAsk;

        int    bracketCount    = 3;
        double bracketStepEv   = 2.;
        double bracketCenterEv = 0.;
    };

    struct Source {
        const FramebufferModel* activeModel = nullptr;
        OpenEXRImage*           sourceImage = nullptr;
        ResolutionLevel resolutionLevel;
        std::shared_ptr<const EnvironmentProjection::Snapshot> environment;
        std::shared_ptr<const PreviewImage::Snapshot> preview;
    };

    struct Result {
        Status      status = StatusFailed;
        QString     message;
        QStringList paths;
    };

    QString     extension(Format format);
    QString     filter(Format format);
    QStringList outputPaths(const Source& source, const Options& options);
    Result      save(const Source& source, const Options& options);
}   // namespace ImageSave
