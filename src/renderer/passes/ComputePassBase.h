#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <vector>

class VulkanDevice;
class RHIDevice;
class RHIPipeline;
class RHIBindingLayout;

/**
 * ComputePassBase - 计算通道基类 (RHI)
 * 
 * 所有计算通道（如 GPU Culling、粒子更新等）的基类。
 * 提供通用的计算管线管理和资源绑定功能。
 */
class ComputePassBase {
public:
    ComputePassBase(std::shared_ptr<VulkanDevice> device, RHIDevice* rhiDevice, const std::string& name);
    virtual ~ComputePassBase();

    ComputePassBase(const ComputePassBase&) = delete;
    ComputePassBase& operator=(const ComputePassBase&) = delete;

    virtual void init() = 0;
    virtual void record(VkCommandBuffer commandBuffer) = 0;
    virtual void cleanup();

    const std::string& getName() const { return name; }

protected:
    /**
     * 插入内存屏障（用于同步）
     */
    void insertMemoryBarrier(VkCommandBuffer commandBuffer,
                             VkPipelineStageFlags srcStage,
                             VkPipelineStageFlags dstStage,
                             VkAccessFlags srcAccess,
                             VkAccessFlags dstAccess);

    /**
     * 插入缓冲区屏障
     */
    void insertBufferBarrier(VkCommandBuffer commandBuffer,
                             VkBuffer buffer,
                             VkDeviceSize size,
                             VkPipelineStageFlags srcStage,
                             VkPipelineStageFlags dstStage,
                             VkAccessFlags srcAccess,
                             VkAccessFlags dstAccess);

    std::shared_ptr<VulkanDevice> device;
    RHIDevice* rhiDevice_ = nullptr;
    std::string name;
    
    // RHI resources
    std::unique_ptr<RHIPipeline>      pipeline_;
    std::unique_ptr<RHIBindingLayout> bindingLayout_;

    // Descriptor set (native Vulkan — hybrid approach)
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
};