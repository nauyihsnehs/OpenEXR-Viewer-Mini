# OpenEXR Viewer Mini

A desktop viewer for inspecting OpenEXR and Radiance RGBE images, source channels,
metadata and HDR values. Built with Qt Widgets and OpenEXR; derived from
[Alban Fichet's OpenEXR Viewer](https://github.com/afichet/openexr-viewer).

## What it does

- Open `.exr` and `.hdr` files from the menu, command line or drag and drop.
- Browse parts, layers, channels and metadata; inspect source values and NaN/Inf.
- Adjust exposure, tone mapping and Scalar Mapping ranges for RGB luminance or
  individual channels.
- View stored Mipmap/Ripmap levels, stereo pairs, Deep Scanline depth ranges and
  environment projections.
- Switch between the complete workspace and a compact image window.
- Copy previews or export PNG/JPEG, source EXR/HDR, exposure brackets and projection conversions.
- On Windows, present floating-point HDR and optionally integrate with Explorer.

## Status and platforms

**Windows x64 is the primary release target.** This repository's release preparation
has been reviewed statically. Builds, manual acceptance, installer checks and
HDR hardware acceptance remain pending. See [Release Readiness](docs/release-readiness.md)
for the feature audit, known risks and exact verification boundaries.

| Platform | Current scope |
| --- | --- |
| Windows x64 | Viewer, portable ZIP, per-user NSIS installer; optional Windows 11 Explorer integration; HDR requires a compatible display and Direct3D 11 presentation |
| Linux | Qt viewer and desktop/MIME installation rules; build and packaging unverified |
| macOS | Qt viewer and application bundle configuration; build and deployment unverified |

HDR presentation and Explorer integration are Windows-specific. Qt5 source
compatibility remains in CMake; the Windows baseline is Qt 6.11/MSVC x64.
Upstream package-store listings do not establish availability of this fork.

## Windows quick start

When Windows assets are available, use this repository's
[Releases](https://github.com/nauyihsnehs/OpenEXR-Viewer-Mini/releases).
Extract the portable ZIP and run `openexr-viewer.exe`, keeping its DLLs and plugin
folders together. The NSIS package installs for the current user under
`LocalAppData/Programs`. Both formats start with Explorer integration off.

To build from source with Git, Visual Studio C++ x64 tools, CMake and Qt installed:

```powershell
.\build_windows.ps1 -Configuration Release -QtPath 'C:/Qt/6.11.1/msvc2022_64' -Portable
```

This single script acquires missing dependency sources in `dependencies/`, builds
shared and optional Shell dependencies, compiles the viewer and deploys its runtime
files. Subsequent runs use incremental builds. With Qt at the default path (or
`QT_ROOT_DIR` set), the everyday command is simply `.\build_windows.ps1`.
Dependency sources survive deletion of `build/`.

The current version produces `build/openexr-viewer-0.6.1-windows-x64.zip`.
Use `-Package` instead of `-Portable` for NSIS, or `-WithoutShell` for a viewer-only
build. Existing build caches keep their toolchain. New builds select the newest
installed Visual Studio supported by CMake; `-Generator` can select a specific
version. Automated test and benchmark sources have been removed; the release
checklist describes pending manual validation. The [development guide](docs/development.md)
covers prerequisites and paths.

Choose **File > Open**, then use **View > Show > Inspector**: single-click a layer
to inspect its details, and double-click to open its preview. Hover over the
image for source coordinates and values. Use the wheel to zoom, **Ctrl+wheel** to
adjust color parameters, and double-click the image to enter or leave minimal view.

**Before moving or deleting a portable copy with Explorer integration enabled,
use Undo all in File > Windows integration...** Keep its hidden rollback journal
until undo succeeds. See [Windows Integration](docs/windows-shell.md).

## Important limits

- Deep preview supports base RGBA/Z point samples in Deep Scanline at level `(0,0)`.
  Deep Tiled, ZBack volumes, cross-file compositing and per-pixel sample inspection
  are unsupported.
- RGBE import supports flat, modern RLE and legacy RLE. XYZE is unsupported.
- Raw flat EXR exports contain the selected resolution level as scanlines, not
  the original tile layout or complete resolution pyramid.
- Display color conversion uses chromaticities to linear Rec.709, without
  white-point adaptation or a full ACES/OCIO display pipeline.
- Files are viewed individually; sequence playback and standard-input opening
  are unsupported.

## Documentation

- [User Guide](docs/user-guide.md): controls, formats, display conventions and exports.
- [Development](docs/development.md): dependencies, building, packaging and architecture.
- [Release Readiness](docs/release-readiness.md): full feature assessment, platform matrix and pending acceptance.
- [Windows Integration](docs/windows-shell.md): registration, undo, previews and recovery.

## License and credits

The viewer is distributed under the [BSD 3-Clause license](LICENSE). Preserve the
original notices when redistributing source or binaries.

- Magma, Inferno, Plasma and Viridis come from the
  [BIDS colormaps](https://bids.github.io/colormap/) by Nathaniel J. Smith,
  Stefan van der Walt and, for Viridis, Eric Firing, under CC0.
- [Turbo](https://gist.github.com/mikhailov-work/ee72ba4191942acecc03fe6da94fc73f)
  by Anton Mikhailov is licensed under Apache-2.0.
- Original icons include work by DinosoftLabs from [Flaticon](https://www.flaticon.com/).
- Qt, OpenEXR, Imath, zlib and the bundled CMake deployment helpers retain their
  own licenses and notices. Shell dependency notices are packaged in `shell-licenses`.
