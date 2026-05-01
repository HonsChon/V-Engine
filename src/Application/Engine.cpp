/**
 * @file Engine.cpp
 * @brief 原有架构引擎主入口，用于从 VulkanRenderer 迁移
 */

#include "Engine.h"
#include "Window.h"
#include "SceneRenderer.h"
#include "Scene.h"
#include "Entity.h"
#include "Components.h"
#include "ImGuiLayer.h"
#include "UIManager.h"
#include "RenderSystem.h"
#include "Camera.h"
#include "ForwardPass.h"
#include "GBufferPass.h"
#include "SelectionManager.h"
#include "RayPicker.h"
#include "RenderSettings.h"
#include "nanite/NaniteManager.h"

#include "RHI.h"
#include "RHIDevice.h"
#include "RHISwapChain.h"
#include "Vulkan/VulkanRHIDevice.h"

#include <vulkan/vulkan.h>

#include "panels/DebugPanel.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/AssetBrowserPanel.h"

#include <imgui.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <algorithm>

// ============================================================
// 构造 & 析构
// ============================================================

Engine::Engine(const Config& config) : m_config(config) {
    std::cout << "========================================\n";
    std::cout << " " << m_config.title << "\n";
    std::cout << " Modular Engine Architecture\n";
    std::cout << "========================================\n";
    initializeSubsystems();
}

Engine::~Engine() {
    shutdownSubsystems();
}

// ============================================================
// 初始化
// ============================================================

void Engine::initializeSubsystems() {
    std::cout << "[Engine] Initializing subsystems...\n";

    // 1. Window
    Window::Config wc;
    wc.title = m_config.title;
    wc.width = m_config.width;
    wc.height = m_config.height;
    m_window = std::make_unique<Window>(wc);
    std::cout << "[Engine] Window created\n";

    // 2. RHI Device (standalone — owns Vulkan instance/device/surface)
    m_rhiDevice = std::make_unique<VulkanRHIDevice>(m_window->getNativeHandle());
    std::cout << "[Engine] RHI device created (standalone)\n";

    // 3. SwapChain (via RHI Device factory)
    m_rhiSwapChain = m_rhiDevice->createSwapChain(m_config.width, m_config.height);
    std::cout << "[Engine] SwapChain created\n";

    // 4. Command Buffers
    createCommandBuffers();
    std::cout << "[Engine] Command buffers created\n";

    // 5. Sync Objects
    createSyncObjects();
    std::cout << "[Engine] Sync objects created\n";

    // 6. Camera
    m_camera = std::make_unique<Camera>(glm::vec3(0.0f, 0.0f, 5.0f));
    std::cout << "[Engine] Camera created\n";

    // 7. Scene
    m_scene = std::make_unique<VulkanEngine::Scene>();
    std::cout << "[Engine] Scene created\n";

    // 8. RenderSystem (with RHI device — MeshManager needs it for buffer creation)
    m_renderSystem = std::make_unique<VulkanEngine::RenderSystem>();
    m_renderSystem->init(m_rhiDevice.get());
    std::cout << "[Engine] RenderSystem created\n";

    // 9. SceneRenderer (with ForwardPass)
    m_renderer = std::make_unique<SceneRenderer>(m_rhiDevice.get(), m_rhiSwapChain.get());
    m_renderer->initialize();
    m_renderer->setScene(m_scene.get());
    m_renderer->setCamera(m_camera.get());
    m_renderer->setRenderSystem(m_renderSystem.get());
    std::cout << "[Engine] SceneRenderer created (with Engine-level RHI device)\n";

    // 10. Default Scene
    createDefaultScene();

    // 11. SelectionManager
    VulkanEngine::SelectionManager::getInstance().setScene(m_scene.get());

    // 12. UI (ImGui)
    if (m_config.enableUI) {
        m_imguiLayer = std::make_unique<ImGuiLayer>(
            m_window->getNativeHandle(),
            m_rhiDevice.get(),
            m_rhiSwapChain.get()
        );

        m_uiManager = std::make_unique<UIManager>();

        if (m_uiManager->getAssetBrowserPanel())
            m_uiManager->getAssetBrowserPanel()->setRootPath("assets");
        if (m_uiManager->getInspectorPanel() && m_scene)
            m_uiManager->getInspectorPanel()->setScene(m_scene.get());
        if (m_uiManager->getSceneHierarchyPanel() && m_scene)
            m_uiManager->getSceneHierarchyPanel()->setScene(m_scene.get());

        m_uiManager->setRenderSettings(&m_renderer->getSettings());

        // Pass UI refs to SceneRenderer so it can render UI inside command recording
        m_renderer->setImGuiLayer(m_imguiLayer.get());
        m_renderer->setUIManager(m_uiManager.get());

        std::cout << "[Engine] UI system created\n";
    }

    // 13. Input callbacks
    setupInputCallbacks();

    std::cout << "[Engine] All subsystems initialized\n";
}

