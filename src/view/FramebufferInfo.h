#pragma once

#include <QString>

#include <model/framebuffer/FramebufferModel.h>

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


inline QString framebufferSummaryText(const FramebufferModel* model)
{
    if (model) {
        if (!model->errorString().isEmpty())
            return "Error: " + model->errorString();
        if (model->isLoading()) return "Loading...";
        if (model->isImageLoaded() && !model->isPreviewReady())
            return "Rendering...";
    }
    const QString level = model && model->resolutionLevelCount() > 1
      ? QString("Level %1 | ").arg(QString::fromStdString(model->resolutionLevel().toString())) : QString();
    QString environment;
    if (model && model->rawEnvmap() >= 0) {
        environment = QString("Source %1 | Display %2 %3×%4 | ")
          .arg(EnvironmentProjection::name(EnvironmentProjection::Type(model->rawEnvmap())))
          .arg(EnvironmentProjection::name(model->projectionState().type))
          .arg(model->previewDisplayWindow().width()).arg(model->previewDisplayWindow().height());
        if (!model->environmentSource().available()) environment += model->environmentSource().unavailableReason + " | ";
    }
    return environment + level + "Size " + framebufferSizeText(model)
           + (model && model->isDerivedPreview() ? "   Two-eye source max "
              : model && model->hasDeepSamples() ? "   Deep source max " : "   Max ")
           + framebufferDatasetValueText(model, false);
}
