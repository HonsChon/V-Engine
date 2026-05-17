/**
 * @file Engine.h
 * @brief Engine 主入口 — 管理所有子系统的生命周期
 *
 * 从 VulkanRenderer 迁移而来的模块化引擎架构。
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

namespace VulkanEngine {
    class Scene;
    class RenderSystem;
}

struct EngineConfig {
    std::string title = "Vulkan PBR Renderer";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool enableValidation = true;
    bool enableUI = true;
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
    VulkanEngine::Scene* getScene() const { return m_scene.get(); }
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

    // Config
    Config m_config;
    bool m_running = false;

    // Subsystems
    std::unique_ptr<Window> m_window;
    std::unique_ptr<RHIDevice> m_rhiDevice;
    std::shared_ptr<RHISwapChain> m_rhiSwapChain;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<VulkanEngine::Scene> m_scene;
    std::unique_ptr<VulkanEngine::RenderSystem> m_renderSystem;
    std::unique_ptr<SceneRenderer> m_renderer;
    std::unique_ptr<ImGuiLayer> m_imguiLayer;
    std::unique_ptr<UIManager> m_uiManager;

    // Sync objects (owned by Engine — stored as void* native handles)
    std::vector<void*> m_imageAvailableSemaphores;
    std::vector<void*> m_renderFinishedSemaphores;
    std::vector<void*> m_inFlightFences;
    std::vector<void*> m_imagesInFlight;
    std::vector<void*> m_commandBuffers;
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

    static const int MAX_FRAMES_IN_FLIGHT = 2;
};