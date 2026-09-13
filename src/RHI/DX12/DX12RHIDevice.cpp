#include "DX12RHIDevice.h"
#include "DX12RHIBuffer.h"
#include "DX12RHITexture.h"
#include "DX12RHISampler.h"
#include "DX12RHIShader.h"
#include "DX12RHIDescriptor.h"
#include "DX12RHIPipeline.h"
#include "DX12RHIRenderPass.h"
#include "DX12RHIFramebuffer.h"
#include "DX12RHICommandBuffer.h"
#include "DX12RHISwapChain.h"
#include "DX12TypeConversions.h"

#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <windows.h>

using namespace DX12TypeConversions;

namespace {

constexpr UINT kRingSegments          = 4;
constexpr UINT kResourceRingTotal     = 8192;   // 4 x 2048 CBV/SRV/UAV
constexpr UINT kSamplerRingTotal      = 512;    // 4 x 128 SAMPLER
constexpr UINT kRtvRingTotal          = 256;    // 4 x 64  RTV (CPU only)
constexpr UINT kDsvRingTotal          = 128;    // 4 x 32  DSV (CPU only)
constexpr UINT kCpuUavRingTotal       = 64;     // 4 x 16  CBV/SRV/UAV, CPU only (UAV clears)

// WinPixEventRuntime markers (loaded on demand; absent -> debug labels are no-ops).
using PixBeginFn = void(__stdcall*)(ID3D12GraphicsCommandList*, UINT64, const char*);
using PixEndFn   = void(__stdcall*)(ID3D12GraphicsCommandList*);
PixBeginFn g_pixBegin = nullptr;
PixEndFn   g_pixEnd   = nullptr;

void ensurePixLoaded() {
    if (g_pixBegin || g_pixEnd) {
        return;
    }
    HMODULE mod = LoadLibraryA("WinPixEventRuntime.dll");
    if (mod) {
        g_pixBegin = reinterpret_cast<PixBeginFn>(GetProcAddress(mod, "PIXBeginEventOnCommandList"));
        g_pixEnd   = reinterpret_cast<PixEndFn>(GetProcAddress(mod, "PIXEndEventOnCommandList"));
    }
}

} // namespace

DX12RHIDevice::DX12RHIDevice(GLFWwindow* window)
    : window(window)
{
    createDevice();
    createCommandQueue();
    createDescriptorRings();
    createSingleTimeCommandObjects();
    ensurePixLoaded();

    std::cout << "[DX12RHIDevice] Initialized successfully.\n";
}

DX12RHIDevice::~DX12RHIDevice()
{
    waitForGPU();

    if (fenceEvent) {
        CloseHandle(fenceEvent);
        fenceEvent = nullptr;
    }
    if (ringFenceEvent) {
        CloseHandle(ringFenceEvent);
        ringFenceEvent = nullptr;
    }

    std::cout << "[DX12RHIDevice] Destroyed.\n";
}

// =============================================================================
// Device bootstrap
// =============================================================================

void DX12RHIDevice::GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter)
{
    *ppAdapter = nullptr;
    ComPtr<IDXGIAdapter1> adapter;

    for (UINT adapterIndex = 0;
         pFactory->EnumAdapters1(adapterIndex, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND;
         ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter.Reset();
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                        __uuidof(ID3D12Device), nullptr))) {
            break;
        }
        adapter.Reset();
    }

    *ppAdapter = adapter.Detach();
}

void DX12RHIDevice::createDevice()
{
#if defined(_DEBUG)
    if (enableValidationLayers_) {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
        }
    }
#endif

    UINT factoryFlags = 0;
#if defined(_DEBUG)
    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)))) {
        throw std::runtime_error("failed to create DXGI factory!");
    }

    GetHardwareAdapter(factory.Get(), &adapter);
    if (!adapter) {
        throw std::runtime_error("no D3D12 hardware adapter found!");
    }
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
        throw std::runtime_error("failed to create D3D12 device!");
    }
#if defined(_DEBUG)
    device.As(&infoQueue_);
#endif
}

