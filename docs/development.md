# Development

[Overview](../README.md) · [User guide](user-guide.md) · [Release readiness](release-readiness.md) · [Windows integration](windows-shell.md)

## Supported development baseline

The release target is Windows x64. Qt 6.11 lists MSVC 2022 in its
[supported configurations](https://doc.qt.io/qt-6.11/supported-platforms.html).
The build script can select newer installed Visual Studio versions; those combinations
still need validation with the chosen Qt kit. The October 2026 preparation pass
used static analysis and review only. Compilation, manual acceptance, installation
and hardware checks remain pending. The source retains
Qt 5 branches and Linux/macOS paths; they are not validated configurations.

Dependencies:

| Component | Requirement / role |
|---|---|
| CMake | Project minimum 3.18; the Visual Studio generator and dependencies may require a newer version |
| C++ compiler | Qt-compatible compiler; the project requests C++11 and imported Qt targets can require a newer standard |
| Qt | Widgets and Concurrent |
| OpenEXR / Imath | New checkouts use v3.0.1 |
| zlib | New checkouts use commit `f9dd6009be3ed32415edf1e89d1bc38380ecb95d` |
| OpenMP | Optional acceleration; ordinary paths remain available without it |
| Windows Shell | Windows x64 with MSVC, separate static dependencies and `/MT` runtime |
| NSIS | Required only to create the Windows installer |

The dependency versions above describe the build script, not a security or
support certification. Audit versions and included license/runtime files before
shipping. The script builds Release dependencies; Debug builds need matching
Debug dependencies. RelWithDebInfo can use Release dependencies.

## Windows build

Install Git, CMake, Visual Studio with C++ x64 tools, and a compatible Qt kit.
The script checks prerequisites and stops on an external command failure; it does
not install development tools. From the repository, run:

```powershell
.\build_windows.ps1
```

This is the only PowerShell entry point. It acquires missing dependency sources,
builds shared zlib/Imath/OpenEXR and separate static Shell dependencies, then builds
the viewer and Shell components and deploys runtime files beside the executable.
It prints the executable path and, when requested, the package directory.
Every run configures and incrementally builds the selected dependencies; an
existing DLL alone never causes a dependency build to be skipped.

| Parameter | Behavior |
|---|---|
| `-Configuration` | Defaults to `RelWithDebInfo`; use `Release` for release artifacts |
| `-QtPath` | Qt prefix, not its `bin` directory; defaults to `QT_ROOT_DIR`, then `C:/ProgramData/Qt/6.11.1/msvc2022_64` |
| `-Generator` | Optional CMake generator, subject to installed tools and existing cache compatibility |
| `-WithoutShell` | Omit Shell components, static dependency builds and Shell cache checks |
| `-Portable` | Build and create a portable ZIP |
| `-Package` | Build and create a per-user NSIS installer; requires `makensis` on PATH |

For a different Qt installation or a viewer-only build:

```powershell
.\build_windows.ps1 -QtPath 'C:/Qt/6.11.1/msvc2022_64' -WithoutShell
```

### Source and output directories

| Location | Contents / cleanup policy |
|---|---|
| `dependencies/{zlib,Imath,openexr}` | Local Git checkouts; ignored by this repository; preserve when cleaning builds |
| `build/depends/shared-build` | Shared dependency build trees |
| `build/depends/shell-build` | Static `/MT` dependency build trees |
| `build/depends/lib` | Shared dependency installation prefix |
| `build/depends/shell` | Static Shell dependency installation prefix and notices |
| `build` | Viewer/Shell build outputs, deployed files, installation staging and packages |
| `local/test-images` | Optional local reference images and their original descriptions |
| `local/artwork` | Original icon and banner design material retained locally |

Existing source checkouts are reused without pulling updates, checking out another
revision or resetting local changes. Only new checkouts use the pinned revisions
above. A failed checkout stops the build; repair an incomplete source directory
before retrying. Review local revisions and changes before producing a release.

The cleanup migrated the former dependency sources out of `build/depends/src`,
verified their files and Git revisions, then removed the old build tree. Dependency
builds now stay outside their source checkouts. Future deletion of `build` preserves
both `dependencies` and `local`; the next script run recreates build outputs.
Normal script runs never delete a build directory. To change an existing toolchain,
clean `build` first after keeping any output artifacts you still need.

Reference images and design originals are local material, not tracked source or
package contents. They are absent from a fresh clone; obtain reference images
separately when following the manual checklist. Existing Git history is unchanged.

### Visual Studio selection and existing builds

- Existing viewer, shared dependency and enabled Shell caches determine the
  generator, toolset and VS installation. Conflicting caches or an incompatible
  explicit `-Generator` produce an error identifying the affected directory.
- Each cache retains its platform field, including an empty value. After
  configuration, CMake's compiler metadata must identify MSVC and 64-bit pointers.
- Without caches, `vswhere` selects the newest installed stable Visual Studio
  with C++ x64 tools supported by the current CMake. Build Tools installations
  qualify. New Visual Studio builds explicitly select x64.
- Existing MSVC caches using other generators require the appropriate compiler
  environment. Automatic selection for new builds uses Visual Studio generators.

To select a particular toolchain for a fresh build:

```powershell
.\build_windows.ps1 -Generator 'Visual Studio 18 2026' -Configuration Release
```

[Visual Studio 2022](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2017%202022.html)
requires CMake 3.21 or newer;
[Visual Studio 2026](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html)
requires CMake 4.2 or newer. Qt's `msvc2022_64` kit directory does not force the
VS 2022 generator. Newer compiler/Qt combinations still need validation.

The viewer's shared libraries remain separate from the Shell extension's static
`/MT` dependencies. The native Shell extension must not load Qt or dynamic OpenEXR,
Imath or zlib. Its nested build retains its own cached platform and takes the
parent's toolchain settings for a new build directory.

### Packaging

```powershell
.\build_windows.ps1 -Configuration Release -Portable
.\build_windows.ps1 -Configuration Release -Package
```

Choose one packaging switch. CPack writes packages under `build`; `-WithoutShell`
also works with packaging. The installer is per user, and Shell features start
disabled. Read the [integration lifecycle](windows-shell.md) before moving,
replacing or uninstalling a registered copy.

CMake deploys Qt after building and into the installation directory. Deployment
failures stop the operation. Check packages on a clean machine for Qt plugins,
OpenMP/runtime libraries, third-party DLLs and notices. Version **0.6.1** is unchanged;
signing, tagging and publishing remain separate pending steps.

## Linux and macOS

A typical source build uses installed Qt, Imath and OpenEXR packages:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/path/to/qt;/path/to/dependencies" \
  -DBUILD_WINDOWS_SHELL=OFF
cmake --build build --parallel
cmake --install build --prefix /chosen/install/prefix
```

These are instructions, not results from this preparation pass. Platform-specific
package installation and deploy tools must match the chosen Qt/compiler version.
The Windows build script is not a Linux/macOS installer.

On Linux, review the desktop entry, MIME registration, AppStream metadata and
installation prefix. The obsolete Snap recipe and duplicate Snap icon have been
removed. The [platform assessment](release-readiness.md#platform-assessment) records
its remaining issues.

On macOS, the obsolete forced 10.9 deployment target has been removed. Choose the
minimum supported by the actual Qt kit and dependencies; Qt 6.11 lists macOS 13
and later in its [supported platforms](https://doc.qt.io/qt-6.11/supported-platforms.html).
Use matching architectures for the app and every dependency. The existing bundle,
RPATH and `macdeployqt` path need build/deployment checks; universal binaries,
signing and notarization are not established by this source review.

## CMake switches

| Switch | Purpose |
|---|---|
| `BUILD_WINDOWS_SHELL` | Include the Windows integration targets; Windows default is on |
| `BUILD_SHARED_LIBS` | Select library linkage in the dependency builds |
| `CMAKE_PREFIX_PATH` | Locate the chosen Qt/OpenEXR/Imath dependencies |
| `CMAKE_BUILD_TYPE` | Configuration for single-configuration generators |

For Visual Studio, choose x64 and pass `--config Release` (or RelWithDebInfo) to
build/install commands. The `configure_viewer_target` function sets the application dependencies
and compiler options. Windows
resources and D3D libraries are conditional on Windows. Shell-specific internal
cache options are documented in the CMake module and are not viewer runtime
settings.

## Code map and invariants

| Area | Responsibility |
|---|---|
| `src/io` | Export path planning, source/preview conversion and atomic output |
| `src/model` | EXR/RGBE input, source ownership and shared decoding state |
| `src/model/attribute` | Header tree, typed attribute formatting, parts and layers |
| `src/model/framebuffer` | Channel buffers, source coordinates, statistics and display conversion |
| `src/util` | Colormaps, color math, geometry and shared helpers |
| `src/view` | Asynchronous document/view transactions, controls, rendering and export dialogs |
| `src/shell/windows` | Native integration manager, journaled registry updates, COM handlers and bounded preview decoding |

Preserve these boundaries when editing:

- Workers keep their source alive. Source reads bind buffers and read under the
  source synchronization policy; do not share mutable frame-buffer bindings.
- Cancellation and generation checks prevent stale load/render results from
  replacing newer requests. Closing a page must not wait for a worker to finish.
- Refresh and resolution changes prepare replacement state before committing
  it. Failures keep the prior usable source/view. Do not replace these transactions
  with piecemeal widget updates.
- Exports capture source and display parameters for the requested output. Preview
  saving waits for the required render quality; raw saving retains source values.
- File output uses `QSaveFile` with direct-write fallback disabled. Destroy
  OpenEXR writers before committing their stream. A bracket series commits each
  file separately and reports completed files on a later failure.
- Path conflict planning is not a cross-process file reservation. Concurrent
  writers to the same destination need coordination outside this viewer.
- Shell registration keeps its recovery journal until rollback completes. Preserve
  unrelated registry changes and the existing command/exit-code contract.

Keep menu names, shortcuts, settings keys, formats and retained build switches
compatible. In particular, retain the `afichet` / `OpenEXR Viewer` settings
namespace so existing users keep their preferences. This pass does not rename
it, change the project version or publish release assets.

## Verification policy

This pass uses **static analysis and review only**: source/diff inspection,
PowerShell syntax parsing, CMake/include/resource checks, document links, and
file hashes/Git revisions before and after migration. No build or application
was run.

Automated regression and benchmark sources, Qt Test/CTest integration and the
viewer testing switch have been removed at the maintainer's request. Verification
now relies on static review and the pending manual acceptance checklist; removed
test cases are not evidence of current coverage.

Pending build checks cover first-time dependency acquisition, incremental rebuilds,
rebuilding after deleting `build`, builds with and without Shell, cached empty and
explicit `x64` platforms, VS selection and conflicts, interrupted downloads,
compiler/deployment failures, ZIP/NSIS creation and clean-machine startup.

Manual data checks still include Unicode exports, exhausted rename candidates,
failed-write preservation, partial bracket reporting, wide metadata dimensions,
colormap interpolation and asynchronous cancellation/state restoration. The full
list is in [Release readiness](release-readiness.md#pending-acceptance-checklist).
