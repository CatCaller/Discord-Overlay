#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <iostream>
#include <string_view>
#include <array>
#include <chrono>
#include <thread>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dwmapi.lib")

using Microsoft::WRL::ComPtr;

struct DxgiOverlayRenderer {
    HWND targetHwnd{ nullptr };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11RenderTargetView> renderTargetView;

    void Reset() noexcept {
        renderTargetView.Reset();
        swapChain.Reset();
        context.Reset();
        device.Reset();
        targetHwnd = nullptr;
    }

    bool Initialize(HWND hwnd, UINT width, UINT height) {
        Reset();

        if (!hwnd || !IsWindow(hwnd) || width == 0 || height == 0) {
            return false;
        }

        const UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL featureLevel{};
        const std::array<D3D_FEATURE_LEVEL, 2> featureLevels{
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0
        };

        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createDeviceFlags,
            featureLevels.data(),
            static_cast<UINT>(featureLevels.size()),
            D3D11_SDK_VERSION,
            &device,
            &featureLevel,
            &context
        );
        if (FAILED(hr)) {
            std::wcerr << L"D3D11CreateDevice failed: 0x" << std::hex << hr << std::dec << std::endl;
            return false;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(device.As(&dxgiDevice))) {
            return false;
        }

        ComPtr<IDXGIAdapter> dxgiAdapter;
        if (FAILED(dxgiDevice->GetAdapter(&dxgiAdapter))) {
            return false;
        }

        ComPtr<IDXGIFactory2> dxgiFactory;
        if (FAILED(dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory)))) {
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.Width = width;
        scDesc.Height = height;
        scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scDesc.Stereo = FALSE;
        scDesc.SampleDesc.Count = 1;
        scDesc.SampleDesc.Quality = 0;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.BufferCount = 2;
        scDesc.Scaling = DXGI_SCALING_STRETCH;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

        hr = dxgiFactory->CreateSwapChainForHwnd(
            device.Get(),
            hwnd,
            &scDesc,
            nullptr,
            nullptr,
            &swapChain
        );
        if (FAILED(hr)) {
            std::wcerr << L"CreateSwapChainForHwnd failed: 0x" << std::hex << hr << std::dec << std::endl;
            return false;
        }

        ComPtr<ID3D11Texture2D> backBuffer;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
            return false;
        }

        if (FAILED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView))) {
            return false;
        }

        targetHwnd = hwnd;
        return true;
    }

    bool RenderFrame(float r, float g, float b, float a) {
        if (!context || !renderTargetView || !swapChain) {
            return false;
        }

        const std::array<float, 4> clearColor{ r, g, b, a };
        context->OMSetRenderTargets(1, renderTargetView.GetAddressOf(), nullptr);
        context->ClearRenderTargetView(renderTargetView.Get(), clearColor.data());

        const HRESULT hr = swapChain->Present(1, 0);
        return SUCCEEDED(hr);
    }
};

HWND FindDiscordOverlayHwnd() {
    HWND targetHwnd = nullptr;

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        if (!IsWindowVisible(hwnd)) {
            return TRUE;
        }

        std::array<wchar_t, 256> className{};
        const int classLen = GetClassNameW(hwnd, className.data(), static_cast<int>(className.size()));
        if (classLen == 0 || std::wstring_view(className.data(), classLen) != L"Chrome_WidgetWin_1") {
            return TRUE;
        }

        std::array<wchar_t, 256> title{};
        const int titleLen = GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
        if (titleLen == 0) {
            return TRUE;
        }

        const std::wstring_view titleView(title.data(), titleLen);
        if (titleView == L"Discord Overlay" || titleView == L"Discord Popout") {
            *reinterpret_cast<HWND*>(lParam) = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&targetHwnd));

    return targetHwnd;
}

int main() {
    DxgiOverlayRenderer renderer{};
    auto lastAttemptTime = std::chrono::steady_clock::now();

    std::wcout << L"DXGI Discord Overlay PoC" << std::endl;

    while (true) {
        const auto now = std::chrono::steady_clock::now();

        if (!renderer.targetHwnd || !IsWindow(renderer.targetHwnd)) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastAttemptTime).count() >= 1000) {
                lastAttemptTime = now;
                const HWND overlayHwnd = FindDiscordOverlayHwnd();
                if (overlayHwnd) {
                    RECT rect{};
                    GetClientRect(overlayHwnd, &rect);
                    const UINT width = static_cast<UINT>(rect.right - rect.left);
                    const UINT height = static_cast<UINT>(rect.bottom - rect.top);

                    if (width > 0 && height > 0) {
                        std::wcout << L"Attempting HWND: 0x"
                                   << std::hex << reinterpret_cast<uintptr_t>(overlayHwnd)
                                   << std::dec << L" (" << width << L"x" << height << L")" << std::endl;

                        if (renderer.Initialize(overlayHwnd, width, height)) {
                            std::wcout << L"Attached DXGI SwapChain to HWND" << std::endl;
                        }
                    }
                }
            }
        }

        if (renderer.targetHwnd) {
            if (!renderer.RenderFrame(0.0f, 1.0f, 0.0f, 0.35f)) {
                renderer.Reset();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    return 0;
}
