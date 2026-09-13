/**
 * @file Engine.h
 * @brief Engine 主入口 — 管理所有子系统的生命周期
 *
 * 模块化引擎架构:场景/ECS 层不依赖具体图形 API,渲染经 RHI 抽象(Vulkan/DX12)。
 */

#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

// Forward declarations
class Window;
class SceneRenderer;
class UIManager;
class ImGuiLayer;
class Camera;
class RHIDevice;
class RHISwapChain;
class RHICommandBuffer;

namespace VEngine {
    class Scene;
    class RenderSystem;
}

struct EngineConfig {
    std::string title = "Vulkan PBR Renderer";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool enableValidation = true;
    bool enableUI = true;

    // Automation (--autotest): 按键序列(GLFW 键码,每 interval 秒注入一个,
    // 走与真实键盘回调相同的 handleKey 路径);序列发完后运行 seconds 秒退出。
    // 0 = 不退出。
    // Automation (--autotest): key sequence (GLFW key codes) injected one per
    // `interval` seconds through the same handleKey path as real keyboard
    // input; after the sequence drains, run for `seconds` more and exit
    // (0 = never auto-exit). Consumed by Engine::pumpAutotest, parsed in main().
    std::vector<int> autotestKeys;
    double autotestInterval = 3.0;
    double autotestSeconds = 0.0;
};

class Engine {
public:
    using Config = EngineConfig;

    Engine(const Config& config = Config{});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void run();
    void requestExit();

    // Subsystem accessors
    Window* getWindow() const { return m_window.get(); }
    SceneRenderer* getRenderer() const { return m_renderer.get(); }
    RHIDevice* getRHIDevice() const { return m_rhiDevice.get(); }
    RHISwapChain* getRHISwapChain() const { return m_rhiSwapChain.get(); }
    VEngine::Scene* getScene() const { return m_scene.get(); }
    Camera* getCamera() const { return m_camera.get(); }

    float getDeltaTime() const { return m_deltaTime; }
    float getFPS() const { return m_fps; }

private:
    void initializeSubsystems();
    void createDefaultScene();
    void setupInputCallbacks();
    void shutdownSubsystems();

    // Frame sync
    void createSyncObjects();
    void createCommandBuffers();

    // Main loop
    void mainLoop();
    void drawFrame();
    void recreateSwapChain();
    void updateFrameStats();

    // Input (direct GLFW callbacks via Window)
    void processKeyboardInput(float dt);
    void handleMousePicking();
    void handleKey(int key);           // shared by GLFW callback + autotest
    void pumpAutotest();               // per-frame autotest driver: timed key
                                       // injection + timed exit (see Engine.cpp)

    // Config
    Config m_config;
    bool m_running = false;

    // Subsystems
    std::unique_ptr<Window> m_window;
    std::unique_ptr<RHIDevice> m_rhiDevice;
    std::shared_ptr<RHISwapChain> m_rhiSwapChain;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<VEngine::Scene> m_scene;
    std::unique_ptr<VEngine::RenderSystem> m_renderSystem;
    std::unique_ptr<SceneRenderer> m_renderer;
    std::unique_ptr<ImGuiLayer> m_imguiLayer;
    std::unique_ptr<UIManager> m_uiManager;

    // Sync objects (owned by Engine — stored as void* native handles)
    std::vector<void*> m_imageAvailableSemaphores;
    std::vector<void*> m_renderFinishedSemaphores;
    std::vector<void*> m_inFlightFences;
    std::vector<void*> m_imagesInFlight;
    std::vector<void*> m_commandBuffers;
    // Persistent RHI wrappers over m_commandBuffers (one per frame; begin/end
    // the record session each frame). Index-aligned with m_commandBuffers.
    std::vector<std::shared_ptr<RHICommandBuffer>> m_rhiCommandBuffers;
    uint32_t m_currentFrame = 0;
    bool m_framebufferResized = false;

    // Mouse state
    float m_lastMouseX = 640.0f;
    float m_lastMouseY = 360.0f;
    bool m_firstMouse = true;
    bool m_mouseEnabled = false;

    // Frame stats
    float m_deltaTime = 0.0f;
    float m_lastFrameTime = 0.0f;
    float m_totalTime = 0.0f;
    float m_fps = 0.0f;
    float m_fpsUpdateTimer = 0.0f;
    int m_fpsFrameCount = 0;

    // Autotest state (see EngineConfig)
    size_t m_autotestIndex = 0;
    double m_autotestStart = 0.0;
    double m_autotestNextAt = 0.0;

    static const int MAX_FRAMES_IN_FLIGHT = 2;
};