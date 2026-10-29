#include "presenter.h"

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <cstring>

namespace wren {
namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT kBufferCount = 2;

// D3D11 + DirectComposition 呈现：预乘 alpha 只能走组合交换链，透明处露出 Mica。
class GpuPresenter final : public Presenter {
public:
    ~GpuPresenter() override {
        release_composition();
        release();
    }

    PresenterKind kind() const override { return PresenterKind::Gpu; }
    const wchar_t* name() const override { return L"D3D11 + DComp · VSync"; }
    HANDLE frame_wait_handle() const override { return frame_wait_; }
    bool healthy() const override { return healthy_; }
    ID2D1DeviceContext* d2d_context() const override { return d2d_context_ != nullptr ? d2d_context_.Get() : nullptr; }
    bool compositor_paced() const override { return true; }

    bool initialize(HWND hwnd, int width_px, int height_px) override {
        if (hwnd == nullptr || width_px <= 0 || height_px <= 0) {
            return false;
        }

        hwnd_ = hwnd;
        width_ = width_px;
        height_ = height_px;

        if (!create_device() || !create_swap_chain(width_px, height_px) || !create_composition() ||
            !create_staging(width_px, height_px)) {
            release_composition();
            release();
            return false;
        }

        // D2D 失败还能退回「DIB → staging → 交换链」，所以不算致命错误。
        create_d2d();

        // 重定向位图必须清成 alpha=0，DComp 的透明处才会露出 Mica。
        clear_redirection_surface();

        healthy_ = true;
        return true;
    }

    bool begin_frame(float dpi) override {
        if (d2d_context_ == nullptr || !healthy_) {
            return false;
        }

        if (!ensure_d2d_target()) {
            return false;
        }

        d2d_context_->SetDpi(96.0f, 96.0f);
        d2d_context_->SetTransform(
            D2D1::Matrix3x2F::Scale(dpi > 0.0f ? dpi / 96.0f : 1.0f, dpi > 0.0f ? dpi / 96.0f : 1.0f));
        d2d_context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        d2d_context_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        d2d_context_->BeginDraw();
        d2d_context_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        drawing_ = true;
        return true;
    }

    void end_frame() override {
        if (!drawing_ || d2d_context_ == nullptr) {
            return;
        }

        const HRESULT status = d2d_context_->EndDraw();
        drawing_ = false;

        // 含 D2DERR_RECREATE_TARGET：目标已失效，本帧不出，交给 Host 降级。
        if (FAILED(status)) {
            healthy_ = false;
            return;
        }

        ComPtr<ID3D11Texture2D> back_buffer;
        if (d2d_texture_ == nullptr || FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) ||
            back_buffer == nullptr) {
            healthy_ = false;
            return;
        }

        context_->CopyResource(back_buffer.Get(), d2d_texture_.Get());
        back_buffer.Reset();
        present_back_buffer();
    }

    void resize(int width_px, int height_px) override {
        if (width_px <= 0 || height_px <= 0) {
            return;
        }

        if (swap_chain_ == nullptr || !healthy_) {
            width_ = width_px;
            height_ = height_px;
            return;
        }

        if (width_px == width_ && height_px == height_) {
            return;
        }

        // 交换链 back buffer 不能在仍被引用时 ResizeBuffers；present 里用完就放。
        staging_.Reset();
        context_->ClearState();

        const HRESULT hr = swap_chain_->ResizeBuffers(0, static_cast<UINT>(width_px), static_cast<UINT>(height_px),
                                                      DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) {
            healthy_ = false;
            return;
        }

        width_ = width_px;
        height_ = height_px;
        if (!create_staging(width_px, height_px)) {
            healthy_ = false;
            return;
        }

        if (d2d_context_ != nullptr) {
            d2d_context_->SetTarget(nullptr);
        }
        d2d_target_.Reset();
        d2d_texture_.Reset();
        d2d_texture_width_ = 0;
        d2d_texture_height_ = 0;

        swap_chain2_->SetMaximumFrameLatency(1);
        if (composition_device_ != nullptr) {
            composition_device_->Commit();
        }

        // 缩放/拖动会重建重定向位图，得重新清成 alpha=0。
        clear_redirection_surface();
    }

    void present(const Frame& frame, HDC dc) override {
        (void)dc;

        if (!healthy_) {
            return;
        }

        if (frame.width_px() != width_ || frame.height_px() != height_) {
            resize(frame.width_px(), frame.height_px());
            if (!healthy_) {
                return;
            }
        }

        if (!upload(frame)) {
            healthy_ = false;
            return;
        }

        ComPtr<ID3D11Texture2D> back_buffer;
        if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) || back_buffer == nullptr) {
            healthy_ = false;
            return;
        }

        context_->CopyResource(back_buffer.Get(), staging_.Get());
        back_buffer.Reset();

        present_back_buffer();
    }

