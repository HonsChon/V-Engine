#pragma once

#include "RenderPassBase.h"
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
class RHICommandBuffer;

/**
 * GBufferPass - 几何缓冲区渲染通道 (Pure RHI)
 */
class GBufferPass : public RenderPassBase {
public:
    enum Attachment { POSITION = 0, NORMAL = 1, ALBEDO = 2, DEPTH = 3, COUNT = 4 };

    struct PushConstantData {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 normalMatrix;
    };

    struct MaterialDescriptor {
        std::vector<std::shared_ptr<RHIBindingGroup>> groups;
        bool valid = false;
    };

    struct UniformBufferObject {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::vec4 viewPos;
        alignas(16) glm::vec4 lightPos;
        alignas(16) glm::vec4 lightColor;
    };

    GBufferPass(RHIDevice* rhiDevice,
                uint32_t width, uint32_t height, uint32_t maxFramesInFlight = 2);
    ~GBufferPass();

    GBufferPass(const GBufferPass&) = delete;
    GBufferPass& operator=(const GBufferPass&) = delete;

    void resize(uint32_t width, uint32_t height);

    // Pure RHI texture accessors
    RHITexture* getPositionTexture() const { return attachmentTextures_[POSITION].get(); }
    RHITexture* getNormalTexture() const   { return attachmentTextures_[NORMAL].get(); }
    RHITexture* getAlbedoTexture() const   { return attachmentTextures_[ALBEDO].get(); }
    RHITexture* getDepthTexture() const    { return attachmentTextures_[DEPTH].get(); }
    RHISampler* getRHISampler() const      { return sampler_.get(); }
    RHIRenderPass* getRHIRenderPass() const { return renderPass_.get(); }
    RHIFramebuffer* getRHIFramebuffer() const { return framebuffer_.get(); }

    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }

    // Render pass control (Pure RHI)
    void beginRenderPass(RHICommandBuffer* cmd);
    void endRenderPass(RHICommandBuffer* cmd);

    // Pipeline (Pure RHI)
    void bindPipeline(RHICommandBuffer* cmd) const;
    void bindGlobalDescriptorSet(RHICommandBuffer* cmd, uint32_t frameIndex) const;
    void bindMaterialDescriptorSet(RHICommandBuffer* cmd, uint32_t frameIndex, MaterialDescriptor* material) const;
    void drawMesh(RHICommandBuffer* cmd, RHIBuffer* vertexBuffer, RHIBuffer* indexBuffer, uint32_t indexCount) const;
    void pushModelMatrix(RHICommandBuffer* cmd, const glm::mat4& model);

    // 描述符
    void createDescriptorSets();

    // 材质描述符管理 (Pure RHI)
    MaterialDescriptor* allocateMaterialDescriptor(const std::string& materialId);
    MaterialDescriptor* getMaterialDescriptor(const std::string& materialId);
    void updateMaterialTextures(MaterialDescriptor* material,
                                RHITexture* albedoTex, RHISampler* albedoSampler,
                                RHITexture* normalTex, RHISampler* normalSampler,
                                RHITexture* specularTex, RHISampler* specularSampler);

    // UBO 更新
    void updateUniformBuffer(uint32_t frameIndex, const UniformBufferObject& ubo);

    // Accessor for material layout
    RHIBindingLayout* getMaterialLayout() const { return materialLayout_.get(); }

private:
    void createAttachments();
    void createRHIRenderPass();
    void createRHIFramebuffer();
    void createRHISampler();
    void createBindingLayouts();
    void createPipeline();
    void createUniformBuffers();
    void createGlobalBindingGroups();
    void cleanup();

    RHIDevice* rhiDevice_ = nullptr;
    uint32_t width_;
    uint32_t height_;
    uint32_t maxFramesInFlight_;

    std::array<std::shared_ptr<RHITexture>, COUNT> attachmentTextures_;
    std::shared_ptr<RHISampler> sampler_;
    std::shared_ptr<RHIRenderPass> renderPass_;
    std::shared_ptr<RHIFramebuffer> framebuffer_;
    std::shared_ptr<RHIPipeline> pipeline_;
    std::shared_ptr<RHIBindingLayout> globalLayout_;
    std::shared_ptr<RHIBindingLayout> materialLayout_;
    std::vector<std::shared_ptr<RHIBuffer>> uniformBuffers_;
    std::vector<std::shared_ptr<RHIBindingGroup>> globalBindingGroups_;
    std::unordered_map<std::string, MaterialDescriptor> materialDescriptorCache_;
};