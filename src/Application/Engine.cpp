/**
 * @file Engine.cpp
 * @brief Engine main entry implementation
 * 
 * This file implements the new modular engine architecture,
 * migrating logic from the monolithic VulkanRenderer.
 */

#include "Engine.h"
#include "Window.h"
#include "Input.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "FrameResources.h"
#include "SceneRenderer.h"
#include "Scene.h"
#include "Entity.h"
#include "ImGuiLayer.h"
#include "UIManager.h"
#include "RenderSystem.h"
#include "Camera.h"
#include "ForwardPass.h"
#include "SelectionManager.h"
#include "Components.h"
#include "RenderContext.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <chrono>

// Constants
static const int MAX_FRAMES_IN_FLIGHT = 2;

Engine::Engine(const Config& config)
    : m_config(config)
{
    std::cout << "========================================\n";
    std::cout << " " << m_config.title << "\n";
    std::cout << " New Architecture Engine\n";
    std::cout << "========================================\n";
    
    initializeSubsystems();
}

Engine::~Engine() {
    shutdownSubsystems();
}

void Engine::initializeSubsystems() {
    std::cout << "[Engine] Initializing subsystems...\n";

    // 1. Create Window
    Window::Config windowConfig;
    windowConfig.title = m_config.title;
    windowConfig.width = m_config.width;
    windowConfig.height = m_config.height;
    m_window = std::make_unique<Window>(windowConfig);
    std::cout << "[Engine] Window created\n";

    // 2. Create Vulkan Device
    m_device = std::make_unique<VulkanDevice>(m_window->getNativeHandle());
    std::cout << "[Engine] Vulkan device created\n";

    // 3. Create SwapChain
    auto deviceShared = std::shared_ptr<VulkanDevice>(m_device.get(), [](VulkanDevice*){});
    m_swapChain = std::make_unique<VulkanSwapChain>(deviceShared, m_config.width, m_config.height);
    std::cout << "[Engine] SwapChain created\n";

    // 4. Create Frame Resources (command buffers, sync objects)
    m_frameResources = std::make_unique<FrameResources>(m_device.get());
    std::cout << "[Engine] Frame resources created\n";

    // 5. Create Input System
    m_input = std::make_unique<Input>(m_window.get());
    std::cout << "[Engine] Input system created\n";

    // 6. Create Camera
    m_camera = std::make_unique<Camera>(glm::vec3(0.0f, 0.0f, 5.0f));
    std::cout << "[Engine] Camera created\n";

    // 7. Create Scene (ECS)
    m_scene = std::make_unique<VulkanEngine::Scene>();
    std::cout << "[Engine] Scene created\n";

    // 8. Create RenderSystem
    m_renderSystem = std::make_unique<VulkanEngine::RenderSystem>();
    m_renderSystem->init(deviceShared);
    std::cout << "[Engine] RenderSystem created\n";

    // 9. Create SceneRenderer with passes
    m_renderer = std::make_unique<SceneRenderer>(m_device.get(), m_swapChain.get());
    m_renderer->initialize();
    std::cout << "[Engine] SceneRenderer created\n";

    // 10. Create sample entities
    createDefaultScene();

    // 11. Set up SelectionManager
    VulkanEngine::SelectionManager::getInstance().setScene(m_scene.get());

    // 12. Create ImGui Layer (if UI enabled)
    if (m_config.enableUI) {
        m_imguiLayer = std::make_unique<ImGuiLayer>();
        m_imguiLayer->init(
            m_window->getNativeHandle(),
            deviceShared,
            m_swapChain->getRenderPass(),
            static_cast<uint32_t>(m_swapChain->getImageCount())
        );
        
        m_uiManager = std::make_unique<UIManager>();
        m_uiManager->setScene(m_scene.get());

        // 将 SceneRenderer 的渲染选项传递给 UI，用于 SSAO 等开关控制
        if (m_renderer) {
            m_uiManager->setRenderSettings(&m_renderer->getSettings());
        }

        std::cout << "[Engine] ImGui layer created\n";
    }

    // 13. Set up input callbacks
    setupInputCallbacks();

    std::cout << "[Engine] All subsystems initialized\n";
}