private:
    /// 交换链 back buffer 不能当 D2D target，先画到普通纹理，end_frame 时拷回。
    bool ensure_d2d_target() {
        if (d2d_target_ != nullptr && d2d_texture_ != nullptr && d2d_texture_width_ == width_ &&
            d2d_texture_height_ == height_) {
            return true;
        }

        d2d_context_->SetTarget(nullptr);
        d2d_target_.Reset();
        d2d_texture_.Reset();
        d2d_texture_width_ = 0;
        d2d_texture_height_ = 0;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(width_);
        desc.Height = static_cast<UINT>(height_);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(device_->CreateTexture2D(&desc, nullptr, d2d_texture_.ReleaseAndGetAddressOf()))) {
            return false;
        }

        ComPtr<IDXGISurface> surface;
        if (FAILED(d2d_texture_.As(&surface)) || surface == nullptr) {
            return false;
        }

        const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);

        if (FAILED(d2d_context_->CreateBitmapFromDxgiSurface(surface.Get(), &properties,
                                                             d2d_target_.ReleaseAndGetAddressOf()))) {
            return false;
        }

        d2d_context_->SetTarget(d2d_target_.Get());
        d2d_texture_width_ = width_;
        d2d_texture_height_ = height_;
        return true;
    }

    void present_back_buffer() {
        if (swap_chain_ == nullptr) {
            return;
        }

        const HRESULT hr = swap_chain_->Present(1, 0);
        if (composition_device_ != nullptr) {
            // 组合交换链的新内容要随视觉树一起提交，DWM 才会在下一个合成周期取用。
            composition_device_->Commit();
        }

        // Present 失败这一帧就丢了，按设备丢失处理，交给 Host 降级到 GDI。
        if (FAILED(hr)) {
            healthy_ = false;
        }
    }

    bool create_d2d() {
        if (d2d_factory_ == nullptr &&
            FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr,
                                       reinterpret_cast<void**>(d2d_factory_.ReleaseAndGetAddressOf())))) {
            return false;
        }

        if (d2d_device_ == nullptr &&
            FAILED(d2d_factory_->CreateDevice(dxgi_device_.Get(), d2d_device_.ReleaseAndGetAddressOf()))) {
            return false;
        }

        if (d2d_context_ == nullptr && FAILED(d2d_device_->CreateDeviceContext(
                                           D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2d_context_.ReleaseAndGetAddressOf()))) {
            return false;
        }

        return d2d_context_ != nullptr;
    }

    /// 重定向位图清成 alpha=0（BLACKNESS 连 alpha 一起清），透明处才露出 Mica。
    void clear_redirection_surface() {
        if (hwnd_ == nullptr || ::IsWindow(hwnd_) == FALSE) {
            return;
        }

        RECT client{};
        ::GetClientRect(hwnd_, &client);
        if (client.right <= 0 || client.bottom <= 0) {
            return;
        }

        HDC dc = ::GetDC(hwnd_);
        if (dc == nullptr) {
            return;
        }

        ::PatBlt(dc, 0, 0, client.right, client.bottom, BLACKNESS);
        ::ReleaseDC(hwnd_, dc);
    }

    bool create_device() {
        static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                                   D3D_FEATURE_LEVEL_10_0};

        D3D_FEATURE_LEVEL level{};
        const HRESULT hr =
            ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                static_cast<UINT>(ARRAYSIZE(levels)), D3D11_SDK_VERSION, &device_, &level, &context_);
        if (FAILED(hr)) {
            return false;
        }

        return SUCCEEDED(device_.As(&dxgi_device_));
    }

    bool create_swap_chain(int width_px, int height_px) {
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgi_device_->GetAdapter(&adapter)) || adapter == nullptr) {
            return false;
        }

        ComPtr<IDXGIFactory2> factory;
        if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))) || factory == nullptr) {
            return false;
        }

        // 组合交换链只接受 FLIP_SEQUENTIAL；waitable 在个别驱动上不支持，失败就去掉标志重试。
        const struct {
            DXGI_SCALING scaling;
            UINT flags;
        } attempts[] = {
            {DXGI_SCALING_NONE, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT},
            {DXGI_SCALING_NONE, 0},
            {DXGI_SCALING_STRETCH, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT},
            {DXGI_SCALING_STRETCH, 0},
        };

        HRESULT last = E_FAIL;
        bool waitable = false;
        for (const auto& attempt : attempts) {
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = static_cast<UINT>(width_px);
            desc.Height = static_cast<UINT>(height_px);
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.Stereo = FALSE;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = kBufferCount;
            desc.Scaling = attempt.scaling;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
            desc.Flags = attempt.flags;

            last = factory->CreateSwapChainForComposition(device_.Get(), &desc, nullptr, &swap_chain_);
            if (SUCCEEDED(last)) {
                waitable = (attempt.flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) != 0;
                break;
            }
        }

        if (FAILED(last) || swap_chain_ == nullptr) {
            return false;
        }

        if (FAILED(swap_chain_.As(&swap_chain2_))) {
            return false;
        }

        swap_chain2_->SetMaximumFrameLatency(1);
        // 没有 waitable 标志时拿不到句柄，Host 会退回 WM_TIMER 驱动。
        frame_wait_ = waitable ? swap_chain2_->GetFrameLatencyWaitableObject() : nullptr;
        return true;
    }

    bool create_composition() {
        const HRESULT hr = ::DCompositionCreateDevice(dxgi_device_.Get(), IID_PPV_ARGS(&composition_device_));
        if (FAILED(hr) || composition_device_ == nullptr) {
            return false;
        }

        if (FAILED(composition_device_->CreateTargetForHwnd(hwnd_, TRUE, &composition_target_)) ||
            composition_target_ == nullptr) {
            return false;
        }

        if (FAILED(composition_device_->CreateVisual(&composition_visual_)) || composition_visual_ == nullptr) {
            return false;
        }

        if (FAILED(composition_visual_->SetContent(swap_chain_.Get()))) {
            return false;
        }

        if (FAILED(composition_target_->SetRoot(composition_visual_.Get()))) {
            return false;
        }

        return SUCCEEDED(composition_device_->Commit());
    }

    bool create_staging(int width_px, int height_px) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(width_px);
        desc.Height = static_cast<UINT>(height_px);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        // DYNAMIC 纹理必须带至少一个 bind flag（BindFlags=0 会 E_INVALIDARG），这里只为 CPU 可写。
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        return SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, &staging_));
    }

    bool upload(const Frame& frame) {
        if (staging_ == nullptr || frame.bits() == nullptr) {
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return false;
        }

        const auto* source = static_cast<const unsigned char*>(frame.bits());
        auto* destination = static_cast<unsigned char*>(mapped.pData);
        const size_t row_bytes = static_cast<size_t>(width_) * 4u;
        for (int y = 0; y < height_; ++y) {
            std::memcpy(destination + static_cast<size_t>(mapped.RowPitch) * static_cast<size_t>(y),
                        source + row_bytes * static_cast<size_t>(y), row_bytes);
        }

        context_->Unmap(staging_.Get(), 0);
        return true;
    }

    void release_composition() {
        if (composition_visual_ != nullptr) {
            composition_visual_->SetContent(nullptr);
        }

        if (composition_target_ != nullptr) {
            composition_target_->SetRoot(nullptr);
        }

        if (composition_device_ != nullptr) {
            composition_device_->Commit();
        }

        composition_visual_.Reset();
        composition_target_.Reset();
        composition_device_.Reset();
    }

    void release() {
        frame_wait_ = nullptr;  // 归交换链所有，不能 CloseHandle
        if (d2d_context_ != nullptr) {
            d2d_context_->SetTarget(nullptr);
        }
        d2d_target_.Reset();
        d2d_texture_.Reset();
        d2d_texture_width_ = 0;
        d2d_texture_height_ = 0;
        d2d_context_.Reset();
        d2d_device_.Reset();
        d2d_factory_.Reset();
        staging_.Reset();
        swap_chain2_.Reset();
        swap_chain_.Reset();
        dxgi_device_.Reset();
        context_.Reset();
        device_.Reset();
        drawing_ = false;
    }

    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool healthy_ = false;
    bool drawing_ = false;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIDevice> dxgi_device_;
    ComPtr<IDXGISwapChain1> swap_chain_;
    ComPtr<IDXGISwapChain2> swap_chain2_;
    ComPtr<IDCompositionDevice> composition_device_;
    ComPtr<IDCompositionTarget> composition_target_;
    ComPtr<IDCompositionVisual> composition_visual_;
    ComPtr<ID2D1Factory1> d2d_factory_;
    ComPtr<ID2D1Device> d2d_device_;
    ComPtr<ID2D1DeviceContext> d2d_context_;
    ComPtr<ID2D1Bitmap1> d2d_target_;
    ComPtr<ID3D11Texture2D> d2d_texture_;
    int d2d_texture_width_ = 0;
    int d2d_texture_height_ = 0;
    ComPtr<ID3D11Texture2D> staging_;
    HANDLE frame_wait_ = nullptr;
};

}  // namespace

std::unique_ptr<Presenter> make_gpu_presenter(HWND hwnd, int width_px, int height_px) {
    auto presenter = std::make_unique<GpuPresenter>();
    if (!presenter->initialize(hwnd, width_px, height_px)) {
        return nullptr;
    }

    return presenter;
}

}  // namespace wren
