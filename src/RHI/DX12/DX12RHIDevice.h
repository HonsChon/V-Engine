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

class DX12RHIBuffer;
class DX12RHITexture;
class DX12RHISampler;
class DX12RHIShader;
class DX12RHIBindingLayout;
class DX12RHIBindingGroup;
class DX12RHIPipeline;
class DX12RHIRenderPass;
class DX12RHIFramebuffer;
class DX12RHICommandBuffer;
class DX12RHISwapChain;

/// Vulkan-style semaphore/fence emulation over a monotonic ID3D12Fence value.
/// A fence can only move forward in D3D12: "reset" therefore bumps the target
/// value and "wait" blocks on that value via a Win32 event.
struct DX12FenceSync {
    ComPtr<ID3D12Fence> fence;
    uint64_t            value = 0;   // current target value
    HANDLE              event = nullptr;

    /// @param signaled  pre-signaled (waitForFence must not block).
    DX12FenceSync(ID3D12Device* d3dDevice, bool signaled) {
        d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        value = signaled ? 0 : 1;   // completed value starts at 0
    }
    ~DX12FenceSync() {
        if (event) { CloseHandle(event); event = nullptr; }
    }
};

class DX12RHIDevice : public RHIDevice
{
public:
    explicit DX12RHIDevice(GLFWwindow* window);
    ~DX12RHIDevice() override;

    // ---- RHIDevice factory methods ----
    std::shared_ptr<RHIBuffer>    createBuffer(const RHIBufferDesc& desc) override;
    std::shared_ptr<RHITexture>   createTexture(const RHITextureDesc& desc) override;
    std::shared_ptr<RHISampler>   createSampler(const RHISamplerDesc& desc) override;
    std::shared_ptr<RHIShader>    createShader(RHIShaderStage stage, const std::string& filePath) override;
    std::shared_ptr<RHIBindingLayout> createBindingLayout(const RHIBindingLayoutDesc& desc) override;
    std::shared_ptr<RHIBindingGroup>  createBindingGroup(RHIBindingLayout* layout,
                                                          const RHIBindingGroupDesc& desc) override;
    std::shared_ptr<RHIBindingGroup>  allocateBindingGroup(RHIBindingLayout* layout) override;

    std::shared_ptr<RHIGraphicsPipelineBuilder> createGraphicsPipelineBuilder() override;
    std::shared_ptr<RHIComputePipelineBuilder>  createComputePipelineBuilder() override;

    std::shared_ptr<RHIRenderPass>  createRenderPass(const RHIRenderPassDesc& desc) override;
    std::shared_ptr<RHIFramebuffer> createFramebuffer(const RHIFramebufferDesc& desc) override;

    std::shared_ptr<RHISwapChain> createSwapChain(const RHISwapChainDesc& desc) override;

    // ---- Wrap external (native) handles ----
    std::shared_ptr<RHIRenderPass>    wrapExternalRenderPass(void* nativeHandle) override;
    std::shared_ptr<RHIBuffer>        wrapExternalBuffer(void* nativeBuffer, uint64_t size) override;
    std::shared_ptr<RHITexture>       wrapExternalTexture(void* nativeImage, void* nativeImageView,
                                                           uint32_t width, uint32_t height,
                                                           RHIFormat format) override;
    std::shared_ptr<RHISampler>       wrapExternalSampler(void* nativeSampler) override;
    std::shared_ptr<RHICommandBuffer> wrapCommandBuffer(void* nativeCmd) override;

    // ---- Queue operations ----
    void waitIdle() override;

    // ---- Memory / format helpers ----
    uint32_t findMemoryType(uint32_t typeFilter, uint32_t propertyFlags) override;
    RHIFormat findSupportedFormat(const std::vector<RHIFormat>& candidates,
                                  uint32_t tiling, uint32_t features) override;
    RHIFormat findDepthFormat() override;

    // ---- Raw buffer/image interop (legacy, engine currently never calls) ----
    void createRawBuffer(uint64_t size, uint32_t usage, uint32_t memoryProperties,
                         void* outBuffer, void* outMemory) override;
    void createRawImage(uint32_t width, uint32_t height, RHIFormat format,
                        uint32_t tiling, uint32_t usage, uint32_t memoryProperties,
                        void* outImage, void* outMemory) override;
    void copyBuffer(void* srcBuffer, void* dstBuffer, uint64_t size) override;

    // ---- Single-time command helpers ----
    void* beginSingleTimeCommands() override;
    void  endSingleTimeCommands(void* commandBuffer) override;

    // ---- Debug labels (WinPixEventRuntime loaded on demand, no link dep) ----
    void beginDebugLabel(void* commandBuffer, const char* name,
                         float r, float g, float b, float a) override;
    void endDebugLabel(void* commandBuffer) override;
    void insertDebugLabel(void* commandBuffer, const char* name,
                          float r, float g, float b, float a) override;

    // ---- Sync objects (DX12FenceSync emulation) ----
    void* createSemaphore() override;
    void* createFence(bool signaled) override;
    void  destroySemaphore(void* semaphore) override;
    void  destroyFence(void* fence) override;
    void  waitForFence(void* fence) override;
    void  resetFence(void* fence) override;

    // ---- Command buffer allocation ----
    std::vector<void*> allocateCommandBuffers(uint32_t count) override;

    // ---- Queue submission ----
    void submitGraphicsQueue(const std::vector<void*>& waitSemaphores,
                             const std::vector<void*>& commandBuffers,
                             const std::vector<void*>& signalSemaphores,
                             void* fence) override;

