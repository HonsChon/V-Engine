/**
 * @file SceneRenderer.cpp
 * @brief SceneRenderer 实现 — RHI 无关的渲染调度(Vulkan/DX12 双后端)
 */

#include "SceneRenderer.h"

#include "Camera.h"
#include "Scene.h"
#include "Entity.h"
#include "Components.h"
#include "RenderSystem.h"

// Render Passes
#include "ForwardPass.h"
#include "GBufferPass.h"
#include "LightingPass.h"
#include "TransparentPass.h"
#include "ReflectionPass.h"
#include "FXAAPass.h"
#include "SSRPass.h"
#include "WaterPass.h"
#include "ssao/SSAOPass.h"
#include "GPUDrivenRenderer.h"
#include "NaniteDebugPass.h"

// Nanite
#include "nanite/Nanite.h"
#include "nanite/NaniteManager.h"

// RHI
#include "RHIDevice.h"
#include "RHISwapChain.h"
#include "RHIRenderPass.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"
#include "RHICommandBuffer.h"


// UI
#include "ImGuiLayer.h"
#include "UIManager.h"
#include "panels/DebugPanel.h"
#include "panels/InspectorPanel.h"

#include <iostream>
#include <array>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

// ============================================================
// 构造 & 析构
// ============================================================

SceneRenderer::SceneRenderer(RHIDevice* device, RHISwapChain* swapChain)
    : m_rhiDevice(device), m_swapChain(swapChain)
{
}

SceneRenderer::~SceneRenderer() {
    cleanup();
}

// ============================================================
// 初始化 & 清理
// ============================================================

void SceneRenderer::initialize() {
    if (m_initialized) return;

    auto extent = m_swapChain->getExtent();
    uint32_t w = extent.width;
    uint32_t h = extent.height;

    // FXAA 离屏场景目标（先建——场景 Pass 的管线都针对它的 RenderPass 创建）
    createOffscreenTarget();
    std::cout << "[SceneRenderer] Offscreen scene target created (FXAA)\n";

    // 创建 ForwardPass（始终可用）— 场景渲染进离屏目标
    m_forwardPass = std::make_unique<ForwardPass>(
        m_rhiDevice,
        m_offscreenRenderPass.get(), w, h, MAX_FRAMES_IN_FLIGHT);
    std::cout << "[SceneRenderer] ForwardPass created (RHI)\n";

    // FXAA 合成 Pass（输出到 swapchain，在 UI 之前）
    m_fxaaPass = std::make_unique<FXAAPass>(
        m_rhiDevice, m_swapChain->getRHIRenderPass(), w, h);

    m_initialized = true;
    std::cout << "[SceneRenderer] Initialized\n";
}

void SceneRenderer::cleanup() {
    if (!m_initialized) return;
    
    if (m_rhiDevice) m_rhiDevice->waitIdle();

    cleanupNanite();
    cleanupGPUDrivenRendering();
    cleanupDeferredShading();
    m_fxaaPass.reset();
    m_forwardPass.reset();
    cleanupOffscreenTarget();
    m_initialized = false;

    std::cout << "[SceneRenderer] Cleaned up\n";
}

// ============================================================
// 延迟渲染（按需初始化）
// ============================================================

void SceneRenderer::initDeferredShading() {
    if (m_deferredInitialized) return;

    std::cout << "[SceneRenderer] Initializing deferred shading...\n";

    auto extent = m_swapChain->getExtent();
    uint32_t w = extent.width;
    uint32_t h = extent.height;

    try {
        // 1. GBuffer (now uses RHI)
        m_gbuffer = std::make_unique<GBufferPass>(m_rhiDevice, w, h, MAX_FRAMES_IN_FLIGHT);
        std::cout << "  GBuffer created (RHI)\n";

        // 2. SSR
        m_ssrPass = std::make_unique<SSRPass>(m_rhiDevice, w, h);
        std::cout << "  SSR Pass created\n";

        // 3. Water（合成阶段 → 离屏目标，FXAA 后进 swapchain）
        m_waterPass = std::make_unique<WaterPass>(m_rhiDevice, w, h, m_offscreenRenderPass.get());
        m_waterPass->setWaterHeight(-1.5f);
        m_waterPass->setWaterColor(glm::vec3(0.0f, 0.4f, 0.6f), 0.7f);
        std::cout << "  Water Pass created\n";

        // 3.2 平面反射（镜像相机 → 半分辨率 RT；复用 ForwardPass 材质布局）
        if (m_forwardPass) {
            m_reflectionPass = std::make_unique<ReflectionPass>(
                m_rhiDevice, w, h, m_forwardPass->getMaterialLayout(), MAX_FRAMES_IN_FLIGHT);
            std::cout << "  Reflection Pass created (planar, half-res RT)\n";
        }

        // 3.5 Transparent（延迟模式半透明前向绘制，复用 ForwardPass 材质布局）
        if (m_forwardPass) {
            m_transparentPass = std::make_unique<TransparentPass>(
                m_rhiDevice, m_offscreenRenderPass.get(),
                m_forwardPass->getMaterialLayout(), w, h, MAX_FRAMES_IN_FLIGHT);
            std::cout << "  Transparent Pass created\n";
        }

        // 4. Scene color image (for SSR sampling)
        createSceneColorImage();
        std::cout << "  Scene color image created\n";

        // 5. GBuffer descriptor sets
        if (m_gbuffer) {
            m_gbuffer->createDescriptorSets();
            std::cout << "  GBuffer descriptor sets created\n";
        }

        // 6. LightingPass（Pure RHI；合成阶段 → 离屏目标）
        m_lightingPass = std::make_unique<LightingPass>(
            m_rhiDevice, w, h,
            m_offscreenRenderPass.get(), MAX_FRAMES_IN_FLIGHT);
        m_lightingPass->setAmbientLight(glm::vec3(0.03f), 1.0f);
        std::cout << "  LightingPass created (Pure RHI)\n";

        // 7. SSAOPass
        m_ssaoPass = std::make_unique<SSAOPass>(m_rhiDevice, w, h);
        m_ssaoPass->init();
        std::cout << "  SSAOPass created (" << w << "x" << h << ")\n";

        // 8. Set LightingPass G-Buffer inputs (Pure RHI textures)
        if (m_gbuffer) {
            m_lightingPass->setGBufferInputs(
                m_gbuffer->getPositionTexture(),
                m_gbuffer->getNormalTexture(),
                m_gbuffer->getAlbedoTexture(),
                m_gbuffer->getRHISampler()
            );
            std::cout << "  LightingPass G-Buffer inputs set (Pure RHI)\n";
        }

        // 9. Bind SSAO output to LightingPass (binding 4)
        if (m_ssaoPass) {
            m_lightingPass->setSSAOTexture(
                m_ssaoPass->getOutputAOTexture(),
                m_ssaoPass->getOutputAOSampler()
            );
            std::cout << "  LightingPass SSAO texture bound (Pure RHI)\n";
        }

        // 10. Bind sceneColor RHI texture to WaterPass
        if (m_waterPass && m_gbuffer && m_sceneColorTexture) {
            m_waterPass->setGBufferInputs(m_gbuffer.get(),
                m_sceneColorTexture.get(), m_sceneColorSampler.get());
            std::cout << "  Water Pass descriptors bound (Pure RHI)\n";
        }

        // 11. Bind reflection RT to WaterPass (binding 5)
        if (m_waterPass && m_reflectionPass) {
            m_waterPass->setReflectionTexture(
                m_reflectionPass->getReflectionTexture(),
                m_reflectionPass->getReflectionSampler());
            std::cout << "  Water Pass reflection RT bound (Pure RHI)\n";
        }

        m_deferredInitialized = true;
        std::cout << "[SceneRenderer] Deferred shading initialized!\n";

    } catch (const std::exception& e) {
        std::cerr << "[SceneRenderer] Failed to init deferred: " << e.what() << "\n";
        cleanupDeferredShading();
        m_settings.renderMode = RenderMode::Normal;
    }
}

