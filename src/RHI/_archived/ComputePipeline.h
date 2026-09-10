#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

class VulkanDevice;

/**
 * ComputePipeline - Vulkan 计算管线封装
 * 
 * 用于 GPU 驱动渲染、粒子系统、后处理等计算任务。
 * 支持多个描述符集布局和Push Constants。
 */
class ComputePipeline {
public:
    /**
     * 描述符绑定信息
     */
    struct DescriptorBinding {
        uint32_t binding;                    // 绑定点
        VkDescriptorType type;               // 描述符类型
        VkShaderStageFlags stageFlags;       // 着色器阶段（通常是COMPUTE：
        uint32_t count = 1;                  // 数组大小
    };

    /**
     * Push Constant 范围
     */
    struct PushConstantRange {
        VkShaderStageFlags stageFlags;
        uint32_t offset;
        uint32_t size;
    };

    /**
     * 配置结构体
     */
    struct Config {
        std::string shaderPath;                              // SPIR-V 着色器路径
        std::vector<DescriptorBinding> bindings;             // 描述符绑定
        std::vector<PushConstantRange> pushConstantRanges;   // Push Constants
        std::string entryPoint = "main";                     // 入口点名称
    };

public:
    ComputePipeline(std::shared_ptr<VulkanDevice> device, const Config& config);
    ~ComputePipeline();

    // 禁止拷贝
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    /**
     * 绑定管线到命令缓冲
     */
    void bind(VkCommandBuffer commandBuffer);

    /**
     * 调度计算任务
     * @param commandBuffer 命令缓冲
     * @param groupCountX X 方向工作组数量
     * @param groupCountY Y 方向工作组数量
     * @param groupCountZ Z 方向工作组数量
     */
    void dispatch(VkCommandBuffer commandBuffer, 
                  uint32_t groupCountX, 
                  uint32_t groupCountY = 1, 
                  uint32_t groupCountZ = 1);

    /**
     * 推送常量
     */
    template<typename T>
    void pushConstants(VkCommandBuffer commandBuffer, 
                       VkShaderStageFlags stageFlags,
                       uint32_t offset,
                       const T& data) {
        vkCmdPushConstants(commandBuffer, pipelineLayout, stageFlags, 
                          offset, sizeof(T), &data);
    }

    /**
     * 间接调度（从 GPU 缓冲区读取调度参数）
     */
    void dispatchIndirect(VkCommandBuffer commandBuffer, 
                          VkBuffer buffer, 
                          VkDeviceSize offset = 0);

    // Getters
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

    /**
     * 创建描述符集
     */
    VkDescriptorSet createDescriptorSet(VkDescriptorPool pool);

    /**
     * 分配描述符集（使用内部Descriptor Pool：
     */
    VkDescriptorSet allocateDescriptorSet();

    /**
     * 获取内部 Descriptor Pool
     */
    VkDescriptorPool getDescriptorPool() const { return descriptorPool; }

private:
    void createDescriptorPool();
    void createDescriptorSetLayout(const std::vector<DescriptorBinding>& bindings);
    void createPipelineLayout(const std::vector<PushConstantRange>& pushConstantRanges);
    void createPipeline(const std::string& shaderPath, const std::string& entryPoint);
    
    std::vector<char> readShaderFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);

    std::shared_ptr<VulkanDevice> device;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    
    std::vector<DescriptorBinding> storedBindings;  // 存储绑定信息用于创建 Pool
};

/**
 * 间接调度命令结构体
 * 与vkCmdDispatchIndirect 配合使用
 */
struct DispatchIndirectCommand {
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

/**
 * 间接绘制命令结构体（索引：
 * 与vkCmdDrawIndexedIndirect 配合使用
 */
struct DrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
};

/**
 * 间接绘制命令结构体（非索引）
 * 与vkCmdDrawIndirect 配合使用
 */
struct DrawIndirectCommand {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
};
