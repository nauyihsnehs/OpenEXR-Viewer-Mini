#pragma once
#include "FramebufferData.h"
#include <model/ExrInput.h>

namespace DeepPreview {
    DepthBounds bounds(const FramebufferData& data);
    DecodeResult decode(std::shared_ptr<FramebufferData> data,
      const std::shared_ptr<ExrInput>& source, int part,
      const std::array<std::string, 4>& names, bool scalar, const Cancellation& cancel);
    std::shared_ptr<const FramebufferData> compose(
      const std::shared_ptr<const FramebufferData>& source, DepthRange range, const Cancellation& cancel);
}