void SceneRenderer::cleanupDeferredShading() {
    if (m_rhiDevice) m_rhiDevice->waitIdle();

    cleanupSceneColorImage();
    m_waterPass.reset();
    m_reflectionPass.reset();
    m_ssrPass.reset();
    m_ssaoPass.reset();
    m_transparentPass.reset();
    m_lightingPass.reset();
    m_gbuffer.reset();
    m_deferredInitialized = false;
}

// ============================================================
// FXAA 离屏场景目标
// ============================================================

void SceneRenderer::createOffscreenTarget() {
    auto extent = m_swapChain->getExtent();
    uint32_t w = extent.width;
    uint32_t h = extent.height;

    // 颜色（与 swapchain 同格式；finalLayout=ShaderReadOnly 供 FXAA 采样）
    RHITextureDesc colorDesc{};
    colorDesc.width = w;
    colorDesc.height = h;
    colorDesc.format = RHIFormat::R8G8B8A8_UNORM;
    colorDesc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled;
    m_offscreenColor = m_rhiDevice->createTexture(colorDesc);

    // 深度（Forward 场景深度；延迟合成阶段不使用但保持 RP 结构统一）
    RHITextureDesc depthDesc{};
    depthDesc.width = w;
    depthDesc.height = h;
    depthDesc.format = RHIFormat::D32_SFLOAT;
    depthDesc.usage = RHITextureUsage::DepthStencilAttachment;
    m_offscreenDepth = m_rhiDevice->createTexture(depthDesc);

    // 后处理采样器（Clamp）
    RHISamplerDesc sampDesc{};
    sampDesc.magFilter = RHIFilter::Linear;
    sampDesc.minFilter = RHIFilter::Linear;
    sampDesc.addressModeU = RHIAddressMode::ClampToEdge;
    sampDesc.addressModeV = RHIAddressMode::ClampToEdge;
    sampDesc.addressModeW = RHIAddressMode::ClampToEdge;
    m_offscreenSampler = m_rhiDevice->createSampler(sampDesc);

    // RenderPass：color + depth（结构与 swapchain RP 一致，各场景管线可无差别重绑）
    RHIRenderPassDesc rpDesc;
    rpDesc.addColorAttachment(
        RHIFormat::R8G8B8A8_UNORM,
        RHILoadOp::Clear, RHIStoreOp::Store,
        RHIImageLayout::Undefined, RHIImageLayout::ShaderReadOnly);
    rpDesc.setDepthAttachment(
        RHIFormat::D32_SFLOAT,
        RHILoadOp::Clear, RHIStoreOp::DontCare,
        RHIImageLayout::Undefined, RHIImageLayout::DepthStencilAttachment);
    m_offscreenRenderPass = m_rhiDevice->createRenderPass(rpDesc);

    RHIFramebufferDesc fbDesc;
    fbDesc.renderPass = m_offscreenRenderPass.get();
    fbDesc.width = w;
    fbDesc.height = h;
    fbDesc.attachments.push_back(m_offscreenColor.get());
    fbDesc.attachments.push_back(m_offscreenDepth.get());
    m_offscreenFramebuffer = m_rhiDevice->createFramebuffer(fbDesc);
}

void SceneRenderer::cleanupOffscreenTarget() {
    m_offscreenFramebuffer.reset();
    m_offscreenRenderPass.reset();
    m_offscreenSampler.reset();
    m_offscreenDepth.reset();
    m_offscreenColor.reset();
}

void SceneRenderer::createSceneColorImage() {
    auto extent = m_swapChain->getExtent();
    uint32_t w = extent.width;
    uint32_t h = extent.height;

    // Create scene color texture via RHI
    RHITextureDesc texDesc{};
    texDesc.width = w;
    texDesc.height = h;
    texDesc.format = RHIFormat::R8G8B8A8_UNORM;
    texDesc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled | RHITextureUsage::TransferDst;
    m_sceneColorTexture = m_rhiDevice->createTexture(texDesc);

    // Create scene color sampler via RHI
    RHISamplerDesc sampDesc{};
    sampDesc.magFilter = RHIFilter::Linear;
    sampDesc.minFilter = RHIFilter::Linear;
    sampDesc.addressModeU = RHIAddressMode::ClampToEdge;
    sampDesc.addressModeV = RHIAddressMode::ClampToEdge;
    sampDesc.addressModeW = RHIAddressMode::ClampToEdge;
    sampDesc.anisotropyEnable = false;
    sampDesc.maxAnisotropy = 1.0f;
    sampDesc.mipMapFilter = RHIFilter::Linear;
    sampDesc.minLod = 0.0f;
    sampDesc.maxLod = 1.0f;
    m_sceneColorSampler = m_rhiDevice->createSampler(sampDesc);
}

void SceneRenderer::cleanupSceneColorImage() {
    m_sceneColorSampler.reset();
    m_sceneColorTexture.reset();
}

// ============================================================
// Uniform 更新
// ============================================================

glm::mat4 SceneRenderer::applyApiYFlip(const glm::mat4& proj) const {
    if (m_rhiDevice && m_rhiDevice->getBackend() == RHIBackend::Vulkan) {
        glm::mat4 flipped = proj;
        flipped[1][1] *= -1.0f;
        return flipped;
    }
    return proj;
}