void Engine::createDefaultScene() {
    // Sphere (Earth texture)
    auto sphereEntity = m_scene->createEntity("Sphere");
    sphereEntity.addComponent<VulkanEngine::MeshRendererComponent>("sphere", "earth_material");
    auto& sphereMat = sphereEntity.addComponent<VulkanEngine::PBRMaterialComponent>();
    sphereMat.albedoMap = "../../assets/Earth/Maps/Color Map.jpg";
    sphereMat.normalMap = "../../assets/Earth/Maps/Bump.jpg";
    sphereMat.metallicMap = "../../assets/Earth/Maps/Spec Mask.png";

    // UFO
    auto ufoEntity = m_scene->createEntity("UFO");
    ufoEntity.addComponent<VulkanEngine::MeshRendererComponent>(
        "../../assets/UFO/UFO_Empty.obj", "ufo_material");
    auto& ufoTx = ufoEntity.getComponent<VulkanEngine::TransformComponent>();
    ufoTx.position = glm::vec3(3.0f, 0.0f, 0.0f);
    ufoTx.scale = glm::vec3(1.0f);
    auto& ufoMat = ufoEntity.addComponent<VulkanEngine::PBRMaterialComponent>();
    ufoMat.albedoMap = "../../assets/UFO/textures/UFO_color.jpg";
    ufoMat.normalMap = "../../assets/UFO/textures/UFO_nmap.jpg";
    ufoMat.metallicMap = "../../assets/UFO/textures/UFO_metalness.jpg";

    // Plane
    auto planeEntity = m_scene->createEntity("Plane");
    planeEntity.addComponent<VulkanEngine::MeshRendererComponent>("plane", "plane_material");
    auto& planeTx = planeEntity.getComponent<VulkanEngine::TransformComponent>();
    planeTx.position = glm::vec3(0.0f, -1.5f, 0.0f);
    planeEntity.addComponent<VulkanEngine::PBRMaterialComponent>();

    std::cout << "[Engine] Default scene created (3 entities)\n";
}

// ============================================================
// Input Callbacks (via Window)
// ============================================================

