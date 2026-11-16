/**
 * @file Engine.cpp
 * @brief 引擎主入口 — 通过 RHI 抽象驱动渲染后端(Vulkan/DX12)
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
#include "SceneSerializer.h"
#include "ModelImporter.h"
#include "SoftwareRaytracer.h"
#include "ImageWriter.h"

#include "nfd/nfd.h"

#include "RHI.h"
#include "RHIDevice.h"
#include "RHISwapChain.h"
#include "RHICommandBuffer.h"

#include "panels/DebugPanel.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/AssetBrowserPanel.h"

#include <imgui.h>
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <algorithm>

// ============================================================
// 构造 & 析构
// ============================================================

namespace {

// 编辑器状态持久化：CWD 下的小 JSON（与 imgui.ini 同位置约定，不进 git）
constexpr const char* kEditorSettingsFile = "editor_settings.json";

// editor_settings.json 为 UTF-8 JSON：路径按 u8string 存取，中文路径不损坏
void saveLastScene(const std::filesystem::path& scenePath) {
    nlohmann::json j{{"lastScene", scenePath.u8string()}};
    std::ofstream ofs(kEditorSettingsFile, std::ios::binary);
    if (ofs.is_open()) ofs << j.dump(2) << std::endl;
}

std::string loadLastScene() {   // 返回 UTF-8 字符串（调用方经 u8path() 还原）
    std::ifstream ifs(kEditorSettingsFile, std::ios::binary);
    if (!ifs.is_open()) return {};
    try {
        auto j = nlohmann::json::parse(ifs);
        return j.value("lastScene", std::string());
    } catch (const nlohmann::json::parse_error&) {
        return {};
    }
}

} // anonymous namespace

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

    // 0. NFD（文件对话框的 COM 初始化；失败仅告警——对话框可能因
    //    其他组件已初始化 COM 而仍可用）
    if (NFD_Init() != NFD_OKAY) {
        std::cout << "[Engine] Warning: NFD_Init failed (" << NFD_GetError() << ")\n";
    }

    // 1. Window
    Window::Config wc;
    wc.title = m_config.title;
    wc.width = m_config.width;
    wc.height = m_config.height;
    m_window = std::make_unique<Window>(wc);
    std::cout << "[Engine] Window created\n";

    // 2. RHI Device (factory — backend chosen at CMake configure time)
    //    VENGINE_RHI_DX12 is defined by the build system when the engine is
    //    configured with -DVENGINE_RHI_BACKEND=dx12 (Windows only).
#if defined(VENGINE_RHI_DX12)
    const RHIBackend backend = RHIBackend::DX12;
#else
    const RHIBackend backend = RHIBackend::Vulkan;
#endif
    m_rhiDevice = RHI::CreateDevice(backend, m_window->getNativeHandle());
    std::cout << "[Engine] RHI device created (" << (backend == RHIBackend::DX12 ? "DX12" : "Vulkan")
              << ")\n";

    // 3. SwapChain (via RHI Device factory)
    RHISwapChainDesc swapChainDesc;
    swapChainDesc.width  = m_config.width;
    swapChainDesc.height = m_config.height;
    m_rhiSwapChain = m_rhiDevice->createSwapChain(swapChainDesc);
    std::cout << "[Engine] SwapChain created\n";

    // 4. Command Buffers
    createCommandBuffers();
    std::cout << "[Engine] Command buffers created\n";

    // 5. Sync Objects
    createSyncObjects();
    std::cout << "[Engine] Sync objects created\n";

    // 6. Camera
    m_camera = std::make_unique<Camera>(glm::vec3(0.0f, 0.0f, 5.0f));
    if (m_config.camOverride) {
        m_camera->setPosition(glm::vec3(m_config.camPos[0], m_config.camPos[1], m_config.camPos[2]));
        m_camera->setOrientation(m_config.camYaw, m_config.camPitch);
        std::cout << "[Engine] Camera override: pos(" << m_config.camPos[0] << "," << m_config.camPos[1]
                  << "," << m_config.camPos[2] << ") yaw=" << m_config.camYaw
                  << " pitch=" << m_config.camPitch << "\n";
    }
    std::cout << "[Engine] Camera created\n";

    // 7. Scene
    m_scene = std::make_unique<VEngine::Scene>();
    std::cout << "[Engine] Scene created\n";

    // 8. RenderSystem (with RHI device — MeshManager needs it for buffer creation)
    m_renderSystem = std::make_unique<VEngine::RenderSystem>();
    m_renderSystem->init(m_rhiDevice.get());
    std::cout << "[Engine] RenderSystem created\n";

    // 9. SceneRenderer (with ForwardPass)
    //    离线软光追不需要 GPU 管线（部分环境 MoltenVK 管线创建可能失败，
    //    跳过以保证 --raytrace 仅依赖 CPU 与 MeshManager）
    if (!m_config.raytraceOffline) {
        m_renderer = std::make_unique<SceneRenderer>(m_rhiDevice.get(), m_rhiSwapChain.get());
        m_renderer->initialize();
        m_renderer->setScene(m_scene.get());
        m_renderer->setCamera(m_camera.get());
        m_renderer->setRenderSystem(m_renderSystem.get());
        std::cout << "[Engine] SceneRenderer created (with Engine-level RHI device)\n";
    }

    // 10. Scene: 恢复上次打开的场景（editor_settings.json）；
    //     无记录则默认加载 Sponza（资产缺失时回退演示场景）
    {
        std::string lastScene = loadLastScene();
        if (!lastScene.empty()) {
            std::cout << "[Engine] Restoring last scene: " << lastScene << "\n";
            if (!openSceneFromFile(std::filesystem::u8path(lastScene))) {
                std::cout << "[Engine] Last scene unavailable, using default scene\n";
                createDefaultScene();
            }
        } else if (!openSceneFromFile(std::filesystem::path("assets/scenes/sponza.vscene"))) {
            std::cout << "[Engine] Default Sponza scene unavailable, using demo scene\n";
            createDefaultScene();
        }
    }

    // 11. SelectionManager
    VEngine::SelectionManager::getInstance().setScene(m_scene.get());

    // 12. UI (ImGui)
    if (m_config.enableUI && !m_config.raytraceOffline) {
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

        // File 菜单 / 快捷键 / 资源浏览器动作回调
        setupUICallbacks();

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
    sphereEntity.addComponent<VEngine::MeshRendererComponent>("sphere", "earth_material");
    auto& sphereMat = sphereEntity.addComponent<VEngine::PBRMaterialComponent>();
    sphereMat.albedoMap = "../../assets/Earth/Maps/Color Map.jpg";
    sphereMat.normalMap = "../../assets/Earth/Maps/Bump.jpg";
    sphereMat.metallicMap = "../../assets/Earth/Maps/Spec Mask.png";

    // UFO
    auto ufoEntity = m_scene->createEntity("UFO");
    ufoEntity.addComponent<VEngine::MeshRendererComponent>(
        "../../assets/UFO/UFO_Empty.obj", "ufo_material");
    auto& ufoTx = ufoEntity.getComponent<VEngine::TransformComponent>();
    ufoTx.position = glm::vec3(3.0f, 0.0f, 0.0f);
    ufoTx.scale = glm::vec3(1.0f);
    auto& ufoMat = ufoEntity.addComponent<VEngine::PBRMaterialComponent>();
    ufoMat.albedoMap = "../../assets/UFO/textures/UFO_color.jpg";
    ufoMat.normalMap = "../../assets/UFO/textures/UFO_nmap.jpg";
    ufoMat.metallicMap = "../../assets/UFO/textures/UFO_metalness.jpg";

    // 注：不再创建共面地面 Plane——它 y=-1.5 与水面完全共面，作为不透明几何
    // 写入 GBuffer 后会把水面 alpha 压到近 0（water.frag 深度遮挡的共面分支），
    // 挡住水面反射。水面本身即承担地面职责。
    std::cout << "[Engine] Default scene created (2 entities: Sphere + UFO)\n";
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

    // Key (feature toggles) — GLFW callback; logic lives in handleKey so the
    // autotest path exercises the identical code.
    m_window->setKeyCallback([this](int key, int scancode, int action, int mods) {
        if (action != GLFW_PRESS) return;
        (void)scancode; (void)mods;
        handleKey(key);
    });

    // Drag & drop
    m_window->setDropCallback([this](int count, const char** paths) {
        if (count == 0 || !m_scene) return;
        for (int i = 0; i < count; ++i) {
            importModelFile(paths[i]);
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
    m_rhiCommandBuffers.clear();
    for (void* native : m_commandBuffers) {
        m_rhiCommandBuffers.push_back(m_rhiDevice->wrapCommandBuffer(native));
    }
}

// ============================================================
// Key handling (shared by GLFW callback and autotest)
// ============================================================

void Engine::handleKey(int key) {
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
            // 展示位姿：倒影俯角 ≈ atan((眼高+物高)/距离) 远大于直接视角，
            // 低机位平视时倒影基本在 45° FOV 画框外（需俯视才可见）。
            // 抬高机位 + 俯角让物体与完整倒影同框，开箱即见平面反射效果。
            // --cam 覆盖时不重置（自动化验证需要固定自定义位姿）
            if (m_camera && !m_config.camOverride) {
                m_camera->setPosition(glm::vec3(0.0f, 3.0f, 6.0f));
                m_camera->setOrientation(-90.0f, -25.0f);
            }
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
    case GLFW_KEY_F6: {
        // GPU 硬件光追（DXR；Tier 不可用时由 SceneRenderer 回退 Normal）
        if (settings.renderMode != RenderMode::GPURayTrace) {
            settings.renderMode = RenderMode::GPURayTrace;
            std::cout << "[Engine] → GPU Ray Trace (DXR)\n";
        } else {
            settings.renderMode = RenderMode::Normal;
            std::cout << "[Engine] → Normal (Forward)\n";
        }
        break;
    }
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
        m_renderer->cycleNaniteDebugMode();
        break;
    case GLFW_KEY_Z:
        settings.naniteFrustumCulling = !settings.naniteFrustumCulling;
        std::cout << "[Engine] Nanite Frustum Culling " << (settings.naniteFrustumCulling ? "ON" : "OFF") << "\n";
        break;
    case GLFW_KEY_X:
        settings.naniteConeCulling = !settings.naniteConeCulling;
        std::cout << "[Engine] Nanite Cone Culling " << (settings.naniteConeCulling ? "ON" : "OFF") << "\n";
        break;
    case GLFW_KEY_B:
        settings.naniteForceLOD = (settings.naniteForceLOD >= 7) ? -1 : settings.naniteForceLOD + 1;
        std::cout << "[Engine] Nanite Force LOD "
                  << (settings.naniteForceLOD < 0 ? "OFF" : std::to_string(settings.naniteForceLOD)) << "\n";
        break;
    }
}

// ============================================================
// Autotest pump (called once per frame from mainLoop)
//
// Drives unattended regression soaks for both backends:
//  1. Key injection: drains the --autotest key sequence on a timer
//     (one key every --interval seconds) through handleKey() — the
//     exact same path the real GLFW keyboard callback takes.
//  2. Timed exit: once the sequence is fully injected AND the
//     --seconds budget (measured from main-loop start) is reached,
//     exits through the normal requestExit() teardown path.
// No-op when no autotest options were given.
// Example: VulkanPBR --autotest 8900 --interval 4 --seconds 15
// presses 8 (cluster) -> 9 (viz on) -> 0 -> 0 (cycle debug mode),
// soaks ~15 s, then exits cleanly.
// ============================================================
void Engine::pumpAutotest() {
    if (m_config.autotestKeys.empty() && m_config.autotestSeconds <= 0.0) return;
    const double now = glfwGetTime();

    if (m_autotestIndex < m_config.autotestKeys.size()) {
        if (now >= m_autotestNextAt) {
            const int key = m_config.autotestKeys[m_autotestIndex++];
            std::cout << "[autotest] inject key " << key << "\n";
            handleKey(key);
            m_autotestNextAt = now + m_config.autotestInterval;
        }
        return;   // 序列发完前不退出计时 / exit timer is only evaluated after the sequence drains
    }
    if (m_config.autotestSeconds > 0.0 &&
        now - m_autotestStart >= m_config.autotestSeconds) {
        std::cout << "[autotest] time budget reached, exiting\n";
        requestExit();
    }
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
    m_autotestStart = glfwGetTime();
    m_autotestNextAt = m_autotestStart + m_config.autotestInterval;

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

    // Water (and other time-driven passes) read SceneRenderer::m_totalTime.
    if (m_renderer) m_renderer->setTotalTime(m_totalTime);

    // Poll events
    m_window->pollEvents();
    pumpAutotest();

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
        m_renderSystem->updateRenderables(m_scene.get(), passes,
                                          m_camera ? m_camera->getPosition() : glm::vec3(0.0f));
    }

    // GPU culling data
    if (m_renderer->getSettings().enableGPUCulling)
        m_renderer->prepareGPUCullingData();

    // Reset fence and record commands
    m_rhiDevice->resetFence(m_inFlightFences[m_currentFrame]);

    RHICommandBuffer* cmd = m_rhiCommandBuffers[m_currentFrame].get();
    cmd->begin();

    // Update debug panel stats
    if (m_uiManager) {
        auto* dp = m_uiManager->getDebugPanel();
        if (dp) { dp->setFPS(m_fps); dp->setFrameTime(m_deltaTime * 1000.0f); }
    }

    // Record all rendering commands (including UI)
    m_renderer->recordCommands(cmd, imageIndex, m_currentFrame);

    cmd->end();

    // Submit via RHI
    m_rhiDevice->submitGraphicsQueue(
        { m_imageAvailableSemaphores[m_currentFrame] },
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

    try {
        m_rhiSwapChain->recreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    } catch (const std::exception& e) {
        // Transient DXGI failures (e.g. DXGI_ERROR_DEVICE_REMOVED/OUT_OF_DATE
        // during rapid resizes) must not kill the app: retry on the next frame.
        std::cerr << "[Engine] SwapChain recreate failed: " << e.what()
                  << " (retrying)\n";
        m_framebufferResized = true;
        return;
    }

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

    VEngine::Ray ray = VEngine::RayPicker::screenToWorldRay(
        (float)mx, (float)my, (float)w, (float)h, view, proj);

    entt::entity hitEntity = entt::null;
    float closestT = std::numeric_limits<float>::max();

    auto& registry = m_scene->getRegistry();
    auto ecsView = registry.view<VEngine::TransformComponent, VEngine::MeshRendererComponent>();
    auto* meshMgr = m_renderSystem->getMeshManager();

    for (auto entity : ecsView) {
        auto& mr = ecsView.get<VEngine::MeshRendererComponent>(entity);

        VEngine::AABB aabb;
        if (meshMgr) aabb = meshMgr->getMeshAABB(mr.meshPath);
        else { aabb.min = glm::vec3(-1); aabb.max = glm::vec3(1); }

        VEngine::AABB world = aabb.transform(VEngine::computeWorldMatrix(registry, entity));
        float tMin, tMax;
        if (VEngine::RayPicker::rayIntersectsAABB(ray, world, tMin, tMax)) {
            if (tMin >= 0 && tMin < closestT) { closestT = tMin; hitEntity = entity; }
        }
    }

    if (hitEntity != entt::null) {
        VEngine::SelectionManager::getInstance().select(hitEntity);
        if (m_uiManager) {
            if (auto* h = m_uiManager->getSceneHierarchyPanel()) h->setSelectedEntity(hitEntity);
            if (auto* i = m_uiManager->getInspectorPanel()) {
                i->setScene(m_scene.get());
                i->setSelectedEntity(hitEntity);
            }
        }
    } else {
        VEngine::SelectionManager::getInstance().clearSelection();
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

    NFD_Quit();   // 与 initializeSubsystems 的 NFD_Init 配对

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

// ============================================================
// Scene file & model import
// ============================================================

void Engine::setupUICallbacks() {
    if (!m_uiManager) return;

    m_uiManager->setOnNewScene([this]() { newScene(); });
    m_uiManager->setOnOpenScene([this]() { openSceneDialog(); });
    m_uiManager->setOnSaveScene([this]() { saveScene(); });
    m_uiManager->setOnSaveSceneAs([this]() { saveSceneAs(); });
    m_uiManager->setOnExit([this]() { requestExit(); });

    // 资源浏览器：双击模型 → 导入；双击场景文件 → 打开
    if (auto* browser = m_uiManager->getAssetBrowserPanel()) {
        browser->setOnAssetDoubleClicked([this](const std::string& path, AssetBrowserPanel::AssetType type) {
            if (type == AssetBrowserPanel::AssetType::Model) {
                importModelFile(path);
            } else if (type == AssetBrowserPanel::AssetType::Scene) {
                openSceneFromFile(path);
            }
        });
    }
    updateSceneTitle();
}

void Engine::importModelFile(const std::string& filePath) {
    if (!m_scene) return;
    if (!VEngine::ModelImporter::isSupported(filePath)) {
        std::cout << "[Engine] Unsupported model file (expect .obj/.gltf/.glb): " << filePath << "\n";
        return;
    }
    auto root = VEngine::ModelImporter::importModel(m_scene.get(), filePath);
    if (root) {
        VEngine::SelectionManager::getInstance().select(root.getHandle());
        if (m_uiManager) {
            if (auto* h = m_uiManager->getSceneHierarchyPanel()) h->setSelectedEntity(root.getHandle());
            if (auto* i = m_uiManager->getInspectorPanel()) i->setSelectedEntity(root.getHandle());
        }
    }
}

void Engine::newScene() {
    if (!m_scene) return;
    m_scene->clear();
    VEngine::SelectionManager::getInstance().clearSelection();
    if (m_uiManager) {
        if (auto* h = m_uiManager->getSceneHierarchyPanel()) h->setSelectedEntity(entt::null);
        if (auto* i = m_uiManager->getInspectorPanel()) i->setSelectedEntity(entt::null);
    }
    m_currentScenePath.clear();
    saveLastScene("");   // 清除记忆 → 下次启动回演示场景
    updateSceneTitle();
    std::cout << "[Engine] New scene\n";
}

void Engine::openSceneDialog() {
    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filterItems[2] = {
        { "V-Engine Scene", "vscene,json" },
        { "All Files", "*" }
    };
    nfdresult_t result = NFD_OpenDialog(&outPath, filterItems, 2, nullptr);
    if (result == NFD_OKAY && outPath) {
        // NFD 返回 UTF-8 路径；经 u8path 转原生宽字符（中文路径的关键）
        std::filesystem::path path = std::filesystem::u8path(outPath);
        NFD_FreePath(outPath);
        if (!openSceneFromFile(path)) {
            std::cout << "[Engine] Failed to open scene: " << path.string() << "\n";
        }
    } else if (result == NFD_ERROR) {
        std::cout << "[Engine] Open dialog error: " << NFD_GetError() << "\n";
    }
}

bool Engine::openSceneFromFile(const std::filesystem::path& filePath) {
    if (!m_scene) return false;
    VEngine::SceneSerializer serializer(m_scene.get());
    if (serializer.deserialize(filePath)) {
        m_currentScenePath = filePath;
        saveLastScene(filePath);   // 记住最近场景（下次启动恢复）
        updateSceneTitle();
        if (m_window) {
            // 窗口标题反映当前场景（GLFW 要求 UTF-8；失败则忽略——纯显示用途）
            std::string title = m_config.title + " - " + filePath.filename().u8string();
            m_window->setTitle(title);
        }
        return true;
    }
    return false;
}

bool Engine::openSceneFromFile(const std::string& filePath) {
    // argv / 引擎内部字符串按本地 ANSI 代码页解释（与 CRT 一致）
    return openSceneFromFile(std::filesystem::path(filePath));
}

bool Engine::saveSceneToPath(const std::filesystem::path& filePath) {
    if (!m_scene || filePath.empty()) return false;
    VEngine::SceneSerializer serializer(m_scene.get());
    if (serializer.serialize(filePath)) {
        m_currentScenePath = filePath;
        saveLastScene(filePath);   // 记住最近场景
        updateSceneTitle();
        return true;
    }
    return false;
}

// ============================================================
// 离线软光追（--raytrace）
// ============================================================

void Engine::injectTestLights(int count) {
    if (!m_scene || count <= 0) return;

    auto& registry = m_scene->getRegistry();

    // 覆盖 Sponza 走廊范围的网格（x∈[-8,8] z∈[-5,5]，比例 16:10）
    int nx = std::max(1, static_cast<int>(std::sqrt(count * 16.0 / 10.0)));
    int nz = std::max(1, (count + nx - 1) / nx);
    float stepX = 16.0f / nx;
    float stepZ = 10.0f / nz;

    int placed = 0;
    for (int iz = 0; iz < nz && placed < count; ++iz) {
        for (int ix = 0; ix < nx && placed < count; ++ix) {
            auto lightEntity = m_scene->createEntity("RT_TestLight_" + std::to_string(placed));
            auto& transform = lightEntity.getComponent<VEngine::TransformComponent>();
            transform.position = glm::vec3(-8.0f + (ix + 0.5f) * stepX, 3.2f,
                                           -5.0f + (iz + 0.5f) * stepZ);

            auto& light = lightEntity.addComponent<VEngine::LightComponent>(
                VEngine::LightType::Point);
            // 确定性色彩变化（暖-冷），物理 1/d² 衰减
            uint32_t h = VEngine::RT::pcgHash(static_cast<uint32_t>(placed) * 2654435761u);
            float tint = (h & 0xFF) / 255.0f;   // 0..1
            light.color = glm::mix(glm::vec3(1.0f, 0.75f, 0.55f),
                                   glm::vec3(0.55f, 0.75f, 1.0f), tint);
            light.intensity = 2.0f;
            light.constantAttenuation = 0.0f;
            light.linearAttenuation = 0.0f;
            light.quadraticAttenuation = 1.0f;   // 1/d²
            light.range = 0.0f;                  // 无限
            ++placed;
        }
    }
    std::cout << "[Engine] Injected " << placed << " test point lights" << std::endl;
}

bool Engine::renderOfflineRaytrace() {
    if (!m_scene || !m_camera) {
        std::cerr << "[Engine] Offline raytrace: scene/camera not ready" << std::endl;
        return false;
    }

    // 多光源注入（--rt-lights，ReSTIR 测试用）
    if (m_config.rtLights > 0) injectTestLights(m_config.rtLights);

    VEngine::RT::SoftwareRaytracer rt;

    std::cout << "[Engine] Offline raytrace: building scene (BVH)..." << std::endl;
    rt.buildFromScene(m_scene.get());

    // 相机（GL 惯例投影；Engine 构造已应用 --cam 覆盖）
    glm::mat4 view = m_camera->getViewMatrix();
    glm::mat4 proj = glm::perspective(
        glm::radians(m_camera->getZoom()),
        static_cast<float>(m_config.rtWidth) / static_cast<float>(m_config.rtHeight),
        0.1f, 1000.0f);
    rt.setCamera(view, proj, m_config.rtWidth, m_config.rtHeight);

    rt.settings.algorithm = m_config.rtRestir
        ? VEngine::RT::Algorithm::PathTracingRestir
        : (m_config.rtPathTracing ? VEngine::RT::Algorithm::PathTracing
                                  : VEngine::RT::Algorithm::Whitted);
    rt.settings.samplesPerPixel = m_config.rtSamples;
    rt.settings.maxBounces = m_config.rtBounces;
    rt.settings.shadows = true;
    rt.settings.restirShadowInTarget = m_config.rtShadowInTarget;
    rt.settings.exposure = m_config.rtExposure;
    rt.settings.restirDenoiser = m_config.rtDenoise;

    const char* algoName = rt.settings.algorithm == VEngine::RT::Algorithm::PathTracingRestir
        ? "path tracing + ReSTIR"
        : rt.settings.algorithm == VEngine::RT::Algorithm::PathTracing
            ? "path tracing" : "whitted";
    std::cout << "[Engine] Offline raytrace: rendering " << m_config.rtWidth << "x"
              << m_config.rtHeight << " spp=" << m_config.rtSamples
              << " bounces=" << m_config.rtBounces
              << " frames=" << m_config.rtFrames
              << " algorithm=" << algoName
              << "..." << std::endl;

    auto startTime = std::chrono::high_resolution_clock::now();
    for (int f = 0; f < std::max(1, m_config.rtFrames); ++f) {
        rt.renderFrame(static_cast<uint32_t>(f));
    }
    auto endTime = std::chrono::high_resolution_clock::now();
    float renderMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

    std::cout << "[Engine] Offline raytrace: done in " << renderMs << " ms ("
              << rt.totalTriangles() << " tris, " << rt.totalNodes() << " BVH nodes, "
              << rt.lightCount() << " lights, " << rt.rayCount() << " rays)" << std::endl;

    auto rgba = rt.resolveForOutput();
    if (!VEngine::RT::savePNG(m_config.rtOutputPath, rgba,
                              m_config.rtWidth, m_config.rtHeight)) {
        std::cerr << "[Engine] Offline raytrace: failed to save "
                  << m_config.rtOutputPath << std::endl;
        return false;
    }

    std::cout << "[Engine] Offline raytrace: saved " << m_config.rtOutputPath << std::endl;
    return true;
}

void Engine::saveScene() {
    if (!m_scene) return;
    if (m_currentScenePath.empty()) {
        saveSceneAs();
        return;
    }
    VEngine::SceneSerializer serializer(m_scene.get());
    if (serializer.serialize(m_currentScenePath)) {
        updateSceneTitle();
    }
}

void Engine::saveSceneAs() {
    if (!m_scene) return;
    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filterItems[1] = { "V-Engine Scene", "vscene" };
    nfdresult_t result = NFD_SaveDialog(&outPath, filterItems, 1, nullptr, "scene.vscene");
    if (result == NFD_OKAY && outPath) {
        // NFD 返回 UTF-8 路径；经 u8path 转原生宽字符（中文路径的关键）
        std::filesystem::path path = std::filesystem::u8path(outPath);
        NFD_FreePath(outPath);

        // 未带扩展名时补默认后缀
        if (!path.has_extension()) {
            path += ".vscene";
        }

        if (saveSceneToPath(path)) {
            if (m_window) {
                std::string title = m_config.title + " - " + path.filename().u8string();
                m_window->setTitle(title);
            }
        } else {
            std::cout << "[Engine] Failed to save scene: " << path.string() << "\n";
        }
    } else if (result == NFD_ERROR) {
        std::cout << "[Engine] Save dialog error: " << NFD_GetError() << "\n";
    }
}

void Engine::updateSceneTitle() {
    if (!m_uiManager) return;
    std::string title = m_currentScenePath.empty()
        ? std::string("Untitled")
        : m_currentScenePath.filename().u8string();   // ImGui 文本要求 UTF-8
    m_uiManager->setSceneTitle("Scene: " + title);
}
