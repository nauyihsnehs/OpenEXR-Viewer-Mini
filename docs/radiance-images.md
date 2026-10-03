# Radiance import: pending runtime checks

The importer and regression cases have only been reviewed statically. No build,
test executable, viewer interaction or HDR hardware check was run for this change.

Format reference: [Radiance picture format](https://radsite.lbl.gov/radiance/refer/Notes/picture_format.html)
and [Radiance file formats](https://radsite.lbl.gov/radiance/refer/filefmts.pdf).

## Regression cases in ViewerTests.cpp

- Flat, modern per-channel RLE (runs and literals), legacy RLE and multi-byte repeats.
- Both magic headers; all eight scan orientations/axis orders; Chinese paths and
  a Radiance payload with an EXR suffix.
- Black (zero exponent), highlights above 1, opaque display alpha and RGB/R/G/B views.
- Invalid dimensions, truncated headers/pixels/packets, wrong scanline width,
  zero/overflowing RLE packets, legacy shift overflow, malformed primaries/aspect.
- Cancellation before opening, decoding and returning cached pixels.
- PRIMARIES conversion with source values retained; cumulative inverse PIXASPECT;
  EXPOSURE/COLORCORR retained as metadata; raw whole-source EXR export.
- HDR preview float frames and same-EV SDR PNG export, HDR export/reopen,
  environment projection export, and failed/valid refresh transactions.
- Unmarked 2:1 EXR inference, explicit metadata precedence, non-2:1 images,
  crops, non-square pixels, scalar, Deep and stereo composite exclusions.

## Manual acceptance

1. Open flat and RLE HDR images via menu, drop, command line and recent files.
   Include uppercase suffixes, Unicode paths and large images; cancel while loading.
2. Check raw values and statistics in RGB and each channel. With HDR enabled in
   Windows, distinguish RGB=1 from RGB=4 and verify +1 EV. Switch display modes,
   zoom/pan and enter/leave minimal view.
3. Open unmarked 2:1 HDR/EXR images. Confirm the original image is the default and
   perspective, cube and sphere controls/export are available. Confirm ordinary
   scalar, cropped, Deep and stereo composite previews do not gain inferred projection.
4. Change projection parameters and refresh the source rapidly. Truncate or replace
   the file during refresh: errors must preserve the previous committed frame.
5. Compare copied/PNG/JPEG SDR previews at the selected EV. Reopen source HDR/EXR
   and projected EXR exports; check retained linear values and source chromaticities.
6. Confirm invalid XYZE/oversized/truncated files show a useful error and the viewer
   can subsequently open a valid image.
