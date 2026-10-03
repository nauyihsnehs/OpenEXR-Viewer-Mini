#include "ShellIds.h"
#include "ComSupport.h"
#include "ExrPreview.h"
#include <thumbcache.h>
#include <shobjidl.h>
#include <propsys.h>
#include <atomic>
#include <algorithm>
#include <memory>
#include <new>

namespace {
HINSTANCE module = nullptr;
std::atomic<long> objects {0};

class Lifetime {
public:
    Lifetime() { ++objects; }
    virtual ~Lifetime() { --objects; }
    ULONG retain() { return ++references; }
    ULONG release() { const ULONG n = --references; if (!n) delete this; return n; }
private:
    std::atomic<ULONG> references {1};
};

class Thumbnail final : public Lifetime, public IThumbnailProvider, public IInitializeWithStream {
public:
    IFACEMETHODIMP QueryInterface(REFIID iid, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IThumbnailProvider)) *out = static_cast<IThumbnailProvider*>(this);
        else if (iid == __uuidof(IInitializeWithStream)) *out = static_cast<IInitializeWithStream*>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return retain(); }
    IFACEMETHODIMP_(ULONG) Release() override { return release(); }
    IFACEMETHODIMP Initialize(IStream* input, DWORD) override
    {
        if (!input) return E_POINTER;
        if (stream) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        stream = ComPtr<IStream>(input); return S_OK;
    }
    IFACEMETHODIMP GetThumbnail(UINT edge, HBITMAP* out, WTS_ALPHATYPE* alpha) override
    {
        if (!out || !alpha) return E_POINTER;
        *out = nullptr; *alpha = WTSAT_UNKNOWN;
        if (!stream || !edge) return E_INVALIDARG;
        const auto result = ShellPreview::render(stream.get(), edge, true, {});
        if (result.image.pixels.empty()) return E_FAIL;
        *out = ShellPreview::bitmap(result.image);
        if (!*out) return E_OUTOFMEMORY;
        *alpha = WTSAT_ARGB;
        return S_OK;
    }
private:
    ComPtr<IStream> stream;
};

struct Work {
    ShellPreview::Cancellation cancel = std::make_shared<std::atomic_bool>(false);
    ShellPreview::Result result;
    std::atomic_bool ready {false};
};
struct Job {
    std::shared_ptr<Work> work;
    ComPtr<IGlobalInterfaceTable> interfaces;
    DWORD cookie = 0;
    HMODULE library = nullptr;
};

void CALLBACK decodeWork(PTP_CALLBACK_INSTANCE callback, void* context)
{
    std::unique_ptr<Job> job(static_cast<Job*>(context));
    FreeLibraryWhenCallbackReturns(callback, job->library);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        if (SUCCEEDED(initialized)) {
            ComPtr<IStream> input;
            const HRESULT hr = job->interfaces->GetInterfaceFromGlobal(job->cookie, IID_PPV_ARGS(input.put()));
            ShellPreview::Result result;
            if (SUCCEEDED(hr)) result = ShellPreview::render(input.get(), 2048, false, job->work->cancel);
            else result.error = L"Cannot access the preview stream.";
            job->work->result = std::move(result);
        } else job->work->result.error = L"Cannot initialize the preview worker.";
    } catch (...) { /* The UI supplies a fallback message for an empty result. */ }
    job->interfaces.reset();
    if (SUCCEEDED(initialized)) CoUninitialize();
    job->work->ready.store(true);
    job.reset();
    --objects;
    // The extra module reference is released by Windows after this callback returns.
}

