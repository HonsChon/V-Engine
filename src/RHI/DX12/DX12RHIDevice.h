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

    std::shared_ptr<RHIShader>               createShader(RHIShaderStage stage, const std::string& filePath) override;
    std::shared_ptr<RHIBindingLayout>        createBindingLayout(const RHIBindingLayoutDesc& desc) override;

    std::shared_ptr<RHIGraphicsPipelineBuilder> createGraphicsPipelineBuilder() override;
    std::shared_ptr<RHIComputePipelineBuilder>  createComputePipelineBuilder() override;

    // ---- Accessors ----
    ID3D12Device* getDevice() const { return device.Get(); }
    IDXGIFactory4* getFactory() const { return factory.Get(); }
    ID3D12CommandQueue* getCommandQueue() const { return commandQueue.Get(); }
    GLFWwindow* getWindow() const { return window; }

private:
    void createDevice();

    /// @brief Create a command queue for the device.
    void createCommandQueue();

    /// @brief create a fence event handle
    void createFenceEvent();

    /// @brief Block until all GPU work on commandQueue has completed (equivalent to vkDeviceWaitIdle).
    void waitForGPU();

    /// @brief Check if the validation layers are supported.
    bool checkValidationLayerSupport();

    /// @brief Enable the debug layer for the device.
    bool enableDebugLayer();

    /// @brief Enumerate hardware adapters and find the first one that supports D3D12.
    static void GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter);

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