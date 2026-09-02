// ============================================================================
// core/hud_gpu_renderer.cpp  — see hud_gpu_renderer.h
// ============================================================================
#include "hud_gpu_renderer.h"
#include "render_batch.h"

#if defined(_WIN32)

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include <cstddef>   // offsetof, for the input-layout contract below
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "render_asset_decode.h"
#include "../diagnostics/logger.h"

namespace hudgpu {
namespace {

// Tiny intrusive ComPtr so this TU needs no wrl (mingw parity).
template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    ~Com() { reset(); }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T** out() { reset(); return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// One shader pair. texel * color is the game's texture stage (the same rule
// hud_sw_renderer's drawQuad applies per pixel); the text shader samples the
// A8 glyph atlas as coverage, exactly drawStringFnt's blend(col, cov).
const char* kShaderSrc = R"(
struct VSIn  { float2 pos : POS; float2 uv : UV; float4 col : COL; };
struct VSOut { float4 pos : SV_Position; float2 uv : UV; float4 col : COL; };
VSOut vsMain(VSIn i) {
    VSOut o; o.pos = float4(i.pos, 0, 1); o.uv = i.uv; o.col = i.col; return o;
}
Texture2D tex : register(t0);
SamplerState smp : register(s0);
float4 psSprite(VSOut i) : SV_Target { return tex.Sample(smp, i.uv) * i.col; }
float4 psText(VSOut i) : SV_Target {
    return float4(i.col.rgb, tex.Sample(smp, i.uv).a * i.col.a);
}
)";

// Vertex and Run now come from core/render_batch.h, shared with the GL
// backend. Vertex's layout is unchanged and still matches the input layout
// element-for-element (POS float2, UV float2, COL R8G8B8A8_UNORM); Run carries
// an opaque texture handle and a Shader enum instead of the two COM pointers,
// which is the only thing the extraction altered here.
using hudbatch::Vertex;
using hudbatch::Run;

}  // namespace

struct Renderer::Impl {
    HMODULE d3dLib = nullptr, compilerLib = nullptr;
    Com<ID3D11Device> dev;
    Com<ID3D11DeviceContext> ctx;
    Com<IDXGISwapChain> swap;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> psSprite, psText;
    Com<ID3D11InputLayout> layout;
    Com<ID3D11SamplerState> sampler;
    Com<ID3D11BlendState> blend;
    Com<ID3D11RasterizerState> raster;
    Com<ID3D11DepthStencilState> noDepth;
    Com<ID3D11Buffer> vb;
    size_t vbCapacity = 0;

    // Render targets, rebuilt on client-size change.
    int rtW = 0, rtH = 0;
    UINT samples = 1;
    Com<ID3D11Texture2D> msaaTex;
    Com<ID3D11RenderTargetView> msaaRtv;
    HWND hwnd = nullptr;
    bool dead = false;                // any hard failure latches all calls false

    // Asset caches (SRV or a recorded miss). Font atlas is A8 coverage;
    // textures/icons are RGBA8. The white 1x1 serves untextured quads so every
    // primitive goes through the one texel*color shader.
    Com<ID3D11ShaderResourceView> white;
    struct CachedTex { Com<ID3D11ShaderResourceView> srv; int w = 0, h = 0; bool ok = false; };
    std::map<std::string, CachedTex> texs;   // dropTextureCache clears (live art reload)
    struct CachedFont { Com<ID3D11ShaderResourceView> srv; hudassets::FntFont font; };
    std::map<std::string, CachedFont> fonts; // kept — nobody iterates on a .fnt

    // CPU-side batch scratch, capacity reused across frames.
    std::vector<Vertex> verts;
    std::vector<Run> runs;