class Preview final : public Lifetime, public IPreviewHandler, public IInitializeWithStream,
                      public IObjectWithSite, public IOleWindow, public IPreviewHandlerVisuals {
public:
    ~Preview() override { Unload(); }
    IFACEMETHODIMP QueryInterface(REFIID iid, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IPreviewHandler)) *out = static_cast<IPreviewHandler*>(this);
        else if (iid == __uuidof(IInitializeWithStream)) *out = static_cast<IInitializeWithStream*>(this);
        else if (iid == __uuidof(IObjectWithSite)) *out = static_cast<IObjectWithSite*>(this);
        else if (iid == __uuidof(IOleWindow)) *out = static_cast<IOleWindow*>(this);
        else if (iid == __uuidof(IPreviewHandlerVisuals)) *out = static_cast<IPreviewHandlerVisuals*>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return retain(); }
    IFACEMETHODIMP_(ULONG) Release() override { return release(); }
    IFACEMETHODIMP Initialize(IStream* input, DWORD) override
    {
        if (!input) return E_POINTER;
        if (stream) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        stream = ComPtr<IStream>(input); return S_OK;
    }
    IFACEMETHODIMP SetWindow(HWND host, const RECT* area) override
    {
        if (!host || !area) return E_INVALIDARG;
        parent = host; bounds = *area;
        if (window) { SetParent(window, host); resize(); }
        return S_OK;
    }
    IFACEMETHODIMP SetRect(const RECT* area) override
    { if (!area) return E_POINTER; bounds = *area; resize(); return S_OK; }
    IFACEMETHODIMP DoPreview() override
    {
        if (!stream || !parent) return E_UNEXPECTED;
        if (window) return S_OK;
        try {
            WNDCLASSW cls = {};
            cls.lpfnWndProc = procedure; cls.hInstance = module;
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            cls.lpszClassName = L"OpenEXRViewerMini.Preview";
            if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return HRESULT_FROM_WIN32(GetLastError());
            work = std::make_shared<Work>();
            window = CreateWindowExW(0, cls.lpszClassName, L"EXR preview", WS_CHILD | WS_VISIBLE,
                0, 0, 1, 1, parent, nullptr, module, this);
            if (!window) { work.reset(); return HRESULT_FROM_WIN32(GetLastError()); }
            resize();
            // Poll shared state on the apartment thread. Workers never keep a HWND
            // or a handler pointer, so delayed work cannot reach a recycled window.
            if (!SetTimer(window, 1, 50, nullptr)) { Unload(); return E_OUTOFMEMORY; }
            std::unique_ptr<Job> job(new Job);
            job->work = work;
            HRESULT hr = CoCreateInstance(CLSID_StdGlobalInterfaceTable, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(interfaces.put()));
            if (FAILED(hr)) { Unload(); return hr; }
            hr = interfaces->RegisterInterfaceInGlobal(stream.get(), IID_IStream, &cookie);
            if (FAILED(hr)) { Unload(); return hr; }
            job->interfaces = ComPtr<IGlobalInterfaceTable>(interfaces.get());
            job->cookie = cookie;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    reinterpret_cast<LPCWSTR>(&decodeWork), &job->library)) {
                hr = HRESULT_FROM_WIN32(GetLastError()); Unload(); return hr;
            }
            ++objects;
            if (!TrySubmitThreadpoolCallback(decodeWork, job.get(), nullptr)) {
                --objects;
                hr = HRESULT_FROM_WIN32(GetLastError());
                FreeLibrary(job->library);
                Unload(); return hr;
            }
            job.release();
            return S_OK;
        } catch (const std::bad_alloc&) { Unload(); return E_OUTOFMEMORY; }
        catch (...) { Unload(); return E_FAIL; }
    }
    IFACEMETHODIMP Unload() override
    {
        if (work) work->cancel->store(true);
        if (cookie && interfaces) interfaces->RevokeInterfaceFromGlobal(cookie);
        cookie = 0; interfaces.reset();
        if (window) { KillTimer(window, 1); DestroyWindow(window); window = nullptr; }
        work.reset(); stream.reset();
        if (image) { DeleteObject(image); image = nullptr; }
        if (font) { DeleteObject(font); font = nullptr; }
        text.clear(); imageWidth = imageHeight = 0;
        return S_OK;
    }
    IFACEMETHODIMP SetFocus() override { if (window) ::SetFocus(window); return window ? S_OK : S_FALSE; }
    IFACEMETHODIMP QueryFocus(HWND* focus) override
    { if (!focus) return E_POINTER; *focus = ::GetFocus(); return S_OK; }
    IFACEMETHODIMP TranslateAccelerator(MSG* message) override
    {
        if (!message) return E_POINTER;
        ComPtr<IPreviewHandlerFrame> frame;
        if (site && SUCCEEDED(site->QueryInterface(IID_PPV_ARGS(frame.put())))) return frame->TranslateAccelerator(message);
        return S_FALSE;
    }
    IFACEMETHODIMP SetSite(IUnknown* value) override { site = ComPtr<IUnknown>(value); return S_OK; }
    IFACEMETHODIMP GetSite(REFIID iid, void** out) override
    { if (!out) return E_POINTER; *out = nullptr; return site ? site->QueryInterface(iid, out) : E_FAIL; }
    IFACEMETHODIMP GetWindow(HWND* out) override
    { if (!out) return E_POINTER; *out = window; return window ? S_OK : E_FAIL; }
    IFACEMETHODIMP ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    IFACEMETHODIMP SetBackgroundColor(COLORREF color) override
    { background = color; if (window) InvalidateRect(window, nullptr, TRUE); return S_OK; }
    IFACEMETHODIMP SetTextColor(COLORREF color) override
    { foreground = color; if (window) InvalidateRect(window, nullptr, TRUE); return S_OK; }
    IFACEMETHODIMP SetFont(const LOGFONTW* value) override
    {
        if (!value) return E_POINTER;
        HFONT replacement = CreateFontIndirectW(value);
        if (!replacement) return E_OUTOFMEMORY;
        if (font) DeleteObject(font);
        font = replacement;
        if (window) InvalidateRect(window, nullptr, TRUE);
        return S_OK;
    }
