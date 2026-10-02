#pragma once

#include "FramebufferData.h"

// Published together with the SDR preview, after the render generation commits.
// The data is already linear sRGB, including gamut conversion and projection.
struct HdrPreviewFrame {
    std::shared_ptr<const FramebufferData> data;
    double exposure = 0.; // EV; the display's white level is applied by the viewport.
};