void DX12RHIDevice::createCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)))) {
        throw std::runtime_error("failed to create command queue!");
    }

    // Idle fence.
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        throw std::runtime_error("failed to create fence!");
    }
    fenceValue = 0;
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent) {
        throw std::runtime_error("failed to create fence event!");
    }

    // Descriptor-ring gating fence.
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ringFence)))) {
        throw std::runtime_error("failed to create ring fence!");
    }
    ringFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!ringFenceEvent) {
        throw std::runtime_error("failed to create ring fence event!");
    }
}

void DX12RHIDevice::createDescriptorRings() {
    auto createHeap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool shaderVisible,
                          RingHeap& out) {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = type;
        desc.NumDescriptors = count;
        desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                   : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&out.heap)))) {
            throw std::runtime_error("failed to create descriptor ring heap");
        }
        out.descriptorSize = device->GetDescriptorHandleIncrementSize(type);
        out.total = count;
        out.cursor = 0;
        out.segmentDescriptors = count / kRingSegments;
        out.shaderVisible = shaderVisible;
        out.segmentSignal.assign(kRingSegments, 0);
    };

    createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kResourceRingTotal, true, resourceRing_);
    createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,      kSamplerRingTotal,  true, samplerRing_);
    createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,          kRtvRingTotal,      false, rtvRing_);
    createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,          kDsvRingTotal,      false, dsvRing_);
    createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kCpuUavRingTotal,    false, cpuUavRing_);
}

void DX12RHIDevice::createSingleTimeCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&singleTimeAllocator_)))) {
        throw std::runtime_error("failed to create single-time command allocator");
    }
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         singleTimeAllocator_.Get(), nullptr,
                                         IID_PPV_ARGS(&singleTimeList_)))) {
        throw std::runtime_error("failed to create single-time command list");
    }
    singleTimeList_->Close();
}

// =============================================================================
// Fence / ring plumbing
// =============================================================================

void DX12RHIDevice::waitForGPU() {
    if (!commandQueue || !fence) {
        return;
    }
    const uint64_t waitValue = ++fenceValue;
    commandQueue->Signal(fence.Get(), waitValue);

    if (fence->GetCompletedValue() < waitValue) {
        fence->SetEventOnCompletion(waitValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    reportValidationMessages();
}

void DX12RHIDevice::reportValidationMessages() {
#if defined(_DEBUG)
    if (!infoQueue_) {
        return;
    }
    const UINT64 total = infoQueue_->GetNumStoredMessages();
    if (total <= validationReported_) {
        return;
    }
    for (UINT64 i = validationReported_; i < total; ++i) {
        SIZE_T size = 0;
        if (FAILED(infoQueue_->GetMessage(i, nullptr, &size)) || size == 0) {
            continue;
        }
        std::vector<uint8_t> buffer(size);
        D3D12_MESSAGE* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
        if (FAILED(infoQueue_->GetMessage(i, message, &size))) {
            continue;
        }
        const char* severity = "INFO";
        if (message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) severity = "CORRUPTION";
        else if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR) severity = "ERROR";
        else if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING) severity = "WARNING";
        std::fprintf(stderr, "[D3D12 validation] %s: %s\n", severity,
                     message->pDescription ? message->pDescription : "(no description)");
    }
    validationReported_ = total;
#endif
}

void DX12RHIDevice::waitForFenceSync(DX12FenceSync* sync, uint64_t targetValue) {
    if (!sync || !sync->fence) {
        return;
    }
    if (sync->fence->GetCompletedValue() >= targetValue) {
        return;
    }
    sync->fence->SetEventOnCompletion(targetValue, sync->event);
    WaitForSingleObject(sync->event, INFINITE);
}

// =============================================================================
// Descriptor ring allocation
// =============================================================================

void DX12RHIDevice::ringWaitForSegment(const RingHeap& ring, UINT segment) {
    const uint64_t signal = ring.segmentSignal[segment];
    if (signal == 0) {
        return;   // never used yet
    }
    if (ringFence->GetCompletedValue() < signal) {
        ringFence->SetEventOnCompletion(signal, ringFenceEvent);
        WaitForSingleObject(ringFenceEvent, INFINITE);
    }
}