glm::mat4 SceneRenderer::makeObliqueProjection(const glm::mat4& proj, const glm::vec4& clipPlane) const {
    // 斜近裁剪投影 —— [0,1] 深度约定版（适配本引擎实际生效的 API 裁剪区间）
    //
    // ⚠ 根因背景（RenderDoc + 数值分析定位，2026/09/17）：
    //   引擎未定义 GLM_FORCE_DEPTH_ZERO_TO_ONE，投影矩阵产出 GL 约定
    //   z_ndc ∈ [-1,1]，而 Vulkan/DX12 均按 [0,1] 裁剪。普通渲染该错配
    //   只影响距相机 <0.2 单位的区域（无感）；但 Lengyel 原式把裁剪平面
    //   映射到 z_ndc = -1（GL 近平面），平面正侧一大段带落入 [-1,0) 被
    //   API 硬裁——平面反射中表现为"贴水带整段消失，只剩远水一半"。
    //   Vulkan 上投影 Y 翻转还使原式 D=dot(C,q)<0，方向再反转。
    //
    // [0,1] 约定推导（列向量，clip = P·v）：
    //   近测试为 z_clip ≥ 0 → 直接令第三行 r = k·C（平面自然映射到
    //   z_clip = 0，正侧 C·p>0 得正 z ✓ 无 GL 式的 +1 偏移）。
    //   比例 k 由远角钉扎确定：取远平面角落 Q = P⁻¹·(±sgnCx,±sgnCy,1,1)
    //   （该点 z_clip 旧值 = 1，w_clip = 1），令 k·(C·Q) = 1 → k = 1/dot(C,Q)。
    //   角点符号选使 dot(C,Q) > 0 的角——自动兼容 Vulkan Y 翻转与 DX12
    //   （翻转使 D 变号时取对侧角，两后端数值一致）。
    //   相机空间约定：C 正侧为可见侧（水上），相机须在负侧（水下镜像机位）。
    //
    // 数值自检（Vulkan 翻转矩阵，C=(0,-1,0,-1.52)，相机(0,0,8)）：
    //   球底 cam(0,-2,-8,1) → z_clip=+1.20, w=8, z_ndc=0.15 ∈[0,1] ✓
    //   球顶 cam(0,-4,-8,1) → z_ndc=0.78 ✓   水下点 → z_clip<0 被裁 ✓
    auto sgn = [](float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); };

    glm::mat4 m = proj;
    glm::mat4 invP = glm::inverse(proj);

    glm::vec4 Q = invP * glm::vec4(sgn(clipPlane.x), sgn(clipPlane.y), 1.0f, 1.0f);
    float D = glm::dot(clipPlane, Q);
    if (D < 0.0f) {
        // Vulkan Y 翻转会变号 → 取对侧远角（该角 dot>0 且同样是远平面角落）
        Q = invP * glm::vec4(-sgn(clipPlane.x), -sgn(clipPlane.y), 1.0f, 1.0f);
        D = glm::dot(clipPlane, Q);
    }
    if (std::abs(D) < 1e-8f) return proj;   // 退化保护：平面穿过远角落

    // k = 1/D。钳制上限：角仰角趋近水平（相机接近水面出画的仰角）时 D→0、
    // k→∞，倾斜远平面压垮一切反射内容（表现为转动相机时倒影突然消失/闪烁）。
    // row = k·C 对任意 k>0 都精确映射水面到 z=0（水下剔除不受影响），k 只
    // 决定远平面倾斜度；水面入画时反射内容的 C·p/w 理论极限 ≈ tan45° = 1，
    // kmax = 1.5（约束 0.67）覆盖全部可见内容，RT 深度无后续消费，安全。
    float k = glm::min(1.0f / D, 1.5f);
    glm::vec4 r = clipPlane * k;             // 新第三行 = k·C
    m[0][2] = r.x;
    m[1][2] = r.y;
    m[2][2] = r.z;
    m[3][2] = r.w;
    return m;
}

void SceneRenderer::updateUniforms(uint32_t frameIndex) {
    if (!m_camera) return;

    // 计算公共矩阵
    glm::mat4 view = m_camera->getViewMatrix();
    float fov = glm::radians(m_camera->getZoom());
    auto scExtent = m_swapChain->getExtent();
    float aspect = scExtent.width / (float)scExtent.height;
    glm::mat4 proj = applyApiYFlip(glm::perspective(fov, aspect, 0.1f, 100.0f));
    glm::vec3 camPos = m_camera->getPosition();

    // 光源（固定位置）
    glm::vec3 lightPos(5.0f, 3.0f, 5.0f);

    // ForwardPass UBO
    if (m_forwardPass) {
        ForwardPass::UniformBufferObject ubo{};
        ubo.view = view;
        ubo.proj = proj;
        ubo.viewPos = glm::vec4(camPos, 1.0f);
        ubo.lightPos = glm::vec4(lightPos, 1.0f);
        ubo.lightColor = glm::vec4(300.0f, 300.0f, 300.0f, 1.0f);
        m_forwardPass->updateUniformBuffer(frameIndex, ubo);
    }

    // 延迟渲染模式专属
    if (m_settings.renderMode == RenderMode::WaterScene && m_deferredInitialized) {
        // GBuffer UBO
        if (m_gbuffer) {
            GBufferPass::UniformBufferObject gbufferUBO{};
            gbufferUBO.view = view;
            gbufferUBO.proj = proj;
            gbufferUBO.viewPos = glm::vec4(camPos, 1.0f);
            gbufferUBO.lightPos = glm::vec4(lightPos, 1.0f);
            gbufferUBO.lightColor = glm::vec4(300.0f, 300.0f, 300.0f, 1.0f);
            m_gbuffer->updateUniformBuffer(frameIndex, gbufferUBO);
        }

        // LightingPass
        if (m_lightingPass) {
            m_lightingPass->updateUniforms(frameIndex, camPos, lightPos,
                                            glm::vec3(300.0f, 300.0f, 300.0f), 1.0f);
        }

        // TransparentPass（与 ForwardPass 同光照 + 视口尺寸）
        if (m_transparentPass) {
            TransparentPass::UniformBufferObject tUbo{};
            tUbo.view = view;
            tUbo.proj = proj;
            tUbo.viewPos = glm::vec4(camPos, 1.0f);
            tUbo.lightPos = glm::vec4(lightPos, 1.0f);
            tUbo.lightColor = glm::vec4(300.0f, 300.0f, 300.0f, 1.0f);
            tUbo.viewportInfo = glm::vec4(
                static_cast<float>(scExtent.width), static_cast<float>(scExtent.height), 0.0f, 0.0f);
            m_transparentPass->updateUniformBuffer(frameIndex, tUbo);
        }

        // 平面反射：镜像相机 + 斜近裁剪投影
        if (m_reflectionPass) {
            float waterHeight = m_waterPass ? m_waterPass->getWaterHeight() : 0.0f;
            ReflectionPass::UniformBufferObject reflUBO{};
            reflUBO.lightPos = glm::vec4(lightPos, 1.0f);
            reflUBO.lightColor = glm::vec4(300.0f, 300.0f, 300.0f, 1.0f);

            if (camPos.y > waterHeight + 0.05f) {
                m_cameraAboveWater = true;
                // 绕 y=waterHeight 的镜像矩阵：p.y' = 2h - p.y
                glm::mat4 mirror =
                    glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f * waterHeight, 0.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
                glm::mat4 reflView = view * mirror;
                glm::vec3 reflCamPos(camPos.x, 2.0f * waterHeight - camPos.y, camPos.z);
                // 世界空间水面平面 (0,1,0,-h)（正侧=水上）→ 镜像相机空间。
                // +0.02 偏置：与水面共面的几何（如默认场景的地面 Plane）恰好
                // 落在裁剪边界上会逐像素随机裁剪 → 反射中出现闪烁条纹；
                // 抬高裁剪面使其稳定落在被裁侧
                glm::vec4 clipPlane = glm::transpose(glm::inverse(reflView)) *
                                      glm::vec4(0.0f, 1.0f, 0.0f, -(waterHeight + 0.02f));
                reflUBO.view = reflView;
                // 反射专用基础投影：远平面加长到 1000。斜裁剪会使远平面绕
                // 「水面∩远平面」交线倾斜，倾斜程度 ∝ h/far —— far=100 时
                // 崩溃带约 h/far 弧度宽（水面尚未转出画框，倒影先消失/被裁）；
                // far=1000 将其压缩到 ~0.26°，恰好贴合水面物理出画的位置。
                // 主画面各 pass 的 far=100 不受影响。
                auto reflExtent = m_swapChain->getExtent();
                float reflAspect = reflExtent.width / (float)reflExtent.height;
                glm::mat4 reflProjBase = applyApiYFlip(glm::perspective(
                    fov, reflAspect, 0.1f, 1000.0f));
                reflUBO.proj = makeObliqueProjection(reflProjBase, clipPlane);
                reflUBO.viewPos = glm::vec4(reflCamPos, 1.0f);
            } else {
                // 相机在水下：镜像相机无效（斜近裁剪要求相机在平面负侧），
                // 本帧只清屏，水面采样到天空色回退
                m_cameraAboveWater = false;
                reflUBO.view = view;
                reflUBO.proj = proj;
                reflUBO.viewPos = glm::vec4(camPos, 1.0f);
            }
            m_reflectionPass->updateUniformBuffer(frameIndex, reflUBO);
        }

        // Water & SSR
        if (m_waterPass) {
            m_waterPass->setReflectionParams(
                m_settings.waterPlanarReflection ? 1.0f : 0.0f,
                m_settings.waterReflectionDistortion);
            m_waterPass->updateUniforms(view, proj, camPos, m_totalTime, frameIndex);
        }
        if (m_ssrPass) {
            m_ssrPass->updateParams(proj, view, camPos, frameIndex);
        }
    }
}

