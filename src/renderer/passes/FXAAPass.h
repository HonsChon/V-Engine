#pragma once

#include "RenderPassBase.h"
#include <glm/glm.hpp>
#include <memory>

// RHI forward declarations
class RHIDevice;
class RHIPipeline;
class RHITexture;
class RHISampler;
class RHIBindingLayout;
class RHIBindingGroup;
class RHICommandBuffer;
class RHIRenderPass;

/**
 * @brief FXAAPass - FXAA 快速近似抗锯齿后处理
 *
 * 在 swapchain 渲染通道内、UI 之前绘制：
 * 采样离屏场景颜色纹理 → 全屏 FXAA → 输出到 swapchain。
 * 管线绑定外部（swapchain）RenderPass；输入纹理每帧经 setSourceTexture 更新。
 *
 * 关闭抗锯齿时 shader 走直通分支（同一管线，无 blit 备用路径）。
 */
class FXAAPass : public RenderPassBase {
public:
    FXAAPass(RHIDevice* rhiDevice, RHIRenderPass* renderPass,
             uint32_t width, uint32_t height);
    ~FXAAPass() override;

    FXAAPass(const FXAAPass&) = delete;
    FXAAPass& operator=(const FXAAPass&) = delete;

    void recreate(RHIRenderPass* renderPass, uint32_t width, uint32_t height);

    /// 设置输入场景纹理（离屏颜色目标，每帧调用，廉价描述符写）
    void setSourceTexture(RHITexture* texture, RHISampler* sampler);

    /**
     * @brief 全屏 FXAA 绘制（在已 begin 的 swapchain render pass 内调用）
     * @param enabled false 时 shader 直通采样（管线不变）
     */
    void render(RHICommandBuffer* cmd, uint32_t frameIndex, bool enabled);

private:
    void createPipeline();
    void cleanup();

    RHIDevice* rhiDevice_;
    RHIRenderPass* renderPass_ = nullptr;   // NOT owned（swapchain 的）

    uint32_t width_;
    uint32_t height_;

    std::shared_ptr<RHIPipeline> pipeline_;
    std::shared_ptr<RHIBindingLayout> bindingLayout_;   // 单个 CombinedImageSampler
    std::shared_ptr<RHIBindingGroup> bindingGroup_;     // 无逐帧数据，单组即可
    std::shared_ptr<RHISampler> sampler_;               // 后处理专用（Clamp + Linear）
};