void DX12RHIDevice::ringFlushAll(const RingHeap& ring) {
    for (UINT i = 0; i < kRingSegments; ++i) {
        ringWaitForSegment(ring, i);
    }
}

bool DX12RHIDevice::ringAllocate(RingHeap& ring, UINT count, bool persistent,
                                 D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                 D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    if (count == 0) {
        if (cpu) *cpu = {};
        if (gpu) *gpu = {};
        return true;
    }
    if (count > ring.total) {
        return false;   // single allocation larger than the whole ring
    }

    // Descriptor blocks must be contiguous: wrap around by draining the ring
    // (all outstanding batches have completed by then). The wrap target is the
    // persistent watermark so descriptors allocated for binding groups (which
    // live for the app lifetime) are never overwritten by transient ones.
    if (ring.cursor + count > ring.total) {
#ifdef _DEBUG
        std::fprintf(stderr,
                     "[DX12RHIDevice] descriptor ring wrap (heap=%p persistentEnd=%u total=%u)\n",
                     static_cast<void*>(ring.heap.Get()), ring.persistentEnd, ring.total);
#endif
        ringFlushAll(ring);
        ring.cursor = ring.persistentEnd;
        if (ring.cursor + count > ring.total) {
            return false;   // persistent allocations consumed the whole ring
        }
    }

    const UINT begin = ring.cursor;
    const UINT end = begin + count;

    // Gate the segments we are about to overwrite on their last submit.
    for (UINT seg = begin / ring.segmentDescriptors;
         seg <= (end - 1) / ring.segmentDescriptors; ++seg) {
        ringWaitForSegment(ring, seg);
        batchSegments.insert(seg);
    }

    if (cpu) {
        *cpu = ring.heap->GetCPUDescriptorHandleForHeapStart();
        cpu->ptr += static_cast<SIZE_T>(begin) * ring.descriptorSize;
    }
    if (gpu) {
        *gpu = ring.heap->GetGPUDescriptorHandleForHeapStart();
        gpu->ptr += static_cast<SIZE_T>(begin) * ring.descriptorSize;
    }

    ring.cursor = end;
    if (persistent) {
        ring.persistentEnd = std::max(ring.persistentEnd, end);
    }
    return true;
}

bool DX12RHIDevice::allocateResourceDescriptors(UINT count,
                                                D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                                D3D12_GPU_DESCRIPTOR_HANDLE* gpu,
                                                bool persistent) {
    return ringAllocate(resourceRing_, count, persistent, cpu, gpu);
}

bool DX12RHIDevice::allocateSamplerDescriptors(UINT count,
                                               D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                               D3D12_GPU_DESCRIPTOR_HANDLE* gpu,
                                               bool persistent) {
    return ringAllocate(samplerRing_, count, persistent, cpu, gpu);
}

bool DX12RHIDevice::allocateRTVDescriptors(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE* cpu) {
    return ringAllocate(rtvRing_, count, false, cpu, nullptr);
}

bool DX12RHIDevice::allocateDSVDescriptors(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE* cpu) {
    return ringAllocate(dsvRing_, count, false, cpu, nullptr);
}

bool DX12RHIDevice::allocateCpuUAVDescriptors(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE* cpu) {
    return ringAllocate(cpuUavRing_, count, false, cpu, nullptr);
}

void DX12RHIDevice::finalizeDescriptorBatch() {
    if (batchSegments.empty()) {
        return;
    }
    // Batches are closed by the submit that follows them. Each touched segment
    // is guarded by the fence value signaled by that submit.
    const uint64_t value = ++ringFenceValue;
    commandQueue->Signal(ringFence.Get(), value);
    for (uint32_t seg : batchSegments) {
        if (seg < resourceRing_.segmentSignal.size())  resourceRing_.segmentSignal[seg] = value;
        if (seg < samplerRing_.segmentSignal.size())   samplerRing_.segmentSignal[seg] = value;
        if (seg < rtvRing_.segmentSignal.size())       rtvRing_.segmentSignal[seg] = value;
        if (seg < dsvRing_.segmentSignal.size())       dsvRing_.segmentSignal[seg] = value;
        if (seg < cpuUavRing_.segmentSignal.size())    cpuUavRing_.segmentSignal[seg] = value;
    }
    batchSegments.clear();
}

