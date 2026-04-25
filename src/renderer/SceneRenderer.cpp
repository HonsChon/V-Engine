/**
 * @file SceneRenderer.cpp
 * @brief SceneRenderer 实现 — 从 VulkanRenderer 迁移的完整渲染调度
 */

#include "SceneRenderer.h"

#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
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

SceneRenderer::SceneRenderer(VulkanDevice* device, VulkanSwapChain* swapChain)
    : m_device(device), m_swapChain(swapChain)
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

    auto deviceShared = std::shared_ptr<VulkanDevice>(m_device, [](VulkanDevice*){});
    uint32_t w = m_swapChain->getExtent().width;
    uint32_t h = m_swapChain->getExtent().height;

    // 创建 ForwardPass（始终可用）
    m_forwardPass = std::make_unique<ForwardPass>(deviceShared, m_swapChain->getRenderPass(), w, h);
    std::cout << "[SceneRenderer] ForwardPass created\n";

    m_initialized = true;
    std::cout << "[SceneRenderer] Initialized\n";
}

void SceneRenderer::cleanup() {
    if (!m_initialized) return;
    
    if (m_device) vkDeviceWaitIdle(m_device->getDevice());

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

    auto deviceShared = std::shared_ptr<VulkanDevice>(m_device, [](VulkanDevice*){});
    uint32_t w = m_swapChain->getExtent().width;
    uint32_t h = m_swapChain->getExtent().height;

    try {
        // 1. GBuffer
        m_gbuffer = std::make_unique<GBufferPass>(deviceShared, w, h);
        std::cout << "  GBuffer created\n";

        // 2. SSR
        m_ssrPass = std::make_unique<SSRPass>(deviceShared, w, h);
        std::cout << "  SSR Pass created\n";

        // 3. Water
        m_waterPass = std::make_unique<WaterPass>(deviceShared, w, h, m_swapChain->getRenderPass());
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

        // 6. LightingPass
        m_lightingPass = std::make_unique<LightingPass>(deviceShared, w, h, m_swapChain->getRenderPass());
        m_lightingPass->setAmbientLight(glm::vec3(0.03f), 1.0f);
        std::cout << "  LightingPass created\n";

        // 7. SSAOPass
        m_ssaoPass = std::make_unique<SSAOPass>(deviceShared, w, h);
        m_ssaoPass->init();
        std::cout << "  SSAOPass created (" << w << "x" << h << ")\n";

        // 8. Set LightingPass G-Buffer inputs
        if (m_gbuffer) {
            m_lightingPass->setGBufferInputs(
                m_gbuffer->getPositionView(),
                m_gbuffer->getNormalView(),
                m_gbuffer->getAlbedoView(),
                m_gbuffer->getSampler()
            );
            std::cout << "  LightingPass G-Buffer inputs set\n";
        }

        // 9. Bind SSAO to LightingPass
        if (m_ssaoPass && m_lightingPass) {
            m_lightingPass->setSSAOTexture(
                m_ssaoPass->getOutputAOView(),
                m_ssaoPass->getOutputAOSampler()
            );
            std::cout << "  LightingPass SSAO texture bound\n";
        }

        // 10. Update Water descriptors
        if (m_gbuffer) {
            m_waterPass->updateDescriptorSets(
                m_gbuffer.get(),
                m_sceneColorView,
                m_sceneColorSampler
            );
            std::cout << "  Water Pass descriptors updated\n";
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
    if (m_device) vkDeviceWaitIdle(m_device->getDevice());
    
    cleanupSceneColorImage();
    m_waterPass.reset();
    m_ssrPass.reset();
    m_ssaoPass.reset();
    m_lightingPass.reset();
    m_gbuffer.reset();
    m_deferredInitialized = false;
}

void SceneRenderer::createSceneColorImage() {
    uint32_t w = m_swapChain->getExtent().width;
    uint32_t h = m_swapChain->getExtent().height;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = { w, h, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device->getDevice(), &imageInfo, nullptr, &m_sceneColorImage) != VK_SUCCESS)
        throw std::runtime_error("Failed to create scene color image!");

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device->getDevice(), m_sceneColorImage, &memReq);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_device->getPhysicalDevice(), &memProps);

    uint32_t memIdx = 0;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReq.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memIdx = i; break;
        }
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memIdx;
    if (vkAllocateMemory(m_device->getDevice(), &allocInfo, nullptr, &m_sceneColorMemory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate scene color memory!");

    vkBindImageMemory(m_device->getDevice(), m_sceneColorImage, m_sceneColorMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_sceneColorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(m_device->getDevice(), &viewInfo, nullptr, &m_sceneColorView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create scene color image view!");

    VkSamplerCreateInfo sampInfo{};
    sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter = VK_FILTER_LINEAR;
    sampInfo.minFilter = VK_FILTER_LINEAR;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.anisotropyEnable = VK_FALSE;
    sampInfo.maxAnisotropy = 1.0f;
    sampInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampInfo.unnormalizedCoordinates = VK_FALSE;
    sampInfo.compareEnable = VK_FALSE;
    sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(m_device->getDevice(), &sampInfo, nullptr, &m_sceneColorSampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create scene color sampler!");
}

void SceneRenderer::cleanupSceneColorImage() {
    if (!m_device) return;
    VkDevice dev = m_device->getDevice();
    if (m_sceneColorSampler) { vkDestroySampler(dev, m_sceneColorSampler, nullptr); m_sceneColorSampler = VK_NULL_HANDLE; }
    if (m_sceneColorView)    { vkDestroyImageView(dev, m_sceneColorView, nullptr);  m_sceneColorView = VK_NULL_HANDLE; }
    if (m_sceneColorImage)   { vkDestroyImage(dev, m_sceneColorImage, nullptr);     m_sceneColorImage = VK_NULL_HANDLE; }
    if (m_sceneColorMemory)  { vkFreeMemory(dev, m_sceneColorMemory, nullptr);      m_sceneColorMemory = VK_NULL_HANDLE; }
}

// ============================================================
// Uniform 更新
// ============================================================

void SceneRenderer::updateUniforms(uint32_t frameIndex) {
    if (!m_camera) return;

    // 计算公共矩阵
    glm::mat4 view = m_camera->getViewMatrix();
    float fov = glm::radians(m_camera->getZoom());
    float aspect = m_swapChain->getExtent().width / (float)m_swapChain->getExtent().height;
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
        m_gpuDrivenRenderer->executeCulling(cmd);

        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    // Nanite GPU Culling (Compute, before render pass)
    if (m_settings.showClusterVisualization && m_naniteManager && m_naniteDebugPass) {
        prepareNaniteCulling(cmd, imageIndex);
    }

    // Begin render pass
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_swapChain->getRenderPass();
    rpInfo.framebuffer = m_swapChain->getFramebuffers()[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = m_swapChain->getExtent();

    std::array<VkClearValue, 2> clears{};
    clears[0].color = {{0.1f, 0.2f, 0.4f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    rpInfo.clearValueCount = static_cast<uint32_t>(clears.size());
    rpInfo.pClearValues = clears.data();

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    m_device->beginDebugLabel(cmd, "Scene Rendering", 0.2f, 0.8f, 0.2f, 1.0f);

    if (m_forwardPass && m_scene && m_renderSystem) {
        m_forwardPass->begin(cmd);
        m_forwardPass->bindPipeline(cmd);

        if (m_settings.enableGPUCulling && m_gpuDrivenRenderer) {
            VkBuffer indirectBuffer = m_gpuDrivenRenderer->getIndirectDrawBuffer();
            if (indirectBuffer != VK_NULL_HANDLE) {
                m_forwardPass->bindGlobalDescriptorSet(cmd, frameIndex);

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

                    m_forwardPass->pushModelMatrix(cmd, transform.getTransform());

                    ForwardPass::MaterialDescriptor* matDesc = nullptr;
                    for (const auto& r : m_renderSystem->getRenderables()) {
                        if (r.entityHandle == entity && r.materialDescriptor) {
                            matDesc = r.materialDescriptor; break;
                        }
                    }
                    if (!matDesc || !matDesc->valid) continue;
                    m_forwardPass->bindMaterialDescriptorSet(cmd, frameIndex, matDesc);

                    VkBuffer vb[] = { gpuMesh->vertexBuffer->getBuffer() };
                    VkDeviceSize off[] = { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, vb, off);
                    vkCmdBindIndexBuffer(cmd, gpuMesh->indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, gpuMesh->getIndexCount(), 1, 0, 0, 0);
                }
            } else {
                m_renderSystem->render(cmd, m_forwardPass.get(), frameIndex);
            }
        } else {
            if (m_settings.showClusterVisualization && m_naniteDebugPass) {
                recordNaniteDebugCommands(cmd, imageIndex);
            } else {
                m_renderSystem->render(cmd, m_forwardPass.get(), frameIndex);
            }
        }
    }

    m_device->endDebugLabel(cmd);

    // UI
    m_device->beginDebugLabel(cmd, "UI Rendering", 0.8f, 0.2f, 0.8f, 1.0f);
    updateUI();
    renderUI(cmd);
    m_device->endDebugLabel(cmd);

    vkCmdEndRenderPass(cmd);
}

// ============================================================
// 命令录制 — 延迟渲染
// ============================================================

void SceneRenderer::recordDeferredCommands(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t frameIndex) {
    m_device->beginDebugLabel(cmd, "Water Scene Rendering", 0.2f, 0.6f, 0.9f, 1.0f);

    uint32_t w = m_swapChain->getExtent().width;
    uint32_t h = m_swapChain->getExtent().height;

    VkViewport viewport{ 0, 0, (float)w, (float)h, 0, 1 };
    VkRect2D scissor{ {0,0}, m_swapChain->getExtent() };

    // === Pass 1: GBuffer ===
    if (m_gbuffer && m_scene) {
        m_gbuffer->beginRenderPass(cmd);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        m_gbuffer->bindPipeline(cmd);

        if (m_renderSystem) {
            m_renderSystem->render(cmd, m_gbuffer.get(), frameIndex);
        }
        m_gbuffer->endRenderPass(cmd);
    }

    // === Pass 1.5: Blit Albedo → sceneColorImage ===
    if (m_gbuffer && m_sceneColorImage != VK_NULL_HANDLE) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_sceneColorImage;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit{};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blit.srcOffsets[0] = {0,0,0};
        blit.srcOffsets[1] = {(int32_t)w, (int32_t)h, 1};
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blit.dstOffsets[0] = {0,0,0};
        blit.dstOffsets[1] = {(int32_t)w, (int32_t)h, 1};
        vkCmdBlitImage(cmd,
            m_gbuffer->getAlbedoImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            m_sceneColorImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // === Pass 1.8: SSAO ===
    if (m_ssaoPass && m_gbuffer) {
        if (m_settings.enableSSAO) {
            m_device->beginDebugLabel(cmd, "SSAO Pass", 0.6f, 0.4f, 0.8f, 1.0f);
            float aspect = (float)w / (float)h;
            glm::mat4 projection = glm::perspective(
                glm::radians(m_camera ? m_camera->getZoom() : 45.0f), aspect, 0.1f, 100.0f);
            projection[1][1] *= -1;
            glm::mat4 view = m_camera ? m_camera->getViewMatrix() : glm::mat4(1.0f);
            m_ssaoPass->execute(cmd, m_gbuffer.get(), frameIndex, projection, view);
            m_device->endDebugLabel(cmd);
        } else {
            // SSAO off: clear to white
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_ssaoPass->getOutputAOImage();
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkClearColorValue white = {{ 1.0f, 1.0f, 1.0f, 1.0f }};
            VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdClearColorImage(cmd, m_ssaoPass->getOutputAOImage(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &white, 1, &range);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    }

    // === Pass 2: SSR ===
    if (m_ssrPass && m_gbuffer && m_sceneColorView) {
        m_ssrPass->execute(cmd, m_gbuffer.get(), m_sceneColorView, frameIndex);
    }

    // === Pass 3: Final — render to swapchain ===
    {
        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = m_swapChain->getRenderPass();
        rpInfo.framebuffer = m_swapChain->getFramebuffers()[imageIndex];
        rpInfo.renderArea = { {0,0}, m_swapChain->getExtent() };

        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{0.02f, 0.05f, 0.1f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        rpInfo.clearValueCount = static_cast<uint32_t>(clears.size());
        rpInfo.pClearValues = clears.data();

        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Deferred lighting
        if (m_lightingPass && m_gbuffer) {
            m_lightingPass->render(cmd, frameIndex);
        }

        // Water
        if (m_waterPass) {
            m_waterPass->render(cmd, frameIndex);
        }

        m_device->endDebugLabel(cmd); // end Water Scene Rendering

        // UI
        m_device->beginDebugLabel(cmd, "UI Rendering", 0.8f, 0.2f, 0.8f, 1.0f);
        updateUI();
        renderUI(cmd);
        m_device->endDebugLabel(cmd);

        vkCmdEndRenderPass(cmd);
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
        m_forwardPass->recreate(m_swapChain->getRenderPass(), width, height);
    }
    if (m_ssaoPass) {
        m_ssaoPass->resize(width, height);
        if (m_lightingPass) {
            m_lightingPass->setSSAOTexture(m_ssaoPass->getOutputAOView(), m_ssaoPass->getOutputAOSampler());
        }
    }
}

void SceneRenderer::onSwapChainRecreated(VulkanSwapChain* newSwapChain) {
    m_swapChain = newSwapChain;
    uint32_t w = newSwapChain->getExtent().width;
    uint32_t h = newSwapChain->getExtent().height;
    onResize(w, h);
}

// ============================================================
// GPU-Driven Rendering
// ============================================================

void SceneRenderer::initGPUDrivenRendering() {
    std::cout << "[SceneRenderer] Initializing GPU-Driven Rendering...\n";
    try {
        auto deviceShared = std::shared_ptr<VulkanDevice>(m_device, [](VulkanDevice*){});
        GPUDrivenRenderer::Config config;
        config.maxInstances = 100000;
        config.enableFrustumCulling = true;
        m_gpuDrivenRenderer = std::make_unique<GPUDrivenRenderer>(deviceShared, config);
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
    float aspect = m_swapChain->getExtent().width / (float)m_swapChain->getExtent().height;
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
        auto deviceShared = std::shared_ptr<VulkanDevice>(m_device, [](VulkanDevice*){});
        m_naniteManager = std::make_unique<Nanite::NaniteManager>(deviceShared);
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
        auto deviceShared = std::shared_ptr<VulkanDevice>(m_device, [](VulkanDevice*){});
        auto swapChainShared = std::shared_ptr<VulkanSwapChain>(m_swapChain, [](VulkanSwapChain*){});
        auto naniteShared = std::shared_ptr<Nanite::NaniteManager>(m_naniteManager.get(), [](Nanite::NaniteManager*){});

        m_naniteDebugPass = std::make_unique<NaniteDebugPass>(deviceShared, swapChainShared, naniteShared);
        m_naniteDebugPass->initialize(m_swapChain->getRenderPass());
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

    VkExtent2D extent = m_swapChain->getExtent();
    float aspect = (float)extent.width / (float)extent.height;
    glm::mat4 proj = m_camera->getProjectionMatrix(aspect, m_camera->getZoom());
    proj[1][1] *= -1;
    glm::mat4 view = m_camera->getViewMatrix();
    glm::vec3 camPos = m_camera->getPosition();

    m_naniteManager->setScreenParams(extent.width, extent.height);
    m_naniteManager->performCulling(cmd, view, proj, camPos, imageIndex);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

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

    m_naniteDebugPass->recordCommandsWithLOD(cmd, imageIndex, meshMatrices, m_naniteManager.get());
}