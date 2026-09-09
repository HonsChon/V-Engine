#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <dxgi.h>

#include "DX12RHISwapChain.h"
#include "DX12RHIDevice.h"
#include "DX12RHITexture.h"
#include "DX12RHIFramebuffer.h"
#include "DX12RHIRenderPass.h"
#include "DX12TypeConversions.h"

#include <stdexcept>

using namespace DX12TypeConversions;

namespace {

RHIRenderPassDesc makeSwapChainRenderPassDesc(RHIFormat colorFormat) {
    RHIRenderPassDesc desc;
    desc.addColorAttachment(colorFormat,
                            RHILoadOp::Clear, RHIStoreOp::Store,
                            RHIImageLayout::Undefined, RHIImageLayout::PresentSrc);
    desc.setDepthAttachment(RHIFormat::D32_SFLOAT,
                            RHILoadOp::Clear, RHIStoreOp::DontCare,
                            RHIImageLayout::Undefined, RHIImageLayout::DepthStencilReadOnly);
    return desc;
}

} // namespace

DX12RHISwapChain::DX12RHISwapChain(DX12RHIDevice* device, const RHISwapChainDesc& desc)
    : device_(device)
    , desc_(desc)
    , bufferCount_(std::max(2u, desc.bufferCount))
    , width_(desc.width)
    , height_(desc.height)
    , format_(desc.format)
{
    createSwapChainInternal();
    createViewsAndWrappers();
}

DX12RHISwapChain::~DX12RHISwapChain() = default;

// -----------------------------------------------------------------------------

void DX12RHISwapChain::createSwapChainInternal() {
    HWND hwnd = glfwGetWin32Window(device_->getWindow());

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width       = width_;
    swapChainDesc.Height      = height_;
    swapChainDesc.Format      = toDXGIFormat(format_);
    swapChainDesc.SampleDesc.Count   = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = bufferCount_;
    swapChainDesc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Scaling     = DXGI_SCALING_STRETCH;
    swapChainDesc.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain1;
    if (FAILED(device_->getFactory()->CreateSwapChainForHwnd(
            device_->getCommandQueue(), hwnd, &swapChainDesc,
            nullptr, nullptr, &swapChain1))) {
        throw std::runtime_error("[DX12RHISwapChain] failed to create swap chain");
    }
    device_->getFactory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    if (FAILED(swapChain1.As(&swapChain_))) {
        throw std::runtime_error("[DX12RHISwapChain] failed to query IDXGISwapChain3");
    }
}

void DX12RHISwapChain::destroyViewsAndWrappers() {
    rhiFramebuffers_.clear();
    backBufferTextures_.clear();
    depthTexture_.reset();
    rtvHeap_.Reset();
    dsvHeap_.Reset();
    rtvHandles_.clear();
    dsvHandle_ = {};
}

void DX12RHISwapChain::createViewsAndWrappers() {
    ID3D12Device* d3d = device_->getDevice();

    // RTV heap: one descriptor per back buffer.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = bufferCount_;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(d3d->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_)))) {
        throw std::runtime_error("[DX12RHISwapChain] failed to create RTV heap");
    }

    const UINT rtvSize = d3d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    rtvHandles_.clear();
    backBufferTextures_.clear();
    for (UINT i = 0; i < bufferCount_; ++i) {
        ComPtr<ID3D12Resource> backBuffer;
        if (FAILED(swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffer)))) {
            throw std::runtime_error("[DX12RHISwapChain] failed to get back buffer");
        }
        RHITextureDesc texDesc;
        texDesc.width  = width_;
        texDesc.height = height_;
        texDesc.format = format_;
        texDesc.usage  = RHITextureUsage::ColorAttachment;

        auto texture = std::make_shared<DX12RHITexture>(device_, backBuffer, texDesc,
                                                        D3D12_RESOURCE_STATE_COMMON);
        d3d->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvCpu);
        rtvHandles_.push_back(rtvCpu);
        backBufferTextures_.push_back(texture);
        rtvCpu.ptr += rtvSize;
    }

    // Internal depth buffer + DSV.
    {
        RHITextureDesc depthDesc;
        depthDesc.width  = width_;
        depthDesc.height = height_;
        depthDesc.format = RHIFormat::D32_SFLOAT;
        depthDesc.usage  = RHITextureUsage::DepthStencilAttachment;
        depthTexture_ = std::make_shared<DX12RHITexture>(device_, depthDesc);

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(d3d->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_)))) {
            throw std::runtime_error("[DX12RHISwapChain] failed to create DSV heap");
        }
        dsvHandle_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        d3d->CreateDepthStencilView(depthTexture_->getD3D12Resource(), &dsvDesc, dsvHandle_);
    }

    // RHI wrappers.
    rhiRenderPass_ = std::make_shared<DX12RHIRenderPass>(makeSwapChainRenderPassDesc(format_));

    rhiFramebuffers_.clear();
    for (UINT i = 0; i < bufferCount_; ++i) {
        std::vector<DX12RHITexture*> colors = { backBufferTextures_[i].get() };
        rhiFramebuffers_.push_back(std::make_shared<DX12RHIFramebuffer>(
            device_, width_, height_, colors, std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>{ rtvHandles_[i] },
            depthTexture_.get(), dsvHandle_, true));
    }

    // Per-image fences.
    imageFences_.clear();
    for (UINT i = 0; i < bufferCount_; ++i) {
        imageFences_.push_back(std::make_unique<DX12FenceSync>(d3d, /*signaled=*/true));
    }
}

