# User Guide

[Overview](../README.md) · [Release assessment and sample checks](release-readiness.md)

This guide describes implemented behavior. Visual, hardware and runtime acceptance
are still pending; source review alone does not establish a successful release.

## Open and navigate

Open one or more `.exr`/`.hdr` files with **File > Open**, drag and drop, or
`openexr-viewer.exe image.exr lighting.hdr`. File content determines the reader;
menu/drop filters accept both extensions, including uppercase. Standard input
(`--`) is unsupported. The last folder is remembered; there is no recent-files menu.

Each file gets a tab while its header and pixels load in the background. Progress
appears after one second of continuous work; errors appear immediately in a tab
with copyable details. Percentages refer to the current stage. Closing a tab
cancels its work. Failed refreshes retain the last committed source and image.

Double-click layers or displayable attributes to open preview tabs. A single
preview hides its tab bar. **View > Show** controls Attributes and Layers;
**View > Theme** selects Light or Dark. Window geometry, workspace splitters,
theme and last folder are saved. Per-preview display settings and export choices
are preserved within the applicable document/session, not as a saved project.

| Input | Action |
| --- | --- |
| Ctrl+O / Ctrl+S / Ctrl+W | Open / export / close file |
| F5 / Esc | Refresh / quit |
| Ctrl+Tab / Ctrl+Shift+Tab | Next / previous file tab |
| Ctrl+C / Ctrl+Shift+C | Copy at up to 1024 pixels wide / full-size copy |
| Wheel; + / - | Zoom |
| 0 / 1; zoom percentage button | Fit / 100%; toggle Fit and 100% |
| Left or middle drag | Pan in the complete workspace; see projection exceptions below |
| Ctrl+wheel | Exposure, first tone parameter or false-color upper bound; scalar views unchanged |
| Right-click image | Reset current color mode; tone mapping also returns to Reinhard |
| Double-click image | Enter or leave minimal view |

### Minimal view

Minimal view shows the image and a compact footer. Drag moves the window; wheel
scales image and window together, capped to the available screen. Returning to the
workspace restores its view position. Reset also returns minimal view to 100%,
capped to fit. The footer prioritizes pixel values at narrow widths; hover to see
full text. Projection, depth, resolution and anomaly controls remain linked to the
active preview. Small resolution levels reserve footer space without stretching pixels.

## Source data and display

EXR Scanline, single-level Tiled and stored Mipmap/Ripmap data are supported,
including Multipart. Combined RGB/RGBA/YA/YC/YCA use color controls; individual
R/G/B/A/Y/RY/BY and custom channels use scalar Colormap controls. Pure Y normally
opens as scalar grayscale, with range 0–1 and no exposure or sRGB transform.

Pixel readouts use file coordinates, including negative/nonzero data-window
origins. Source statistics count actual channel samples before display conversion,
including stored alpha, and exclude synthesized components. Subsampled readouts
identify the actual source sample coordinates with `@ (x, y)`.

### Color modes and ranges

- **Exposure** applies EV to linear display colors and encodes an SDR preview.
- **Tone mapping** offers Reinhard, ACES, Filmic/Hable, Log and Clamp. ACES here names
  the available tone curve, not a complete color-management pipeline.
- **False color** maps luminance through the chosen color scale.
- **HDR** is the Windows display mode described below.
- Scalar and false-color maps: Grayscale, BBGR, Turbo, Magma, Inferno, Plasma,
  Viridis. Manual ranges accept scientific notation and finite FLOAT extremes;
  Auto needs finite samples. Constant ranges map finite values to the lower end.
  Scalar ranges use source values directly; color false-color ranges use luminance.

Combined color data is shown opaque over black. Premultiplied RGB is not multiplied
or divided by alpha again, preserving zero-alpha emission. Missing pixels remain
transparent. Original alpha remains inspectable in the A channel and raw export.