// =============================================================================
// Resource factories
// =============================================================================

std::shared_ptr<RHIBuffer> DX12RHIDevice::createBuffer(const RHIBufferDesc& desc)
{
    return std::make_shared<DX12RHIBuffer>(this, desc);
}

std::shared_ptr<RHITexture> DX12RHIDevice::createTexture(const RHITextureDesc& desc)
{
    RHITextureDesc resolved = desc;
    resolveMipLevels(resolved);
    return std::make_shared<DX12RHITexture>(this, resolved);
}

std::shared_ptr<RHISampler> DX12RHIDevice::createSampler(const RHISamplerDesc& desc)
{
    return std::make_shared<DX12RHISampler>(this, desc);
}

std::shared_ptr<RHIShader> DX12RHIDevice::createShader(RHIShaderStage stage, const std::string& filePath)
{
    return std::make_shared<DX12RHIShader>(this, stage, filePath);
}

std::shared_ptr<RHIBindingLayout> DX12RHIDevice::createBindingLayout(const RHIBindingLayoutDesc& desc)
{
    return std::make_shared<DX12RHIBindingLayout>(this, desc);
}

std::shared_ptr<RHIBindingGroup> DX12RHIDevice::createBindingGroup(RHIBindingLayout* layout,
                                                                   const RHIBindingGroupDesc& desc)
{
    return std::make_shared<DX12RHIBindingGroup>(this, layout, desc);
}

std::shared_ptr<RHIBindingGroup> DX12RHIDevice::allocateBindingGroup(RHIBindingLayout* layout)
{
    return std::make_shared<DX12RHIBindingGroup>(this, layout, RHIBindingGroupDesc{});
}

std::shared_ptr<RHIGraphicsPipelineBuilder> DX12RHIDevice::createGraphicsPipelineBuilder()
{
    return std::make_shared<DX12GraphicsPipelineBuilder>(this);
}

std::shared_ptr<RHIComputePipelineBuilder> DX12RHIDevice::createComputePipelineBuilder()
{
    return std::make_shared<DX12ComputePipelineBuilder>(this);
}

std::shared_ptr<RHIRenderPass> DX12RHIDevice::createRenderPass(const RHIRenderPassDesc& desc)
{
    return std::make_shared<DX12RHIRenderPass>(desc);
}

std::shared_ptr<RHIFramebuffer> DX12RHIDevice::createFramebuffer(const RHIFramebufferDesc& desc)
{
    return std::make_shared<DX12RHIFramebuffer>(this, desc);
}

std::shared_ptr<RHISwapChain> DX12RHIDevice::createSwapChain(const RHISwapChainDesc& desc)
{
    return std::make_shared<DX12RHISwapChain>(this, desc);
}

// =============================================================================
// External wrapping (engine currently never calls these on DX12)
// =============================================================================

std::shared_ptr<RHIRenderPass> DX12RHIDevice::wrapExternalRenderPass(void* /*nativeHandle*/)
{
    // A Vulkan VkRenderPass has no DX12 counterpart; produce a descriptive pass.
    RHIRenderPassDesc desc;
    desc.addColorAttachment(RHIFormat::R8G8B8A8_UNORM, RHILoadOp::Clear, RHIStoreOp::Store,
                            RHIImageLayout::Undefined, RHIImageLayout::ShaderReadOnly);
    return std::make_shared<DX12RHIRenderPass>(desc);
}

