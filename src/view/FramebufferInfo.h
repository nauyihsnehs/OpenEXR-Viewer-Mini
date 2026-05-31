#pragma once

#include <QString>

#include <model/framebuffer/FramebufferModel.h>

inline QString framebufferDatasetValueText(
  const FramebufferModel* model, bool minValue)
{
    if (!model || !model->hasFiniteSamples()) return "n/a";

    const double value =
      minValue ? model->getDatasetMin() : model->getDatasetMax();

    return QString::number(value, 'g', 6);
}


inline QString framebufferSizeText(const FramebufferModel* model)
{
    if (!model || !model->isImageLoaded()) return "n/a";

    return QString("%1 x %2").arg(model->width()).arg(model->height());
}


inline QString framebufferSummaryText(const FramebufferModel* model)
{
    return "Size " + framebufferSizeText(model) + "   Max "
      + framebufferDatasetValueText(model, false);
}
