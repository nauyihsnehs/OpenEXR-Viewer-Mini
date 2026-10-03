# Windows integration

[Overview](../README.md) · [User guide](user-guide.md) · [Development](development.md) · [Release readiness](release-readiness.md)

The implementation targets Windows x64 with MSVC. This preparation pass reviewed
source only; Explorer, registration, rollback and package acceptance remain pending.

## Enable and undo

Run `openexr-shell-config.exe` beside `openexr-viewer.exe` and `openexr-shell.dll`,
or select **File > Windows integration...**. All options start off and apply only
to the current Windows user:

- **File icon and Open with registration** adds the viewer to the supported
  applications for `.exr`. Select it as the default app in Windows Settings to
  use its file icon. The manager never writes Windows' `UserChoice` entry.
- **Explorer thumbnails** provides image thumbnails in Explorer views.
- **Explorer preview pane** displays an image and basic information in Alt+P.

Apply enables checked features and undoes unchecked features that have rollback
records. **Undo all** removes all recorded integration. Apply's disable and enable
steps are separate transactions: if enabling fails after a successful disable,
the disabled feature stays off. Refresh shows the resulting state.

The directory must be writable before a feature can be enabled. Original registry
values, including types and absence, are saved before each write in hidden
`.openexr-shell-<Windows-user-SID>.state` files beside the executables. Keep these
files until undo succeeds. A failed operation retains the information needed to
retry undo. Successful undo removes the journal. Status checks create no files or
registry keys.

Undo compares each registry value to what this installation wrote. It restores
the original only when they still match, preserving subsequent changes by other
software. It removes only recorded, empty parent keys. A conflicting handler is
reported and is not replaced. If another viewer copy owns the registration, undo
from that copy before enabling this one.

**Before moving or deleting the portable directory, use Undo all.** A moved
directory that retains its journal can undo the old paths before enabling the new
ones. Deleting the directory first removes the uninstall mechanism. The program
does not install a second recovery copy, service or startup task elsewhere.

The per-user NSIS installer calls the same manager before replacing or removing
files. A failed rollback or a loaded DLL pauses the operation while keeping the
uninstall entry. Close the viewer, integration settings and Explorer preview
windows, then retry; signing out releases remaining Shell hosts. An upgrade turns
integration off; enable the desired features again after installation.

Windows owns its icon and thumbnail caches. Undo sends an association-change
notification without erasing these shared caches. Cached images may remain
temporarily. A default-app choice made by the user is also owned by Windows;
choose another default app in Settings when removing this viewer.

## Command line

Run from the executable directory. These commands change registration only when
explicitly invoked:

```powershell
.\openexr-shell-config.exe --status
.\openexr-shell-config.exe --enable thumbnails
.\openexr-shell-config.exe --enable preview
.\openexr-shell-config.exe --enable icons
.\openexr-shell-config.exe --disable all
```

`--enable` and `--disable` accept `icons`, `thumbnails`, `preview`, or `all`.
`--prepare-uninstall` undoes everything and then checks whether the viewer and DLL
can be replaced. `--quiet` suppresses messages for installer use. With PowerShell,
use `Start-Process -Wait -PassThru` when the exit code must be observed reliably
for a GUI executable. Exit codes: 0 success; 1 error; 2 integration undone but
files/settings still in use during `--prepare-uninstall`.

## Preview behavior and limits

- Original decoding accepts one non-Deep part with exactly `R`, `G`, `B` and an
  optional `A`, all sampled at 1x1. Scanline and `ONE_LEVEL` Tiled are supported.
- Thumbnails prefer the first valid embedded preview. The pane prefers supported
  original pixels and falls back to an embedded preview on decode failure or a
  dimension/memory rejection. Multipart, layers, auxiliary channels, YC, Deep and
  multiresolution files require an embedded preview.
- Cancellation or deadline expiry ends the request. Without a usable image, the
  thumbnail request fails so Explorer can display the file icon; the pane shows
  a short reason. No source image is modified.
- Original pixels use 0 EV SDR with the viewer's chromaticity conversion and sRGB
  encoding. Premultiplied color is displayed over black, matching the viewer.
  Missing display-window pixels stay transparent. Data outside the display window
  is cropped; pixel aspect ratio changes the horizontal extent.