void Engine::setupInputCallbacks() {
    if (!m_window) return;

    // Resize
    m_window->setResizeCallback([this](uint32_t w, uint32_t h) {
        m_framebufferResized = true;
    });

    // Mouse cursor (camera rotation)
    m_window->setCursorPosCallback([this](double xpos, double ypos) {
        if (!m_mouseEnabled) return;
        float x = static_cast<float>(xpos);
        float y = static_cast<float>(ypos);
        if (m_firstMouse) { m_lastMouseX = x; m_lastMouseY = y; m_firstMouse = false; }
        float xoff = x - m_lastMouseX;
        float yoff = m_lastMouseY - y;
        m_lastMouseX = x; m_lastMouseY = y;
        if (m_camera) m_camera->processMouseMovement(xoff, yoff);
    });

    // Scroll (FOV)
    m_window->setScrollCallback([this](double, double yoff) {
        if (m_camera) m_camera->processMouseScroll(static_cast<float>(yoff));
    });

    // Mouse button
    m_window->setMouseButtonCallback([this](int button, int action, int mods) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) return;

        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            handleMousePicking();
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                m_mouseEnabled = true; m_firstMouse = true;
                m_window->setCursorMode(GLFW_CURSOR_DISABLED);
            } else if (action == GLFW_RELEASE) {
                m_mouseEnabled = false;
                m_window->setCursorMode(GLFW_CURSOR_NORMAL);
            }
        }
    });

    // Key (feature toggles)
    m_window->setKeyCallback([this](int key, int scancode, int action, int mods) {
        if (action != GLFW_PRESS) return;
        auto& settings = m_renderer->getSettings();

        switch (key) {
        case GLFW_KEY_ESCAPE:
            requestExit(); break;
        case GLFW_KEY_F1:
            settings.showUI = !settings.showUI;
            std::cout << "[Engine] UI " << (settings.showUI ? "ON" : "OFF") << "\n"; break;
        case GLFW_KEY_5:
            if (settings.renderMode == RenderMode::Normal) {
                settings.renderMode = RenderMode::WaterScene;
                std::cout << "[Engine] → Water Scene (Deferred)\n";
                if (!m_renderer->isDeferredInitialized())
                    m_renderer->initDeferredShading();
            } else {
                settings.renderMode = RenderMode::Normal;
                std::cout << "[Engine] → Normal (Forward)\n";
            }
            break;
        case GLFW_KEY_6:
            settings.enableGPUCulling = !settings.enableGPUCulling;
            std::cout << "[Engine] GPU Culling " << (settings.enableGPUCulling ? "ON" : "OFF") << "\n";
            if (settings.enableGPUCulling) m_renderer->initGPUDrivenRendering();
            break;
        case GLFW_KEY_7:
            settings.enableNanite = !settings.enableNanite;
            std::cout << "[Engine] Nanite " << (settings.enableNanite ? "ON" : "OFF") << "\n";
            if (settings.enableNanite) m_renderer->initNanite();
            break;
        case GLFW_KEY_8:
            m_renderer->initNanite();
            m_renderer->testNaniteClustering();
            break;
        case GLFW_KEY_9:
            settings.showClusterVisualization = !settings.showClusterVisualization;
            std::cout << "[Engine] Cluster Vis " << (settings.showClusterVisualization ? "ON" : "OFF") << "\n";
            if (settings.showClusterVisualization) m_renderer->initNaniteDebugPass();
            break;
        case GLFW_KEY_0:
            // cycle debug mode handled by NaniteDebugPass directly
            break;
        }
    });

    // Drag & drop
    m_window->setDropCallback([this](int count, const char** paths) {
        if (count == 0 || !m_scene) return;
        std::string filePath = paths[0];
        std::string ext = std::filesystem::path(filePath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".obj") {
            auto e = m_scene->createEntity("Dropped Model");
            e.addComponent<VulkanEngine::MeshRendererComponent>(filePath, "default_material");
            std::cout << "[Engine] Loaded: " << filePath << "\n";
        }
    });

    std::cout << "[Engine] Input callbacks registered\n";
}

// ============================================================
// Sync Objects & Command Buffers
// ============================================================

void Engine::createSyncObjects() {
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imagesInFlight.resize(m_rhiSwapChain->getImageCount(), nullptr);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_imageAvailableSemaphores[i] = m_rhiDevice->createSemaphore();
        m_renderFinishedSemaphores[i] = m_rhiDevice->createSemaphore();
        m_inFlightFences[i] = m_rhiDevice->createFence(true); // signaled
    }
}

void Engine::createCommandBuffers() {
    m_commandBuffers = m_rhiDevice->allocateCommandBuffers(MAX_FRAMES_IN_FLIGHT);
}

// ============================================================
// Main Loop
// ============================================================

void Engine::run() {
    m_running = true;

    std::cout << "[Engine] Starting main loop...\n";
    std::cout << "Controls:\n";
    std::cout << "  WASD / Space / Shift - Move camera\n";
    std::cout << "  Right mouse - Look around\n";
    std::cout << "  Left mouse - Select object\n";
    std::cout << "  Scroll - Zoom\n";
    std::cout << "  5 - Toggle Water Scene\n";
    std::cout << "  6/7/8/9 - GPU Culling / Nanite\n";
    std::cout << "  F1 - Toggle UI\n";
    std::cout << "  ESC - Exit\n";

    m_lastFrameTime = static_cast<float>(glfwGetTime());

    while (m_running && !m_window->shouldClose()) {
        mainLoop();
    }

    m_rhiDevice->waitIdle();
    std::cout << "[Engine] Main loop ended\n";
}

void Engine::mainLoop() {
    // Delta time
    float now = static_cast<float>(glfwGetTime());
    m_deltaTime = now - m_lastFrameTime;
    m_lastFrameTime = now;
    m_totalTime = now;

    // Poll events
    m_window->pollEvents();

    // Keyboard input
    processKeyboardInput(m_deltaTime);

    // Draw
    drawFrame();

    // Stats
    updateFrameStats();
}