// ============================================================
// 命令录制 — 入口
// ============================================================

void SceneRenderer::recordCommands(RHICommandBuffer* cmd, uint32_t imageIndex, uint32_t frameIndex) {
    if (m_settings.renderMode == RenderMode::WaterScene && m_deferredInitialized) {
        recordDeferredCommands(cmd, imageIndex, frameIndex);
    } else {
        recordForwardCommands(cmd, imageIndex, frameIndex);
    }
}

// ============================================================
// 命令录制 — 前向渲染
// ============================================================

void SceneRenderer::recordForwardCommands(RHICommandBuffer* cmd, uint32_t imageIndex, uint32_t frameIndex) {
    // GPU Culling (Compute, before render pass)
    if (m_settings.enableGPUCulling && m_gpuDrivenRenderer) {
        m_gpuDrivenRenderer->executeCulling(cmd);
        // Barrier already handled inside FrustumCullingPass::record()
    }

    // Nanite GPU Culling (Compute, before render pass)
    if (m_settings.showClusterVisualization && m_naniteManager && m_naniteDebugPass) {
        prepareNaniteCulling(cmd, imageIndex, frameIndex);
    }

    // === Pass 1: 场景 → 离屏目标（FXAA 输入）===
    {
        std::vector<RHIClearValue> clears = {
            RHIClearValue::Color(0.1f, 0.2f, 0.4f, 1.0f),
            RHIClearValue::DepthStencil(1.0f, 0)
        };
        cmd->beginRenderPass(m_offscreenRenderPass.get(), m_offscreenFramebuffer.get(), clears);
    }

    m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "Scene Rendering", 0.2f, 0.8f, 0.2f, 1.0f);

    if (m_forwardPass && m_scene && m_renderSystem) {
        m_forwardPass->begin(cmd);
        m_forwardPass->bindPipeline(cmd);

        if (m_settings.enableGPUCulling && m_gpuDrivenRenderer) {
            RHIBuffer* indirectBuffer = m_gpuDrivenRenderer->getIndirectDrawBuffer();
            if (indirectBuffer != nullptr) {
                m_forwardPass->bindGlobalDescriptorSet(cmd, frameIndex);

                const auto& visibleIndices = m_gpuDrivenRenderer->getVisibleIndices();
                uint32_t visibleCount = static_cast<uint32_t>(visibleIndices.size());

                auto& registry = m_scene->getRegistry();
                auto ecsView = registry.view<VEngine::TransformComponent, VEngine::MeshRendererComponent>();
                std::vector<entt::entity> entityList;
                for (auto e : ecsView) entityList.push_back(e);

                for (uint32_t i = 0; i < visibleCount; ++i) {
                    uint32_t idx = visibleIndices[i];
                    if (idx >= entityList.size()) continue;

                    auto entity = entityList[idx];
                    auto& meshRenderer = ecsView.get<VEngine::MeshRendererComponent>(entity);

                    // 透明物体不走间接绘制（无序混合会出错），由 CPU 路径排序绘制
                    if (m_renderSystem->isTransparentEntity(entity)) continue;

                    auto gpuMesh = VEngine::MeshManager::getInstance().getMesh(meshRenderer.meshPath);
                    if (!gpuMesh) continue;

                    // 查找 renderable（取材质描述符 + 透明参数，Mask alpha test 需要）
                    glm::vec4 materialParams(1.0f, 0.0f, 0.5f, 0.0f);
                    for (const auto& r : m_renderSystem->getRenderables()) {
                        if (r.entityHandle == entity) {
                            materialParams = r.materialParams;
                            break;
                        }
                    }
                    m_forwardPass->pushModelMatrix(cmd, VEngine::computeWorldMatrix(registry, entity), materialParams);

                    ForwardPass::MaterialDescriptor* matDesc = nullptr;
                    for (const auto& r : m_renderSystem->getRenderables()) {
                        if (r.entityHandle == entity && r.materialDescriptor) {
                            matDesc = r.materialDescriptor; break;
                        }
                    }
                    if (!matDesc || !matDesc->valid) continue;
                    m_forwardPass->bindMaterialDescriptorSet(cmd, frameIndex, matDesc);

                    m_forwardPass->drawMesh(cmd,
                        gpuMesh->getVertexBuffer(),
                        gpuMesh->getIndexBuffer(),
                        gpuMesh->getIndexCount());
                }

                // 透明物体：CPU 收集 + back-to-front 排序后用透明管线绘制
                m_renderSystem->renderTransparent(cmd, m_forwardPass.get(), frameIndex);
            } else {
                m_renderSystem->render(cmd, m_forwardPass.get(), frameIndex);
            }
        } else {
            if (m_settings.showClusterVisualization && m_naniteDebugPass) {
                recordNaniteDebugCommands(cmd, frameIndex);
            } else {
                m_renderSystem->render(cmd, m_forwardPass.get(), frameIndex);
            }
        }
    }

    m_rhiDevice->endDebugLabel(cmd->getNativeHandle());
    cmd->endRenderPass();   // 结束离屏场景 Pass

    // === Pass 2: swapchain — FXAA 合成 + UI ===
    {
        std::vector<RHIClearValue> clears = {
            RHIClearValue::Color(0.1f, 0.2f, 0.4f, 1.0f),
            RHIClearValue::DepthStencil(1.0f, 0)
        };
        cmd->beginRenderPass(
            m_swapChain->getRHIRenderPass(),
            m_swapChain->getRHIFramebuffer(imageIndex),
            clears);
    }

    // FXAA（关闭时 shader 直通）
    m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "FXAA Pass", 0.4f, 0.8f, 0.4f, 1.0f);
    if (m_fxaaPass && m_offscreenColor) {
        m_fxaaPass->setSourceTexture(m_offscreenColor.get(), m_offscreenSampler.get());
        m_fxaaPass->render(cmd, frameIndex, m_settings.enableFXAA);
    }
    m_rhiDevice->endDebugLabel(cmd->getNativeHandle());

    // UI
    m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "UI Rendering", 0.8f, 0.2f, 0.8f, 1.0f);
    updateUI();
    renderUI(cmd);
    m_rhiDevice->endDebugLabel(cmd->getNativeHandle());

    cmd->endRenderPass();
}

