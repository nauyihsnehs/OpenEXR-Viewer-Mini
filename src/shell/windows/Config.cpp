#include "Registration.h"
#include "ConfigResource.h"
#include "ComSupport.h"
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::wstring widen(const char* text)
{
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (!size) return L"Windows integration failed.";
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, &result[0], size);
    result.pop_back(); return result;
}
void refresh(HWND window)
{
    const auto status = ShellRegistration::status();
    CheckDlgButton(window, IDC_ICONS, status.enabled & ShellRegistration::Icons ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_THUMBNAILS, status.enabled & ShellRegistration::Thumbnails ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_PREVIEW, status.enabled & ShellRegistration::Preview ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(window, IDC_STATUS, status.details.c_str());
}
INT_PTR CALLBACK dialog(HWND window, UINT message, WPARAM wparam, LPARAM)
{
    try {
        if (message == WM_INITDIALOG) {
            SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1))));
            refresh(window); return TRUE;
        }
        if (message == WM_CLOSE) { EndDialog(window, 0); return TRUE; }
        if (message != WM_COMMAND) return FALSE;
        switch (LOWORD(wparam)) {
        case IDCANCEL: EndDialog(window, 0); return TRUE;
        case IDC_REFRESH: refresh(window); return TRUE;
        case IDC_DEFAULTS:
            ShellExecuteW(window, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        case IDC_UNDO:
            ShellRegistration::disable(ShellRegistration::All);
            refresh(window);
            MessageBoxW(window, L"Integration has been undone for this user. Windows manages its own icon and thumbnail caches.", L"OpenEXR Viewer", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        case IDC_APPLY: {
            unsigned requested = 0;
            if (IsDlgButtonChecked(window, IDC_ICONS) == BST_CHECKED) requested |= ShellRegistration::Icons;
            if (IsDlgButtonChecked(window, IDC_THUMBNAILS) == BST_CHECKED) requested |= ShellRegistration::Thumbnails;
            if (IsDlgButtonChecked(window, IDC_PREVIEW) == BST_CHECKED) requested |= ShellRegistration::Preview;
            // Each operation is individually recoverable. Disable first so an
            // incomplete/changed registration can be removed without taking over.
            const auto current = ShellRegistration::status();
            const unsigned remove = current.recorded & ~requested;
            if (remove) ShellRegistration::disable(remove);
            if (requested) ShellRegistration::enable(requested);
            refresh(window); return TRUE;
        }
        default: return FALSE;
        }
    } catch (const std::exception& error) {
        const auto text = widen(error.what());
        SetDlgItemTextW(window, IDC_STATUS, text.c_str());
        MessageBoxW(window, text.c_str(), L"OpenEXR Windows integration", MB_OK | MB_ICONERROR);
    } catch (...) {
        MessageBoxW(window, L"Windows integration failed. Keep this folder and retry Undo all.", L"OpenEXR Viewer", MB_OK | MB_ICONERROR);
    }
    return TRUE;
}
unsigned feature(const std::wstring& name)
{
    if (name == L"icons") return ShellRegistration::Icons;
    if (name == L"thumbnails") return ShellRegistration::Thumbnails;
    if (name == L"preview") return ShellRegistration::Preview;
    if (name == L"all") return ShellRegistration::All;
    throw std::runtime_error("Expected icons, thumbnails, preview or all.");
}
void print(const std::wstring& text, bool quiet, bool error = false)
{
    if (quiet) return;
    AttachConsole(ATTACH_PARENT_PROCESS);
    HANDLE output = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD mode = 0, written = 0;
    const auto line = text + L"\r\n";
    if (GetConsoleMode(output, &mode)) WriteConsoleW(output, line.c_str(), DWORD(line.size()), &written, nullptr);
    else {
        const int size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), int(line.size()), nullptr, 0, nullptr, nullptr);
        std::string bytes(size, '\0');
        if (size) WideCharToMultiByte(CP_UTF8, 0, line.c_str(), int(line.size()), &bytes[0], size, nullptr, nullptr);
        if (!WriteFile(output, bytes.data(), DWORD(bytes.size()), &written, nullptr))
            MessageBoxW(nullptr, text.c_str(), L"OpenEXR Windows integration", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    bool quiet = false;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int exitCode = 0;
    try {
        if (FAILED(com)) throw std::runtime_error("Cannot initialize Windows shell configuration.");
        int count = 0;
        LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!raw) throw std::runtime_error("Cannot read command line.");
        std::vector<std::wstring> args;
        try {
            for (int i = 1; i < count; ++i) {
                if (std::wstring(raw[i]) == L"--quiet") quiet = true;
                else args.emplace_back(raw[i]);
            }
        } catch (...) { LocalFree(raw); throw; }
        LocalFree(raw);
        if (args.empty()) {
            if (quiet) throw std::runtime_error("--quiet requires a command.");
            Handle settings(CreateMutexW(nullptr, FALSE, ShellRegistration::settingsMutexName().c_str()));
            if (!settings) throw std::runtime_error("Cannot open the integration settings lock.");
            if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_CONFIG), nullptr, dialog, 0) == -1)
                throw std::runtime_error("Cannot create Windows integration window.");
        } else if (args.size() == 1 && args[0] == L"--status") print(ShellRegistration::status().details, quiet);
        else if (args.size() == 2 && args[0] == L"--enable") {
            ShellRegistration::enable(feature(args[1])); print(ShellRegistration::status().details, quiet);
        } else if (args.size() == 2 && args[0] == L"--disable") {
            ShellRegistration::disable(feature(args[1])); print(ShellRegistration::status().details, quiet);
        } else if (args.size() == 1 && args[0] == L"--prepare-uninstall") {
            ShellRegistration::disable(ShellRegistration::All);
            if (!ShellRegistration::filesAvailable()) {
                print(L"Integration is off, but files are still in use. Close the viewer, integration settings and Explorer preview windows, then retry. Signing out releases remaining Shell hosts.", quiet, true);
                exitCode = 2;
            }
        } else throw std::runtime_error("Usage: openexr-shell-config [--status | --enable icons|thumbnails|preview|all | --disable icons|thumbnails|preview|all | --prepare-uninstall] [--quiet]");
    } catch (const std::exception& error) { print(widen(error.what()), quiet, true); exitCode = 1; }
    catch (...) { exitCode = 1; }
    if (SUCCEEDED(com)) CoUninitialize();
    return exitCode;
}
