#include "DX12RHISwapChain.h"
#include "DX12RHIDevice.h"
#include "DX12TypeConversions.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <stdexcept>

using namespace DX12TypeConversions;

DX12RHISwapChain::DX12RHISwapChain(DX12RHIDevice *device, const RHISwapChainDesc &desc)
    : device(device), desc(desc)
{
    HWND hwnd = glfwGetWin32Window(device->getWindow());

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width       = desc.width;
    swapChainDesc.Height      = desc.height;
    swapChainDesc.Format      = toDXGIFormat(desc.format);
    swapChainDesc.SampleDesc.Count   = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = desc.bufferCount;
    swapChainDesc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Scaling     = DXGI_SCALING_STRETCH;
    swapChainDesc.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;

    ComPtr<IDXGISwapChain1> swapChain1;
    if (FAILED(device->getFactory()->CreateSwapChainForHwnd(
            device->getCommandQueue(),
            hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain1)))
    {
        throw std::runtime_error("Failed to create swap chain!");
    }

    // Disable Alt+Enter fullscreen toggle
    device->getFactory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    // Upgrade to IDXGISwapChain3
    if (FAILED(swapChain1.As(&swapChain))) {
        throw std::runtime_error("Failed to upgrade swap chain to IDXGISwapChain3!");
    }

    frameIndex = swapChain->GetCurrentBackBufferIndex();
}