// ============================================================
// 命令录制 — 延迟渲染
// ============================================================

void SceneRenderer::recordDeferredCommands(RHICommandBuffer* cmd, uint32_t imageIndex, uint32_t frameIndex) {
    m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "Deferred Pipeline", 0.2f, 0.6f, 0.9f, 1.0f);

    auto dExtent = m_swapChain->getExtent();
    uint32_t w = dExtent.width;
    uint32_t h = dExtent.height;

    // === Pass 1: GBuffer ===
    m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "GBuffer Pass", 0.4f, 0.8f, 0.2f, 1.0f);
    if (m_gbuffer && m_scene) {
        m_gbuffer->beginRenderPass(cmd);
        m_gbuffer->bindPipeline(cmd);

        if (m_renderSystem) {
            m_renderSystem->render(cmd, m_gbuffer.get(), frameIndex);
        }
        m_gbuffer->endRenderPass(cmd);
    }
    m_rhiDevice->endDebugLabel(cmd->getNativeHandle());

    // === Pass 1.5: Blit Albedo → SceneColor ===
    m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "Blit Albedo -> SceneColor", 0.9f, 0.7f, 0.2f, 1.0f);
    if (m_gbuffer && m_sceneColorTexture) {
        // Pre-blit transitions
        cmd->transitionImageLayout(
            m_gbuffer->getAlbedoTexture(),
            RHIImageLayout::ShaderReadOnly, RHIImageLayout::TransferSrc,
            RHIPipelineStage::ColorAttachmentOutput, RHIPipelineStage::Transfer);

        cmd->transitionImageLayout(
            m_sceneColorTexture.get(),
            RHIImageLayout::Undefined, RHIImageLayout::TransferDst,
            RHIPipelineStage::TopOfPipe, RHIPipelineStage::Transfer);

        // Blit
        cmd->blitImage(
            m_gbuffer->getAlbedoTexture(), RHIImageLayout::TransferSrc,
            m_sceneColorTexture.get(), RHIImageLayout::TransferDst,
            w, h, w, h, RHIFilter::Linear);

        // Post-blit transitions
        cmd->transitionImageLayout(
            m_gbuffer->getAlbedoTexture(),
            RHIImageLayout::TransferSrc, RHIImageLayout::ShaderReadOnly,
            RHIPipelineStage::Transfer, RHIPipelineStage::FragmentShader);

        cmd->transitionImageLayout(
            m_sceneColorTexture.get(),
            RHIImageLayout::TransferDst, RHIImageLayout::ShaderReadOnly,
            RHIPipelineStage::Transfer, RHIPipelineStage::FragmentShader);
    }
    m_rhiDevice->endDebugLabel(cmd->getNativeHandle());

    // === Pass 1.8: SSAO ===
    m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "SSAO Pass", 0.6f, 0.3f, 0.8f, 1.0f);
    if (m_ssaoPass && m_gbuffer) {
        if (m_settings.enableSSAO) {
            float aspect = (float)w / (float)h;
            glm::mat4 projection = applyApiYFlip(glm::perspective(
                glm::radians(m_camera ? m_camera->getZoom() : 45.0f), aspect, 0.1f, 100.0f));
            glm::mat4 view = m_camera ? m_camera->getViewMatrix() : glm::mat4(1.0f);
            m_ssaoPass->execute(cmd, m_gbuffer.get(), frameIndex, projection, view);
        } else {
            // SSAO 关闭：清空 AO 纹理为白色（1.0 = 无遮蔽）
            cmd->transitionImageLayout(
                m_ssaoPass->getOutputAOTexture(),
                RHIImageLayout::ShaderReadOnly, RHIImageLayout::TransferDst,
                RHIPipelineStage::FragmentShader, RHIPipelineStage::Transfer);
            cmd->clearColorImage(
                m_ssaoPass->getOutputAOTexture(), 1.0f, 1.0f, 1.0f, 1.0f);
            cmd->transitionImageLayout(
                m_ssaoPass->getOutputAOTexture(),
                RHIImageLayout::TransferDst, RHIImageLayout::ShaderReadOnly,
                RHIPipelineStage::Transfer, RHIPipelineStage::FragmentShader);
        }
    }
    m_rhiDevice->endDebugLabel(cmd->getNativeHandle());

    // === Pass 2: SSR ===
    m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "SSR Pass", 0.2f, 0.8f, 0.8f, 1.0f);
    if (m_ssrPass && m_gbuffer && m_sceneColorTexture) {
        m_ssrPass->execute(cmd, m_gbuffer.get(),
            m_sceneColorTexture.get(), m_sceneColorSampler.get(), frameIndex);
    }
    m_rhiDevice->endDebugLabel(cmd->getNativeHandle());

    // === Pass 2.5: 平面反射（镜像相机 → 半分辨率 RT，WaterPass 采样）===
    m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "Planar Reflection", 0.8f, 0.5f, 0.2f, 1.0f);
    if (m_reflectionPass && m_renderSystem && m_settings.waterPlanarReflection) {
        m_reflectionPass->render(cmd, m_renderSystem, frameIndex, m_cameraAboveWater);
    }
    m_rhiDevice->endDebugLabel(cmd->getNativeHandle());

    // === Pass 3: Final Composition (Swapchain RenderPass) ===
    m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "Final Composition", 0.9f, 0.4f, 0.1f, 1.0f);
    {
        std::vector<RHIClearValue> clears = {
            RHIClearValue::Color(0.02f, 0.05f, 0.1f, 1.0f),
            RHIClearValue::DepthStencil(1.0f, 0)
        };
        cmd->beginRenderPass(m_offscreenRenderPass.get(), m_offscreenFramebuffer.get(), clears);

        // Deferred Lighting
        m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "Lighting Pass", 1.0f, 0.9f, 0.3f, 1.0f);
        if (m_lightingPass && m_gbuffer) {
            m_lightingPass->render(cmd, frameIndex);
        }
        m_rhiDevice->endDebugLabel(cmd->getNativeHandle());

        // Transparent（半透明物体前向绘制，手动深度剔除 against GBuffer depth）
        m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "Transparent Pass", 0.6f, 0.9f, 0.3f, 1.0f);
        if (m_transparentPass && m_gbuffer && m_renderSystem) {
            m_transparentPass->setDepthTexture(m_gbuffer->getDepthTexture(),
                                               m_gbuffer->getRHISampler());
            m_transparentPass->render(cmd, m_renderSystem, frameIndex);
        }
        m_rhiDevice->endDebugLabel(cmd->getNativeHandle());

        // Water
        m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "Water Pass", 0.1f, 0.5f, 0.9f, 1.0f);
        if (m_waterPass) {
            m_waterPass->render(cmd, frameIndex);
        }
        m_rhiDevice->endDebugLabel(cmd->getNativeHandle());

        cmd->endRenderPass();   // 结束离屏合成 Pass

        // === swapchain: FXAA 合成 + UI ===
        cmd->beginRenderPass(
            m_swapChain->getRHIRenderPass(),
            m_swapChain->getRHIFramebuffer(imageIndex),
            clears);

        m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "FXAA Pass", 0.4f, 0.8f, 0.4f, 1.0f);
        if (m_fxaaPass && m_offscreenColor) {
            m_fxaaPass->setSourceTexture(m_offscreenColor.get(), m_offscreenSampler.get());
            m_fxaaPass->render(cmd, frameIndex, m_settings.enableFXAA);
        }
        m_rhiDevice->endDebugLabel(cmd->getNativeHandle());

        // UI
        m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "UI Rendering", 0.8f, 0.2f, 0.8f, 1.0f);
        updateUI();
        renderUI(cmd);
        m_rhiDevice->endDebugLabel(cmd->getNativeHandle());

        cmd->endRenderPass();
    }
    m_rhiDevice->endDebugLabel(cmd->getNativeHandle()); // end Final Composition

    m_rhiDevice->endDebugLabel(cmd->getNativeHandle()); // end Deferred Pipeline
}

