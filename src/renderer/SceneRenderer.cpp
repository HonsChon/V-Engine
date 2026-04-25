/**
 * @file SceneRenderer.cpp
 * @brief Scene Renderer implementation
 */

#include "SceneRenderer.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"

// Pass headers
#include "GBufferPass.h"
#include "LightingPass.h"
#include "ForwardPass.h"
#include "SSRPass.h"
#include "WaterPass.h"
#include "NaniteDebugPass.h"
#include "ssao/SSAOPass.h"

// Scene related
#include "Scene.h"
#include "Camera.h"

#include <iostream>
#include <chrono>

SceneRenderer::SceneRenderer(VulkanDevice* device, VulkanSwapChain* swapChain)
    : m_device(device)
    , m_swapChain(swapChain)
{
    std::cout << "[SceneRenderer] Created\n";
}

SceneRenderer::~SceneRenderer() {
    cleanup();
}

void SceneRenderer::initialize() {
    if (m_initialized) return;

    std::cout << "[SceneRenderer] Initializing render passes...\n";
    
    createPasses();
    
    m_initialized = true;
    std::cout << "[SceneRenderer] Initialization complete\n";
}

void SceneRenderer::createPasses() {
    if (!m_device || !m_swapChain) {
        std::cerr << "[SceneRenderer] Cannot create passes: device or swapchain is null\n";
        return;
    }

    auto deviceShared = std::shared_ptr<VulkanDevice>(m_device, [](VulkanDevice*){});
    
    // Create ForwardPass for forward rendering
    m_forwardPass = std::make_unique<ForwardPass>(
        deviceShared,
        m_swapChain->getRenderPass(),
        m_swapChain->getExtent().width,
        m_swapChain->getExtent().height,
        MAX_FRAMES_IN_FLIGHT
    );
    std::cout << "[SceneRenderer] ForwardPass created\n";
    
    // Create SSAO Pass
    {
        uint32_t w = m_swapChain->getExtent().width;
        uint32_t h = m_swapChain->getExtent().height;
        m_ssaoPass = std::make_unique<SSAOPass>(m_device, w, h);
        m_ssaoPass->init();
        std::cout << "[SceneRenderer] SSAOPass created (" << w << "x" << h << ")\n";
    }

    // Note: Other passes (GBuffer, Lighting, SSR, etc.) can be created here
    // when deferred rendering is needed
}

void SceneRenderer::destroyPasses() {
    // Destroy in reverse creation order
    m_naniteDebugPass.reset();
    m_waterPass.reset();
    m_ssrPass.reset();
    m_ssaoPass.reset();
    m_forwardPass.reset();
    m_lightingPass.reset();
    m_gBufferPass.reset();
}

