#pragma once

#include <windows.h>

namespace ShellIds {
// Stable identities, shared by the DLL and the registration manager.
static const CLSID Thumbnail = {0xb864715e, 0x38ba, 0x45bc, {0x94, 0xb2, 0x85, 0x93, 0x6b, 0x78, 0xb1, 0xd5}};
static const CLSID Preview = {0xc59dab34, 0x42c5, 0x490c, {0x98, 0x26, 0x68, 0xc5, 0x76, 0x13, 0x9e, 0x5b}};
static const wchar_t ThumbnailClsid[] = L"{B864715E-38BA-45BC-94B2-85936B78B1D5}";
static const wchar_t PreviewClsid[] = L"{C59DAB34-42C5-490C-9826-68C576139E5B}";
static const wchar_t ThumbnailSlot[] = L"{E357FCCD-A995-4576-B01F-234630154E96}";
static const wchar_t PreviewSlot[] = L"{8895B1C6-B41F-4C1C-A562-0D564250836F}";
static const wchar_t ProgId[] = L"OpenEXRViewerMini.EXR";
static const wchar_t DllName[] = L"openexr-shell.dll";
}