// ============================================================
// UI
// ============================================================

void SceneRenderer::updateUI() {
    if (!m_uiManager || !m_camera) return;

    auto* debugPanel = m_uiManager->getDebugPanel();
    if (debugPanel) {
        debugPanel->setCameraPosition(m_camera->getPosition());
        debugPanel->setCameraFOV(m_camera->getZoom());

        if (m_settings.showClusterVisualization && m_naniteDebugPass) {
            // Cluster Vis 激活时：统计跟随实际绘制的 cluster
            const auto& s = m_naniteDebugPass->getDrawnStats();
            debugPanel->setVertices(s.drawnVertices);
            debugPanel->setTriangles(s.drawnTriangles);
            debugPanel->setDrawCalls(s.drawnClusters);
            debugPanel->setNaniteStats(s.totalClusters, s.visibleClusters, s.drawnClusters,
                                       s.drawnTriangles, s.drawnVertices,
                                       s.lodClusterCounts, 8);
        } else {
            if (m_renderSystem) {
                debugPanel->setVertices(m_renderSystem->getTotalVertexCount());
                debugPanel->setTriangles(m_renderSystem->getTotalTriangleCount());
                debugPanel->setDrawCalls(m_renderSystem->getDrawCallCount());
            }
            debugPanel->setNaniteStats(0, 0, 0, 0, 0, nullptr, 0);
        }
    }

    // Inspector 需要 Nanite 数据来显示选中 mesh 的 cluster/LOD 统计
    if (auto* inspector = m_uiManager->getInspectorPanel()) {
        inspector->setNaniteManager(m_naniteManager.get());
    }
}

void SceneRenderer::renderUI(RHICommandBuffer* cmd) {
    if (!m_imguiLayer || !m_uiManager || !m_settings.showUI) return;
    m_imguiLayer->beginFrame();
    m_uiManager->render();
    m_imguiLayer->endFrame(cmd->getNativeHandle());
}

// ============================================================
// Resize
// ============================================================