void Engine::drawFrame() {
    // Wait fence
    m_rhiDevice->waitForFence(m_inFlightFences[m_currentFrame]);

    // Nanite readback (safe after fence)
    auto* nm = m_renderer->getNaniteManager();
    if (nm) nm->readbackCullingResults(m_currentFrame);

    // Acquire image
    uint32_t imageIndex;
    RHISwapChainResult acquireResult = m_rhiSwapChain->acquireNextImage(
        m_imageAvailableSemaphores[m_currentFrame], &imageIndex);

    if (acquireResult == RHISwapChainResult::OutOfDate) { recreateSwapChain(); return; }
    else if (acquireResult == RHISwapChainResult::Error)
        throw std::runtime_error("Failed to acquire swap chain image!");

    // Check if this image is still in use
    if (m_imagesInFlight[imageIndex] != nullptr)
        m_rhiDevice->waitForFence(m_imagesInFlight[imageIndex]);
    m_imagesInFlight[imageIndex] = m_inFlightFences[m_currentFrame];

    // Update uniforms BEFORE resetting fence
    m_renderer->updateUniforms(m_currentFrame);

    // Update RenderSystem (material descriptors etc.)
    if (m_renderSystem && m_scene && m_renderer) {
        auto& settings = m_renderer->getSettings();
        std::vector<RenderPassBase*> passes;
        auto* fp = m_renderer->getForwardPass();
        if (fp) passes.push_back(fp);
        if (settings.renderMode == RenderMode::WaterScene && m_renderer->getGBufferPass())
            passes.push_back(m_renderer->getGBufferPass());
        m_renderSystem->updateRenderables(m_scene.get(), passes);
    }

    // GPU culling data
    if (m_renderer->getSettings().enableGPUCulling)
        m_renderer->prepareGPUCullingData();

    // Reset fence and record commands
    m_rhiDevice->resetFence(m_inFlightFences[m_currentFrame]);

    VkCommandBuffer cmd = static_cast<VkCommandBuffer>(m_commandBuffers[m_currentFrame]);
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin command buffer!");

    // Update debug panel stats
    if (m_uiManager) {
        auto* dp = m_uiManager->getDebugPanel();
        if (dp) { dp->setFPS(m_fps); dp->setFrameTime(m_deltaTime * 1000.0f); }
    }

    // Record all rendering commands (including UI)
    m_renderer->recordCommands(cmd, imageIndex, m_currentFrame);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer!");

    // Submit via RHI
    m_rhiDevice->submitGraphicsQueue(
        { m_imageAvailableSemaphores[m_currentFrame] },
        { (uint32_t)VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
        { m_commandBuffers[m_currentFrame] },
        { m_renderFinishedSemaphores[m_currentFrame] },
        m_inFlightFences[m_currentFrame]
    );

    // Present via RHI SwapChain
    RHISwapChainResult presentResult = m_rhiSwapChain->present(
        m_renderFinishedSemaphores[m_currentFrame], imageIndex);

    if (presentResult == RHISwapChainResult::OutOfDate ||
        presentResult == RHISwapChainResult::Suboptimal || m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapChain();
    } else if (presentResult == RHISwapChainResult::Error) {
        throw std::runtime_error("Failed to present!");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Engine::recreateSwapChain() {
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window->getNativeHandle(), &w, &h);
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(m_window->getNativeHandle(), &w, &h);
        glfwWaitEvents();
    }

    m_rhiDevice->waitIdle();

    m_rhiSwapChain->recreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h));

    m_imagesInFlight.clear();
    m_imagesInFlight.resize(m_rhiSwapChain->getImageCount(), nullptr);

    if (m_renderer) m_renderer->onSwapChainRecreated(m_rhiSwapChain.get());

    if (m_imguiLayer) {
        m_imguiLayer->onResize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    }

    std::cout << "[Engine] SwapChain recreated: " << w << "x" << h << "\n";
}

// ============================================================
// Input Helpers
// ============================================================

void Engine::processKeyboardInput(float dt) {
    if (!m_camera || !m_window) return;
    GLFWwindow* win = m_window->getNativeHandle();

    if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) m_camera->processKeyboard(FORWARD, dt);
    if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) m_camera->processKeyboard(BACKWARD, dt);
    if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) m_camera->processKeyboard(LEFT, dt);
    if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) m_camera->processKeyboard(RIGHT, dt);
    if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) m_camera->processKeyboard(UP, dt);
    if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) m_camera->processKeyboard(DOWN, dt);
}