    bool createTargets(int w, int h);
    CachedTex* texture(const std::string& base, bool icon, const std::string& root);
    CachedFont* font(const std::string& base, const std::string& root);
    void buildBatch(const hudsw::Frame& frame, int w, int h,
                    float vx, float vy, float vw, float vh);
    bool drawBatch(uint8_t bgR, uint8_t bgG, uint8_t bgB, uint8_t bgA);
};

Renderer::Renderer() = default;

Renderer::~Renderer() {
    if (!m_impl) return;
    // COM members release in Impl's destructor; the DLLs stay loaded (freeing
    // d3d11 while its objects just died is asking for teardown ordering bugs,
    // and the game process almost certainly holds them anyway).
    delete m_impl;
}

bool Renderer::ok() const { return m_impl && !m_impl->dead; }

bool Renderer::init(void* hwndRaw) {
    if (m_impl) return ok();
    Impl* im = new Impl();
    m_impl = im;
    im->hwnd = static_cast<HWND>(hwndRaw);
    im->dead = true;   // cleared only on full success

    // Dynamic loads: the plugin must gain NO import that could fail to resolve
    // at DLL load (the game runs on machines this code has never met). Missing
    // pieces just mean "software path".
    im->d3dLib = LoadLibraryW(L"d3d11.dll");
    im->compilerLib = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!im->d3dLib || !im->compilerLib) {
        DEBUG_INFO("hudgpu: d3d11/d3dcompiler_47 unavailable - software rendering");
        return false;
    }
    auto createDevice = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(
        reinterpret_cast<void*>(GetProcAddress(im->d3dLib, "D3D11CreateDevice")));
    auto compile = reinterpret_cast<pD3DCompile>(
        reinterpret_cast<void*>(GetProcAddress(im->compilerLib, "D3DCompile")));
    if (!createDevice || !compile) return false;

    // BGRA support: the swapchain and MSAA targets are B8G8R8A8.
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                         D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = createDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                              D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 3,
                              D3D11_SDK_VERSION, im->dev.out(), nullptr, im->ctx.out());
    if (FAILED(hr)) {
        DEBUG_INFO_F("hudgpu: D3D11CreateDevice failed (0x%08lx) - software rendering",
                     static_cast<unsigned long>(hr));
        return false;
    }

    // Shaders.
    Com<ID3DBlob> vsb, psb1, psb2, err;
    if (FAILED(compile(kShaderSrc, strlen(kShaderSrc), nullptr, nullptr, nullptr,
                       "vsMain", "vs_4_0", 0, 0, vsb.out(), err.out())) ||
        FAILED(compile(kShaderSrc, strlen(kShaderSrc), nullptr, nullptr, nullptr,
                       "psSprite", "ps_4_0", 0, 0, psb1.out(), err.out())) ||
        FAILED(compile(kShaderSrc, strlen(kShaderSrc), nullptr, nullptr, nullptr,
                       "psText", "ps_4_0", 0, 0, psb2.out(), err.out())) ) {
        DEBUG_WARN("hudgpu: shader compile failed - software rendering");
        return false;
    }
    if (FAILED(im->dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(),
                                           nullptr, im->vs.out())) ||
        FAILED(im->dev->CreatePixelShader(psb1->GetBufferPointer(), psb1->GetBufferSize(),
                                          nullptr, im->psSprite.out())) ||
        FAILED(im->dev->CreatePixelShader(psb2->GetBufferPointer(), psb2->GetBufferSize(),
                                          nullptr, im->psText.out())))
        return false;

    // COMPILE-TIME CONTRACT, not a comment: the descriptors below read Vertex by
    // BYTE OFFSET and stride sizeof(Vertex), but Vertex now lives in the SHARED
    // render_batch.h that the GL backend also consumes - so a field added there
    // for GL's benefit would silently shift these offsets and corrupt every D3D
    // draw, with no test able to see it (the batch tests check values, not
    // layout, and there is no headless coverage of the D3D path). Change Vertex
    // and this TU stops compiling, which is the point.
    static_assert(sizeof(hudbatch::Vertex) == 20, "input layout stride");
    static_assert(offsetof(hudbatch::Vertex, x) == 0, "POS at byte 0");
    static_assert(offsetof(hudbatch::Vertex, u) == 8, "UV at byte 8");
    static_assert(offsetof(hudbatch::Vertex, rgba) == 16, "COL at byte 16");
    const D3D11_INPUT_ELEMENT_DESC elems[] = {
        { "POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "UV",  0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(im->dev->CreateInputLayout(elems, 3, vsb->GetBufferPointer(),
                                          vsb->GetBufferSize(), im->layout.out())))
        return false;

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(im->dev->CreateSamplerState(&sd, im->sampler.out()))) return false;

    // Straight-alpha src-over; the alpha channel accumulates coverage
    // (ONE, INV_SRC_ALPHA) — over a transparent clear this yields premultiplied
    // RGBA, which is what the layered present consumes (see the header).
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(im->dev->CreateBlendState(&bd, im->blend.out()))) return false;

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.MultisampleEnable = TRUE;
    if (FAILED(im->dev->CreateRasterizerState(&rd, im->raster.out()))) return false;

    D3D11_DEPTH_STENCIL_DESC dd{};   // depth off: painter's order IS the z-order
    if (FAILED(im->dev->CreateDepthStencilState(&dd, im->noDepth.out()))) return false;

    // 4x MSAA when the device offers it (edge AA on colored quads — the map
    // ribbon and needles are what the software path visibly lacks).
    UINT quality = 0;
    if (SUCCEEDED(im->dev->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, 4,
                                                         &quality)) && quality > 0)
        im->samples = 4;

    // White 1x1 so untextured quads share the sprite shader.
    {
        const uint32_t px = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{ &px, 4, 0 };
        Com<ID3D11Texture2D> t;
        if (FAILED(im->dev->CreateTexture2D(&td, &init, t.out())) ||
            FAILED(im->dev->CreateShaderResourceView(t.p, nullptr, im->white.out())))
            return false;
    }

    Com<IDXGIDevice> dxgiDev;
    Com<IDXGIAdapter> adapter;
    if (FAILED(im->dev->QueryInterface(__uuidof(IDXGIDevice),
                                       reinterpret_cast<void**>(dxgiDev.out()))) ||
        FAILED(dxgiDev->GetAdapter(adapter.out())))
        return false;

    // Blt-model swapchain: works everywhere this plugin does (incl. Wine),
    // and skipping Present on an unchanged frame keeps the last image.
    {
        Com<IDXGIFactory> factory;
        if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory),
                                      reinterpret_cast<void**>(factory.out()))))
            return false;
        DXGI_SWAP_CHAIN_DESC sc{};
        sc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sc.SampleDesc.Count = 1;
        sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.BufferCount = 1;
        sc.OutputWindow = im->hwnd;
        sc.Windowed = TRUE;
        sc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        if (FAILED(factory->CreateSwapChain(im->dev.p, &sc, im->swap.out()))) return false;
    }

    im->dead = false;
    DEBUG_INFO_F("hudgpu: D3D11 renderer up (%ux MSAA, swapchain)", im->samples);
    return true;
}