void SceneRenderer::onResize(uint32_t width, uint32_t height) {
    // FXAA 离屏目标重建（新 RP 对象 → 各场景管线显式重绑）
    if (m_rhiDevice) m_rhiDevice->waitIdle();
    cleanupOffscreenTarget();
    createOffscreenTarget();

    if (m_forwardPass) {
        m_forwardPass->recreate(m_offscreenRenderPass.get(), width, height);
    }
    if (m_transparentPass) {
        m_transparentPass->recreate(m_offscreenRenderPass.get(), width, height);
    }
    if (m_fxaaPass) {
        m_fxaaPass->recreate(m_swapChain->getRHIRenderPass(), width, height);
    }
    // Lighting/Water/NaniteDebug: 离屏 RP 与 swapchain RP 结构一致，
    // Vulkan RP 兼容性下管线仍有效（与既有 resize 行为一致）
    if (m_ssaoPass) {
        m_ssaoPass->resize(width, height);
    }

    // 延迟管线的 RT（GBuffer/SceneColor/Water/SSR/Reflection）统一重建并重绑。
    // 修复：此前这些 pass 的 resize 未接线，窗口缩放后延迟管线保持旧尺寸
    // （blit 尺寸不匹配 / 采样错位）。
    if (m_deferredInitialized) {
        if (m_gbuffer) {
            m_gbuffer->resize(width, height);
        }
        cleanupSceneColorImage();
        createSceneColorImage();
        if (m_waterPass)  m_waterPass->resize(width, height);
        if (m_ssrPass)    m_ssrPass->resize(width, height);
        if (m_reflectionPass) m_reflectionPass->resize(width, height);

        // 纹理对象全部更新 → 消费方描述符重绑（与 initDeferredShading 一致）
        if (m_lightingPass && m_gbuffer) {
            m_lightingPass->setGBufferInputs(
                m_gbuffer->getPositionTexture(),
                m_gbuffer->getNormalTexture(),
                m_gbuffer->getAlbedoTexture(),
                m_gbuffer->getRHISampler());
        }
        if (m_lightingPass && m_ssaoPass) {
            m_lightingPass->setSSAOTexture(
                m_ssaoPass->getOutputAOTexture(),
                m_ssaoPass->getOutputAOSampler());
        }
        if (m_waterPass && m_gbuffer && m_sceneColorTexture) {
            m_waterPass->setGBufferInputs(m_gbuffer.get(),
                m_sceneColorTexture.get(), m_sceneColorSampler.get());
        }
        if (m_waterPass && m_reflectionPass) {
            m_waterPass->setReflectionTexture(
                m_reflectionPass->getReflectionTexture(),
                m_reflectionPass->getReflectionSampler());
        }
    }
}

void SceneRenderer::onSwapChainRecreated(RHISwapChain* newSwapChain) {
    m_swapChain = newSwapChain;
    auto ext = newSwapChain->getExtent();
    uint32_t w = ext.width;
    uint32_t h = ext.height;
    onResize(w, h);
}

// ============================================================
// GPU-Driven Rendering
// ============================================================

void SceneRenderer::initGPUDrivenRendering() {
    std::cout << "[SceneRenderer] Initializing GPU-Driven Rendering...\n";
    try {
        GPUDrivenRenderer::Config config;
        config.maxInstances = 100000;
        config.enableFrustumCulling = true;
        m_gpuDrivenRenderer = std::make_unique<GPUDrivenRenderer>(m_rhiDevice, config);
        m_gpuDrivenRenderer->init();
        std::cout << "[SceneRenderer] GPU-Driven Rendering initialized!\n";
    } catch (const std::exception& e) {
        std::cerr << "[SceneRenderer] GPU-Driven init failed: " << e.what() << "\n";
        m_gpuDrivenRenderer.reset();
        m_settings.enableGPUCulling = false;
    }
}

void SceneRenderer::cleanupGPUDrivenRendering() {
    m_gpuDrivenRenderer.reset();
}

void SceneRenderer::prepareGPUCullingData() {
    if (!m_gpuDrivenRenderer || !m_scene || !m_camera || !m_renderSystem) return;

    std::vector<GPUInstanceData> instances;
    auto& registry = m_scene->getRegistry();
    auto view = registry.view<VEngine::TransformComponent, VEngine::MeshRendererComponent>();
    auto* meshManager = m_renderSystem->getMeshManager();

    for (auto entity : view) {
        auto& meshRenderer = view.get<VEngine::MeshRendererComponent>(entity);

        GPUInstanceData data{};
        data.modelMatrix = VEngine::computeWorldMatrix(registry, entity);

        VEngine::AABB meshAABB;
        if (meshManager) meshAABB = meshManager->getMeshAABB(meshRenderer.meshPath);
        else { meshAABB.min = glm::vec3(-1.0f); meshAABB.max = glm::vec3(1.0f); }

        glm::vec3 center = (meshAABB.min + meshAABB.max) * 0.5f;
        float radius = glm::length(meshAABB.max - center);
        data.boundingSphere = glm::vec4(center, radius);
        data.aabbMin = glm::vec4(meshAABB.min, 0.0f);
        data.aabbMax = glm::vec4(meshAABB.max, 0.0f);
        data.meshIndex = static_cast<uint32_t>(entity);
        data.materialIndex = 0;
        data.flags = 1;
        data.padding = 0;
        instances.push_back(data);
    }
    if (instances.empty()) return;

    float fov = glm::radians(m_camera->getZoom());
    auto gpuExtent = m_swapChain->getExtent();
    float aspect = gpuExtent.width / (float)gpuExtent.height;
    glm::mat4 proj = applyApiYFlip(glm::perspective(fov, aspect, 0.1f, 100.0f));
    m_gpuDrivenRenderer->prepare(instances, m_camera->getViewMatrix(), proj, m_camera->getPosition());
}

// ============================================================
// Nanite
// ============================================================

void SceneRenderer::initNanite() {
    std::cout << "\n========================================\n";
    std::cout << "Initializing Nanite System...\n";
    std::cout << "========================================\n";
    try {
        m_naniteManager = std::make_unique<Nanite::NaniteManager>(m_rhiDevice);
        m_naniteManager->initialize();

        Nanite::NaniteConfig config;
        config.enableClusterCulling = true;
        config.enableConeCulling = true;
        config.enableLODSelection = true;
        config.screenSpaceErrorThreshold = 1.0f;
        m_naniteManager->setConfig(config);

        m_naniteInitialized = true;
        std::cout << "Nanite initialized!\n";
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize Nanite: " << e.what() << "\n";
        m_naniteManager.reset();
        m_naniteInitialized = false;
    }
}

void SceneRenderer::cleanupNanite() {
    if (m_naniteDebugPass) { m_naniteDebugPass->cleanup(); m_naniteDebugPass.reset(); }
    if (m_naniteManager) { m_naniteManager->cleanup(); m_naniteManager.reset(); }
    m_naniteInitialized = false;
    m_settings.showClusterVisualization = false;
    m_lastClusterizedMeshPath.clear();
}

void SceneRenderer::initNaniteDebugPass() {
    if (m_naniteDebugPass) return;
    if (!m_naniteManager) return;

    try {
        auto naniteShared = std::shared_ptr<Nanite::NaniteManager>(m_naniteManager.get(), [](Nanite::NaniteManager*){});

        m_naniteDebugPass = std::make_unique<NaniteDebugPass>(m_rhiDevice, m_swapChain, naniteShared);
        m_naniteDebugPass->initialize(m_offscreenRenderPass.get());   // 场景绘制 → 离屏（FXAA）
        m_naniteDebugPass->setClusterCullingPass(m_naniteManager->getCullingPass());

        if (!m_lastClusterizedMeshPath.empty()) {
            m_naniteDebugPass->setTargetMesh(m_lastClusterizedMeshPath);
        }
        std::cout << "[NaniteDebugPass] Initialized\n";
    } catch (const std::exception& e) {
        std::cerr << "[NaniteDebugPass] Init failed: " << e.what() << "\n";
        m_naniteDebugPass.reset();
    }
}

