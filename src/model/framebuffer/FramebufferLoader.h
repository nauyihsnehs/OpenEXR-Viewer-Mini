#pragma once

#include "FramebufferData.h"
#include <model/LoadProgress.h>
#include <model/ExrInput.h>
#include <array>
#include <string>
#include <mutex>

namespace FramebufferLoader
{
    enum Layout
    {
        Scalar,
        RGB,
        Luminance,
        Chroma
    };
    DecodeResult decode(
      const std::shared_ptr<ExrInput>& file,
      int                                             part,
      Layout                                          layout,
      const std::array<std::string, 4>&               channels,
      const Cancellation&                             cancel, ResolutionLevel level = {}, const Progress& progress = {});
}   // namespace FramebufferLoader