bool Renderer::Impl::createTargets(int w, int h) {
    msaaRtv.reset(); msaaTex.reset();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = samples;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, msaaTex.out())) ||
        FAILED(dev->CreateRenderTargetView(msaaTex.p, nullptr, msaaRtv.out())))
        return false;
    rtW = w; rtH = h;
    return true;
}

Renderer::Impl::CachedTex* Renderer::Impl::texture(const std::string& base, bool icon,
                                                   const std::string& root) {
    auto it = texs.find(base);
    if (it != texs.end()) return it->second.ok ? &it->second : nullptr;
    CachedTex ct;
    hudassets::Texture t = hudassets::decodeTga(hudsw::readFile(hudassets::spritePath(root, base, icon)));
    if (t.ok) {
        // ICONS ONLY, and the same reasoning as the glyph atlas: an icon is
        // minified roughly uniformly, which is the case a mip chain answers.
        // Nine-slice panel art is stretched hard in one axis instead, where the
        // level is chosen from the stretched derivative and blurs the sharp one.
        // See hudassets::buildTexMips, which also explains the premultiply.
        if (icon) hudassets::buildTexMips(t);
        std::vector<D3D11_SUBRESOURCE_DATA> init;
        init.reserve(t.mips.size());
        for (const hudassets::MipLevel& m : t.mips)
            init.push_back(D3D11_SUBRESOURCE_DATA{ m.px.data(), static_cast<UINT>(m.w * 4), 0 });
        if (init.empty())
            init.push_back(D3D11_SUBRESOURCE_DATA{ t.rgba.data(), static_cast<UINT>(t.w * 4), 0 });
        D3D11_TEXTURE2D_DESC td{};
        td.Width = t.w; td.Height = t.h;
        td.MipLevels = static_cast<UINT>(init.size()); td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Com<ID3D11Texture2D> gt;
        if (SUCCEEDED(dev->CreateTexture2D(&td, init.data(), gt.out())) &&
            SUCCEEDED(dev->CreateShaderResourceView(gt.p, nullptr, ct.srv.out()))) {
            ct.w = t.w; ct.h = t.h; ct.ok = true;
        }
    }
    auto& ref = texs[base];
    ref.srv.p = ct.srv.p; ct.srv.p = nullptr;   // move the ref into the map
    ref.w = ct.w; ref.h = ct.h; ref.ok = ct.ok;
    return ref.ok ? &ref : nullptr;
}