void SceneRenderer::cycleNaniteDebugMode() {
    if (!m_naniteDebugPass) {
        std::cout << "[Engine] Cluster Vis is OFF (press 9 first)\n";
        return;
    }
    m_naniteDebugPass->cycleDebugMode();
    std::cout << "[Engine] Nanite debug mode: " << m_naniteDebugPass->getDebugModeName() << "\n";
}

void SceneRenderer::testNaniteClustering() {
    if (!m_naniteManager || !m_renderSystem) return;

    std::cout << "\nTesting Nanite Mesh Clustering...\n";
    auto* meshManager = m_renderSystem->getMeshManager();
    if (!meshManager) return;

    uint32_t processedMeshes = 0, totalClusters = 0;

    if (m_scene) {
        auto& registry = m_scene->getRegistry();
        auto view = registry.view<VEngine::MeshRendererComponent>();
        std::set<std::string> processed;

        for (auto entity : view) {
            auto& mr = view.get<VEngine::MeshRendererComponent>(entity);
            if (processed.count(mr.meshPath)) continue;
            processed.insert(mr.meshPath);

            auto gpuMesh = meshManager->getMesh(mr.meshPath);
            if (!gpuMesh || !gpuMesh->mesh) continue;

            Nanite::InputMesh inputMesh = Nanite::InputMesh::fromMesh(*gpuMesh->mesh);
            auto clusterized = m_naniteManager->processMesh(inputMesh, mr.meshPath);
            if (clusterized) {
                totalClusters += clusterized->getTotalClusterCount();
                m_lastClusterizedMeshPath = mr.meshPath;
                processedMeshes++;
            }
        }
    }

    if (processedMeshes > 0) {
        m_naniteManager->uploadToGPU();
        if (!m_naniteDebugPass) initNaniteDebugPass();
        if (m_naniteDebugPass && !m_lastClusterizedMeshPath.empty())
            m_naniteDebugPass->setTargetMesh(m_lastClusterizedMeshPath);
    }

    std::cout << "Clustering done: " << processedMeshes << " meshes, " << totalClusters << " clusters\n";
}

void SceneRenderer::prepareNaniteCulling(RHICommandBuffer* cmd, uint32_t imageIndex, uint32_t frameIndex) {
    if (!m_naniteManager || !m_naniteDebugPass) return;

    m_naniteDebugPass->setRenderAllMeshes();
    m_naniteDebugPass->ensureRenderDataBuilt();

    // 应用 UI 侧的 LOD 调参（每帧写入 config，shader uniform 使用）
    {
        Nanite::NaniteConfig cfg = m_naniteManager->getConfig();
        // Force LOD 诊断时关闭 GPU LOD 选择,让所有层级先通过剔除,再由 CPU 过滤
        cfg.enableLODSelection = m_settings.naniteLODSelection && (m_settings.naniteForceLOD < 0);
        cfg.enableClusterCulling = m_settings.naniteFrustumCulling;
        cfg.enableConeCulling = m_settings.naniteConeCulling;
        cfg.screenSpaceErrorThreshold = m_settings.naniteErrorThreshold;
        cfg.lodErrorScale = m_settings.naniteErrorScale;
        m_naniteManager->setConfig(cfg);
        m_naniteDebugPass->setForceLOD(m_settings.naniteForceLOD);
    }

    // 每个 mesh 的世界矩阵（顺序必须与 getAllMeshNames() 排序一致，供 GPU 剔除用）
    {
        std::unordered_map<std::string, glm::mat4> xforms;
        if (m_scene) {
            auto& registry = m_scene->getRegistry();
            auto view = registry.view<VEngine::TransformComponent, VEngine::MeshRendererComponent>();
            for (auto entity : view) {
                auto& mr = view.get<VEngine::MeshRendererComponent>(entity);
                xforms[mr.meshPath] = VEngine::computeWorldMatrix(registry, entity);
            }
        }
        std::vector<glm::mat4> ordered;
        for (const auto& name : m_naniteManager->getAllMeshNames()) {
            auto it = xforms.find(name);
            ordered.push_back(it != xforms.end() ? it->second : glm::mat4(1.0f));
        }
        m_naniteManager->setMeshTransforms(ordered);
    }

    auto nExtent = m_swapChain->getExtent();
    float aspect = (float)nExtent.width / (float)nExtent.height;
    glm::mat4 proj = applyApiYFlip(m_camera->getProjectionMatrix(aspect, m_camera->getZoom()));
    glm::mat4 view = m_camera->getViewMatrix();
    glm::vec3 camPos = m_camera->getPosition();

    m_naniteManager->setScreenParams(nExtent.width, nExtent.height);
    // 注意:readback slot 必须用 frameIndex(与 Engine 的 readbackCullingResults 一致),
    // 不能用 imageIndex —— 交换链图像数(3)与帧槽数(2)不同会导致读到未完成的 slot
    m_naniteManager->performCulling(cmd, view, proj, camPos, frameIndex);

    cmd->pipelineBarrier(
        RHIPipelineStage::ComputeShader | RHIPipelineStage::Transfer,
        RHIPipelineStage::VertexInput | RHIPipelineStage::VertexShader | RHIPipelineStage::FragmentShader,
        RHIAccessFlags::ShaderWrite,
        RHIAccessFlags::ShaderRead);

    // GPU-driven 间接绘制准备:可见 cluster 几何展开 + drawArgs(Vulkan 禁止
    // render pass 内 dispatch compute,必须在 pass 外完成)
    m_naniteDebugPass->prepareIndirectDraw(cmd);

    m_naniteDebugPass->updateUniforms(frameIndex, view, proj, camPos,
        glm::vec3(10.0f, 10.0f, 10.0f), glm::vec3(1.0f, 1.0f, 1.0f));
}

void SceneRenderer::recordNaniteDebugCommands(RHICommandBuffer* cmd, uint32_t frameIndex) {
    if (!m_naniteDebugPass || !m_settings.showClusterVisualization) return;

    std::unordered_map<std::string, glm::mat4> meshMatrices;
    if (m_scene && m_naniteManager) {
        auto& registry = m_scene->getRegistry();
        auto view = registry.view<VEngine::TransformComponent, VEngine::MeshRendererComponent>();
        auto names = m_naniteManager->getAllMeshNames();
        std::set<std::string> nameSet(names.begin(), names.end());

        for (auto entity : view) {
            auto& mr = view.get<VEngine::MeshRendererComponent>(entity);
            if (nameSet.count(mr.meshPath)) {
                meshMatrices[mr.meshPath] = VEngine::computeWorldMatrix(registry, entity);
            }
        }
    }
    if (meshMatrices.empty()) return;

    m_naniteDebugPass->recordCommandsWithLOD(cmd, frameIndex, meshMatrices, m_naniteManager.get());
}