    // ---- Native accessors ----
    RHIBackend getBackend() const override { return RHIBackend::DX12; }
    void*    getNativeDevice() const override          { return device.Get(); }
    void*    getNativeInstance() const override        { return nullptr; }   // DX12 has no instance
    void*    getNativePhysicalDevice() const override  { return adapter.Get(); }
    uint32_t getGraphicsQueueFamilyIndex() const override { return 0; }     // single queue
    void*    getNativeGraphicsQueue() const override   { return commandQueue.Get(); }
    void*    getNativeCommandPool() const override     { return nullptr; }

    // ---- DX12 accessors ----
    ID3D12Device* getDevice() const { return device.Get(); }
    IDXGIFactory4* getFactory() const { return factory.Get(); }
    ID3D12CommandQueue* getCommandQueue() const { return commandQueue.Get(); }
    GLFWwindow* getWindow() const { return window; }

    // ---- Descriptor ring allocation (record-time) ----
    ID3D12DescriptorHeap* getShaderVisibleResourceHeap() const { return resourceRing_.heap.Get(); }
    ID3D12DescriptorHeap* getShaderVisibleSamplerHeap() const  { return samplerRing_.heap.Get(); }

    bool allocateResourceDescriptors(UINT count,
                                     D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                     D3D12_GPU_DESCRIPTOR_HANDLE* gpu);
    bool allocateSamplerDescriptors(UINT count,
                                    D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                    D3D12_GPU_DESCRIPTOR_HANDLE* gpu);
    bool allocateRTVDescriptors(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE* cpu);
    bool allocateDSVDescriptors(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE* cpu);

    UINT getDescriptorIncrement(D3D12_DESCRIPTOR_HEAP_TYPE type) const {
        return device->GetDescriptorHandleIncrementSize(type);
    }

    // ---- Command list pool (drives Reset/Close from the demo / Engine loop) ----
    ID3D12CommandAllocator* getCommandAllocator(uint32_t index) const;
    ID3D12GraphicsCommandList* getCommandList(uint32_t index) const;
    /// Reset the per-frame allocator + command list (equivalent of vkResetCommandBuffer
    /// + vkBeginCommandBuffer; the list is left in the recording state).
    void resetCommandBuffer(uint32_t index);
    /// Same as above, addressed by list pointer (used by RHICommandBuffer::begin).
    void resetCommandBuffer(ID3D12GraphicsCommandList* cmdList);

    // ---- Lazily-created scaled-blit pipeline (vkCmdBlit parity) ----
    std::shared_ptr<DX12RHIPipeline> getOrCreateBlitPipeline();
    /// Fullscreen triangle that writes a constant color (used by clearColorImage,
    /// which avoids the ALLOW_UNORDERED_ACCESS requirement of UAV clears).
    std::shared_ptr<DX12RHIPipeline> getOrCreateClearPipeline();

    /// Mark the current open descriptor batch as submitted at the next fence value.
    /// Called from submitGraphicsQueue.
    void finalizeDescriptorBatch();

private:
    friend class DX12RHISwapChain;
    friend class DX12RHIBuffer;
    friend class DX12RHITexture;

    struct RingHeap {
        ComPtr<ID3D12DescriptorHeap> heap;
        UINT   descriptorSize = 0;
        UINT   total = 0;          // total descriptors
        UINT   cursor = 0;
        UINT   segmentDescriptors = 0;
        bool   shaderVisible = false;
        // Per segment: fence value of the submit that last used it (0 = never).
        std::vector<uint64_t> segmentSignal;
    };

    struct CommandListPair {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
    };

    void createDevice();
    void createCommandQueue();
    void createDescriptorRings();
    void createSingleTimeCommandObjects();
    static void GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter);

    // ---- fence/event plumbing ----
    void waitForGPU();                       // queue idle
    void waitForFenceSync(DX12FenceSync* sync, uint64_t targetValue);

    // ---- descriptor ring plumbing ----
    void ringWaitForSegment(const RingHeap& ring, UINT segment);
    void ringFlushAll(const RingHeap& ring);
    bool ringAllocate(RingHeap& ring, UINT count,
                      D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                      D3D12_GPU_DESCRIPTOR_HANDLE* gpu);

    GLFWwindow* window = nullptr;

    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> commandQueue;

    // Idle fence (waitForGPU / waitIdle).
    ComPtr<ID3D12Fence> fence;
    uint64_t            fenceValue = 0;
    HANDLE              fenceEvent = nullptr;

    // Fence that gates descriptor-ring segment reuse.
    ComPtr<ID3D12Fence> ringFence;
    uint64_t            ringFenceValue = 0;
    HANDLE              ringFenceEvent = nullptr;
    std::set<uint32_t>  batchSegments;    // segments touched by the open batch

    RingHeap resourceRing_;   // CBV/SRV/UAV, shader-visible (4 x 2048)
    RingHeap samplerRing_;    // SAMPLER,   shader-visible (4 x 128)
    RingHeap rtvRing_;        // RTV,       CPU-only      (4 x 64)
    RingHeap dsvRing_;        // DSV,       CPU-only      (4 x 32)

    std::vector<CommandListPair> commandLists_;

    ComPtr<ID3D12CommandAllocator> singleTimeAllocator_;
    ComPtr<ID3D12GraphicsCommandList> singleTimeList_;

    std::shared_ptr<DX12RHIPipeline> blitPipeline_;
    std::shared_ptr<DX12RHIPipeline> clearPipeline_;

    // WinPixEventRuntime (optional, loaded dynamically).
    bool pixLoaded_ = false;

#ifdef NDEBUG
    static constexpr bool enableValidationLayers_ = false;
#else
    static constexpr bool enableValidationLayers_ = true;
#endif
};
