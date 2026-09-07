// rhi_dx12_smoke (Phase 0 acceptance smoke test)
//
// Scope:
//   - Open an empty GLFW window
//   - Create a D3D12 device (debug layer in Debug builds)
//   - Create a command queue and a DXGI FLIP_DISCARD swapchain (2x R8G8B8A8)
//   - Run the message loop until the window is closed (or ESC), then tear down cleanly
//
// Nothing is rendered. The DX12 RHI classes are abstract until Phase 1, so this
// target talks to raw D3D12/DXGI on purpose.
//
// Exit code 0 = clean smoke pass.

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <directx/d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>

using Microsoft::WRL::ComPtr;

namespace {

constexpr int   kWidth      = 800;
constexpr int   kHeight     = 600;
constexpr UINT  kBufferCount = 2;

void Fail(const char* what) {
    std::fprintf(stderr, "[rhi_dx12_smoke] FATAL: %s\n", what);
    std::exit(1);
}

void Check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        Fail(what);
    }
}

} // namespace

int main() {
    std::puts("[rhi_dx12_smoke] GLFW init...");
    if (!glfwInit()) {
        Fail("glfwInit failed");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(kWidth, kHeight, "RHI DX12 Smoke (Phase 0)", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        Fail("glfwCreateWindow failed");
    }

#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            std::puts("[rhi_dx12_smoke] D3D12 debug layer enabled");
        }
    }
#endif

    // ---- DXGI factory + first hardware adapter with D3D12 support ----
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2 failed");

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc = {};
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter.Reset();
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                        __uuidof(ID3D12Device), nullptr))) {
            break;  // keep this adapter
        }
        adapter.Reset();
    }
    if (!adapter) {
        Fail("no hardware adapter with D3D12 feature level 12_0 found");
    }
    DXGI_ADAPTER_DESC1 adapterDesc = {};
    adapter->GetDesc1(&adapterDesc);
    std::printf("[rhi_dx12_smoke] Adapter: %ls\n", adapterDesc.Description);

    // ---- Device ----
    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)),
          "D3D12CreateDevice failed");

    // ---- Command queue (single DIRECT queue, matches engine usage) ----
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    ComPtr<ID3D12CommandQueue> queue;
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue failed");

    // ---- Swapchain (FLIP_DISCARD, same model as DX12RHISwapChain) ----
    HWND hwnd = glfwGetWin32Window(window);

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width              = kWidth;
    swapChainDesc.Height             = kHeight;
    swapChainDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count   = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount        = kBufferCount;
    swapChainDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Scaling            = DXGI_SCALING_STRETCH;
    swapChainDesc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain1;
    Check(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &swapChainDesc,
                                          nullptr, nullptr, &swapChain1),
          "CreateSwapChainForHwnd failed");
    Check(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER),
          "MakeWindowAssociation failed");

    ComPtr<IDXGISwapChain3> swapChain;
    if (FAILED(swapChain1.As(&swapChain))) {
        Fail("failed to query IDXGISwapChain3");
    }

    std::printf("[rhi_dx12_smoke] Swapchain created: %u back buffers, %ux%u (current index %u)\n",
                kBufferCount, kWidth, kHeight, swapChain->GetCurrentBackBufferIndex());

    // ---- Message loop: no rendering, just keep the window alive ----
    std::puts("[rhi_dx12_smoke] Entering message loop - close the window or press ESC to exit");
    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        glfwPollEvents();
    }

    // ---- Clean teardown (swapchain must outlive its HWND) ----
    swapChain.Reset();
    swapChain1.Reset();
    queue.Reset();
    device.Reset();
    adapter.Reset();
    factory.Reset();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::puts("[rhi_dx12_smoke] Clean exit");
    return 0;
}
