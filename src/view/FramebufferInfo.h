#pragma once

#include <QString>

#include <model/framebuffer/FramebufferModel.h>
#include <model/framebuffer/PixelDiagnostics.h>

inline QString
framebufferDatasetValueText(const FramebufferModel* model, bool minValue)
{
    if (!model || !model->hasFiniteSamples()) return "n/a";

    const double value
      = minValue ? model->getDatasetMin() : model->getDatasetMax();

    return QString::number(value, 'g', 6);
}


inline QString framebufferSizeText(const FramebufferModel* model)
{
    if (!model || !model->isImageLoaded()) return "n/a";

    return QString("%1 x %2").arg(model->width()).arg(model->height());
}


inline QString framebufferSummaryText(const FramebufferModel* model, bool compact = true)
{
    if (model) {
        if (!model->errorString().isEmpty())
            return compact ? "Error" : "Error: " + model->errorString();
        if (model->isLoading()) return "Loading...";
        if (model->isImageLoaded() && !model->isPreviewReady())
            return "Rendering...";
    }
    if (compact) {
        if (!model || !model->isImageLoaded()) return QString();
        const QSize canvas = model->isProjected() ? model->projectionCanvasSize()
                                                 : model->previewDisplayWindow().size();
        const QString maximum = model->hasFiniteSamples()
          ? QString::fromStdString(PixelDiagnostics::compactSampleText(model->getDatasetMax()))
          : QString::fromUtf8("—");
        return QString("%1×%2  Max %3").arg(canvas.width()).arg(canvas.height()).arg(maximum);
    }
    const QString level = model && model->resolutionLevelCount() > 1
      ? QString("Level %1 | ").arg(QString::fromStdString(model->resolutionLevel().toString())) : QString();
    QString environment;
    if (model && model->rawEnvmap() >= 0) {
        const QSize canvas = model->isProjected() ? model->projectionCanvasSize() : model->previewDisplayWindow().size();
        environment = QString("Source %1 | Display %2 %3×%4 | ")
          .arg(EnvironmentProjection::name(EnvironmentProjection::Type(model->rawEnvmap())))
          .arg(EnvironmentProjection::name(model->projectionState().type))
          .arg(canvas.width()).arg(canvas.height());
        if (!model->environmentSource().available()) environment += model->environmentSource().unavailableReason + " | ";
    }
    QString maximumLabel = "   Max ";
    if (model && model->isDerivedPreview())
        maximumLabel = "   Two-eye source max ";
    else if (model && model->hasDeepSamples())
        maximumLabel = "   Deep source max ";
    return environment + level + "Source size " + framebufferSizeText(model)
           + maximumLabel + framebufferDatasetValueText(model, false);
}

inline QString framebufferSummaryToolTip(const FramebufferModel* model)
{
    QString detail = framebufferSummaryText(model, false);
    if (model && model->isImageLoaded()) {
        detail += "\nMax: maximum finite source channel sample, before display color transforms.";
        if (model->hasDeepSamples())
            detail += "\nDeep statistics include all stored samples, before Depth Range filtering.";
        if (model->isDerivedPreview())
            detail += "\nStereo statistics combine both eyes.";
    }
    return detail;
}
