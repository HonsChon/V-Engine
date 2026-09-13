#pragma once

#include "RenderPassBase.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

// RHI forward declarations
class RHIDevice;
class RHIPipeline;
class RHIBuffer;
class RHITexture;
class RHISampler;
class RHIBindingLayout;
class RHIBindingGroup;
class RHICommandBuffer;
class RHIRenderPass;

/**
 * ForwardPass - 前向渲染通道 (Pure RHI)
 */
class ForwardPass : public RenderPassBase {
public:
    struct PushConstantData {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 normalMatrix;
        alignas(16) glm::vec4 materialParams;  // x=opacity y=alphaMode(0/1/2) z=alphaCutoff
    };

    struct UniformBufferObject {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::vec4 viewPos;
        alignas(16) glm::vec4 lightPos;
        alignas(16) glm::vec4 lightColor;
    };

    struct MaterialDescriptor {
        std::vector<std::shared_ptr<RHIBindingGroup>> groups;  // 每帧一个 BindingGroup (未来)
        bool valid = false;
    };

    ForwardPass(RHIDevice* rhiDevice,
                RHIRenderPass* renderPass,
                uint32_t width, uint32_t height,
                uint32_t maxFramesInFlight = 2);
    ~ForwardPass();

    // 禁止拷贝/赋值
    ForwardPass(const ForwardPass&) = delete;
    ForwardPass& operator=(const ForwardPass&) = delete;

    void recreate(RHIRenderPass* renderPass, uint32_t width, uint32_t height);

    // UBO 更新
    void updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo);

    // 材质描述符管理 (Pure RHI)
    MaterialDescriptor* allocateMaterialDescriptor(const std::string& materialId);
    void updateMaterialTextures(MaterialDescriptor* material,
                                RHITexture* albedoTex, RHISampler* albedoSampler,
                                RHITexture* normalTex, RHISampler* normalSampler,
                                RHITexture* specularTex, RHISampler* specularSampler);
    MaterialDescriptor* getMaterialDescriptor(const std::string& materialId);

    // 渲染命令 (Pure RHI)
    void begin(RHICommandBuffer* cmd);
    void bindPipeline(RHICommandBuffer* cmd);                    // 不透明管线
    // 透明两遍绘制（back faces → front faces，保证凸网格正确的混合顺序）：
    // 同一像素内外两层的绘制先后若随位置翻转（如 UV 球按纬度存储三角形），
    // 顺序相关的 alpha 混合会产生上下半球色差 + 分界线锯齿
    void bindTransparentBackPipeline(RHICommandBuffer* cmd);     // cull Front：先画内层
    void bindTransparentFrontPipeline(RHICommandBuffer* cmd);    // cull Back：再画外层
    void bindGlobalDescriptorSet(RHICommandBuffer* cmd, uint32_t frameIndex);
    void bindMaterialDescriptorSet(RHICommandBuffer* cmd, uint32_t frameIndex, MaterialDescriptor* material);
    void pushModelMatrix(RHICommandBuffer* cmd, const glm::mat4& model,
                         const glm::vec4& materialParams = glm::vec4(1.0f, 0.0f, 0.5f, 0.0f));
    void drawMesh(RHICommandBuffer* cmd, RHIBuffer* vertexBuffer, RHIBuffer* indexBuffer, uint32_t indexCount);

    // 访问器
    RHIBindingLayout* getMaterialLayout() const { return materialLayout_.get(); }

private:
    void createBindingLayouts();
    void createPipeline();          // 创建 opaque + 透明 back/front 三条管线
    void createUniformBuffers();
    void createGlobalBindingGroups();
    void cleanup();

    RHIDevice* rhiDevice_;
    RHIRenderPass* renderPass_ = nullptr;  // NOT owned

    uint32_t width_;
    uint32_t height_;
    uint32_t maxFramesInFlight_;

    std::shared_ptr<RHIPipeline> pipeline_;                   // 不透明
    std::shared_ptr<RHIPipeline> transparentBackPipeline_;    // 透明内层（cull Front）
    std::shared_ptr<RHIPipeline> transparentFrontPipeline_;   // 透明外层（cull Back）
    std::shared_ptr<RHIBindingLayout> globalLayout_;
    std::shared_ptr<RHIBindingLayout> materialLayout_;
    std::vector<std::shared_ptr<RHIBindingGroup>> globalBindingGroups_;
    std::vector<std::shared_ptr<RHIBuffer>> uniformBuffers_;
    std::unordered_map<std::string, MaterialDescriptor> materialDescriptorCache_;
};