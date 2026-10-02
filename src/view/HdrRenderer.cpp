#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "HdrRenderer.h"
#include <QCoreApplication>

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <limits>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
QString translate(const char* text)
{ return QCoreApplication::translate("HdrRenderer", text); }

struct Display {
    HWND window = nullptr;
    HMONITOR monitor = nullptr;
    LUID adapter = {};
    bool found = false;
    bool hdr = false;
    bool whiteKnown = false;
    ULONG whiteLevel = 1000; // Windows scale: 1000 = 80 nits.
    bool operator==(const Display& other) const
    {
        return window == other.window && monitor == other.monitor
          && adapter.LowPart == other.adapter.LowPart && adapter.HighPart == other.adapter.HighPart
          && found == other.found && hdr == other.hdr && whiteKnown == other.whiteKnown
          && whiteLevel == other.whiteLevel;
    }
};

void queryWhiteLevel(const DXGI_OUTPUT_DESC1& output, Display& display)
{
    // A display's target ID need not match its GDI source ID (or adapter).
    for (int attempt = 0; attempt < 2; ++attempt) {
        UINT32 pathCount = 0, modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
            return;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const LONG result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                                              &modeCount, modes.data(), nullptr);
        if (result == ERROR_INSUFFICIENT_BUFFER) continue; // Topology changed during the query.
        if (result != ERROR_SUCCESS) return;
        for (UINT32 i = 0; i < pathCount; ++i) {
            const auto& path = paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
            source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId = path.sourceInfo.adapterId;
            source.header.id = path.sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS
                || std::wcscmp(source.viewGdiDeviceName, output.DeviceName) != 0) continue;
            DISPLAYCONFIG_SDR_WHITE_LEVEL white = {};
            white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
            white.header.size = sizeof(white);
            white.header.adapterId = path.targetInfo.adapterId;
            white.header.id = path.targetInfo.id;
            if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel > 0) {
                display.whiteLevel = white.SDRWhiteLevel;
                display.whiteKnown = true;
            }
            return;
        }
        return;
    }
}

// SV_Position is in physical pixels; all viewer geometry remains in Qt logical pixels.
const char* shaderSource = R"hlsl(
cbuffer Settings : register(b0) {
    float4 viewport; // physical width/height, DPR, SDR white / 80 nits
    float4 imageTransform; // scale X/Y, translation X/Y, in logical pixels
    float4 visibleRect;
    float4 sourceWindow[2]; // source offsets in the common image, width/height
    float4 options; // exposure * white, smooth, stereo, mask/overlay flags
};
Texture2D<float4> leftImage : register(t0);
Texture2D<float4> rightImage : register(t1);
Texture2D<float> leftCoverage : register(t2);
Texture2D<float> rightCoverage : register(t3);
Texture2D<float4> checker : register(t4);
Texture2D<float4> overlay : register(t5);

float4 vertexMain(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p.x * 2 - 1, 1 - p.y * 2, 0, 1);
}
float3 linearSrgb(float3 color) {
    float3 result;
    [unroll] for (int c = 0; c < 3; ++c)
        result[c] = color[c] <= 0.04045 ? color[c] / 12.92 : pow((color[c] + 0.055) / 1.055, 2.4);
    return result;
}
float4 fetchPixel(Texture2D<float4> image, Texture2D<float> coverage,
                  int2 p, float2 size, bool masked) {
    if (any(p < 0) || any(p >= size)) return 0;
    if (masked && coverage.Load(int3(p, 0)) == 0) return 0;
    float3 color = image.Load(int3(p, 0)).rgb;
    // Sanitize each tap before interpolation, so invalid samples do not poison neighbors.
    [unroll] for (int c = 0; c < 3; ++c)
        if (isnan(color[c]) || isinf(color[c])) color[c] = 0;
    return float4(color, 1); // Existing previews show premultiplied source RGB over black.
}
float4 fetchSource(int2 p, uint flags) {
    float4 left = fetchPixel(leftImage, leftCoverage, int2(p-sourceWindow[0].xy),
                             sourceWindow[0].zw, (flags & 1) != 0);
    if (options.z == 0) return left;
    float4 right = fetchPixel(rightImage, rightCoverage, int2(p-sourceWindow[1].xy),
                              sourceWindow[1].zw, (flags & 2) != 0);
    return float4(left.r, right.g, right.b, max(left.a, right.a));
}
float4 sampleSource(float2 p, uint flags) {
    if (options.y == 0) return fetchSource(int2(floor(p)), flags);
    float2 center = p - 0.5;
    int2 base = int2(floor(center));
    float2 weight = frac(center);
    // Form stereo's union coverage at each tap, before filtering it.
    return fetchSource(base, flags) * (1-weight.x) * (1-weight.y)
         + fetchSource(base+int2(1,0), flags) * weight.x * (1-weight.y)
         + fetchSource(base+int2(0,1), flags) * (1-weight.x) * weight.y
         + fetchSource(base+int2(1,1), flags) * weight.x * weight.y;
}
float4 pixelMain(float4 position : SV_Position) : SV_Target {
    float2 logical = position.xy / viewport.z;
    float3 background = linearSrgb(checker.Load(int3(int2(floor(logical)) % 32, 0)).rgb) * viewport.w;
    float3 color = background;
    uint flags = (uint)options.w;
    if (all(logical >= visibleRect.xy) && all(logical < visibleRect.zw)) {
        float2 p = (logical - imageTransform.zw) / imageTransform.xy;
        float4 source = sampleSource(p, flags);
        color = source.rgb * options.x + background * (1-source.a);
    }
    if ((flags & 4) != 0) {
        float4 mark = overlay.Load(int3(int2(position.xy), 0));
        if (mark.a > 0)
            color = color * (1-mark.a) + linearSrgb(saturate(mark.rgb / mark.a)) * viewport.w * mark.a;
    }
    return float4(clamp(color, -65504.0, 65504.0), 1);
}
)hlsl";

