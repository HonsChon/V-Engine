#pragma once

#include "RenderPassBase.h"
#include "RenderContext.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>
#include <array>

class VulkanDevice;
class Mesh;
class Material;

/**
 * ForwardPass - 前向渲染通道
 * 
 * 封装 PBR 前向渲染管线，用于：
 * - 直接渲染到交换链（不使用延迟渲染时）
 * - 或渲染到 G-Buffer 后的最终合成阶段
 */
class ForwardPass : public RenderPassBase {
public:
    // UBO 结构体 - 使用 GLM 类型，内存布局与 float 数组兼容
    struct UniformBufferObject {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::mat4 normalMatrix;
        alignas(16) glm::vec4 viewPos;
        alignas(16) glm::vec4 lightPos;
        alignas(16) glm::vec4 lightColor;
    };

    ForwardPass(std::shared_ptr<VulkanDevice> device, 
                VkRenderPass renderPass,
                uint32_t width, uint32_t height,
                uint32_t maxFramesInFlight = 2);
    ~ForwardPass();

    // 禁止拷贝
    ForwardPass(const ForwardPass&) = delete;
    ForwardPass& operator=(const ForwardPass&) = delete;

    // 重建管线（窗口大小改变或 RenderPass 改变时）
    void recreate(VkRenderPass renderPass, uint32_t width, uint32_t height);

    // 获取器
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }
    
    // 获取 Uniform Buffers（供 GBuffer 等其他 Pass 共享使用）
    const std::vector<VkBuffer>& getUniformBuffers() const { return uniformBuffers; }
    VkDeviceSize getUniformBufferSize() const { return sizeof(UniformBufferObject); }

    // 更新 UBO
    void updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo);

    // 绑定材质纹理到描述符集
    void updateMaterialDescriptor(uint32_t frameIndex, 
                                   VkImageView albedoView, VkSampler albedoSampler,
                                   VkImageView normalView, VkSampler normalSampler,
                                   VkImageView specularView, VkSampler specularSampler);

    // 渲染
    void begin(VkCommandBuffer cmd);
    void bindPipeline(VkCommandBuffer cmd);
    void bindDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex);
    // 绘制网格 - 需要提供预创建的 Vulkan 缓冲区
    void drawMesh(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkBuffer indexBuffer, uint32_t indexCount);

private:
    void createDescriptorSetLayout();
    void createPipeline();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void cleanup();

    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    std::shared_ptr<VulkanDevice> device;
    VkRenderPass renderPass;
    
    uint32_t width;
    uint32_t height;
    uint32_t maxFramesInFlight;

    // Pipeline
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

    // Descriptor
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // Uniform Buffers
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;
};
