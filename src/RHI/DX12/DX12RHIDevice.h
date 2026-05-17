#pragma once

#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"
#include "RHIShader.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIRenderPass.h"
#include "RHISwapChain.h"
#include "RHICommandBuffer.h"

#include <directx/d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <GLFW/glfw3.h>

#include <vector>
#include <memory>
#include <optional>
#include <set>
#include <string>

using Microsoft::WRL::ComPtr;

class DX12RHIDevice : public RHIDevice{
public:
    explicit DX12RHIDevice(GLFWwindow* window);
    ~DX12RHIDevice() override;

    // ---- RHIDevice factory methods ----
    std::shared_ptr<RHIBuffer> createBuffer(const RHIBufferDesc& desc) override;

private:
    void createDevice();

    /// @brief Create a command queue for the device.
    void createCommandQueue();

    /// @brief create a fence event handle
    void createFenceEvent();

    /// @brief Block until all GPU work on commandQueue has completed (equivalent to vkDeviceWaitIdle).
    void waitForGPU();

    bool checkValidationLayerSupport();
    bool enableDebugLayer();

    GLFWwindow* window = nullptr;

    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> commandQueue;

    // Fence for GPU synchronization
    ComPtr<ID3D12Fence> fence;
    uint64_t            fenceValue = 0;
    HANDLE              fenceEvent = nullptr;

#ifdef NDEBUG
    static constexpr bool enableValidationLayers_ = false;
#else
    static constexpr bool enableValidationLayers_ = true;
#endif
};