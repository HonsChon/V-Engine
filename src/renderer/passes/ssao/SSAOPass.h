#pragma once

#include "RenderPassBase.h"
#include <glm/glm.hpp>
#include <memory>
#include <array>
#include <vector>
#include <string>

// RHI forward declarations — NO Vulkan headers
class RHIDevice;
class RHITexture;
class RHISampler;
class RHIBuffer;
class RHIPipeline;
class RHIBindingLayout;
class RHIBindingGroup;
class RHIRenderPass;
class RHIFramebuffer;
class RHICommandBuffer;

class GBufferPass;

/**
 * SSAOPass - 屏幕空间环境遮蔽渲染通道 (Pure RHI)
 * 
 * 采用 Deinterleaved Texturing 优化的 SSAO 实现。
 * 内部管理 4 个子阶段：
 * 1. Deinterleave: 将 GBuffer Position/Normal 按 4×4 拆分为 16 层 texture array
 * 2. SSAO 计算: 对 16 层子纹理分别执行法线导向半球 SSAO 采样
 * 3. Reinterleave: 将 16 层 AO 结果重组为全分辨率纹理
 * 4. Blur: 对全分辨率 AO 纹理执行 4×4 box blur
 * 
 * 对外只暴露最终模糊后的 AO 纹理。
 */
class SSAOPass : public RenderPassBase {
public:
    static constexpr int KERNEL_SIZE = 64;
    static constexpr int DEINTERLEAVE_FACTOR = 4;
    static constexpr int NUM_LAYERS = DEINTERLEAVE_FACTOR * DEINTERLEAVE_FACTOR; // 16

    // SSAO 参数（运行时可调）
    struct SSAOSettings {
        float radius = 0.5f;
        float bias = 0.025f;
        float power = 1.0f;
        float amount = 1.5f;
        int kernelSize = KERNEL_SIZE;
    };

    // SSAO UBO 结构（须与 shader 匹配）
    struct SSAOParamsUBO {
        alignas(16) glm::vec4 samples[KERNEL_SIZE];
        alignas(16) glm::mat4 projection;
        alignas(16) glm::mat4 view;
        alignas(4)  float radius;
        alignas(4)  float bias;
        alignas(4)  float power;
        alignas(4)  float amount;
        alignas(4)  int kernelSize;
    };

    // Push constants
    struct DeinterleavePushConstants {
        int fullWidth;
        int fullHeight;
    };

    struct SSAOPushConstants {
        int layerIndex;
        float rotationAngle;
        int subWidth;
        int subHeight;
    };

    struct ReinterleavePushConstants {
        int fullWidth;
        int fullHeight;
    };

    SSAOPass(RHIDevice* rhiDevice,
             uint32_t width, uint32_t height);
    ~SSAOPass();

    SSAOPass(const SSAOPass&) = delete;
    SSAOPass& operator=(const SSAOPass&) = delete;

    void init();

    /**
     * 执行完整的 SSAO 流程 (Pure RHI)
     */
    void execute(RHICommandBuffer* cmd, GBufferPass* gbuffer, uint32_t frameIndex,
                 const glm::mat4& projection, const glm::mat4& view);

    void resize(uint32_t newWidth, uint32_t newHeight) override;
    void updateSettings(const SSAOSettings& settings);

    // 获取最终输出 (RHI)
    RHITexture* getOutputAOTexture() const { return m_blurredAOTex.get(); }
    RHISampler* getOutputAOSampler() const { return m_aoSampler.get(); }

    SSAOSettings& getSettings() { return m_settings; }
    const SSAOSettings& getSettings() const { return m_settings; }

private:
    void cleanup();

    // 资源创建
    void createDeinterleavedTextures();
    void createAOTextures();
    void createSamplers();
    void generateKernel();
    void generateLayerRotations();

    // Deinterleave 阶段
    void createDeinterleaveResources();

    // SSAO 计算阶段
    void createSSAOResources();

    // Reinterleave 阶段
    void createReinterleaveResources();

    // Blur 阶段
    void createBlurResources();

    // 执行子阶段
    void executeDeinterleave(RHICommandBuffer* cmd, GBufferPass* gbuffer);
    void executeSSAO(RHICommandBuffer* cmd, uint32_t frameIndex);
    void executeReinterleave(RHICommandBuffer* cmd);
    void executeBlur(RHICommandBuffer* cmd);

    // ========== 成员变量 ==========
    RHIDevice* rhiDevice_ = nullptr;

    SSAOSettings m_settings;
    uint32_t m_subWidth = 0;
    uint32_t m_subHeight = 0;

    // 采样核心
    std::array<glm::vec4, KERNEL_SIZE> m_kernel;
    std::array<float, NUM_LAYERS> m_layerRotations;

    // ---- Deinterleaved Textures (16-layer arrays) ----
    std::shared_ptr<RHITexture> m_deinterleavedPosTex;   // R16G16B16A16_SFLOAT, 16 layers
    std::shared_ptr<RHITexture> m_deinterleavedNorTex;   // R16G16B16A16_SFLOAT, 16 layers

    // ---- AO Texture Array ----
    std::shared_ptr<RHITexture> m_aoArrayTex;            // R8_UNORM, 16 layers
    std::array<std::shared_ptr<RHITexture>, NUM_LAYERS> m_aoLayerViews; // per-layer views

    // ---- Full Resolution AO ----
    std::shared_ptr<RHITexture> m_fullAOTex;             // R8_UNORM, full res
    std::shared_ptr<RHITexture> m_blurredAOTex;          // R8_UNORM, full res — final output

    // ---- Samplers ----
    std::shared_ptr<RHISampler> m_aoSampler;
    std::shared_ptr<RHISampler> m_deinterleaveSampler;

    // ---- Deinterleave (Compute) ----
    std::shared_ptr<RHIBindingLayout> m_deinterleaveLayout;
    std::shared_ptr<RHIBindingGroup>  m_deinterleaveGroup;
    std::shared_ptr<RHIPipeline>      m_deinterleavePipeline;

    // ---- SSAO (Graphics, per-layer render pass) ----
    std::shared_ptr<RHIRenderPass>  m_ssaoRenderPass;
    std::array<std::shared_ptr<RHIFramebuffer>, NUM_LAYERS> m_ssaoFramebuffers;
    std::shared_ptr<RHIBindingLayout> m_ssaoLayout;
    std::shared_ptr<RHIPipeline>      m_ssaoPipeline;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    std::array<std::shared_ptr<RHIBuffer>, MAX_FRAMES_IN_FLIGHT> m_ssaoUBOs;
    std::array<std::shared_ptr<RHIBindingGroup>, MAX_FRAMES_IN_FLIGHT> m_ssaoGroups;

    // ---- Reinterleave (Compute) ----
    std::shared_ptr<RHIBindingLayout> m_reinterleaveLayout;
    std::shared_ptr<RHIBindingGroup>  m_reinterleaveGroup;
    std::shared_ptr<RHIPipeline>      m_reinterleavePipeline;

    // ---- Blur (Graphics, fullscreen) ----
    std::shared_ptr<RHIRenderPass>    m_blurRenderPass;
    std::shared_ptr<RHIFramebuffer>   m_blurFramebuffer;
    std::shared_ptr<RHIBindingLayout> m_blurLayout;
    std::shared_ptr<RHIBindingGroup>  m_blurGroup;
    std::shared_ptr<RHIPipeline>      m_blurPipeline;
};