void SceneRenderer::render(const RenderContext& context) {
    if (!m_initialized) {
        std::cerr << "[SceneRenderer] Not initialized!\n";
        return;
    }

    // Reset statistics
    m_stats.reset();
    
    auto startTime = std::chrono::high_resolution_clock::now();

    // Add RenderDoc debug marker
    m_device->beginDebugLabel(context.commandBuffer, "SceneRenderer::render", 0.2f, 0.8f, 0.2f);

    // ========== Render Pipeline ==========
    
    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_swapChain->getRenderPass();
    renderPassInfo.framebuffer = m_swapChain->getFramebuffer(context.imageIndex);
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapChain->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.01f, 0.01f, 0.02f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(context.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(context.extent.width);
    viewport.height = static_cast<float>(context.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(context.commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = context.extent;
    vkCmdSetScissor(context.commandBuffer, 0, 1, &scissor);

    // 1. GBuffer Pass - Geometry data write
    if (m_gBufferPass) {
        executeGBufferPass(context);
    }

    // 2. SSAO Pass - Screen Space Ambient Occlusion (runs outside render pass)
    // Note: SSAO uses compute + offscreen graphics, executed before lighting
    if (m_settings.enableSSAO && m_ssaoPass) {
        // SSAO must end the current render pass, run its own passes, then restart
        // For now we call it between GBuffer and Lighting
        executeSSAOPass(context);
    }

    // 3. Lighting Pass - Deferred lighting calculation
    if (m_lightingPass) {
        executeLightingPass(context);
    }

    // 4. SSR Pass - Screen Space Reflections
    if (m_settings.enableSSR && m_ssrPass) {
        executeSSRPass(context);
    }

    // 5. Forward Pass - Forward rendering (transparent objects, etc.)
    if (m_forwardPass) {
        executeForwardPass(context);
    }

    // 6. Water Pass - Water rendering
    if (m_settings.enableWater && m_waterPass) {
        executeWaterPass(context);
    }

    // 7. Nanite Debug Pass
    if (m_settings.showClusterVisualization && m_naniteDebugPass) {
        executeNanitePass(context);
    }

    // 8. Debug Pass - Debug visualization
    executeDebugPass(context);

    // Note: vkCmdEndRenderPass is called by Engine after UI rendering

    m_device->endDebugLabel(context.commandBuffer);

    // Calculate CPU time
    auto endTime = std::chrono::high_resolution_clock::now();
    m_stats.cpuTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();
}

void SceneRenderer::executeGBufferPass(const RenderContext& context) {
    m_device->beginDebugLabel(context.commandBuffer, "GBuffer Pass", 0.8f, 0.3f, 0.3f);
    
    // TODO: Call m_gBufferPass->execute(context)
    
    m_device->endDebugLabel(context.commandBuffer);
}

void SceneRenderer::executeSSAOPass(const RenderContext& context) {
    m_device->beginDebugLabel(context.commandBuffer, "SSAO Pass", 0.6f, 0.4f, 0.8f);

    if (m_ssaoPass && m_gBufferPass) {
        // SSAOPass::execute needs the command buffer, camera matrices, and G-Buffer views
        // The actual G-Buffer image views come from GBufferPass
        m_ssaoPass->execute(
            context.commandBuffer,
            context.frameIndex,
            context.projectionMatrix,
            context.viewMatrix
        );

        // After SSAO, bind the blurred result to LightingPass
        if (m_lightingPass) {
            m_lightingPass->setSSAOTexture(
                m_ssaoPass->getOutputImageView(),
                m_ssaoPass->getOutputSampler()
            );
        }
    }

    m_device->endDebugLabel(context.commandBuffer);
}

void SceneRenderer::executeLightingPass(const RenderContext& context) {
    m_device->beginDebugLabel(context.commandBuffer, "Lighting Pass", 0.3f, 0.8f, 0.3f);
    
    // TODO: Call m_lightingPass->execute(context)
    
    m_device->endDebugLabel(context.commandBuffer);
}

void SceneRenderer::executeForwardPass(const RenderContext& context) {
    m_device->beginDebugLabel(context.commandBuffer, "Forward Pass", 0.3f, 0.3f, 0.8f);
    
    if (m_forwardPass && context.scene) {
        // Update uniform buffer
        ForwardPass::UniformBufferObject ubo{};
        ubo.view = context.viewMatrix;
        ubo.proj = context.projectionMatrix;
        ubo.viewPos = glm::vec4(context.cameraPosition, 1.0f);
        ubo.lightPos = glm::vec4(context.lightPosition, 1.0f);
        ubo.lightColor = glm::vec4(context.lightColor, 1.0f);
        
        m_forwardPass->updateUniformBuffer(context.frameIndex, ubo);
        
        // The actual drawing will be done through RenderSystem
        // which calls ForwardPass methods
    }
    
    m_device->endDebugLabel(context.commandBuffer);
}

void SceneRenderer::executeSSRPass(const RenderContext& context) {
    m_device->beginDebugLabel(context.commandBuffer, "SSR Pass", 0.8f, 0.8f, 0.3f);
    
    // TODO: Call m_ssrPass->execute(context)
    
    m_device->endDebugLabel(context.commandBuffer);
}

void SceneRenderer::executeWaterPass(const RenderContext& context) {
    m_device->beginDebugLabel(context.commandBuffer, "Water Pass", 0.3f, 0.8f, 0.8f);
    
    // TODO: Call m_waterPass->execute(context)
    
    m_device->endDebugLabel(context.commandBuffer);
}

void SceneRenderer::executeNanitePass(const RenderContext& context) {
    m_device->beginDebugLabel(context.commandBuffer, "Nanite Debug Pass", 0.8f, 0.3f, 0.8f);
    
    // TODO: Call m_naniteDebugPass->execute(context)
    
    m_device->endDebugLabel(context.commandBuffer);
}

void SceneRenderer::executeDebugPass(const RenderContext& context) {
    // Debug visualization (normals, depth, etc.)
    // Execute based on settings
}

void SceneRenderer::onResize(uint32_t width, uint32_t height) {
    std::cout << "[SceneRenderer] Resize to " << width << "x" << height << "\n";
    
    // Recreate SSAOPass with new dimensions
    if (m_ssaoPass) {
        m_ssaoPass->onResize(width, height);
    }

    // Recreate ForwardPass with new dimensions
    if (m_forwardPass && m_swapChain) {
        m_forwardPass->recreate(m_swapChain->getRenderPass(), width, height);
    }
}

void SceneRenderer::onSwapChainRecreated(VulkanSwapChain* newSwapChain) {
    m_swapChain = newSwapChain;
    
    if (m_swapChain) {
        onResize(m_swapChain->getExtent().width, m_swapChain->getExtent().height);
    }
}

void SceneRenderer::cleanup() {
    if (!m_initialized) return;

    std::cout << "[SceneRenderer] Cleaning up...\n";
    
    // Wait for GPU to finish
    if (m_device) {
        vkDeviceWaitIdle(m_device->getDevice());
    }

    destroyPasses();
    
    m_initialized = false;
    std::cout << "[SceneRenderer] Cleanup complete\n";
}