Missing/standard chromaticities use linear Rec.709. Other chromaticities convert
source RGB to linear Rec.709 for display, without white-point adaptation. XYZ
basis coordinates with zero primary y are valid EXR metadata; source channels
keep their original names and values. YC/YCA first reconstruct source RGB, then
convert chromaticities. Standard 2×2 chroma uses OpenEXR reconstruction and
saturation correction; 1×1 chroma converts directly. Other sampling layouts can
be inspected channel by channel but cannot form a combined YC preview.

### Canvas, aspect and transparency

Native previews use the Display Window as their canvas. Data outside it is
cropped from previews; missing areas show a checkerboard and have no readout.
The crop icon indicates data outside the canvas, with directions in its tooltip;
padding alone does not show it. This icon is not part of output pixels.

Pixel aspect scales horizontally. At full output size, height equals display-window
height and width is `round(display width × pixel aspect)`, at least one pixel.
Copy/PNG retain transparent padding. JPEG offers Black (0, default) or White (1)
background, remembered for the application session. The checkerboard is never
exported. Raw EXR retains the full Data Window, including cropped data.

### NaN/Inf diagnostics

Click the bottom indicator to toggle markers; keyboard focus and Space also work.
Hollow gray means statistics unavailable, solid gray means no anomalies, and a red
exclamation means anomalies exist. Selected background indicates markers enabled.
Markers start off, remain independent per preview, and survive refresh, level
changes, minimal view and color resets. Loading disables the indicator; ordinary
rendering keeps it available. A loaded anomaly-free image still permits toggling.

NaN is magenta, +Inf cyan and -Inf yellow, in that priority order. Isolated samples
use 12-pixel circles; eight-connected regions use outlines, and overlapping screen
markers merge. Outlines can enclose normal values: use readouts for exact samples.
Strokes remain opaque and independent of exposure/alpha. Copies and preview
exports draw them after resizing. Raw EXR and projection-conversion EXR omit them.
Finite negatives and values above one are not anomalies. With markers off, normal
SDR component mapping sends NaN/-Inf to black and +Inf to full brightness; false
color sends NaN to black and infinities to the range ends.

## Stored resolution levels

**View > Show > Resolution** selects a stored `(X,Y)` pair for the file.
Mipmap uses `(L,L)`; Ripmap has X then Y submenus. The available set is the
intersection of actual valid pairs across parts. Scanline/ONE_LEVEL restrict it
to `(0,0)`; mixed Mipmap/Ripmap can restrict it to diagonal pairs.

Preview/footer sliders show target dimensions while dragging and load on release.
Only the sliders disable during loading; the menu, copying and exporting refer
to the committed level. Arrow keys and Home/End select levels; slider wheel input
does not zoom. All open source and Anaglyph previews commit together. Failure or
cancellation retains the old level and complete images; controls, views, focus and
toolbar scroll survive successful switches. Single-level files hide the sliders.

Each file starts at `(0,0)`. Refresh and minimal-view changes preserve selection,
manual parameters and anomaly settings. Auto recomputes, Fit refits, and manual
zoom keeps its scale and relative center. Data-window origins follow OpenEXR;
display-window origins stay fixed while width/height shrink with the source
rounding mode. Readouts describe the selected level, not level-zero coordinates.
Ripmap keeps its native shape without compensating for different X/Y reductions.
`wrapmodes`, `ilut` and `xDensity` are descriptive metadata, not extra transforms.

## Multiview and stereo

Singlepart `multiView` preserves the original default-view order; Multipart uses
`view`, not part names. Unassigned channels display **No view**, and mixed-view
groups do not receive a single-eye label. Combined layers never cross part or eye
boundaries. Default prefers that view's color layer.

**View > Show > Stereo** offers Default, Left Eye, Right Eye and
Anaglyph 3D. Choices are per file and session. Eye shortcuts keep the layer tree
and other tabs available. Anaglyph requires one unambiguous pair in the default
color-layer family, equal display windows and equal pixel aspect; disabled items
explain incompatibility. Manually opening a source layer leaves shortcut mode.