struct Constants {
    float viewport[4];
    float transform[4];
    float clip[4];
    float windows[2][4];
    float options[4];
};
static_assert(sizeof(Constants) == 96, "HDR shader constants must match HLSL packing");
}

struct HdrRenderer::Impl {
    Display display;
    QString failure;
    QSize failureViewportSize;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> rasterizer;
    struct Texture {
        std::shared_ptr<const FramebufferData> data;
        ComPtr<ID3D11ShaderResourceView> color, coverage;
    } textures[2];
    ComPtr<ID3D11ShaderResourceView> checker, markers;
    qint64 checkerKey = 0;
    QSize size;

    void releaseDevice()
    {
        if (context) { context->ClearState(); context->Flush(); }
        for (auto& texture : textures) texture = Texture();
        checker.Reset(); markers.Reset(); checkerKey = 0;
        target.Reset(); swapChain.Reset(); rasterizer.Reset();
        constants.Reset(); pixel.Reset(); vertex.Reset();
        context.Reset(); device.Reset(); size = QSize();
    }

    bool fail(const QString& reason, HRESULT result = S_OK)
    {
        failure = reason;
        if (FAILED(result)) failure += QString(" (0x%1)").arg(quint32(result), 8, 16, QLatin1Char('0'));
        RECT rect = {};
        GetClientRect(display.window, &rect);
        failureViewportSize = QSize(rect.right - rect.left, rect.bottom - rect.top);
        releaseDevice();
        return false;
    }