// -----------------------------------------------------------------------------

RHISwapChainResult DX12RHISwapChain::acquireNextImage(void* /*signalSemaphore*/,
                                                      uint32_t* outImageIndex) {
    if (width_ == 0 || height_ == 0) {
        return RHISwapChainResult::OutOfDate;   // minimized / not yet sized
    }
    // DXGI FLIP model: the swapchain hands us the next buffer index directly.
    const UINT index = swapChain_->GetCurrentBackBufferIndex();

    // Block until the previous present that used this image has completed.
    if (index < imageFences_.size()) {
        DX12FenceSync& sync = *imageFences_[index];
        if (sync.fence->GetCompletedValue() < sync.value) {
            sync.fence->SetEventOnCompletion(sync.value, sync.event);
            WaitForSingleObject(sync.event, INFINITE);
        }
    }

    *outImageIndex = index;
    return RHISwapChainResult::Success;
}

RHISwapChainResult DX12RHISwapChain::present(void* /*waitSemaphore*/, uint32_t imageIndex) {
    const UINT syncInterval = (desc_.presentMode == RHIPresentMode::Immediate) ? 0 : 1;
    HRESULT hr = swapChain_->Present(syncInterval, 0);

    // Queue the per-image fence signal behind the just-presented frame.
    if (imageIndex < imageFences_.size()) {
        DX12FenceSync& sync = *imageFences_[imageIndex];
        device_->getCommandQueue()->Signal(sync.fence.Get(), ++sync.value);
    }

    return mapPresentResult(hr);
}

RHISwapChainResult DX12RHISwapChain::mapPresentResult(HRESULT hr) const {
    if (SUCCEEDED(hr)) {
        return RHISwapChainResult::Success;
    }
    // DXGI_ERROR_OUT_OF_DATE = 0x887A0005, DXGI_STATUS_OCCLUDED = 0x087A0001
    // (not provided by the DirectX-Headers dxgi.h; keep literals).
    if (static_cast<unsigned long>(hr) == 0x887A0005UL) {
        return RHISwapChainResult::OutOfDate;
    }
    if (static_cast<unsigned long>(hr) == 0x087A0001UL) {
        return RHISwapChainResult::Success;   // nothing was presented, not an error
    }
    return RHISwapChainResult::Error;
}

void DX12RHISwapChain::recreate(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        width_ = 0;
        height_ = 0;
        return;
    }
    device_->waitIdle();

    // Release everything referencing the old buffers before ResizeBuffers.
    destroyViewsAndWrappers();

    desc_.width = width;
    desc_.height = height;
    width_ = width;
    height_ = height;

    HRESULT hr = swapChain_->ResizeBuffers(bufferCount_, width_, height_,
                                           toDXGIFormat(format_),
                                           0 /* DXGI_SWAP_CHAIN_FLAG_NONE */);
    if (FAILED(hr)) {
        // Fall back: full teardown + recreation.
        swapChain_.Reset();
        createSwapChainInternal();
    }

    createViewsAndWrappers();
}

RHIRenderPass* DX12RHISwapChain::getRHIRenderPass() const {
    return rhiRenderPass_.get();
}

RHIFramebuffer* DX12RHISwapChain::getRHIFramebuffer(uint32_t imageIndex) const {
    if (imageIndex >= rhiFramebuffers_.size()) {
        return nullptr;
    }
    return rhiFramebuffers_[imageIndex].get();
}
