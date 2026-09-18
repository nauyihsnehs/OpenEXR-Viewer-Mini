OpenEXR Viewer
==============

[![openexr-viewer](https://snapcraft.io/openexr-viewer/badge.svg)](https://snapcraft.io/openexr-viewer)

A simple viewer for OpenEXR files with detailed metadata probing.

You can display various types of layers, automatically combines RGB,
Luminance-Chroma and Y layers.

![Screenshot from 2021-09-11 01-58-27](https://user-images.githubusercontent.com/7930348/132928984-fd31c2c3-c66f-43c9-b63b-2f1836a09fe8.png)

Viewing controls
================

- Layer previews use tabs, with the tab bar hidden when only one preview is open.
- View > Show toggles the Attributes and Layers panels. Data and display window
  coordinates remain available in Attributes; previews have no window outlines
  or outside-frame dimming.
- Native previews use the display window as their canvas, cropping data outside it.
  Missing data shows the existing checkerboard and has no pixel readout. Copies
  and PNG previews keep missing areas transparent. JPEG previews offer a
  **Transparency background** choice of **Black (0)** (default) or **White (1)**,
  remembered with the other save options for the current application session.
  A small crop icon in the information bar (also in minimal view) indicates source
  data outside the display window. Hover for the affected directions; padding alone
  does not trigger it. This interface indicator is not included in copied or exported images.
  Pixel aspect ratio is applied horizontally in both views and preview outputs.
  Full-size output height is the display-window height, and width is its width
  times pixel aspect ratio, rounded to the nearest pixel (minimum 1).
  The minimal footer labels display dimensions; source data dimensions remain in
  Attributes. Raw EXR exports retain the full data window, including cropped pixels;
  Basic metadata preserves pixel aspect ratio. HDR and exposure-bracket exports
  retain their existing behavior. See the [Display Window checklist](docs/display-window-images.md).
  Its t07/t15/t16 reference JPGs are identical and do not demonstrate the different
  EXR pixel aspect ratios; t15 and t16 correctly export at 722x371 and 321x371.
- Double-click an image to switch between the full workspace and a minimal
  image window. Double-click again to restore the workspace and its view position.
- In the minimal view, drag the image to move the window. The wheel scales the
  image and window together, up to the available screen size.
- Ctrl + wheel keeps the current mode's adjustment: exposure, the first tone
  mapping parameter, or the false-color upper bound. Scalar layers are unchanged.
- Right-click an image to reset the current mode's parameters without changing
  other layers. In tone mapping this also restores the Reinhard method. The full
  workspace keeps its zoom; the minimal view resets to 100%, capped to fit the screen.
- The minimal footer shows file coordinates (including the data-window origin)
  and raw channel values under the pointer,
  prioritizing pixel information when the window is narrow. Hover over the footer
  to read the full text.
- All individual channels (R/G/B/A/Y/RY/BY and custom names such as V) use the same
  Colormap, Range, and Auto controls, defaulting to grayscale over 0 to 1. The range
  uses the source channel value directly, without exposure or an sRGB transform.
  Pixel readouts and active-layer EXR exports retain source channel names.
- Combined RGB/RGBA/YA/YC/YCA layers retain the color preview modes. Changing the
  color preview mode does not change individual-channel previews. Combined color
  previews and their PNG/JPEG exports and copies keep actual data opaque over black: premultiplied
  RGB is neither multiplied nor divided by alpha, preserving zero-alpha emission.
  Source alpha remains available in raw data, statistics, and the A channel.
- Nonstandard RGB chromaticities are converted to linear Rec.709 for display using
  the existing matrix transform, without white-point adaptation or a full ACES/OCIO
  display pipeline. Raw readouts and EXR exports retain source values; Basic metadata
  preserves their chromaticities, while None removes it. YC/YCA readouts and raw EXR
  exports retain Y/RY/BY and any source alpha, including channel prefixes and sampling
  rates. Subsampled readouts include the source sample's file coordinates. Standard
  2x2 chroma uses OpenEXR's reconstruction filters; 1x1 chroma converts directly.
  Other YC sampling combinations remain available as individual channels, but their
  combined preview is unsupported. HDR and exposure-bracket exports use display-linear
  colors. See the [ScanLines manual checklist](docs/scanline-images.md) for checks
  still requiring a running viewer.
- Single-level tiled images use the same color and individual-channel previews as
  scanline images, including depth channels, source values, and anomaly markers.
  Mipmap and Ripmap images also support their stored resolution levels. Flat raw EXR
  exports use scanline storage and retain the existing channel,
  window, and metadata options; they do
  not preserve the source tile layout. See the [Tiles manual checklist](docs/tiled-images.md).
- **View > Show > Resolution Level** selects a stored level for the current file. All
  open source and Anaglyph previews change together only after every new preview is
  ready; cancellation or failure retains the committed images and level. Each file
  starts at `(0,0)`; refresh and minimal-window transitions preserve its selection.
  Mipmap uses a flat list of `(L,L)` levels; Ripmap uses **X level > Y level** submenus.
  Only the final pair selection starts a load. The menu intersects actual valid pairs
  across all parts (scanline/ONE_LEVEL allow only `(0,0)`, Mipmap only diagonal pairs).
  Views use the selected level's native dimensions, retain manual display parameters
  and zoom, recompute Auto ranges, and re-fit when Fit is active.
  Data-window origins follow OpenEXR; the application retains the display-window
  origin and shrinks width by X and height by Y using the source rounding mode.
  Readouts identify the current level and its sample coordinates, not corresponding
  level-0 positions.
  Copy/preview export uses that level's display canvas. Active and whole-file raw EXR
  exports save only the selected pair as scanline data, not the resolution pyramid; the save
  dialog states this explicitly. Source HDR/bracket exports also use the loaded level.
  Ripmap retains the selected level's native shape: Kapaa `(1,0)` is 400×546,
  while `(0,1)` is 799×273. No aspect compensation is applied. `wrapmodes` remains
  descriptive metadata; this viewer does not repeat or mirror textures on a surface.
  See the [MultiView checklist](docs/multiview-images.md) and
  [Multi-Resolution checklist](docs/multi-resolution-images.md) for pending checks.
- The export option **Color channels (RGB/YC)** includes both RGB and luminance/chroma
  channels. See the [Chromaticities checklist](docs/chromaticities-images.md) and
  [Luminance/Chroma checklist](docs/luminance-chroma-images.md) for pending visual checks.
- Multiview files label parts, channels, and previews with their source view and
  prefer the default view's color layer (right RGBA in Beachball). Mixed-view
  containers have no single view label; unassigned channels show **No view**.
  Multipart views come from `view`, not part names; singlepart `multiView` keeps
  its original default-view order. Basic raw EXR exports retain these attributes;
  None removes them. **Preserve multipart** keeps part order, names, windows, and
  channels using scanline storage (Deep parts retain Deep Scanline). Multiview multipart flattening is unsupported.
  Color-only export skips noncolor parts and reports an error for an empty selection.
  Beachball is viewed one file at a time, without sequence playback.
  **View > Show > Stereo** offers **Default**, **Left eye only**, **Right eye only**,
  and **Anaglyph 3D**, independently for each open file. Eye shortcuts open color
  layers (including pure Y) while leaving the full layer tree available. Anaglyph combines the left
  red component with the right green/blue components after applying the same
  display settings to both eyes. It requires a unique pair in the default color
  layer family with matching display windows and pixel aspect ratios; unavailable
  choices explain why in their tooltips. Different data windows align by file
  coordinates, with transparent areas wherever both eyes lack data.
  Default honors the original view order: Adjuster opens center, while its stereo pair
  remains left/right. Pure Y eye pages use scalar Colormap (gray, 0–1 by default);
  their Anaglyph interprets Y as linear gray and applies the color preview settings,
  so its default brightness can differ. `ilut` and `xDensity` are displayed metadata,
  not additional LUT or physical-size transforms.
  Anaglyph readouts show each eye's source samples and its statistics include both
  eyes. Copies and PNG/JPEG exports include the derived preview. Active original,
  HDR and bracketed exports require selecting a source eye layer; whole-file EXR
  export remains available. Refresh and minimal view retain stereo state; opening
  a source layer manually returns the menu to Default. See the
  [Beachball checklist](docs/beachball-images.md), including
  frame 6 cropping and the difference between stored zeros and missing data.
- Complete LatLong/Cube sources with `envmap` metadata support **View > Show >
  Projection**: **LatLong**, **Cube**, **Pers-view**, and **Sphere**. New previews
  show the source layout; the source projection is identified separately in the
  information bar. Color layers and individual channels share projection controls.
  Every view samples the original current resolution level, so switching or rotating
  never discards the hidden hemisphere or repeatedly resamples a previous preview.
  Cube uses OpenEXR's vertical +X/-X/+Y/-Y/+Z/-Z strip and face orientations.
  LatLong uses its duplicated longitude endpoints and poles; cube interpolation
  joins the duplicated edge/corner samples of adjacent faces.
  Pers-view: left-drag to turn; wheel inside the displayed canvas changes horizontal
  FOV (90° initially, 10°–150°), while wheel outside it zooms the image.
  Sphere: left-drag to rotate an orthographic, unlit textured sphere; wheel zooms
  the image. Both start toward +Z with +Y up and share yaw/pitch. Middle-drag retains
  panning/window movement. **Reset Projection View** resets orientation and FOV;
  color resets do not. A shared icon button and read-only yaw/pitch (plus perspective
  FOV) show the committed frame in both normal and minimal views. Information bars
  and non-button areas of the minimal footer also accept wheel zoom. Ctrl+wheel
  retains color adjustment; + / − / 0 / 1 retain zoom controls.
  Refresh and minimal view preserve projection state.
  Dragging and FOV scrolling use a temporary raster capped at 512 pixels on the
  longest edge, on the same logical canvas and source resolution level. Requests
  are coalesced to at most 30 per second with one in-flight render per model.
  Release restores full quality; FOV scrolling restores it after 150 ms idle.
  Copy and preview/conversion saving wait for full quality; raw exports remain
  available. Color changes reuse the completed linear projection when possible.
  These CPU optimizations have not been benchmarked.
  Native readouts stay exact; converted readouts label interpolated linear values
  and source sample coordinates. Sphere's exterior is transparent with no readout.
  Source statistics and Auto use the entire source level, independent of rotation.
  Cube tail levels without six square faces allow native viewing/raw export only,
  with an explanation in the information bar. Existing perspective/sphere images
  (including WavyLinesSphere) are ordinary images, not reconstructable sources.
- **Save > Projection Conversion** chooses output projection separately from file
  format and displayed projection. Default scale N is the cube face width or
  round(LatLong source width / 4), at least 1. LatLong output is 4N×2N; Cube N×6N;
  perspective/sphere 2N×2N. Width/height and camera settings are editable subject to
  each projection's aspect constraints. Editing these options does not alter the view.
  PNG/JPEG apply the captured displayed color parameters, use existing transparency/
  JPEG background rules, and draw enabled anomaly markers at final output size.
  Conversion EXR defaults to FLOAT + ZIP, writes unexposed linear data as one scanline
  part, and does not preserve the resolution pyramid. Color output uses canonical
  RGB/RGBA and linear Rec.709 chromaticities; scalar output preserves the selected
  name and values. Sphere adds coverage alpha when necessary (for a scalar named A,
  the additional mask is `coverage.A`). LatLong/Cube conversions write the target
  `envmap`; perspective/sphere do not. Windows start at (0,0), pixel aspect is 1,
  and EXR conversion never includes display transforms or anomaly marks.
  **Preview Image** and copy use the current display projection; **Active/Layered
  Original** still retain source layout/values, with source `envmap` under Basic
  metadata (removable under None). HDR/bracket exports keep source-linear semantics.
  Saving captures the completed view and its color parameters, not pending rotation
  requests. See the [environment image checklist](docs/environment-images.md).
- Deep Scanline RGBA + Z point samples have a **Depth Range** bar with two handles:
  keep `Z_min ≤ Z ≤ Z_max`, initially all finite depths. Double-click the bar to
  restore the full range; constant-depth inputs disable dragging. RGB, individual
  RGBA/Z, Anaglyph and the minimal-window footer share the control. Each preview
  preserves its range across refresh and window changes; color reset leaves it alone.
  Samples are sorted near-to-far, retaining file order at equal depth, then composited
  with premultiplied front-to-back Over. Empty selections remain transparent; selected
  pixels use the existing opaque black-background display, including zero-alpha emission.
  Hover reads **Composite** linear values before display mapping, or the nearest
  selected Z; empty pixels read **No samples**. There is no per-pixel sample inspector.
  Source statistics include all stored samples and channels of the part (both parts
  for Anaglyph), before filtering; Auto ranges use only current composed values.
  Nonfinite Z never contributes to color or depth bounds, but remains in raw data,
  source statistics and Z-page anomaly markers. Other markers follow selected samples.
  Immutable native samples are shared by previews of the same part, with a checked
  1 GiB cache cap per part. Dragging recomposites in the background without re-reading
  the file; image, values and markers are committed together, only for the latest request.
  Copy and PNG/JPEG export use the current interval; HDR/brackets use its linear colors.
  Raw EXR always retains **all** original Deep samples and file order, using native
  channel types and lossless ZIPS. Active-layer and color-only exports include A/Z
  dependencies; color-only export of an independent Z still fails. Deep multipart
  requires Preserve multipart. Basic/None retain their metadata policy; required Deep
  structure is always written. Anaglyph export restrictions remain unchanged.
  This path supports base R/G/B/A/Z previews at `(0,0)`; Deep Tiled, ZBack volumes and
  cross-file compositing remain unsupported. See the
  [Deep Scanline manual checklist](docs/deep-scanline-images.md); all 12 samples await
  runtime acceptance.
- RGB false-color and scalar ranges accept scientific notation, including tiny
  values and the full finite FLOAT range. Auto range is unavailable without finite
  samples. A constant range maps finite values to the bottom of the color scale.
- Each preview has a small, checkable locator button (**Mark NaN/Inf** on hover).
  Isolated anomalies have 12-pixel screen circles; connected areas have outlines.
  Overlapping markers merge when zoomed out. The legend is NaN magenta, +Inf cyan,
  and -Inf yellow, in that priority order. Outlines can enclose normal pixels;
  use the pixel readout for exact values.
- Markers have opaque strokes, independent of exposure, tone mapping, and alpha.
  The button supports keyboard focus and Space; its selected state survives
  refresh and minimal view, and right-click reset preserves it.
- Preview copies and preview exports draw enabled markers after resizing so they
  remain visible. Raw exports do not include markers. Source statistics count
  channel samples, excluding synthesized components.
  See the [Unusual Pixel Images manual checklist](docs/unusual-pixel-images.md)
  for per-image checks and verification limits.

Disclaimer
==========

This is a very early version... expect crashes, don't expect polished
piece of software ;-)


Installing
==========

Windows
-------

You can install OpenEXR Viewer from winget. Run in a terminal:

```
winget install openexr-viewer
```

Alternatively, there is an installer you can download from the release
section: https://github.com/afichet/openexr-viewer/releases


macOS
-----

There is prebuilt releases for macOS. You just need to download the
installer in the releases section and run it.

Get the releases here: https://github.com/afichet/openexr-viewer/releases


Linux
-----

For Arch Linux, you can use the AUR repository. To install, do:

```
yay -S openexr-viewer
```

For other distribution, there is a snap package available:

[![Get it from the Snap Store](https://snapcraft.io/static/images/badges/en/snap-store-black.svg)](https://snapcraft.io/openexr-viewer)


```
sudo snap install openexr-viewer
```


Building
========

To build this package, you need Qt5 or greater and OpenEXR 3.0.1 or greater.

Linux
-----

If your package manager does come with an older version of OpenEXR,
you will have to build it your own:

```bash
cd /tmp

# Imath dependency
git clone https://github.com/AcademySoftwareFoundation/Imath.git
cd Imath
mkdir build
cd build
cmake ..
sudo make install

cd /tmp

# OpenEXR
git clone https://github.com/AcademySoftwareFoundation/openexr.git
cd openexr
mkdir build
cd build
cmake ..
sudo make install
```

Then, you're ready to build the software:

```bash
git clone https://github.com/afichet/openexr-viewer.git
cd openexr-viewer
mkdir build
cd build
cmake ..
make
```

Windows
-------

To build the Windows x64 version, run these PowerShell scripts from the project root:

```powershell
.\build_dependencies.ps1
.\build_windows.ps1 -Configuration Release -Portable
```

The portable package is generated at:

```
build\openexr-viewer-0.6.1-windows-x64.zip
```

Extract the ZIP and run `openexr-viewer.exe` directly. The extracted directory
contains the Qt, OpenEXR, Imath, zlib and Windows runtime files required by the
application; no installer, PATH change or administrator permission is needed.

To build the existing NSIS installer instead, run:

```powershell
.\build_windows.ps1 -Configuration Release -Package
```

The Qt installation path is configured in `build_windows.ps1` and may need to
be adjusted for a different Qt installation.

macOS
-----

You can install Qt from the official website and get a recent OpenEXR
release from homebrew.

```bash
brew install openexr
```

Then, you need to specify the of Qt's install when running CMake. For example:

```bash
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH=~/Qt/6.1.0/clang64
```

Then, you can build the package:

```bash
make
```


License
=======

It is licensed under the 3-clause BSD license.

This software uses the colormaps:

- from https://bids.github.io/colormap/ by Nathaniel J. Smith, Stefan
van der Walt, and (in the case of viridis) Eric Firing. These are
licensed under CC0 license
(http://creativecommons.org/publicdomain/zero/1.0/).
- Turbo from https://gist.github.com/mikhailov-work/ee72ba4191942acecc03fe6da94fc73f by Anton Mikhailov licensed under Apache-2.0.
- Most icons made by DinosoftLabs from www.flaticon.com 