- Embedded previews already contain display-ready bytes. Area averaging resizes
  them without applying another gamma transform.
- Original images are limited to 16,000,000 data-window pixels and a 256 MiB budget
  for this decoder's buffers. Output longest edge is capped at 2048 pixels;
  thumbnails also respect Explorer's requested size. Headers are limited to
  16 MiB, 256 parts and 1024 attributes per part; each decoded tile is limited to
  1,048,576 pixels.
- Time budgets are two seconds for thumbnails and five seconds for the pane.
  Checks occur during stream reads, decoding and resizing. An outstanding stream
  read or decompression call cannot be forcibly interrupted. OpenEXR's internal
  allocations and host memory are not a hard process-memory cap.
- Background preview streams cross COM apartments through the Global Interface
  Table. Each request has its own cancellation flag and result; workers hold no
  window/handler pointer. The worker callback pins its DLL until return.

The Shell DLL does not create Qt objects or start the viewer. It retains normal
thumbnail process isolation and low-integrity `prevhost.exe` hosting. OpenEXR,
Imath, zlib and the MSVC runtime are statically linked into the Shell targets;
dependency notices are included in `shell-licenses`.

## Troubleshooting

Refresh reports incomplete registration, another handler taking priority and
common Explorer/policy switches. Ensure Explorer permits thumbnails and preview
handlers, and use Alt+P to open the pane. The manager does not change these
preferences or bypass blocked extensions.

Windows security updates from October 14, 2025 can block previews of files marked
as downloaded from the internet or on Internet Zone shares, before the handler
is called. Integration does not remove origin marks or change security zones.

## Verification checklist

Runtime verification requires separate authorization. Use a disposable Windows
11 x64 account or VM for registration and failure-path checks. Do not run this
checklist as part of a static-only review.

1. Package: DLL, manager and licenses beside the viewer; no Qt or dynamic
   OpenEXR/Imath/zlib dependencies in the DLL. Fresh install and ZIP leave all
   features off; no elevation is requested.
2. Images: Scanline/Tiled RGB/RGBA, half/float/uint samples, premultiplied alpha,
   chromaticities, offset/cropped windows, non-square pixels and thin/portrait
   images. Compare originals to the viewer at 0 EV.
3. Fallback: embedded previews of different sizes; multipart, Deep, YC, layers and
   mipmaps with/without embedded previews; oversized originals with previews.
4. Failure: truncated streams, seek/read errors, NaN/Inf, malformed headers,
   oversized preview attributes and invalid windows. Expect controlled failures,
   released handles and no Explorer crash.
5. Lifetime: resize, rapid selection changes, close/reopen, unload during decode,
   and concurrent thumbnail requests. Check stale frames, handle/memory growth
   and whether files become renameable after unload.
6. Registration: snapshot relevant HKCU values; enable each feature, repeat enable,
   disable individually, undo all and repeat undo. Compare data, types and key
   existence. Confirm `UserChoice` and HKLM stay unchanged.
7. Recovery: fail a later registry write/journal commit, interrupt enable, make
   the directory read-only, alter a registered value with another app, or activate
   a third-party handler. Retry undo without losing third-party changes.
8. Deployment: Unicode/space paths, move with a retained journal, two program
   copies, upgrade/uninstall with loaded DLL or open settings, and missing manager
   with a surviving journal. Uninstall pauses before deletion when undo cannot
   complete; successful uninstall removes its shortcuts, entry and registrations.
9. OS restrictions: icon-only mode, disabled preview settings/policies, blocked
   CLSID and internet-marked files. Preserve settings and origin metadata.

References: [thumbnail providers](https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers),
[preview handlers](https://learn.microsoft.com/en-us/windows/win32/shell/preview-handlers),
[association priority](https://learn.microsoft.com/en-us/windows/win32/shell/app-registration),
[OpenEXR previews](https://openexr.com/en/latest/ReadingAndWritingImageFiles.html#preview-images),
[internet-file restrictions](https://support.microsoft.com/en-us/servicing/os/windows/docs/2025/10/file-explorer-automatically-disables-the-preview-feature-for-files-downloaded-from-the-internet).
