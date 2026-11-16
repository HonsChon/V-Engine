#pragma once

#include "RenderPassBase.h"
#include "SceneLighting.h"
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
class RHIRenderPass;
class RHIFramebuffer;
class RHICommandBuffer;

namespace VEngine {
    class RenderSystem;
}

/**
 * ReflectionPass - 水面平面反射通道 (Pure RHI)
 *
 * 把相机关于水面平面 (y = waterHeight) 镜像，用镜像相机将场景
 * （不透明 + Mask）渲染到半分辨率反射 RT，供 WaterPass 采样。
 *
 * 关键点：
 * - 复用 pbr_vert/frag.spv（着色器对相机的全部认知来自 UBO 的
 *   view/proj/viewPos，无屏幕空间假设）→ 零新场景着色器
 * - 镜像矩阵行列式 -1 翻转绕序：反射管线用 frontFace=Clockwise + cull=Back
 *   保留外表面（贴水侧）；用 CCW 会渲染内表面（倒影呈"球内部"）。
 *   注：RT 中"只剩远水一半"的旧现象根因是深度约定错配（见
 *   SceneRenderer::makeObliqueProjection 注释），非绕序问题
 * - 投影矩阵由外部（SceneRenderer）施加 Lengyel 斜近裁剪，
 *   以水面为近裁剪面剔除水下几何
 * - 镜像相机与主相机共享同一投影 → 水面上的点在两幅图像中投影
 *   位置相同（恒等屏幕 UV），water.frag 直接按屏幕坐标采样
 *
 * 结构参照：RT/RP/FB 抄 SSRPass；几何绘制循环抄 TransparentPass
 * （借用 ForwardPass 的 materialLayout，材质描述符直接兼容）。
 */
class ReflectionPass : public RenderPassBase {
public:
    // 与 pbr 着色器 Set 0 布局一致（共用 pbr_vert/frag.spv）
    struct UniformBufferObject {
        alignas(16) glm::mat4 view;       // 镜像 view（view * mirrorMatrix）
        alignas(16) glm::mat4 proj;       // 斜近裁剪投影（已含 API Y 翻转）
        alignas(16) glm::vec4 viewPos;    // 镜像相机世界位置
        alignas(16) glm::vec4 viewportInfo;
        alignas(16) glm::vec4 lightCountPad;   // x = lightCount
        VEngine::RasterLight lights[VEngine::kMaxRasterLights];
    };

    // 与 ForwardPass::PushConstantData 布局一致（共用 pbr_vert.spv）
    struct PushConstantData {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 normalMatrix;
        alignas(16) glm::vec4 materialParams;  // x=opacity y=alphaMode z=alphaCutoff
    };

    /**
     * @param materialLayout ForwardPass 的材质 BindingLayout（复用其材质描述符）
     * @param width/height   全分辨率尺寸（内部 RT 为半分辨率）
     */
    ReflectionPass(RHIDevice* rhiDevice,
                   uint32_t width, uint32_t height,
                   RHIBindingLayout* materialLayout,
                   uint32_t maxFramesInFlight = 2);
    ~ReflectionPass();

    ReflectionPass(const ReflectionPass&) = delete;
    ReflectionPass& operator=(const ReflectionPass&) = delete;

    void resize(uint32_t width, uint32_t height) override;

    void updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo);

    // 反射 RT 访问（供 WaterPass 绑定）
    RHITexture* getReflectionTexture() const { return colorTexture_.get(); }
    RHISampler* getReflectionSampler() const { return sampler_.get(); }

    /**
     * @brief 渲染反射场景（不透明 + Mask）到半分辨率 RT
     * @param renderGeometry false 时仅清屏（相机在水下等无效场景，
     *                       水面采样到天空色回退）
     */
    void render(RHICommandBuffer* cmd, VEngine::RenderSystem* renderSystem,
                uint32_t frameIndex, bool renderGeometry = true);

private:
    void createRenderTargets();
    void createRenderPassAndFramebuffer();
    void createSampler();
    void createBindingLayout();
    void createUniformBuffers();
    void createBindingGroups();
    void createPipeline();
    void cleanup();

    RHIDevice* rhiDevice_ = nullptr;
    RHIBindingLayout* materialLayout_ = nullptr;   // NOT owned（ForwardPass 的）

    uint32_t width_;          // 全分辨率（与主画面一致）
    uint32_t height_;
    uint32_t rtWidth_;        // RT 实际尺寸（半分辨率）
    uint32_t rtHeight_;
    uint32_t maxFramesInFlight_;

    std::shared_ptr<RHITexture>       colorTexture_;   // R8G8B8A8_UNORM
    std::shared_ptr<RHITexture>       depthTexture_;   // D32_SFLOAT
    std::shared_ptr<RHISampler>       sampler_;
    std::shared_ptr<RHIRenderPass>    renderPass_;
    std::shared_ptr<RHIFramebuffer>   framebuffer_;
    std::shared_ptr<RHIPipeline>      pipeline_;
    std::shared_ptr<RHIBindingLayout> globalLayout_;   // Set 0: UBO
    std::vector<std::shared_ptr<RHIBuffer>>       uniformBuffers_;
    std::vector<std::shared_ptr<RHIBindingGroup>> bindingGroups_;
};