std::shared_ptr<RHIBuffer> DX12RHIDevice::wrapExternalBuffer(void* nativeBuffer, uint64_t size)
{
    auto* resource = static_cast<ID3D12Resource*>(nativeBuffer);
    ComPtr<ID3D12Resource> ref(resource);
    RHIBufferDesc desc;
    desc.size = size;
    desc.usage = RHIBufferUsage::None;
    desc.memoryUsage = RHIMemoryUsage::GPUOnly;
    return std::make_shared<DX12RHIBuffer>(this, ref, size, desc, D3D12_RESOURCE_STATE_COMMON);
}

std::shared_ptr<RHITexture> DX12RHIDevice::wrapExternalTexture(void* nativeImage, void* nativeImageView,
                                                               uint32_t width, uint32_t height,
                                                               RHIFormat format)
{
    (void)nativeImageView;
    auto* resource = static_cast<ID3D12Resource*>(nativeImage);
    ComPtr<ID3D12Resource> ref(resource);
    RHITextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.usage = RHITextureUsage::Sampled | RHITextureUsage::ColorAttachment;
    return std::make_shared<DX12RHITexture>(this, ref, desc, D3D12_RESOURCE_STATE_COMMON);
}

std::shared_ptr<RHISampler> DX12RHIDevice::wrapExternalSampler(void* nativeSampler)
{
    (void)nativeSampler;
    return std::make_shared<DX12RHISampler>(this);
}

std::shared_ptr<RHICommandBuffer> DX12RHIDevice::wrapCommandBuffer(void* nativeCmd)
{
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(nativeCmd);
    return std::make_shared<DX12RHICommandBuffer>(this, cmdList);
}

// =============================================================================
// Queue / helpers
// =============================================================================

void DX12RHIDevice::waitIdle()
{
    waitForGPU();
}

uint32_t DX12RHIDevice::findMemoryType(uint32_t /*typeFilter*/, uint32_t /*propertyFlags*/)
{
    // DX12 heap selection is declarative; nothing to search. Engine never calls this.
    return 0;
}

RHIFormat DX12RHIDevice::findSupportedFormat(const std::vector<RHIFormat>& candidates,
                                             uint32_t /*tiling*/, uint32_t /*features*/)
{
    for (RHIFormat format : candidates) {
        if (format == RHIFormat::Undefined) {
            continue;
        }
        DXGI_FORMAT dxgi = toDXGIFormat(format);
        if (dxgi == DXGI_FORMAT_UNKNOWN) {
            continue;
        }
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
        support.Format = dxgi;
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support,
                                                  sizeof(support)))) {
            return format;
        }
    }
    throw std::runtime_error("[DX12RHIDevice] no supported format found");
}

RHIFormat DX12RHIDevice::findDepthFormat()
{
    return RHIFormat::D32_SFLOAT;   // universally supported at FL 12_0
}

void DX12RHIDevice::createRawBuffer(uint64_t size, uint32_t usage, uint32_t memoryProperties,
                                    void* outBuffer, void* outMemory)
{
    (void)memoryProperties;
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = size;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    // VkBufferUsageFlagBits subset: STORAGE_BUFFER_BIT = 0x20 (indirect/vertex/index
    // usage need no resource flag).
    if (usage & 0x20) {
        resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    ComPtr<ID3D12Resource> resource;
    if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr,
                                               IID_PPV_ARGS(&resource)))) {
        throw std::runtime_error("[DX12RHIDevice] createRawBuffer failed");
    }
    *static_cast<ID3D12Resource**>(outBuffer) = resource.Detach();
    if (outMemory) {
        *static_cast<void**>(outMemory) = nullptr;   // committed resources have no separate heap object
    }
}

