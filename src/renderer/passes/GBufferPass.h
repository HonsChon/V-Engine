#pragma once

#include "RenderPassBase.h"
#include "RenderContext.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <array>
#include <string>
#include <unordered_map>

// RHI forward declarations
class RHIDevice;
class RHITexture;
class RHISampler;
class RHIBuffer;
class RHIPipeline;
class RHIRenderPass;
class RHIFramebuffer;
class RHIBindingLayout;
class RHIBindingGroup;

class VulkanDevice;

/**
 * GBufferPass - 几何缓冲区渲染通道 (RHI)
 * 
 * 用于延迟渲染的第一阶段，存储场景的几何信息：
 * - Position (RGB16F) - 世界空间位置
 * - Normal (RGB16F) - 世界空间法线
 * - Albedo (RGBA8) - 反照率+ 金属度
 * - Depth (D32F) - 深度缓冲
 * 
 * 描述符集架构：
 * - Set 0: 全局 UBO（view, proj, 光照）
 * - Set 1: 材质纹理（albedo, normal, specular）每个材质独立
 */
class GBufferPass : public RenderPassBase {
public:
    // G-Buffer 附件索引
    enum Attachment {
        POSITION = 0,
        NORMAL = 1,
        ALBEDO = 2,
        DEPTH = 3,
        COUNT = 4
    };

    // Push Constants 结构体
    struct PushConstantData {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 normalMatrix;
    };
    
    // 材质描述符结构体
    struct MaterialDescriptor {
        std::vector<VkDescriptorSet> nativeSets;  // 原生 VkDescriptorSet（过渡期）
        std::vector<std::unique_ptr<RHIBindingGroup>> groups;  // RHI binding groups
        bool valid = false;
    };

    // UBO 结构体
    struct UniformBufferObject {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::vec4 viewPos;
        alignas(16) glm::vec4 lightPos;
        alignas(16) glm::vec4 lightColor;
    };

    GBufferPass(std::shared_ptr<VulkanDevice> device, RHIDevice* rhiDevice,
                uint32_t width, uint32_t height, uint32_t maxFramesInFlight = 2);
    ~GBufferPass();

    GBufferPass(const GBufferPass&) = delete;
    GBufferPass& operator=(const GBufferPass&) = delete;

    void resize(uint32_t width, uint32_t height);

    // 获取器 — 原生 Vulkan handles（过渡期，供 LightingPass 等使用）
    VkRenderPass getRenderPass() const;
    VkFramebuffer getFramebuffer() const;
    
    VkImageView getPositionView() const;
    VkImageView getNormalView() const;
    VkImageView getAlbedoView() const;
    VkImageView getDepthView() const;
    
    VkImage getPositionImage() const;
    VkImage getNormalImage() const;
    VkImage getAlbedoImage() const;
    VkImage getDepthImage() const;
    
    VkSampler getSampler() const;
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }

    // RenderPass 控制
    void beginRenderPass(VkCommandBuffer cmd);
    void endRenderPass(VkCommandBuffer cmd);
    std::array<VkClearValue, 4> getClearValues() const;

    // Pipeline 相关 — 返回原生 handles（过渡期）
    VkPipeline getPipeline() const;
    VkPipelineLayout getPipelineLayout() const;
    void bindPipeline(VkCommandBuffer cmd) const;
    
    // 描述符绑定
    void bindGlobalDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex) const;
    void bindMaterialDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex, MaterialDescriptor* material) const;
    
    // 绘制
    void drawMesh(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkBuffer indexBuffer, uint32_t indexCount) const;
    void pushModelMatrix(VkCommandBuffer cmd, const glm::mat4& model);
    
    // 初始化描述符
    void createDescriptorSets();
    
    // 材质描述符管理
    MaterialDescriptor* allocateMaterialDescriptor(const std::string& materialId);
    MaterialDescriptor* getMaterialDescriptor(const std::string& materialId);
    void updateMaterialTextures(MaterialDescriptor* material,
                                VkImageView albedoView, VkSampler albedoSampler,
                                VkImageView normalView, VkSampler normalSampler,
                                VkImageView specularView, VkSampler specularSampler);
    
    // UBO 更新
    void updateUniformBuffer(uint32_t frameIndex, const UniformBufferObject& ubo);

    // 基类接口实现
    void recordCommands(VkCommandBuffer cmd, uint32_t frameIndex) override;
    void recordCommands(VkCommandBuffer cmd, const RenderContext& context);

private:
    // RHI resource creation
    void createAttachments();
    void createRHIRenderPass();
    void createRHIFramebuffer();
    void createRHISampler();
    void createBindingLayouts();
    void createPipeline();
    void createUniformBuffers();
    void createGlobalBindingGroups();
    void cleanup();

    // RHI device references
    RHIDevice* rhiDevice_ = nullptr;
    std::shared_ptr<VulkanDevice> vulkanDevice_;
    uint32_t width_;
    uint32_t height_;
    uint32_t maxFramesInFlight_;

    // G-Buffer attachments (RHI textures)
    std::array<std::unique_ptr<RHITexture>, COUNT> attachmentTextures_;
    std::unique_ptr<RHISampler> sampler_;

    // RenderPass & Framebuffer (RHI)
    std::unique_ptr<RHIRenderPass> renderPass_;
    std::unique_ptr<RHIFramebuffer> framebuffer_;

    // Pipeline (RHI)
    std::unique_ptr<RHIPipeline> pipeline_;

    // Binding layouts (RHI)
    std::unique_ptr<RHIBindingLayout> globalLayout_;
    std::unique_ptr<RHIBindingLayout> materialLayout_;

    // Uniform Buffers (RHI)
    std::vector<std::unique_ptr<RHIBuffer>> uniformBuffers_;

    // Global Binding Groups (RHI) — one per frame
    std::vector<std::unique_ptr<RHIBindingGroup>> globalBindingGroups_;

    // 材质描述符缓存
    std::unordered_map<std::string, MaterialDescriptor> materialDescriptorCache_;

    RenderContext currentContext;
};