#pragma once

#include "RHICommandBuffer.h"

#include <directx/d3d12.h>
#include <wrl/client.h>

class DX12RHIDevice;

class DX12RHICommandBuffer : public RHICommandBuffer
{
public:
    DX12RHICommandBuffer(DX12RHIDevice* device, ID3D12GraphicsCommandList* cmdList);
    ~DX12RHICommandBuffer() override = default;

    void reset(ID3D12GraphicsCommandList* cmdList);
    ID3D12GraphicsCommandList* getD3D12CommandList() const { return cmdList_; }

    void beginRenderPass(RHIRenderPass* renderPass, RHIFramebuffer* framebuffer,
                         const std::vector<RHIClearValue>& clearValues) override;
    void endRenderPass() override;

    void bindGraphicsPipeline(RHIPipeline* pipeline) override;
    void bindComputePipeline(RHIPipeline* pipeline) override;

    void setBindingGroup(uint32_t set, RHIBindingGroup* group) override;

    void setViewport(float x, float y, float width, float height,
                     float minDepth = 0.0f, float maxDepth = 1.0f) override;
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

    void bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset = 0) override;
    void bindIndexBuffer(RHIBuffer* buffer, uint64_t offset, RHIIndexType indexType) override;

    void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
              uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                     uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0) override;
    void drawIndexedIndirect(RHIBuffer* buffer, uint64_t offset,
                             uint32_t drawCount, uint32_t stride) override;

    void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
    void dispatchIndirect(RHIBuffer* buffer, uint64_t offset) override;

    void pushConstants(RHIShaderStage stages, uint32_t offset,
                       uint32_t size, const void* data) override;

    void pipelineBarrier(RHIPipelineStage srcStage, RHIPipelineStage dstStage,
                         RHIAccessFlags srcAccess, RHIAccessFlags dstAccess) override;
    void transitionImageLayout(RHITexture* texture,
                               RHIImageLayout oldLayout, RHIImageLayout newLayout,
                               RHIPipelineStage srcStage = RHIPipelineStage::AllCommands,
                               RHIPipelineStage dstStage = RHIPipelineStage::AllCommands) override;

    void copyBuffer(RHIBuffer* src, RHIBuffer* dst, uint64_t size,
                    uint64_t srcOffset = 0, uint64_t dstOffset = 0) override;
    void fillBuffer(RHIBuffer* buffer, uint64_t offset, uint64_t size, uint32_t data) override;
    void blitImage(RHITexture* src, RHIImageLayout srcLayout,
                   RHITexture* dst, RHIImageLayout dstLayout,
                   uint32_t srcWidth, uint32_t srcHeight,
                   uint32_t dstWidth, uint32_t dstHeight,
                   RHIFilter filter = RHIFilter::Linear) override;

    void bufferBarrier(RHIBuffer* buffer, uint64_t size,
                       RHIPipelineStage srcStage, RHIPipelineStage dstStage,
                       RHIAccessFlags srcAccess, RHIAccessFlags dstAccess) override;
    void clearColorImage(RHITexture* texture, float r, float g, float b, float a) override;

private:
    void ensureTempCPUDescriptorHeap();
    D3D12_CPU_DESCRIPTOR_HANDLE allocateTempUAV(ID3D12Resource* resource, DXGI_FORMAT format);

    DX12RHIDevice*                device_;
    ID3D12GraphicsCommandList*    cmdList_ = nullptr;

    ID3D12RootSignature*          currentRootSig_ = nullptr;
    bool                          isCompute_ = false;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> tempCPUHeap_;
    UINT                          tempDescriptorSize_ = 0;
    UINT                          tempDescriptorsAllocated_ = 0;
    static constexpr UINT         kMaxTempDescriptors = 4;
};