void DX12RHIDevice::createRawImage(uint32_t width, uint32_t height, RHIFormat format,
                                   uint32_t /*tiling*/, uint32_t usage, uint32_t memoryProperties,
                                   void* outImage, void* outMemory)
{
    (void)memoryProperties;
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = width;
    resDesc.Height = height;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = toDXGIFormat(format);
    resDesc.SampleDesc.Count = 1;
    // VkImageUsageFlagBits subset: STORAGE_BIT = 0x8, COLOR_ATTACHMENT_BIT = 0x10,
    // DEPTH_STENCIL_ATTACHMENT_BIT = 0x20, SAMPLED_BIT = 0x1.
    if (usage & 0x8) {
        resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    if (usage & 0x10) {
        resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (usage & 0x20) {
        resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }

    ComPtr<ID3D12Resource> resource;
    if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr,
                                               IID_PPV_ARGS(&resource)))) {
        throw std::runtime_error("[DX12RHIDevice] createRawImage failed");
    }
    *static_cast<ID3D12Resource**>(outImage) = resource.Detach();
    if (outMemory) {
        *static_cast<void**>(outMemory) = nullptr;
    }
}

void DX12RHIDevice::copyBuffer(void* srcBuffer, void* dstBuffer, uint64_t size)
{
    auto* src = static_cast<ID3D12Resource*>(srcBuffer);
    auto* dst = static_cast<ID3D12Resource*>(dstBuffer);
    ID3D12GraphicsCommandList* cmd = static_cast<ID3D12GraphicsCommandList*>(
        beginSingleTimeCommands());
    cmd->CopyBufferRegion(dst, 0, src, 0, size);
    endSingleTimeCommands(cmd);
}

void* DX12RHIDevice::beginSingleTimeCommands()
{
    singleTimeAllocator_->Reset();
    singleTimeList_->Reset(singleTimeAllocator_.Get(), nullptr);
    return singleTimeList_.Get();
}

void DX12RHIDevice::endSingleTimeCommands(void* /*commandBuffer*/)
{
    singleTimeList_->Close();
    ID3D12CommandList* lists[] = { singleTimeList_.Get() };
    commandQueue->ExecuteCommandLists(1, lists);
    waitForGPU();   // equivalent of vkQueueWaitIdle inside the Vulkan helper
}

// =============================================================================
// Debug labels (PIX, loaded dynamically)
// =============================================================================

void DX12RHIDevice::beginDebugLabel(void* commandBuffer, const char* name,
                                    float r, float g, float b, float a)
{
    (void)r; (void)g; (void)b; (void)a;
    auto* list = static_cast<ID3D12GraphicsCommandList*>(commandBuffer);
    if (g_pixBegin) {
        g_pixBegin(list, 0, name);
    }
}

void DX12RHIDevice::endDebugLabel(void* commandBuffer)
{
    auto* list = static_cast<ID3D12GraphicsCommandList*>(commandBuffer);
    if (g_pixEnd) {
        g_pixEnd(list);
    }
}

void DX12RHIDevice::insertDebugLabel(void* commandBuffer, const char* name,
                                     float r, float g, float b, float a)
{
    (void)r; (void)g; (void)b; (void)a;
    beginDebugLabel(commandBuffer, name, 0, 0, 0, 0);
    endDebugLabel(commandBuffer);
}

// =============================================================================
// Sync objects
// =============================================================================

void* DX12RHIDevice::createSemaphore()
{
    auto* sync = new DX12FenceSync(device.Get(), /*signaled=*/true);
    return sync;
}

void* DX12RHIDevice::createFence(bool signaled)
{
    auto* sync = new DX12FenceSync(device.Get(), signaled);
    return sync;
}

void DX12RHIDevice::destroySemaphore(void* semaphore)
{
    delete static_cast<DX12FenceSync*>(semaphore);
}

void DX12RHIDevice::destroyFence(void* fenceHandle)
{
    delete static_cast<DX12FenceSync*>(fenceHandle);
}

void DX12RHIDevice::waitForFence(void* handle)
{
    auto* sync = static_cast<DX12FenceSync*>(handle);
    if (!sync) {
        return;
    }
    waitForFenceSync(sync, sync->value);
}

void DX12RHIDevice::resetFence(void* handle)
{
    auto* sync = static_cast<DX12FenceSync*>(handle);
    if (sync) {
        ++sync->value;
    }
}

// =============================================================================
// Command buffer allocation / submission
// =============================================================================

