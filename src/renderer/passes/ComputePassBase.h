#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <vector>

class VulkanDevice;
class ComputePipeline;

/**
 * ComputePassBase - 计算通道基类
 * 
 * 所有计算通道（如 GPU Culling、粒子更新等）的基类�?
 * 提供通用的计算管线管理和资源绑定功能�?
 */
class ComputePassBase {
public:
    ComputePassBase(std::shared_ptr<VulkanDevice> device, const std::string& name);
    virtual ~ComputePassBase();

    // 禁止拷贝
    ComputePassBase(const ComputePassBase&) = delete;
    ComputePassBase& operator=(const ComputePassBase&) = delete;

    /**
     * 初始化计算通道
     * 子类应该在此创建管线、描述符集等资源
     */
    virtual void init() = 0;

    /**
     * 记录计算命令
     * @param commandBuffer 命令缓冲
     */
    virtual void record(VkCommandBuffer commandBuffer) = 0;

    /**
     * 清理资源
     */
    virtual void cleanup();

    /**
     * 获取通道名称
     */
    const std::string& getName() const { return name; }

protected:
    /**
     * 创建描述符池
     */
    void createDescriptorPool(const std::vector<VkDescriptorPoolSize>& poolSizes, 
                              uint32_t maxSets = 1);

    /**
     * 插入内存屏障（用于同步）
     */
    void insertMemoryBarrier(VkCommandBuffer commandBuffer,
                             VkPipelineStageFlags srcStage,
                             VkPipelineStageFlags dstStage,
                             VkAccessFlags srcAccess,
                             VkAccessFlags dstAccess);

    /**
     * 插入缓冲区屏�?
     */
    void insertBufferBarrier(VkCommandBuffer commandBuffer,
                             VkBuffer buffer,
                             VkDeviceSize size,
                             VkPipelineStageFlags srcStage,
                             VkPipelineStageFlags dstStage,
                             VkAccessFlags srcAccess,
                             VkAccessFlags dstAccess);

    std::shared_ptr<VulkanDevice> device;
    std::string name;
    
    std::unique_ptr<ComputePipeline> pipeline;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};
