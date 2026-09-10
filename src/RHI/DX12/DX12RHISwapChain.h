#pragma once

#include "RHISwapChain.h"
#include "RHIRenderPass.h"

#include <directx/d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <memory>
#include <vector>

using Microsoft::WRL::ComPtr;

class DX12RHIDevice;
class DX12RHITexture;
class DX12RHIFramebuffer;
class DX12RHIRenderPass;
struct DX12FenceSync;

class DX12RHISwapChain : public RHISwapChain
{
public:
    DX12RHISwapChain(DX12RHIDevice* device, const RHISwapChainDesc& desc);
    ~DX12RHISwapChain() override;

    // ---- Query ----
    RHIFormat   getFormat() const override    { return format_; }
    RHIExtent2D getExtent() const override    { return { width_, height_ }; }
    uint32_t    getImageCount() const override { return bufferCount_; }

    // ---- Frame lifecycle ----
    RHISwapChainResult acquireNextImage(void* signalSemaphore, uint32_t* outImageIndex) override;
    RHISwapChainResult present(void* waitSemaphore, uint32_t imageIndex) override;
    void recreate(uint32_t width, uint32_t height) override;

    // ---- RHI accessors ----
    RHIRenderPass*   getRHIRenderPass() const override;
    RHIFramebuffer*  getRHIFramebuffer(uint32_t imageIndex) const override;

    // ---- Native handle access ----
    void* getNativeRenderPass() const override    { return nullptr; }  // DX12 has no render-pass object
    void* getNativeFramebuffer(uint32_t imageIndex) const override { (void)imageIndex; return nullptr; }

    // ---- DX12 accessors ----
    IDXGISwapChain3* getD3D12SwapChain() const { return swapChain_.Get(); }

private:
    void createSwapChainInternal();
    void createViewsAndWrappers();
    void destroyViewsAndWrappers();
    RHISwapChainResult mapPresentResult(HRESULT hr) const;

    DX12RHIDevice*      device_;
    RHISwapChainDesc    desc_;

    ComPtr<IDXGISwapChain3> swapChain_;
    uint32_t                bufferCount_ = 2;
    uint32_t                width_  = 0;
    uint32_t                height_ = 0;
    RHIFormat               format_ = RHIFormat::R8G8B8A8_UNORM;

    // Back-buffer views.
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles_;
    std::vector<std::shared_ptr<DX12RHITexture>> backBufferTextures_;

    // Internal depth buffer (D32) + DSV.
    std::shared_ptr<DX12RHITexture> depthTexture_;
    ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_ = {};

    // Per-image fence: image i may not be re-acquired until its fence value
    // (the present of the previous frame using it) has been reached.
    std::vector<std::unique_ptr<DX12FenceSync>> imageFences_;

    std::shared_ptr<DX12RHIRenderPass>    rhiRenderPass_;
    std::vector<std::shared_ptr<DX12RHIFramebuffer>> rhiFramebuffers_;
};