    bool makeTexture(int width, int height, DXGI_FORMAT format, const void* bytes, UINT pitch,
                     ComPtr<ID3D11ShaderResourceView>& view)
    {
        if (width <= 0 || height <= 0 || width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || !bytes)
            return fail(translate("Image exceeds the GPU texture limit."));
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = UINT(width); desc.Height = UINT(height);
        desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA input = {};
        input.pSysMem = bytes; input.SysMemPitch = pitch;
        ComPtr<ID3D11Texture2D> texture;
        HRESULT result = device->CreateTexture2D(&desc, &input, texture.GetAddressOf());
        if (FAILED(result)) return fail(translate("Could not upload the HDR image."), result);
        view.Reset();
        result = device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf());
        if (FAILED(result)) return fail(translate("Could not create the HDR image view."), result);
        return true;
    }

    bool initialize()
    {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        HRESULT result = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
          &level, 1, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf());
        if (FAILED(result)) return fail(translate("Could not initialize Direct3D 11."), result);
        ComPtr<IDXGIFactory2> factory;
        result = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(result)) return fail(translate("Could not create the HDR swap chain."), result);
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = 1; desc.Height = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH; desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        ComPtr<IDXGISwapChain1> chain;
        result = factory->CreateSwapChainForHwnd(device.Get(), display.window, &desc, nullptr, nullptr,
                                                chain.GetAddressOf());
        if (FAILED(result)) return fail(translate("Could not create the HDR swap chain."), result);
        factory->MakeWindowAssociation(display.window, DXGI_MWA_NO_ALT_ENTER);
        result = chain.As(&swapChain);
        if (FAILED(result)) return fail(translate("HDR color spaces are unavailable."), result);
        UINT supported = 0;
        result = swapChain->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &supported);
        if (FAILED(result) || !(supported & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
            return fail(translate("The swap chain cannot present scRGB HDR."), result);
        result = swapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
        if (FAILED(result)) return fail(translate("Could not select the scRGB HDR color space."), result);
        ComPtr<ID3DBlob> code, errors;
        result = D3DCompile(shaderSource, std::strlen(shaderSource), "HDR", nullptr, nullptr,
                            "vertexMain", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                            code.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(result)) return fail(translate("Could not compile the HDR vertex shader."), result);
        result = device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                            vertex.GetAddressOf());
        if (FAILED(result)) return fail(translate("Could not create the HDR vertex shader."), result);
        code.Reset(); errors.Reset();
        result = D3DCompile(shaderSource, std::strlen(shaderSource), "HDR", nullptr, nullptr,
                            "pixelMain", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                            code.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(result)) return fail(translate("Could not compile the HDR pixel shader."), result);
        result = device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                           pixel.GetAddressOf());
        if (FAILED(result)) return fail(translate("Could not create the HDR pixel shader."), result);
        D3D11_BUFFER_DESC buffer = {};
        buffer.ByteWidth = UINT(sizeof(Constants)); buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        result = device->CreateBuffer(&buffer, nullptr, constants.GetAddressOf());
        if (FAILED(result)) return fail(translate("Could not allocate HDR shader constants."), result);
        D3D11_RASTERIZER_DESC raster = {};
        raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        result = device->CreateRasterizerState(&raster, rasterizer.GetAddressOf());
        if (FAILED(result)) return fail(translate("Could not initialize HDR drawing."), result);
        return true;
    }

    bool resize(QSize next)
    {
        if (size == next && target) return true;
        if (next.width() <= 0 || next.height() <= 0
            || next.width() > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || next.height() > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            return fail(translate("Viewport exceeds the GPU texture limit."));
        context->OMSetRenderTargets(0, nullptr, nullptr);
        target.Reset();
        HRESULT result = swapChain->ResizeBuffers(2, UINT(next.width()), UINT(next.height()),
                                                  DXGI_FORMAT_R16G16B16A16_FLOAT, 0);
        if (FAILED(result)) return fail(translate("Could not resize the HDR swap chain."), result);
        result = swapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
        if (FAILED(result)) return fail(translate("Could not restore the scRGB HDR color space."), result);
        ComPtr<ID3D11Texture2D> backBuffer;
        result = swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
        if (FAILED(result)) return fail(translate("Could not access the HDR back buffer."), result);
        result = device->CreateRenderTargetView(backBuffer.Get(), nullptr, target.GetAddressOf());
        if (FAILED(result)) return fail(translate("Could not create the HDR render target."), result);
        size = next;
        return true;
    }

    bool upload(int eye, const std::shared_ptr<const FramebufferData>& data)
    {
        auto& texture = textures[eye];
        if (texture.data == data && texture.color) return true;
        if (!data || data->width <= 0 || data->height <= 0
            || data->width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || data->height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            return fail(translate("Image exceeds the GPU texture limit."));
        const size_t count = size_t(data->width) * data->height;
        if (data->pixels.size() != count * 4
            || (!data->deepCoverage.empty() && data->deepCoverage.size() != count)
            || (data->deep && data->deepCoverage.empty()))
            return fail(translate("Invalid HDR framebuffer layout."));
        texture = Texture();
        if (!makeTexture(data->width, data->height, DXGI_FORMAT_R32G32B32A32_FLOAT,
                         data->pixels.data(), UINT(size_t(data->width) * 4 * sizeof(float)), texture.color)) return false;
        const unsigned char covered = 255;
        const bool masked = !data->deepCoverage.empty();
        if (!makeTexture(masked ? data->width : 1, masked ? data->height : 1, DXGI_FORMAT_R8_UNORM,
                         masked ? data->deepCoverage.data() : &covered,
                         masked ? UINT(data->width) : 1, texture.coverage)) return false;
        texture.data = data;
        return true;
    }
};

void HdrViewport::setHdrPainting(bool enabled)
{
    if (m_hdrPainting == enabled) return;
    m_hdrPainting = enabled;
    setAttribute(Qt::WA_PaintOnScreen, enabled);
    setAttribute(Qt::WA_NoSystemBackground, enabled);
    setAttribute(Qt::WA_OpaquePaintEvent, enabled);
}

QPaintEngine* HdrViewport::paintEngine() const
{ return m_hdrPainting ? nullptr : QWidget::paintEngine(); }

HdrRenderer::HdrRenderer() : m_impl(new Impl) {}
HdrRenderer::~HdrRenderer() { m_impl->releaseDevice(); }

void HdrRenderer::release()
{
    m_impl->releaseDevice();
    m_impl->adapter.Reset();
    m_impl->display = Display();
    m_impl->failure.clear();
}

bool HdrRenderer::refresh(WId window, bool retry)
{
    Display next;
    next.window = reinterpret_cast<HWND>(window);
    next.monitor = MonitorFromWindow(next.window, MONITOR_DEFAULTTONEAREST);
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> selected;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) {
        for (UINT a = 0; ; ++a) {
            ComPtr<IDXGIAdapter1> adapter;
            if (FAILED(factory->EnumAdapters1(a, adapter.GetAddressOf()))) break;
            for (UINT o = 0; ; ++o) {
                ComPtr<IDXGIOutput> output;
                if (FAILED(adapter->EnumOutputs(o, output.GetAddressOf()))) break;
                DXGI_OUTPUT_DESC basic = {};
                if (FAILED(output->GetDesc(&basic)) || basic.Monitor != next.monitor) continue;
                DXGI_ADAPTER_DESC1 adapterDesc = {};
                if (FAILED(adapter->GetDesc1(&adapterDesc))) break;
                next.adapter = adapterDesc.AdapterLuid;
                next.found = true;
                selected = adapter;
                ComPtr<IDXGIOutput6> advanced;
                DXGI_OUTPUT_DESC1 desc = {};
                if (SUCCEEDED(output.As(&advanced)) && SUCCEEDED(advanced->GetDesc1(&desc))) {
                    next.hdr = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                    if (next.hdr) queryWhiteLevel(desc, next);
                }
                break;
            }
            if (next.found) break;
        }
    }
    RECT rect = {};
    GetClientRect(next.window, &rect);
    const QSize viewportSize(rect.right - rect.left, rect.bottom - rect.top);
    const bool resizedAfterFailure = !m_impl->failure.isEmpty() && viewportSize != m_impl->failureViewportSize;
    if (!(next == m_impl->display) || retry || resizedAfterFailure) {
        m_impl->releaseDevice();
        m_impl->failure.clear();
        m_impl->display = next;
        m_impl->adapter = selected;
        return true;
    }
    return false;
}

