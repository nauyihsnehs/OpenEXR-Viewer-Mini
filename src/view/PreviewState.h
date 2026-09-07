#pragma once
#include <array>

struct PreviewState {
    int                   mode           = 0;
    int                   toneMethod     = 0;
    double                exposure       = 0.;
    std::array<double, 4> toneParameters = {{0., 0., 0., 0.}};
    int                   colormap       = 0;
    double                minimum        = 0.;
    double                maximum        = 1.;
    double                savedMinimum   = 0.;
    double                savedMaximum   = 1.;
    bool                  automatic      = false;
    bool                  scaleVisible   = false;
};
