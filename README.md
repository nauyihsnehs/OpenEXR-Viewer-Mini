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
  previews and their PNG/JPEG exports and copies are opaque over black: premultiplied
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
  Mipmap, Ripmap, and Deep images are not supported. Raw EXR exports use scanline
  storage and retain the existing channel, window, and metadata options; they do
  not preserve the source tile layout. See the [Tiles manual checklist](docs/tiled-images.md).
- The export option **Color channels (RGB/YC)** includes both RGB and luminance/chroma
  channels. See the [Chromaticities checklist](docs/chromaticities-images.md) and
  [Luminance/Chroma checklist](docs/luminance-chroma-images.md) for pending visual checks.
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
