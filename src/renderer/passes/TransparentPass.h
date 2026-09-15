#pragma once

#include "RenderPassBase.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

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

namespace VEngine {
    class RenderSystem;
}

/**
 * @brief TransparentPass - 延迟模式下的半透明前向绘制通道
 *
 * 位置：Final Composition 内，LightingPass 之后（WaterPass 之前）。
 * 无法使用固定管线深度测试（合成阶段深度缓冲已清空，不透明深度
 * 在 GBuffer 的深度纹理中），改为采样 GBuffer 深度做手动 discard
 * （WaterPass 同款模式）。
 *
 * 材质纹理复用 ForwardPass 的 materialLayout（BindingGroup 兼容），
 * 全局 Set 0 = UBO + GBuffer 深度采样器。
 */
class TransparentPass : public RenderPassBase {
public:
    struct UniformBufferObject {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::vec4 viewPos;
        alignas(16) glm::vec4 lightPos;
        alignas(16) glm::vec4 lightColor;
        alignas(16) glm::vec4 viewportInfo;   // x=width y=height（gl_FragCoord → UV）
    };

    // 与 ForwardPass::PushConstantData 布局一致（共用 pbr_vert.spv）
    struct PushConstantData {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 normalMatrix;
        alignas(16) glm::vec4 materialParams;  // x=opacity y=alphaMode z=alphaCutoff
    };

    /**
     * @param materialLayout ForwardPass 的材质 BindingLayout（复用其材质描述符）
     */
    TransparentPass(RHIDevice* rhiDevice,
                    RHIRenderPass* renderPass,
                    RHIBindingLayout* materialLayout,
                    uint32_t width, uint32_t height,
                    uint32_t maxFramesInFlight = 2);
    ~TransparentPass();

    TransparentPass(const TransparentPass&) = delete;
    TransparentPass& operator=(const TransparentPass&) = delete;

    void recreate(RHIRenderPass* renderPass, uint32_t width, uint32_t height);

    void updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo);

    /// 绑定 GBuffer 深度纹理（GBuffer 重建后/每帧调用，廉价描述符写）
    void setDepthTexture(RHITexture* depthTexture, RHISampler* sampler);

    /**
     * @brief 绘制所有透明 renderable（RenderSystem 已按 back-to-front 排序）
     */
    void render(RHICommandBuffer* cmd, VEngine::RenderSystem* renderSystem, uint32_t frameIndex);

private:
    void createLayouts();
    void createPipeline();
    void createUniformBuffers();
    void createGlobalBindingGroups();
    void cleanup();

    RHIDevice* rhiDevice_;
    RHIRenderPass* renderPass_ = nullptr;      // NOT owned（swapchain 的）
    RHIBindingLayout* materialLayout_ = nullptr;  // NOT owned（ForwardPass 的）

    uint32_t width_;
    uint32_t height_;
    uint32_t maxFramesInFlight_;

    std::shared_ptr<RHIPipeline> backPipeline_;     // 内层（cull Front）+ blend + 手动深度剔除
    std::shared_ptr<RHIPipeline> frontPipeline_;    // 外层（cull Back）——两遍绘制保证混合顺序
    std::shared_ptr<RHIBindingLayout> globalLayout_;   // UBO + depth sampler
    std::shared_ptr<RHISampler> depthSampler_;
    std::vector<std::shared_ptr<RHIBindingGroup>> globalBindingGroups_;
    std::vector<std::shared_ptr<RHIBuffer>> uniformBuffers_;

    RHITexture* depthTexture_ = nullptr;  // NOT owned（GBuffer 的）
};