std::vector<void*> DX12RHIDevice::allocateCommandBuffers(uint32_t count)
{
    std::vector<void*> handles;
    handles.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        CommandListPair pair;
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&pair.allocator)))) {
            throw std::runtime_error("[DX12RHIDevice] failed to create command allocator");
        }
        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             pair.allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&pair.list)))) {
            throw std::runtime_error("[DX12RHIDevice] failed to create command list");
        }
        pair.list->Close();   // starts in the closed state; resetCommandBuffer reopens it
        handles.push_back(pair.list.Get());
        commandLists_.push_back(std::move(pair));
    }
    return handles;
}

ID3D12CommandAllocator* DX12RHIDevice::getCommandAllocator(uint32_t index) const {
    return (index < commandLists_.size()) ? commandLists_[index].allocator.Get() : nullptr;
}

ID3D12GraphicsCommandList* DX12RHIDevice::getCommandList(uint32_t index) const {
    return (index < commandLists_.size()) ? commandLists_[index].list.Get() : nullptr;
}

void DX12RHIDevice::resetCommandBuffer(uint32_t index) {
    if (index >= commandLists_.size()) {
        throw std::runtime_error("[DX12RHIDevice] resetCommandBuffer: index out of range");
    }
    CommandListPair& pair = commandLists_[index];
    if (FAILED(pair.allocator->Reset())) {
        throw std::runtime_error("[DX12RHIDevice] failed to reset command allocator");
    }
    if (FAILED(pair.list->Reset(pair.allocator.Get(), nullptr))) {
        throw std::runtime_error("[DX12RHIDevice] failed to reset command list");
    }
}

void DX12RHIDevice::resetCommandBuffer(ID3D12GraphicsCommandList* cmdList) {
    for (auto& pair : commandLists_) {
        if (pair.list.Get() == cmdList) {
            resetCommandBuffer(static_cast<uint32_t>(&pair - commandLists_.data()));
            return;
        }
    }
    throw std::runtime_error("[DX12RHIDevice] resetCommandBuffer: "
                             "command list is not from the device pool");
}

void DX12RHIDevice::submitGraphicsQueue(const std::vector<void*>& /*waitSemaphores*/,
                                        const std::vector<void*>& commandBuffers,
                                        const std::vector<void*>& signalSemaphores,
                                        void* fenceHandle)
{
    if (commandBuffers.empty()) {
        return;
    }
    std::vector<ID3D12CommandList*> lists;
    lists.reserve(commandBuffers.size());
    for (void* cmd : commandBuffers) {
        lists.push_back(static_cast<ID3D12GraphicsCommandList*>(cmd));
    }

    // Single DIRECT queue: GPU-GPU waits are implicit (queue order); the signal
    // semaphores are still advanced for bookkeeping.
    commandQueue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());

    for (void* sem : signalSemaphores) {
        auto* sync = static_cast<DX12FenceSync*>(sem);
        if (sync && sync->fence) {
            commandQueue->Signal(sync->fence.Get(), ++sync->value);
        }
    }

    if (fenceHandle) {
        auto* sync = static_cast<DX12FenceSync*>(fenceHandle);
        if (sync && sync->fence) {
            commandQueue->Signal(sync->fence.Get(), sync->value);
        }
    }

    // Close the descriptor batch opened during recording of this frame.
    finalizeDescriptorBatch();
    reportValidationMessages();
}

// =============================================================================
// Internal pipelines (scaled blit + constant-color clear)
// =============================================================================