Anaglyph uses left red and right green/blue after applying the same display
parameters to both eyes. Newly created composites inherit the current preview's
settings, then retain independent settings. Data windows align by file coordinates;
a missing eye contributes zero, and two missing eyes give transparency. Readouts
show both eyes' original samples; statistics include both sources. Pure Y eyes
use scalar controls independently, while their Anaglyph treats Y as linear gray,
so default brightness can differ. There is no eye swap or optimized stereo matrix.

Copies and PNG/JPEG capture Anaglyph. Active Source, HDR and brackets require a
source eye instead. Layered Source remains available for the source document.
Refresh preserves the stereo state; missing pairs or failed loads retain the old
committed document. Beachball frames are opened individually, without playback.

## Deep Scanline

Supported previews use base RGBA/Z point samples at `(0,0)`. **Depth Range**
includes its endpoints, initially spanning all finite Z. Double-click resets it;
constant depths disable dragging. RGB, independent RGBA/Z, stereo and minimal
view share the control behavior. Equal-depth samples retain file order while
near-to-far premultiplied Over produces the composite.

Readouts show **Composite** linear colors or the nearest selected Z. Empty pixels
show **No samples** and remain transparent. Auto uses finite composed values;
source statistics include all stored channels/samples before filtering. Nonfinite
Z never contributes to composition or bounds, but remains in raw data, statistics
and Z markers. Other markers follow selected samples. An empty interval can thus
still show invalid-Z markers.

Samples are shared by previews of a part, with a checked 1 GiB native-cache cap
per part. Range changes recompose without rereading; only a coherent latest result
is committed. Refresh clamps a manual interval to new bounds and recomputes a
full-range selection. Color reset does not reset depth. A new Anaglyph inherits
the interval and applies it to both eyes without changing existing eye pages.

Preview, HDR and bracket outputs use the selected composite. Raw EXR always
retains all native samples, types and file order using Deep Scanline/ZIPS; selected
color exports include required A/Z. Deep Multipart requires the Preserve multipart option.
Deep Tiled, ZBack volumes, cross-file composition and a per-pixel sample table
are unsupported.

## Environment projections

EXR `envmap` metadata takes precedence. Without it, complete 2:1 color EXR/HDR
images with square pixels are inferred as LatLong. Cropped/padded, scalar, Deep
and stereo-derived inputs are not inferred. Channels of a recognized source can
still use projections. New previews show the source layout.

**View > Show > Projection** offers LatLong, Cube, Pers-view and Sphere. Each
samples the original selected source level; switching never loses the hidden
hemisphere or repeatedly resamples a prior preview. Cube is the OpenEXR vertical
+X/-X/+Y/-Y/+Z/-Z strip. Interpolation joins duplicated longitude endpoints/poles
and neighboring cube edges/corners.

- Pers-view: left-drag rotates; wheel inside the displayed canvas adjusts horizontal
  FOV (90° initially, 10°–150°), wheel outside zooms.
- Sphere: an unlit orthographic textured sphere; left-drag rotates and wheel zooms.
  Outside the sphere is transparent and has no readout.
- Middle-drag retains panning/window movement. Ctrl+wheel retains color adjustment.
  Yaw/pitch are shared; **Reset View** restores +Z forward, +Y up and
  90° FOV, leaving color and zoom alone. Refresh/minimal view retain orientation.
- Native readouts remain source-exact; converted readouts say **Interpolated linear**
  and identify source sample coordinates. Statistics and Auto cover the entire
  selected source level, independent of orientation.

During interaction, a temporary raster is capped at 512 pixels on its longest
edge while retaining the logical canvas. Release requests full quality; FOV wheel
input restores it after 150 ms idle. Copy and preview/conversion saving wait for
full quality. Raw export remains available. Incomplete cube-tail levels allow
native viewing/raw export only, with an explanation. Already rendered perspective
or sphere images cannot reconstruct an environment.

## Windows HDR

Enable HDR in Windows Display Settings, then choose **View > Mode > HDR**. Exposure
remains available. At 0 EV, linear RGB=1 follows Windows' SDR content white level;
if querying it fails, the baseline is 80 nits. Finite highlights above one and
negative components remain in the float pipeline. Scalar layers keep colormaps.