Renderer::Impl::CachedFont* Renderer::Impl::font(const std::string& base,
                                                 const std::string& root) {
    auto it = fonts.find(base);
    if (it != fonts.end()) return it->second.font.ok ? &it->second : nullptr;
    CachedFont cf;
    cf.font = hudassets::decodeFnt(hudsw::readFile(hudassets::fntPath(root, base)));
    if (cf.font.ok) {
        // WITH ITS MIP CHAIN - the glyph atlas is the one texture here that is
        // always heavily minified (135px shipped cell, ~20px HUD text), so a
        // single level means the sampler reads 4 texels of a ~49-texel footprint
        // and loses the partial-coverage texels that make a stroke's edge. The
        // sampler is already MIN_MAG_MIP_LINEAR, so supplying the levels is the
        // whole change. See FntFont::mips; the levels are built once at decode
        // and shared with the GL and software backends, so the three cannot
        // disagree about what a glyph looks like.
        const auto& mips = cf.font.mips;
        std::vector<D3D11_SUBRESOURCE_DATA> init;
        init.reserve(mips.size());
        for (const hudassets::MipLevel& m : mips)
            init.push_back(D3D11_SUBRESOURCE_DATA{ m.px.data(), static_cast<UINT>(m.w), 0 });
        if (init.empty())   // decode built no chain: level 0 alone, as before
            init.push_back(D3D11_SUBRESOURCE_DATA{ cf.font.atlas.data(),
                                                   static_cast<UINT>(cf.font.aw), 0 });
        D3D11_TEXTURE2D_DESC td{};
        td.Width = cf.font.aw; td.Height = cf.font.ah;
        td.MipLevels = static_cast<UINT>(init.size()); td.ArraySize = 1;
        td.Format = DXGI_FORMAT_A8_UNORM;   // coverage; psText builds (col.rgb, a*col.a)
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Com<ID3D11Texture2D> gt;
        if (FAILED(dev->CreateTexture2D(&td, init.data(), gt.out())) ||
            FAILED(dev->CreateShaderResourceView(gt.p, nullptr, cf.srv.out())))
            cf.font.ok = false;
    }
    auto& ref = fonts[base];
    ref.srv.p = cf.srv.p; cf.srv.p = nullptr;
    ref.font = std::move(cf.font);
    return ref.font.ok ? &ref : nullptr;
}

void Renderer::Impl::buildBatch(const hudsw::Frame& frame, int w, int h,
                                float vx, float vy, float vw, float vh) {
    // The batching itself lives in core/render_batch.cpp, shared verbatim with
    // the GL backend. All that is left here is resolving names to D3D
    // resources -- the only genuinely API-specific part of the loop.
    struct D3DResolver : hudbatch::Resolver {
        Impl* im;
        explicit D3DResolver(Impl* i) : im(i) {}
        const void* texture(const std::string& base, bool icon,
                            const std::string& root) override {
            CachedTex* t = im->texture(base, icon, root);
            return t ? static_cast<const void*>(t->srv.p) : nullptr;
        }
        const void* font(const std::string& base, const std::string& root,
                         const hudassets::FntFont** outFont) override {
            CachedFont* cf = im->font(base, root);
            if (!cf) return nullptr;
            *outFont = &cf->font;
            return cf->srv.p;
        }
        const void* white() override { return im->white.p; }
    } resolver(this);

    hudbatch::build(frame, w, h, vx, vy, vw, vh, resolver, verts, runs);
}

