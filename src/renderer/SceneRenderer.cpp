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

        // 3.5 Transparent（延迟模式半透明前向绘制，复用 ForwardPass 材质布局）
        if (m_forwardPass) {
            m_transparentPass = std::make_unique<TransparentPass>(
                m_rhiDevice, m_swapChain->getRHIRenderPass(),
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
    m_transparentPass.reset();
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

glm::mat4 SceneRenderer::applyApiYFlip(const glm::mat4& proj) const {
    if (m_rhiDevice && m_rhiDevice->getBackend() == RHIBackend::Vulkan) {
        glm::mat4 flipped = proj;
        flipped[1][1] *= -1.0f;
        return flipped;
    }
    return proj;
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

    // Begin render pass (Pure RHI)
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

    // === Pass 3: Final Composition (Swapchain RenderPass) ===
    m_rhiDevice->beginDebugLabel(cmd->getNativeHandle(), "Final Composition", 0.9f, 0.4f, 0.1f, 1.0f);
    {
        std::vector<RHIClearValue> clears = {
            RHIClearValue::Color(0.02f, 0.05f, 0.1f, 1.0f),
            RHIClearValue::DepthStencil(1.0f, 0)
        };
        cmd->beginRenderPass(
            m_swapChain->getRHIRenderPass(),
            m_swapChain->getRHIFramebuffer(imageIndex),
            clears);

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
    if (m_forwardPass) {
        m_forwardPass->recreate(m_swapChain->getRHIRenderPass(), width, height);
    }
    if (m_transparentPass) {
        m_transparentPass->recreate(m_swapChain->getRHIRenderPass(), width, height);
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