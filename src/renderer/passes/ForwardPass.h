#pragma once

#include "RenderPassBase.h"
#include "RenderContext.h"
#include "RHI.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>
#include <array>
#include <unordered_map>

class VulkanDevice;
class RHIDevice;
class Mesh;
class Material;
class VulkanTexture;

/**
 * ForwardPass - 前向渲染通道 (RHI 重构版)
 * 
 * 内部使用 RHI 接口管理资源:
 * - RHIBindingLayout / RHIBindingGroup 替代原生 DescriptorSet
 * - RHIGraphicsPipelineBuilder 替代手动 VkPipeline 创建
 * - RHIBuffer 替代手动 VkBuffer 管理
 * 
 * 对外接口暂保留 VkCommandBuffer 兼容（SceneRenderer 尚未迁移）
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

    // 材质描述符数据 — 混合模式（native VkDescriptorSet + RHI BindingGroup）
    struct MaterialDescriptor {
        std::vector<std::unique_ptr<RHIBindingGroup>> groups;  // 每帧一个 BindingGroup (未来)
        std::vector<VkDescriptorSet> nativeSets;               // native sets (过渡期使用)
        bool valid = false;
    };

    ForwardPass(std::shared_ptr<VulkanDevice> device,
                RHIDevice* rhiDevice,
                VkRenderPass renderPass,
                uint32_t width, uint32_t height,
                uint32_t maxFramesInFlight = 2);
    ~ForwardPass();

    // 禁止拷贝
    ForwardPass(const ForwardPass&) = delete;
    ForwardPass& operator=(const ForwardPass&) = delete;

    // 重建管线（窗口大小改变或 RenderPass 改变时）
    void recreate(VkRenderPass renderPass, uint32_t width, uint32_t height);

    // 获取原生 Pipeline handle（用于兼容旧代码）
    VkPipeline getPipeline() const;
    VkPipelineLayout getPipelineLayout() const;

    // 更新全局 UBO
    void updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo);

    // ========== 材质描述符管理 ==========
    
    MaterialDescriptor* allocateMaterialDescriptor(const std::string& materialId);
    
    // 更新材质的纹理绑定（旧接口 — 兼容现有材质系统）
    void updateMaterialTextures(MaterialDescriptor* material,
                                VkImageView albedoView, VkSampler albedoSampler,
                                VkImageView normalView, VkSampler normalSampler,
                                VkImageView specularView, VkSampler specularSampler);
    
    MaterialDescriptor* getMaterialDescriptor(const std::string& materialId);

    // ========== 渲染命令（旧 VkCommandBuffer 兼容） ==========
    
    void begin(VkCommandBuffer cmd);
    void bindPipeline(VkCommandBuffer cmd);
    void bindGlobalDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex);
    void bindMaterialDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex, MaterialDescriptor* material);
    void pushModelMatrix(VkCommandBuffer cmd, const glm::mat4& model);
    void drawMesh(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkBuffer indexBuffer, uint32_t indexCount);

private:
    void createBindingLayouts();
    void createPipeline();
    void createUniformBuffers();
    void createGlobalBindingGroups();
    void cleanup();

    RHIDevice* rhiDevice_;
    std::shared_ptr<VulkanDevice> vulkanDevice_;
    VkRenderPass renderPass_;
    
    uint32_t width_;
    uint32_t height_;
    uint32_t maxFramesInFlight_;

    // RHI 资源 — Pipeline
    std::unique_ptr<RHIPipeline> pipeline_;

    // RHI 资源 — Binding Layouts (Set 0: UBO, Set 1: Material Textures)
    std::unique_ptr<RHIBindingLayout> globalLayout_;
    std::unique_ptr<RHIBindingLayout> materialLayout_;

    // RHI 资源 — Global Binding Groups (per frame)
    std::vector<std::unique_ptr<RHIBindingGroup>> globalBindingGroups_;

    // RHI 资源 — Uniform Buffers (per frame)
    std::vector<std::unique_ptr<RHIBuffer>> uniformBuffers_;

    // 材质描述符缓存
    std::unordered_map<std::string, MaterialDescriptor> materialDescriptorCache_;
};