void Engine::createDefaultScene() {
    // Create a sphere entity
    auto sphereEntity = m_scene->createEntity("Sphere");
    sphereEntity.addComponent<VulkanEngine::MeshRendererComponent>("sphere", "earth_material");
    
    // Add PBR material
    auto& sphereMaterial = sphereEntity.addComponent<VulkanEngine::PBRMaterialComponent>();
    sphereMaterial.albedoMap = "../../assets/Earth/Maps/Color Map.jpg";
    sphereMaterial.normalMap = "../../assets/Earth/Maps/Bump.jpg";
    sphereMaterial.metallicMap = "../../assets/Earth/Maps/Spec Mask.png";

    // Create UFO entity
    auto ufoEntity = m_scene->createEntity("UFO");
    ufoEntity.addComponent<VulkanEngine::MeshRendererComponent>(
        "../../assets/UFO/UFO_Empty.obj", "ufo_material");
    
    auto& ufoTransform = ufoEntity.getComponent<VulkanEngine::TransformComponent>();
    ufoTransform.position = glm::vec3(3.0f, 0.0f, 0.0f);
    ufoTransform.scale = glm::vec3(1.0f);
    
    auto& ufoMaterial = ufoEntity.addComponent<VulkanEngine::PBRMaterialComponent>();
    ufoMaterial.albedoMap = "../../assets/UFO/textures/UFO_color.jpg";
    ufoMaterial.normalMap = "../../assets/UFO/textures/UFO_nmap.jpg";
    ufoMaterial.metallicMap = "../../assets/UFO/textures/UFO_metalness.jpg";

    // Create a plane entity
    auto planeEntity = m_scene->createEntity("Plane");
    planeEntity.addComponent<VulkanEngine::MeshRendererComponent>("plane", "plane_material");
    
    auto& planeTransform = planeEntity.getComponent<VulkanEngine::TransformComponent>();
    planeTransform.position = glm::vec3(0.0f, -1.5f, 0.0f);
    
    planeEntity.addComponent<VulkanEngine::PBRMaterialComponent>();

    std::cout << "[Engine] Default scene created with 3 entities\n";
}

void Engine::setupInputCallbacks() {
    if (!m_input) return;

    // Bind ESC to exit
    m_input->bindAction(InputAction::Exit, [this]() {
        requestExit();
    });

    // Bind toggle UI
    m_input->bindAction(InputAction::ToggleUI, [this]() {
        m_showUI = !m_showUI;
        std::cout << "[Engine] UI " << (m_showUI ? "enabled" : "disabled") << "\n";
    });

    // Bind toggle GPU Culling
    m_input->bindAction(InputAction::ToggleGPUCulling, [this]() {
        m_enableGPUCulling = !m_enableGPUCulling;
        std::cout << "[Engine] GPU Culling " << (m_enableGPUCulling ? "enabled" : "disabled") << "\n";
    });

    // Bind toggle Nanite
    m_input->bindAction(InputAction::ToggleNanite, [this]() {
        m_enableNanite = !m_enableNanite;
        std::cout << "[Engine] Nanite " << (m_enableNanite ? "enabled" : "disabled") << "\n";
    });

    // Set camera for input handling
    m_input->setCamera(m_camera.get());
}

void Engine::shutdownSubsystems() {
    std::cout << "[Engine] Shutting down subsystems...\n";

    // Wait for GPU to complete all operations
    if (m_device) {
        vkDeviceWaitIdle(m_device->getDevice());
    }

    // Destroy in reverse order of creation (important!)
    m_uiManager.reset();
    m_imguiLayer.reset();
    m_renderer.reset();
    m_renderSystem.reset();
    m_scene.reset();
    m_camera.reset();
    m_input.reset();
    m_frameResources.reset();
    m_swapChain.reset();
    m_device.reset();
    m_window.reset();

    std::cout << "[Engine] All subsystems shut down\n";
}

void Engine::run() {
    m_running = true;
    
    std::cout << "[Engine] Starting main loop...\n";
    std::cout << "Controls:\n";
    std::cout << "  WASD - Move camera\n";
    std::cout << "  Space/Shift - Move up/down\n";
    std::cout << "  Right mouse button - Enable mouse look\n";
    std::cout << "  Mouse scroll - Zoom in/out\n";
    std::cout << "  F1 - Toggle UI\n";
    std::cout << "  ESC - Exit\n";

    // Initialize time
    m_lastFrameTime = static_cast<float>(glfwGetTime());

    while (m_running && !m_window->shouldClose()) {
        mainLoop();
    }

    std::cout << "[Engine] Main loop ended\n";
}

