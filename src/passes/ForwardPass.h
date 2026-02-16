#pragma once

#include "RenderPassBase.h"
#include "RenderContext.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>
#include <array>
#include <unordered_map>

class VulkanDevice;
class Mesh;
class Material;
class VulkanTexture;

/**
 * ForwardPass - 前向渲染通道
 * 
 * 使用两个描述符集布局：
 * - Set 0: 全局 UBO（view, proj, light）
 * - Set 1: 材质纹理（albedo, normal, specular）- 每个材质独立
 */
class ForwardPass : public RenderPassBase {
public:
    // Push Constants 结构体 - 每个物体独立的变换数据
    struct PushConstantData {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 normalMatrix;
    };
    
    // UBO 结构体 - 全局共享数据（相机、光照）
    struct UniformBufferObject {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::vec4 viewPos;
        alignas(16) glm::vec4 lightPos;
        alignas(16) glm::vec4 lightColor;
    };
    
    // 材质描述符数据 - 每个材质独立
    struct MaterialDescriptor {
        std::vector<VkDescriptorSet> sets;  // 每帧一个描述符集
        bool valid = false;
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
    VkDescriptorSetLayout getGlobalSetLayout() const { return globalSetLayout; }
    VkDescriptorSetLayout getMaterialSetLayout() const { return materialSetLayout; }
    
    // 获取 Uniform Buffers（供 GBuffer 等其他 Pass 共享使用）
    const std::vector<VkBuffer>& getUniformBuffers() const { return uniformBuffers; }
    VkDeviceSize getUniformBufferSize() const { return sizeof(UniformBufferObject); }

    // 更新全局 UBO
    void updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo);

    // ========== 材质描述符管理 ==========
    
    // 为材质分配独立的描述符集
    MaterialDescriptor* allocateMaterialDescriptor(const std::string& materialId);
    
    // 更新材质的纹理绑定
    void updateMaterialTextures(MaterialDescriptor* material,
                                VkImageView albedoView, VkSampler albedoSampler,
                                VkImageView normalView, VkSampler normalSampler,
                                VkImageView specularView, VkSampler specularSampler);
    
    // 获取已分配的材质描述符
    MaterialDescriptor* getMaterialDescriptor(const std::string& materialId);

    // ========== 渲染命令 ==========
    
    void begin(VkCommandBuffer cmd);
    void bindPipeline(VkCommandBuffer cmd);
    
    // 绑定全局描述符集 (Set 0)
    void bindGlobalDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex);
    
    // 绑定材质描述符集 (Set 1)
    void bindMaterialDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex, MaterialDescriptor* material);
    
    // Push Constants - 推送每个物体的变换矩阵
    void pushModelMatrix(VkCommandBuffer cmd, const glm::mat4& model);
    
    // 绘制网格
    void drawMesh(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkBuffer indexBuffer, uint32_t indexCount);

private:
    void createDescriptorSetLayouts();
    void createPipeline();
    void createUniformBuffers();
    void createDescriptorPools();
    void createGlobalDescriptorSets();
    void cleanup();
    void ensureMaterialPoolCapacity();

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
    
    // 两个描述符集布局
    VkDescriptorSetLayout globalSetLayout = VK_NULL_HANDLE;    // Set 0: UBO
    VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;  // Set 1: 纹理

    // 全局描述符池和描述符集
    VkDescriptorPool globalDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> globalDescriptorSets;  // 每帧一个

    // 材质描述符池（可动态增长）
    std::vector<VkDescriptorPool> materialDescriptorPools;
    uint32_t currentMaterialPoolIndex = 0;
    uint32_t allocatedMaterialSets = 0;
    static constexpr uint32_t MATERIALS_PER_POOL = 64;
    
    // 材质描述符缓存
    std::unordered_map<std::string, MaterialDescriptor> materialDescriptorCache;

    // Uniform Buffers
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;
};