bool HdrRenderer::available() const
{ return m_impl->display.hdr && m_impl->adapter && m_impl->failure.isEmpty(); }

QString HdrRenderer::statusText() const
{
    return available() ? translate("HDR enabled")
                       : translate("HDR unavailable — SDR preview");
}

QString HdrRenderer::statusDetail() const
{
    if (!m_impl->failure.isEmpty()) return m_impl->failure;
    if (!m_impl->display.found) return translate("Could not find the current display's HDR capabilities.");
    if (!m_impl->display.hdr) return translate("HDR is disabled in Windows or this display does not support HDR.");
    const double nits = m_impl->display.whiteLevel * 80. / 1000.;
    return m_impl->display.whiteKnown
      ? translate("Linear RGB=1 at 0 EV follows the Windows SDR white level (%1 nits).").arg(nits, 0, 'f', 1)
      : translate("Could not read the Windows SDR white level; using 80 nits for RGB=1 at 0 EV.");
}

bool HdrRenderer::render(WId window, QSize size, qreal dpr, const HdrPreviewFrame& frame,
                         const QTransform& transform, const QRectF& visible, bool smooth,
                         const QPixmap& checkerboard, const QImage& overlay, bool markersEnabled)
{
    auto& state = *m_impl;
    if (!available() || reinterpret_cast<HWND>(window) != state.display.window || !frame.data) return false;
    if (markersEnabled && overlay.isNull())
        return state.fail(translate("Could not allocate the HDR anomaly marker overlay."));
    if (!std::isfinite(dpr) || dpr <= 0. || !std::isfinite(frame.exposure)
        || !std::isfinite(float(transform.m11())) || !std::isfinite(float(transform.m22()))
        || !std::isfinite(float(transform.dx())) || !std::isfinite(float(transform.dy()))
        || float(transform.m11()) <= 0.f || float(transform.m22()) <= 0.f)
        return state.fail(translate("Invalid HDR viewport geometry."));
    if (!state.device && !state.initialize()) return false;
    if (!state.resize(size)) return false;
    const auto& data = frame.data;
    const bool stereo = bool(data->stereo[0]);
    const auto left = stereo ? data->stereo[0] : data;
    const auto right = stereo ? data->stereo[1] : left;
    if (!state.upload(0, left) || (stereo && !state.upload(1, right))) return false;
    if (!state.checker || state.checkerKey != checkerboard.cacheKey()) {
        const QImage image = checkerboard.toImage().convertToFormat(QImage::Format_RGBA8888);
        if (!state.makeTexture(image.width(), image.height(), DXGI_FORMAT_R8G8B8A8_UNORM,
                               image.constBits(), UINT(image.bytesPerLine()), state.checker)) return false;
        state.checkerKey = checkerboard.cacheKey();
    }
    if (!overlay.isNull()) {
        const QImage image = overlay.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        if (!state.makeTexture(image.width(), image.height(), DXGI_FORMAT_R8G8B8A8_UNORM,
                               image.constBits(), UINT(image.bytesPerLine()), state.markers)) return false;
    } else if (!state.markers) {
        const unsigned char transparent[4] = {};
        if (!state.makeTexture(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, transparent, 4, state.markers)) return false;
    }
    const float whiteScale = float(state.display.whiteLevel / 1000.);
    const double gain = std::exp2(frame.exposure) * whiteScale;
    if (!std::isfinite(gain) || gain > std::numeric_limits<float>::max())
        return state.fail(translate("HDR exposure is outside the supported range."));
    Constants settings = {};
    settings.viewport[0] = float(size.width()); settings.viewport[1] = float(size.height());
    settings.viewport[2] = float(dpr); settings.viewport[3] = whiteScale;
    settings.transform[0] = float(transform.m11()); settings.transform[1] = float(transform.m22());
    settings.transform[2] = float(transform.dx()); settings.transform[3] = float(transform.dy());
    const QRectF clip = transform.mapRect(visible);
    settings.clip[0] = float(clip.left()); settings.clip[1] = float(clip.top());
    settings.clip[2] = float(clip.right()); settings.clip[3] = float(clip.bottom());
    const std::shared_ptr<const FramebufferData> sources[2] = {left, right};
    for (int eye = 0; eye < 2; ++eye) {
        const QPoint offset = sources[eye]->dataWindow.topLeft() - data->dataWindow.topLeft();
        settings.windows[eye][0] = float(offset.x()); settings.windows[eye][1] = float(offset.y());
        settings.windows[eye][2] = float(sources[eye]->width);
        settings.windows[eye][3] = float(sources[eye]->height);
    }
    settings.options[0] = float(gain); settings.options[1] = smooth ? 1.f : 0.f;
    settings.options[2] = stereo ? 1.f : 0.f;
    settings.options[3] = float((!left->deepCoverage.empty() ? 1 : 0)
      | (!right->deepCoverage.empty() ? 2 : 0) | (!overlay.isNull() ? 4 : 0));
    state.context->UpdateSubresource(state.constants.Get(), 0, nullptr, &settings, 0, 0);
    ID3D11Buffer* buffer = state.constants.Get();
    state.context->PSSetConstantBuffers(0, 1, &buffer);
    ID3D11ShaderResourceView* views[] = {state.textures[0].color.Get(),
      state.textures[stereo ? 1 : 0].color.Get(), state.textures[0].coverage.Get(),
      state.textures[stereo ? 1 : 0].coverage.Get(), state.checker.Get(), state.markers.Get()};
    state.context->PSSetShaderResources(0, 6, views);
    ID3D11RenderTargetView* target = state.target.Get();
    state.context->OMSetRenderTargets(1, &target, nullptr);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = float(size.width()); viewport.Height = float(size.height()); viewport.MaxDepth = 1.f;
    state.context->RSSetViewports(1, &viewport);
    state.context->RSSetState(state.rasterizer.Get());
    state.context->IASetInputLayout(nullptr);
    state.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    state.context->VSSetShader(state.vertex.Get(), nullptr, 0);
    state.context->PSSetShader(state.pixel.Get(), nullptr, 0);
    state.context->Draw(3, 0);
    const HRESULT result = state.swapChain->Present(1, 0);
    if (FAILED(result)) return state.fail(translate("HDR presentation failed; the GPU device may have been lost."), result);
    return true;
}