private:
    void resize()
    {
        if (window) SetWindowPos(window, nullptr, bounds.left, bounds.top,
            std::max<LONG>(0, bounds.right - bounds.left), std::max<LONG>(0, bounds.bottom - bounds.top), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void complete()
    {
        if (!work) return;
        if (!work->ready.load()) return;
        KillTimer(window, 1);
        auto& result = work->result;
        image = ShellPreview::bitmap(result.image);
        if (image) {
            imageWidth = result.image.width; imageHeight = result.image.height;
            text = std::move(result.image.description);
        } else text = result.error.empty() ? L"Preview unavailable. Open this file in OpenEXR Viewer." : std::move(result.error);
        std::vector<uint8_t>().swap(result.image.pixels);
        InvalidateRect(window, nullptr, TRUE);
    }
    void paint()
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        RECT area; GetClientRect(window, &area);
        const HBRUSH brush = CreateSolidBrush(background);
        FillRect(dc, &area, brush); DeleteObject(brush);
        const int pad = MulDiv(10, GetDpiForWindow(window), 96);
        const int captionHeight = MulDiv(60, GetDpiForWindow(window), 96);
        RECT caption = {pad, std::max(pad, int(area.bottom) - captionHeight), std::max(pad, int(area.right) - pad), area.bottom - pad};
        if (image && area.right > 2 * pad && area.bottom > captionHeight + 2 * pad) {
            const double scale = std::min(double(area.right - 2 * pad) / imageWidth,
                double(area.bottom - captionHeight - 2 * pad) / imageHeight);
            const int w = std::max(1, int(imageWidth * scale)), h = std::max(1, int(imageHeight * scale));
            HDC source = CreateCompatibleDC(dc);
            if (source) {
                HGDIOBJ old = SelectObject(source, image);
                BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
                AlphaBlend(dc, (area.right - w) / 2, (area.bottom - captionHeight - h) / 2, w, h,
                           source, 0, 0, int(imageWidth), int(imageHeight), blend);
                SelectObject(source, old); DeleteDC(source);
            }
        } else caption = {pad, pad, area.right - pad, area.bottom - pad};
        const HGDIOBJ oldFont = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
        ::SetTextColor(dc, foreground); SetBkMode(dc, TRANSPARENT);
        const wchar_t* message = text.empty() ? L"Loading EXR preview..." : text.c_str();
        DrawTextW(dc, message, -1, &caption, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, oldFont);
        EndPaint(window, &ps);
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        Preview* self = reinterpret_cast<Preview*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Preview*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            self->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            try {
                if (message == WM_TIMER) { self->complete(); return 0; }
                if (message == WM_PAINT) { self->paint(); return 0; }
                if (message == WM_ERASEBKGND) return 1;
                if (message == WM_SIZE) { InvalidateRect(hwnd, nullptr, TRUE); return 0; }
                if (message == WM_NCDESTROY) {
                    if (self->work) self->work->cancel->store(true);
                    self->window = nullptr;
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                }
            } catch (...) { KillTimer(hwnd, 1); }
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    ComPtr<IStream> stream;
    ComPtr<IUnknown> site;
    ComPtr<IGlobalInterfaceTable> interfaces;
    DWORD cookie = 0;
    std::shared_ptr<Work> work;
    HWND parent = nullptr, window = nullptr;
    RECT bounds = {};
    HBITMAP image = nullptr;
    HFONT font = nullptr;
    unsigned imageWidth = 0, imageHeight = 0;
    std::wstring text;
    COLORREF background = GetSysColor(COLOR_WINDOW), foreground = GetSysColor(COLOR_WINDOWTEXT);
};

class Factory final : public Lifetime, public IClassFactory {
public:
    explicit Factory(bool isPreview) : preview(isPreview) {}
    IFACEMETHODIMP QueryInterface(REFIID iid, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return retain(); }
    IFACEMETHODIMP_(ULONG) Release() override { return release(); }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        try {
            IUnknown* instance = preview ? static_cast<IUnknown*>(static_cast<IPreviewHandler*>(new Preview))
                                         : static_cast<IUnknown*>(static_cast<IThumbnailProvider*>(new Thumbnail));
            const HRESULT hr = instance->QueryInterface(iid, out);
            instance->Release(); return hr;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (...) { return E_FAIL; }
    }
    IFACEMETHODIMP LockServer(BOOL lock) override { if (lock) ++objects; else --objects; return S_OK; }
private:
    bool preview;
};
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH) module = instance;
    // Static CRT needs thread notifications; do not disable them.
    return TRUE;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out)
{
    if (!out) return E_POINTER;
    *out = nullptr;
    if (clsid != ShellIds::Thumbnail && clsid != ShellIds::Preview) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) Factory(clsid == ShellIds::Preview);
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(iid, out);
    factory->Release(); return hr;
}
extern "C" HRESULT WINAPI DllCanUnloadNow()
{
    if (objects.load() != 0) return S_FALSE;
    UnregisterClassW(L"OpenEXRViewerMini.Preview", module);
    return S_OK;
}
