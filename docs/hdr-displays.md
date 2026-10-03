# Windows HDR display checklist

Implementation has been reviewed statically. Builds, shader compilation, runtime
tests, and HDR hardware checks have not been performed.

Enable HDR in Windows Settings, then choose **View > Mode > HDR**. Use an EXR
with linear RGB patches at 0, 1, 4, and a finite negative component. Keep bright
patches below the monitor's peak when comparing luminance ratios.

- [ ] RGB=1 at 0 EV matches the current Windows SDR white level; RGB=4 is four
  times as bright when the display can reproduce both levels.
- [ ] +1 EV doubles luminance; reset returns to 0 EV.
- [ ] Repeatedly switch HDR to Exposure, Tone mapping, and False color, including
  rapid switches in complete and minimal views. Each mode updates visibly;
  wheel/button zoom, dragging, fit, pixel readout, and context menus keep working.
- [ ] Changing Windows SDR content brightness changes the HDR white baseline
  within one second. The tooltip reports the detected white level, or the
  explicit 80-nit fallback if the query fails.
- [ ] HDR disabled, an SDR screen, or an unsupported GPU produces an SDR preview
  and an explanatory status. Returning to an enabled HDR screen restores HDR.
- [ ] Cross-screen moves, mixed DPI, resize, minimize/restore, and toggling
  complete/minimal view preserve the image and all mouse/keyboard gestures.
- [ ] RGB/YC/Y color layers, Deep depth-range updates, resolution-level changes,
  environment projections, and red/cyan stereo retain their existing geometry,
  coverage, and pixel readout. Scalar layers retain their colormap.
- [ ] NaN/+Inf/-Inf do not poison neighboring display samples. Their original
  values and diagnostic markers remain available. Background and marker colors
  remain coordinated with the SDR interface.
- [ ] Rapid exposure/mode/projection changes show only a coherent committed
  frame; the HDR data and SDR fallback correspond to the same EV.
- [ ] Clipboard and PNG/JPEG preview outputs match Exposure at the same EV;
  they exclude Windows HDR brightness. Source EXR/HDR exports remain unchanged.
- [ ] Oversized images, GPU allocation/presentation failures, and device loss
  fall back to SDR with a reason and working zoom/pan controls. Re-selecting HDR
  retries; unchanged failures do not cause repeated device creation.
- [ ] Loading and error overlays remain visible above the HDR surface.

The HDR swap chain uses a dedicated native child window; the Qt viewport keeps
its own painting path for SDR and fallback. The native back end uses Direct3D 11
and 16-bit floating-point scRGB output.
Windows handles the display's color and luminance limits. The application does
not change the Windows HDR setting.
