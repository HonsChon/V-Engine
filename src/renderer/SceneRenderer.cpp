/**
 * @file SceneRenderer.cpp
 * @brief SceneRenderer 实现 — 从 VulkanRenderer 迁移的完整渲染调度
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

#include <iostream>
#include <array>
#include <algorithm>

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

    // 创建 ForwardPass（始终可用）— 纯 RHI 接口
    m_forwardPass = std::make_unique<ForwardPass>(
        m_rhiDevice,
        m_swapChain->getRHIRenderPass(), w, h, MAX_FRAMES_IN_FLIGHT);
    std::cout << "[SceneRenderer] ForwardPass created (RHI)\n";

    m_initialized = true;
    std::cout << "[SceneRenderer] Initialized\n";
}

void SceneRenderer::cleanup() {
    if (!m_initialized) return;
    
    if (m_rhiDevice) m_rhiDevice->waitIdle();

    cleanupNanite();
    cleanupGPUDrivenRendering();
    cleanupDeferredShading();
    m_forwardPass.reset();
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

        // 3. Water
        m_waterPass = std::make_unique<WaterPass>(m_rhiDevice, w, h, m_swapChain->getRHIRenderPass());
        m_waterPass->setWaterHeight(-1.5f);
        m_waterPass->setWaterColor(glm::vec3(0.0f, 0.4f, 0.6f), 0.7f);
        std::cout << "  Water Pass created\n";

        // 4. Scene color image (for SSR sampling)
        createSceneColorImage();
        std::cout << "  Scene color image created\n";

        // 5. GBuffer descriptor sets
        if (m_gbuffer) {
            m_gbuffer->createDescriptorSets();
            std::cout << "  GBuffer descriptor sets created\n";
        }

        // 6. LightingPass (Pure RHI)
        m_lightingPass = std::make_unique<LightingPass>(
            m_rhiDevice, w, h,
            m_swapChain->getRHIRenderPass(), MAX_FRAMES_IN_FLIGHT);
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
    m_ssrPass.reset();
    m_ssaoPass.reset();
    m_lightingPass.reset();
    m_gbuffer.reset();
    m_deferredInitialized = false;
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

void SceneRenderer::updateUniforms(uint32_t frameIndex) {
    if (!m_camera) return;

    // 计算公共矩阵
    glm::mat4 view = m_camera->getViewMatrix();
    float fov = glm::radians(m_camera->getZoom());
    auto scExtent = m_swapChain->getExtent();
    float aspect = scExtent.width / (float)scExtent.height;
    glm::mat4 proj = glm::perspective(fov, aspect, 0.1f, 100.0f);
    proj[1][1] *= -1;
    glm::vec3 camPos = m_camera->getPosition();

    // 光源（绕 Y 轴旋转）
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(now - startTime).count();
    float lightAngle = time * 0.5f;
    glm::vec3 lightPos(5.0f * cos(lightAngle), 3.0f, 5.0f * sin(lightAngle));

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

        // Water & SSR
        if (m_waterPass) {
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

void SceneRenderer::recordCommands(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t frameIndex) {
    if (m_settings.renderMode == RenderMode::WaterScene && m_deferredInitialized) {
        recordDeferredCommands(cmd, imageIndex, frameIndex);
    } else {
        recordForwardCommands(cmd, imageIndex, frameIndex);
    }
}

// ============================================================
// 命令录制 — 前向渲染
// ============================================================

void SceneRenderer::recordForwardCommands(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t frameIndex) {
    // GPU Culling (Compute, before render pass)
    if (m_settings.enableGPUCulling && m_gpuDrivenRenderer) {
        auto rhiCmdCull = getRHIDevice()->wrapCommandBuffer(static_cast<void*>(cmd));
        m_gpuDrivenRenderer->executeCulling(rhiCmdCull.get());
        // Barrier already handled inside FrustumCullingPass::record()
    }

    // Nanite GPU Culling (Compute, before render pass)
    if (m_settings.showClusterVisualization && m_naniteManager && m_naniteDebugPass) {
        prepareNaniteCulling(cmd, imageIndex);
    }

    // Begin render pass (Pure RHI)
    {
        auto rhiCmdRP = getRHIDevice()->wrapCommandBuffer(static_cast<void*>(cmd));
        std::vector<RHIClearValue> clears = {
            RHIClearValue::Color(0.1f, 0.2f, 0.4f, 1.0f),
            RHIClearValue::DepthStencil(1.0f, 0)
        };
        rhiCmdRP->beginRenderPass(
            m_swapChain->getRHIRenderPass(),
            m_swapChain->getRHIFramebuffer(imageIndex),
            clears);
    }

    m_rhiDevice->beginDebugLabel(static_cast<void*>(cmd), "Scene Rendering", 0.2f, 0.8f, 0.2f, 1.0f);

    if (m_forwardPass && m_scene && m_renderSystem) {
        auto rhiCmd = getRHIDevice()->wrapCommandBuffer(static_cast<void*>(cmd));

        m_forwardPass->begin(rhiCmd.get());
        m_forwardPass->bindPipeline(rhiCmd.get());

        if (m_settings.enableGPUCulling && m_gpuDrivenRenderer) {
            RHIBuffer* indirectBuffer = m_gpuDrivenRenderer->getIndirectDrawBuffer();
            if (indirectBuffer != nullptr) {
                m_forwardPass->bindGlobalDescriptorSet(rhiCmd.get(), frameIndex);

                const auto& visibleIndices = m_gpuDrivenRenderer->getVisibleIndices();
                uint32_t visibleCount = static_cast<uint32_t>(visibleIndices.size());

                auto& registry = m_scene->getRegistry();
                auto ecsView = registry.view<VulkanEngine::TransformComponent, VulkanEngine::MeshRendererComponent>();
                std::vector<entt::entity> entityList;
                for (auto e : ecsView) entityList.push_back(e);

                for (uint32_t i = 0; i < visibleCount; ++i) {
                    uint32_t idx = visibleIndices[i];
                    if (idx >= entityList.size()) continue;

                    auto entity = entityList[idx];
                    auto& transform = ecsView.get<VulkanEngine::TransformComponent>(entity);
                    auto& meshRenderer = ecsView.get<VulkanEngine::MeshRendererComponent>(entity);

                    auto gpuMesh = VulkanEngine::MeshManager::getInstance().getMesh(meshRenderer.meshPath);
                    if (!gpuMesh) continue;

                    m_forwardPass->pushModelMatrix(rhiCmd.get(), transform.getTransform());

                    ForwardPass::MaterialDescriptor* matDesc = nullptr;
                    for (const auto& r : m_renderSystem->getRenderables()) {
                        if (r.entityHandle == entity && r.materialDescriptor) {
                            matDesc = r.materialDescriptor; break;
                        }
                    }
                    if (!matDesc || !matDesc->valid) continue;
                    m_forwardPass->bindMaterialDescriptorSet(rhiCmd.get(), frameIndex, matDesc);

                    m_forwardPass->drawMesh(rhiCmd.get(),
                        gpuMesh->getVertexBuffer(),
                        gpuMesh->getIndexBuffer(),
                        gpuMesh->getIndexCount());
                }
            } else {
                m_renderSystem->render(rhiCmd.get(), m_forwardPass.get(), frameIndex);
            }
        } else {
            if (m_settings.showClusterVisualization && m_naniteDebugPass) {
                recordNaniteDebugCommands(cmd, imageIndex);
            } else {
                m_renderSystem->render(rhiCmd.get(), m_forwardPass.get(), frameIndex);
            }
        }
    }

    m_rhiDevice->endDebugLabel(static_cast<void*>(cmd));

    // UI
    m_rhiDevice->beginDebugLabel(static_cast<void*>(cmd), "UI Rendering", 0.8f, 0.2f, 0.8f, 1.0f);
    updateUI();
    renderUI(cmd);
    m_rhiDevice->endDebugLabel(static_cast<void*>(cmd));

    {
        auto rhiCmdEnd = getRHIDevice()->wrapCommandBuffer(static_cast<void*>(cmd));
        rhiCmdEnd->endRenderPass();
    }
}

// ============================================================
// 命令录制 — 延迟渲染
// ============================================================

void SceneRenderer::recordDeferredCommands(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t frameIndex) {
    m_rhiDevice->beginDebugLabel(static_cast<void*>(cmd), "Water Scene Rendering", 0.2f, 0.6f, 0.9f, 1.0f);

    auto dExtent = m_swapChain->getExtent();
    uint32_t w = dExtent.width;
    uint32_t h = dExtent.height;

    // === Pass 1: GBuffer (Pure RHI) ===
    if (m_gbuffer && m_scene) {
        auto rhiCmdGBuffer = getRHIDevice()->wrapCommandBuffer(static_cast<void*>(cmd));
        m_gbuffer->beginRenderPass(rhiCmdGBuffer.get());
        m_gbuffer->bindPipeline(rhiCmdGBuffer.get());

        if (m_renderSystem) {
            m_renderSystem->render(rhiCmdGBuffer.get(), m_gbuffer.get(), frameIndex);
        }
        m_gbuffer->endRenderPass(rhiCmdGBuffer.get());
    }

    // === Pass 1.5: Blit Albedo → sceneColorTexture (Pure RHI) ===
    if (m_gbuffer && m_sceneColorTexture) {
        auto rhiCmdBlit = getRHIDevice()->wrapCommandBuffer(static_cast<void*>(cmd));

        // Pre-blit transitions
        rhiCmdBlit->transitionImageLayout(
            m_gbuffer->getAlbedoTexture(),
            RHIImageLayout::ShaderReadOnly, RHIImageLayout::TransferSrc,
            RHIPipelineStage::ColorAttachmentOutput, RHIPipelineStage::Transfer);

        rhiCmdBlit->transitionImageLayout(
            m_sceneColorTexture.get(),
            RHIImageLayout::Undefined, RHIImageLayout::TransferDst,
            RHIPipelineStage::TopOfPipe, RHIPipelineStage::Transfer);

        // Blit
        rhiCmdBlit->blitImage(
            m_gbuffer->getAlbedoTexture(), RHIImageLayout::TransferSrc,
            m_sceneColorTexture.get(), RHIImageLayout::TransferDst,
            w, h, w, h, RHIFilter::Linear);

        // Post-blit transitions
        rhiCmdBlit->transitionImageLayout(
            m_gbuffer->getAlbedoTexture(),
            RHIImageLayout::TransferSrc, RHIImageLayout::ShaderReadOnly,
            RHIPipelineStage::Transfer, RHIPipelineStage::FragmentShader);

        rhiCmdBlit->transitionImageLayout(
            m_sceneColorTexture.get(),
            RHIImageLayout::TransferDst, RHIImageLayout::ShaderReadOnly,
            RHIPipelineStage::Transfer, RHIPipelineStage::FragmentShader);
    }

    // === Pass 1.8: SSAO (Pure RHI) ===
    if (m_ssaoPass && m_gbuffer) {
        if (m_settings.enableSSAO) {
            auto rhiCmdSSAO = getRHIDevice()->wrapCommandBuffer(static_cast<void*>(cmd));
            float aspect = (float)w / (float)h;
            glm::mat4 projection = glm::perspective(
                glm::radians(m_camera ? m_camera->getZoom() : 45.0f), aspect, 0.1f, 100.0f);
            projection[1][1] *= -1;
            glm::mat4 view = m_camera ? m_camera->getViewMatrix() : glm::mat4(1.0f);
            m_ssaoPass->execute(rhiCmdSSAO.get(), m_gbuffer.get(), frameIndex, projection, view);
        }
        // When SSAO is disabled, the blurred AO texture stays at its initial cleared state
        // (white = no occlusion). The Lighting pass should handle this gracefully.
    }

    // === Pass 2: SSR (Pure RHI) ===
    if (m_ssrPass && m_gbuffer && m_sceneColorTexture) {
        auto rhiCmdSSR = getRHIDevice()->wrapCommandBuffer(static_cast<void*>(cmd));
        m_ssrPass->execute(rhiCmdSSR.get(), m_gbuffer.get(),
            m_sceneColorTexture.get(), m_sceneColorSampler.get(), frameIndex);
    }

    // === Pass 3: Final — render to swapchain (Pure RHI) ===
    {
        auto rhiCmdFinal = getRHIDevice()->wrapCommandBuffer(static_cast<void*>(cmd));
        std::vector<RHIClearValue> clears = {
            RHIClearValue::Color(0.02f, 0.05f, 0.1f, 1.0f),
            RHIClearValue::DepthStencil(1.0f, 0)
        };
        rhiCmdFinal->beginRenderPass(
            m_swapChain->getRHIRenderPass(),
            m_swapChain->getRHIFramebuffer(imageIndex),
            clears);

        // Deferred lighting (Pure RHI)
        if (m_lightingPass && m_gbuffer) {
            m_lightingPass->render(rhiCmdFinal.get(), frameIndex);
        }

        // Water
        if (m_waterPass) {
            m_waterPass->render(rhiCmdFinal.get(), frameIndex);
        }

        m_rhiDevice->endDebugLabel(static_cast<void*>(cmd)); // end Water Scene Rendering

        // UI
        m_rhiDevice->beginDebugLabel(static_cast<void*>(cmd), "UI Rendering", 0.8f, 0.2f, 0.8f, 1.0f);
        updateUI();
        renderUI(cmd);
        m_rhiDevice->endDebugLabel(static_cast<void*>(cmd));

        rhiCmdFinal->endRenderPass();
    }
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

        if (m_renderSystem) {
            debugPanel->setVertices(m_renderSystem->getTotalVertexCount());
            debugPanel->setTriangles(m_renderSystem->getTotalTriangleCount());
            debugPanel->setDrawCalls(m_renderSystem->getDrawCallCount());
        }
    }
}

void SceneRenderer::renderUI(VkCommandBuffer cmd) {
    if (!m_imguiLayer || !m_uiManager || !m_settings.showUI) return;
    m_imguiLayer->beginFrame();
    m_uiManager->render();
    m_imguiLayer->endFrame(cmd);
}

// ============================================================
// Resize
// ============================================================

void SceneRenderer::onResize(uint32_t width, uint32_t height) {
    if (m_forwardPass) {
        m_forwardPass->recreate(m_swapChain->getRHIRenderPass(), width, height);
    }
    if (m_ssaoPass) {
        m_ssaoPass->resize(width, height);
        // SSAO re-binding deferred until SSAOPass is fully migrated to RHI
        // if (m_lightingPass) {
        //     m_lightingPass->setSSAOTexture(...);
        // }
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
    auto view = registry.view<VulkanEngine::TransformComponent, VulkanEngine::MeshRendererComponent>();
    auto* meshManager = m_renderSystem->getMeshManager();

    for (auto entity : view) {
        auto& transform = view.get<VulkanEngine::TransformComponent>(entity);
        auto& meshRenderer = view.get<VulkanEngine::MeshRendererComponent>(entity);

        GPUInstanceData data{};
        data.modelMatrix = transform.getTransform();

        VulkanEngine::AABB meshAABB;
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
    glm::mat4 proj = glm::perspective(fov, aspect, 0.1f, 100.0f);
    proj[1][1] *= -1;
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
        m_naniteDebugPass->initialize(m_swapChain->getRHIRenderPass());
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

void SceneRenderer::testNaniteClustering() {
    if (!m_naniteManager || !m_renderSystem) return;

    std::cout << "\nTesting Nanite Mesh Clustering...\n";
    auto* meshManager = m_renderSystem->getMeshManager();
    if (!meshManager) return;

    uint32_t processedMeshes = 0, totalClusters = 0;

    if (m_scene) {
        auto& registry = m_scene->getRegistry();
        auto view = registry.view<VulkanEngine::MeshRendererComponent>();
        std::set<std::string> processed;

        for (auto entity : view) {
            auto& mr = view.get<VulkanEngine::MeshRendererComponent>(entity);
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

void SceneRenderer::prepareNaniteCulling(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (!m_naniteManager || !m_naniteDebugPass) return;

    m_naniteDebugPass->setRenderAllMeshes();
    m_naniteDebugPass->ensureRenderDataBuilt();

    auto nExtent = m_swapChain->getExtent();
    float aspect = (float)nExtent.width / (float)nExtent.height;
    glm::mat4 proj = m_camera->getProjectionMatrix(aspect, m_camera->getZoom());
    proj[1][1] *= -1;
    glm::mat4 view = m_camera->getViewMatrix();
    glm::vec3 camPos = m_camera->getPosition();

    m_naniteManager->setScreenParams(nExtent.width, nExtent.height);
    auto rhiCmd = m_rhiDevice->wrapCommandBuffer((void*)cmd);
    m_naniteManager->performCulling(rhiCmd.get(), view, proj, camPos, imageIndex);

    rhiCmd->pipelineBarrier(
        RHIPipelineStage::ComputeShader | RHIPipelineStage::Transfer,
        RHIPipelineStage::VertexInput | RHIPipelineStage::VertexShader | RHIPipelineStage::FragmentShader,
        RHIAccessFlags::ShaderWrite,
        RHIAccessFlags::ShaderRead);

    m_naniteDebugPass->updateUniforms(imageIndex, view, proj, camPos,
        glm::vec3(10.0f, 10.0f, 10.0f), glm::vec3(1.0f, 1.0f, 1.0f));
}

void SceneRenderer::recordNaniteDebugCommands(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (!m_naniteDebugPass || !m_settings.showClusterVisualization) return;

    std::unordered_map<std::string, glm::mat4> meshMatrices;
    if (m_scene && m_naniteManager) {
        auto& registry = m_scene->getRegistry();
        auto view = registry.view<VulkanEngine::TransformComponent, VulkanEngine::MeshRendererComponent>();
        auto names = m_naniteManager->getAllMeshNames();
        std::set<std::string> nameSet(names.begin(), names.end());

        for (auto entity : view) {
            auto& mr = view.get<VulkanEngine::MeshRendererComponent>(entity);
            if (nameSet.count(mr.meshPath)) {
                auto& t = view.get<VulkanEngine::TransformComponent>(entity);
                meshMatrices[mr.meshPath] = t.getTransform();
            }
        }
    }
    if (meshMatrices.empty()) return;

    auto rhiCmdNanite = getRHIDevice()->wrapCommandBuffer(static_cast<void*>(cmd));
    m_naniteDebugPass->recordCommandsWithLOD(rhiCmdNanite.get(), imageIndex, meshMatrices, m_naniteManager.get());
}