void Engine::handleMousePicking() {
    if (!m_camera || !m_scene || !m_renderSystem) return;

    double mx, my;
    m_window->getCursorPos(mx, my);
    int w = m_window->getWidth(), h = m_window->getHeight();

    glm::mat4 view = m_camera->getViewMatrix();
    float fov = glm::radians(m_camera->getZoom());
    float aspect = (float)w / (float)h;
    glm::mat4 proj = glm::perspective(fov, aspect, 0.1f, 100.0f);

    VulkanEngine::Ray ray = VulkanEngine::RayPicker::screenToWorldRay(
        (float)mx, (float)my, (float)w, (float)h, view, proj);

    entt::entity hitEntity = entt::null;
    float closestT = std::numeric_limits<float>::max();

    auto& registry = m_scene->getRegistry();
    auto ecsView = registry.view<VulkanEngine::TransformComponent, VulkanEngine::MeshRendererComponent>();
    auto* meshMgr = m_renderSystem->getMeshManager();

    for (auto entity : ecsView) {
        auto& tx = ecsView.get<VulkanEngine::TransformComponent>(entity);
        auto& mr = ecsView.get<VulkanEngine::MeshRendererComponent>(entity);

        VulkanEngine::AABB aabb;
        if (meshMgr) aabb = meshMgr->getMeshAABB(mr.meshPath);
        else { aabb.min = glm::vec3(-1); aabb.max = glm::vec3(1); }

        VulkanEngine::AABB world = aabb.transform(tx.getTransform());
        float tMin, tMax;
        if (VulkanEngine::RayPicker::rayIntersectsAABB(ray, world, tMin, tMax)) {
            if (tMin >= 0 && tMin < closestT) { closestT = tMin; hitEntity = entity; }
        }
    }

    if (hitEntity != entt::null) {
        VulkanEngine::SelectionManager::getInstance().select(hitEntity);
        if (m_uiManager) {
            if (auto* h = m_uiManager->getSceneHierarchyPanel()) h->setSelectedEntity(hitEntity);
            if (auto* i = m_uiManager->getInspectorPanel()) {
                i->setScene(m_scene.get());
                i->setSelectedEntity(hitEntity);
            }
        }
    } else {
        VulkanEngine::SelectionManager::getInstance().clearSelection();
        if (m_uiManager) {
            if (auto* h = m_uiManager->getSceneHierarchyPanel()) h->setSelectedEntity(entt::null);
            if (auto* i = m_uiManager->getInspectorPanel()) i->setSelectedEntity(entt::null);
        }
    }
}

// ============================================================
// Shutdown & Stats
// ============================================================

void Engine::shutdownSubsystems() {
    std::cout << "[Engine] Shutting down...\n";

    if (m_rhiDevice) m_rhiDevice->waitIdle();

    // UI
    if (m_imguiLayer) { m_imguiLayer->cleanup(); m_imguiLayer.reset(); }
    m_uiManager.reset();

    // Renderer (must release before RHI device)
    m_renderer.reset();
    if (m_renderSystem) {
        m_renderSystem->cleanup();
        m_renderSystem.reset();
    }
    m_scene.reset();
    m_camera.reset();

    // Sync objects
    if (m_rhiDevice) {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            m_rhiDevice->destroySemaphore(m_renderFinishedSemaphores[i]);
            m_rhiDevice->destroySemaphore(m_imageAvailableSemaphores[i]);
            m_rhiDevice->destroyFence(m_inFlightFences[i]);
        }
    }

    // SwapChain must release before device
    m_rhiSwapChain.reset();
    m_rhiDevice.reset();
    m_window.reset();

    std::cout << "[Engine] All subsystems shut down\n";
}

void Engine::updateFrameStats() {
    m_fpsUpdateTimer += m_deltaTime;
    m_fpsFrameCount++;
    if (m_fpsUpdateTimer >= 1.0f) {
        m_fps = (float)m_fpsFrameCount / m_fpsUpdateTimer;
        m_fpsUpdateTimer = 0; m_fpsFrameCount = 0;
    }
}

void Engine::requestExit() {
    std::cout << "[Engine] Exit requested\n";
    m_running = false;
}