void Engine::mainLoop() {
    // 1. Update frame time
    float currentTime = static_cast<float>(glfwGetTime());
    m_deltaTime = currentTime - m_lastFrameTime;
    m_lastFrameTime = currentTime;
    m_totalTime = currentTime;

    // 2. Poll events
    m_window->pollEvents();

    // 3. Update input
    if (m_input) {
        m_input->update(m_deltaTime);
    }

    // 4. Begin frame
    if (m_frameResources && m_swapChain) {
        uint32_t frameIndex = m_frameResources->beginFrame();
        
        // Acquire swapchain image
        uint32_t imageIndex;
        VkResult result = m_swapChain->acquireNextImage(
            m_frameResources->getImageAvailableSemaphore(),
            &imageIndex
        );

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("Failed to acquire swap chain image!");
        }

        // Get current command buffer
        VkCommandBuffer cmd = m_frameResources->getCurrentCommandBuffer();

        // Reset and begin command buffer
        vkResetCommandBuffer(cmd, 0);
        
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0;
        
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("Failed to begin recording command buffer!");
        }

        // 5. Update RenderSystem
        if (m_renderSystem && m_scene && m_renderer) {
            std::vector<RenderPassBase*> passes;
            auto* forwardPass = m_renderer->getForwardPass();
            if (forwardPass) {
                passes.push_back(forwardPass);
            }
            m_renderSystem->updateRenderables(m_scene.get(), passes);
        }

        // 6. Render scene
        if (m_renderer && m_camera) {
            RenderContext ctx;
            ctx.commandBuffer = cmd;
            ctx.frameIndex = frameIndex;
            ctx.imageIndex = imageIndex;
            ctx.deltaTime = m_deltaTime;
            ctx.time = m_totalTime;
            ctx.extent = m_swapChain->getExtent();
            ctx.screenWidth = ctx.extent.width;
            ctx.screenHeight = ctx.extent.height;
            
            // Camera matrices
            ctx.viewMatrix = m_camera->getViewMatrix();
            float aspect = static_cast<float>(ctx.screenWidth) / static_cast<float>(ctx.screenHeight);
            ctx.projectionMatrix = glm::perspective(glm::radians(m_camera->getZoom()), aspect, 0.1f, 100.0f);
            ctx.projectionMatrix[1][1] *= -1; // Vulkan Y-flip
            ctx.cameraPosition = m_camera->getPosition();
            ctx.invViewMatrix = glm::inverse(ctx.viewMatrix);
            ctx.invProjectionMatrix = glm::inverse(ctx.projectionMatrix);
            
            ctx.camera = m_camera.get();
            ctx.scene = m_scene.get();

            m_renderer->render(ctx);
        }

        // 7. Render UI
        if (m_showUI && m_imguiLayer && m_uiManager) {
            m_imguiLayer->beginFrame();
            
            // Update render stats before rendering UI
            UIManager::RenderStats stats;
            stats.fps = m_fps;
            stats.frameTime = m_deltaTime * 1000.0f; // Convert to milliseconds
            m_uiManager->updateRenderStats(stats);
            
            m_uiManager->render();
            m_imguiLayer->endFrame(cmd);
        }

        // End render pass and command buffer
        vkCmdEndRenderPass(cmd);
        
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            throw std::runtime_error("Failed to record command buffer!");
        }

        // 8. Submit and present
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { m_frameResources->getImageAvailableSemaphore() };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        VkSemaphore signalSemaphores[] = { m_frameResources->getRenderFinishedSemaphore() };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, 
                          m_frameResources->getInFlightFence()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to submit draw command buffer!");
        }

        // Present
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = { m_swapChain->getSwapChain() };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(m_device->getPresentQueue(), &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            recreateSwapChain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to present swap chain image!");
        }

        // 9. End frame
        m_frameResources->endFrame();
    }

    // 10. Update frame stats
    updateFrameStats();
    m_frameCount++;
}

void Engine::recreateSwapChain() {
    // Handle minimization
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window->getNativeHandle(), &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window->getNativeHandle(), &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_device->getDevice());

    // Recreate swapchain
    auto deviceShared = std::shared_ptr<VulkanDevice>(m_device.get(), [](VulkanDevice*){});
    m_swapChain = std::make_unique<VulkanSwapChain>(deviceShared, width, height);

    // Recreate renderer passes that depend on swapchain
    if (m_renderer) {
        m_renderer->onSwapChainRecreated(m_swapChain.get());
    }

    std::cout << "[Engine] SwapChain recreated: " << width << "x" << height << "\n";
}

void Engine::updateFrameStats() {
    m_fpsUpdateTimer += m_deltaTime;
    m_fpsFrameCount++;

    // Update FPS every second
    if (m_fpsUpdateTimer >= 1.0f) {
        m_fps = static_cast<float>(m_fpsFrameCount) / m_fpsUpdateTimer;
        m_fpsUpdateTimer = 0.0f;
        m_fpsFrameCount = 0;
    }
}

void Engine::requestExit() {
    std::cout << "[Engine] Exit requested\n";
    m_running = false;
}