The status/tooltip reports HDR or SDR Exposure fallback and its reason. Moving
between displays and changing system HDR/SDR brightness refreshes presentation.
GPU failures fall back to SDR; select HDR again to retry. Copy and PNG/JPEG use
SDR Exposure at the same EV, independent of the Windows white level. HDR works
in complete and minimal views; actual hardware, device-loss and mixed-DPI checks
remain pending. The viewer does not change the system HDR setting.

## Radiance RGBE

Both `#?RADIANCE` and `#?RGBE` are accepted, including flat, modern channel RLE,
legacy RLE, either scan axis and all directions. Linear RGB has alpha one;
RGB/R/G/B views are available. XYZE reports an error. There is no source alpha or
resolution pyramid. Attributes retain original header fields and resolution.
Valid `PRIMARIES` drive display conversion; absent primaries use linear sRGB.
`EXPOSURE`/`COLORCORR` are descriptive and are not reapplied. Cumulative Radiance
`PIXASPECT` is height/width and is inverted for display. Source EXR export produces
one RGB part. Explorer integration remains EXR-only.

## Export

**File > Export** captures a completed preview and its parameters; pending renders
must finish before preview/conversion saving becomes available. Refresh or level
preparation does not substitute an uncommitted source. Options are remembered
for the application session. Full size, 2048w, 1024w and 512w are available where
applicable; JPEG quality and preview transparency background are adjustable.

| Target | Content and limits |
| --- | --- |
| Preview (PNG/JPEG) | Display canvas, aspect correction, selected projection/depth and enabled markers; HDR display becomes SDR Exposure at the same EV |
| Active Source (EXR) | Active source channels, full source windows and selected stored level; YC retains Y/RY/BY sampling and names |
| Layered Source (EXR) | Selected source level across parts/channels; the Preserve multipart option keeps order, names and windows |
| Active Source (HDR) | Display-linear source colors/current Deep composite, before exposure and tone mapping; RGBE cannot preserve negative/nonfinite values or arbitrary channels/alpha |
| Exposure Bracket | 3/5/7/9 PNG/JPEG exposures around a center EV, with configurable step, from source-linear colors |
| Projection | Independent output projection, dimensions and camera; PNG/JPEG apply captured display settings, EXR writes linear converted values |

Flat EXR supports HALF/FLOAT and None/RLE/ZIP/PIZ/DWAA/DWAB compression. Use
FLOAT+ZIP for source-value comparisons; HALF and lossy compression can change
values. Flat output uses scanlines and does not preserve tiles or the full pyramid.
**RGB/YC** includes RGB/Y/RY/BY/A and skips noncolor parts;
an empty selection fails. **Basic** preserves supported chromaticities, pixel
aspect, view and environment metadata; **None** removes optional metadata.
These are selective metadata policies, not an archive of every source attribute.
Deep required structure is always written. Flattening multiview or Deep Multipart
is rejected; ordinary parts must have compatible windows/aspect, and Basic mode
also requires matching chromaticities.

Projection conversion defaults to cube face size N, or `max(1, round(LatLong width/4))`.
Outputs are LatLong 4N×2N, Cube N×6N, perspective/sphere 2N×2N. LatLong/Cube/Sphere
retain their aspect constraints; perspective dimensions and FOV are independent.
Editing save options does not change the view. Conversion EXR defaults to FLOAT+ZIP,
linear Rec.709 RGB/RGBA or the scalar's original name/value, one scanline part,
origin `(0,0)` and pixel aspect one. Sphere adds coverage alpha if needed; scalar
A uses `coverage.A` for the extra mask. LatLong/Cube write target `envmap`;
perspective/sphere omit it. Source layout and values remain available through
Active/Layered Source. HDR/brackets continue to use source layout.

Existing files prompt for Overwrite, Auto Rename or Cancel. Auto Rename fails
clearly if no candidate name is available. Each output is written to a temporary
file and committed only after successful completion; errors retain the original.
An exposure bracket is a series of individual commits: if a later image fails,
the error lists the files already saved. Simultaneous writers to the same output
path are not coordinated across processes.