std::shared_ptr<DX12RHIPipeline> DX12RHIDevice::getOrCreateBlitPipeline(RHIFormat rtvFormat) {
    auto it = blitPipelines_.find(rtvFormat);
    if (it != blitPipelines_.end()) {
        return it->second;
    }

    // Layout: set 0, binding 0 = combined image sampler (fragment).
    RHIBindingLayoutDesc layoutDesc;
    layoutDesc.addBinding(0, RHIDescriptorType::CombinedImageSampler,
                          RHIShaderStage::Fragment, 1);
    auto layout = createBindingLayout(layoutDesc);

    auto builder = createGraphicsPipelineBuilder();
    builder->setVertexShader("shaders_dx12/blit_vert.dxil");
    builder->setFragmentShader("shaders_dx12/blit_frag.dxil");
    builder->addBindingLayout(layout.get());
    builder->setTopology(RHIPrimitiveTopology::TriangleList);
    builder->setCullMode(RHICullMode::None);
    builder->setFrontFace(RHIFrontFace::CounterClockwise);
    builder->setDepthTest(false, false);
    builder->setSampleCount(RHISampleCount::Count1);
    builder->setColorAttachmentCount(1);

    RHIRenderPassDesc rpDesc;
    rpDesc.addColorAttachment(rtvFormat, RHILoadOp::Load, RHIStoreOp::Store);
    auto renderPass = createRenderPass(rpDesc);
    builder->setRenderPass(renderPass.get());

    auto pipeline = std::dynamic_pointer_cast<DX12RHIPipeline>(builder->build());
    blitPipelines_[rtvFormat] = pipeline;
    return pipeline;
}

std::shared_ptr<DX12RHIPipeline> DX12RHIDevice::getOrCreateClearPipeline(RHIFormat rtvFormat) {
    auto it = clearPipelines_.find(rtvFormat);
    if (it != clearPipelines_.end()) {
        return it->second;
    }

    auto builder = createGraphicsPipelineBuilder();
    builder->setVertexShader("shaders_dx12/clear_vert.dxil");
    builder->setFragmentShader("shaders_dx12/clear_frag.dxil");
    builder->addPushConstant(RHIShaderStage::Fragment, 0, 16);
    builder->setTopology(RHIPrimitiveTopology::TriangleList);
    builder->setCullMode(RHICullMode::None);
    builder->setFrontFace(RHIFrontFace::CounterClockwise);
    builder->setDepthTest(false, false);
    builder->setSampleCount(RHISampleCount::Count1);
    builder->setColorAttachmentCount(1);

    RHIRenderPassDesc rpDesc;
    rpDesc.addColorAttachment(rtvFormat, RHILoadOp::Load, RHIStoreOp::Store);
    auto renderPass = createRenderPass(rpDesc);
    builder->setRenderPass(renderPass.get());

    auto pipeline = std::dynamic_pointer_cast<DX12RHIPipeline>(builder->build());
    clearPipelines_[rtvFormat] = pipeline;
    return pipeline;
}

// =============================================================================
// Indirect command signatures
// =============================================================================

ID3D12CommandSignature* DX12RHIDevice::getDrawCommandSignature() {
    if (drawSignature_) {
        return drawSignature_.Get();
    }
    D3D12_INDIRECT_ARGUMENT_DESC arg = {};
    arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);   // 16B == VkDrawIndirectCommand
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs = &arg;
    if (FAILED(device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&drawSignature_)))) {
        throw std::runtime_error("[DX12RHIDevice] CreateCommandSignature(Draw) failed");
    }
    return drawSignature_.Get();
}

ID3D12CommandSignature* DX12RHIDevice::getDrawIndexedCommandSignature() {
    if (drawIndexedSignature_) {
        return drawIndexedSignature_.Get();
    }
    D3D12_INDIRECT_ARGUMENT_DESC arg = {};
    arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);   // 20B == VkDrawIndexedIndirectCommand
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs = &arg;
    if (FAILED(device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&drawIndexedSignature_)))) {
        throw std::runtime_error("[DX12RHIDevice] CreateCommandSignature(DrawIndexed) failed");
    }
    return drawIndexedSignature_.Get();
}

ID3D12CommandSignature* DX12RHIDevice::getDispatchCommandSignature() {
    if (dispatchSignature_) {
        return dispatchSignature_.Get();
    }
    D3D12_INDIRECT_ARGUMENT_DESC arg = {};
    arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);   // 12B == VkDispatchIndirectCommand
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs = &arg;
    if (FAILED(device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&dispatchSignature_)))) {
        throw std::runtime_error("[DX12RHIDevice] CreateCommandSignature(Dispatch) failed");
    }
    return dispatchSignature_.Get();
}