bool Renderer::Impl::drawBatch(uint8_t bgR, uint8_t bgG, uint8_t bgB, uint8_t bgA) {
    // Upload (grow the dynamic VB when the frame outgrew it).
    if (!verts.empty()) {
        if (verts.size() > vbCapacity) {
            vb.reset();
            vbCapacity = verts.size() + verts.size() / 2;
            D3D11_BUFFER_DESC bdsc{};
            bdsc.ByteWidth = static_cast<UINT>(vbCapacity * sizeof(Vertex));
            bdsc.Usage = D3D11_USAGE_DYNAMIC;
            bdsc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bdsc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(dev->CreateBuffer(&bdsc, nullptr, vb.out()))) return false;
        }
        D3D11_MAPPED_SUBRESOURCE mapd{};
        if (FAILED(ctx->Map(vb.p, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapd))) return false;
        std::memcpy(mapd.pData, verts.data(), verts.size() * sizeof(Vertex));
        ctx->Unmap(vb.p, 0);
    }

    const float clear[4] = { bgR / 255.0f, bgG / 255.0f, bgB / 255.0f, bgA / 255.0f };
    ctx->ClearRenderTargetView(msaaRtv.p, clear);
    ID3D11RenderTargetView* rtv = msaaRtv.p;
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vpd{ 0, 0, static_cast<float>(rtW), static_cast<float>(rtH), 0, 1 };
    ctx->RSSetViewports(1, &vpd);
    ctx->RSSetState(raster.p);
    ctx->OMSetBlendState(blend.p, nullptr, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(noDepth.p, 0);
    ctx->IASetInputLayout(layout.p);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(Vertex), offset = 0;
    ID3D11Buffer* vbp = vb.p;
    ctx->IASetVertexBuffers(0, 1, &vbp, &stride, &offset);
    ctx->VSSetShader(vs.p, nullptr, 0);
    ID3D11SamplerState* smp = sampler.p;
    ctx->PSSetSamplers(0, 1, &smp);
    for (const Run& r : runs) {
        if (!r.count) continue;
        ctx->PSSetShader(r.shader == hudbatch::Shader::Text ? psText.p : psSprite.p,
                         nullptr, 0);
        ID3D11ShaderResourceView* srv = static_cast<ID3D11ShaderResourceView*>(
            const_cast<void*>(r.tex));
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->Draw(r.count, r.start);
    }
    return true;
}

bool Renderer::renderSwapchain(const hudsw::Frame& frame, int w, int h,
                               float vx, float vy, float vw, float vh,
                               uint8_t bgR, uint8_t bgG, uint8_t bgB, bool vsync) {
    Impl* im = m_impl;
    if (!im || im->dead || !im->swap) return false;
    if (w < 1 || h < 1) return true;   // minimized: nothing to do, not an error
    if (w != im->rtW || h != im->rtH) {
        im->msaaRtv.reset(); im->msaaTex.reset();
        if (FAILED(im->swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0)) ||
            !im->createTargets(w, h)) { im->dead = true; return false; }
    }
    im->buildBatch(frame, w, h, vx, vy, vw, vh);
    if (!im->drawBatch(bgR, bgG, bgB, 255)) { im->dead = true; return false; }

    Com<ID3D11Texture2D> back;
    if (FAILED(im->swap->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                   reinterpret_cast<void**>(back.out())))) {
        im->dead = true; return false;
    }
    if (im->samples > 1)
        im->ctx->ResolveSubresource(back.p, 0, im->msaaTex.p, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    else
        im->ctx->CopyResource(back.p, im->msaaTex.p);
    HRESULT hr = im->swap->Present(vsync ? 1 : 0, 0);
    if (FAILED(hr)) { im->dead = true; return false; }
    return true;
}

void Renderer::dropTextureCache() {
    if (m_impl) m_impl->texs.clear();
}

}  // namespace hudgpu

#else  // non-Windows link stub (the plugin is Windows-only)
namespace hudgpu {
struct Renderer::Impl {};
Renderer::Renderer() = default;
Renderer::~Renderer() { delete m_impl; }
bool Renderer::init(void*) { return false; }
bool Renderer::ok() const { return false; }
bool Renderer::renderSwapchain(const hudsw::Frame&, int, int, float, float, float, float,
                               uint8_t, uint8_t, uint8_t, bool) { return false; }
void Renderer::dropTextureCache() {}
}  // namespace hudgpu
#endif
