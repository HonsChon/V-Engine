/**
 * @file Engine.h
 * @brief Engine main entry - manages lifecycle of all subsystems
 * 
 * Responsibilities:
 * 1. Initialize and cleanup all subsystems
 * 2. Run the main loop
 * 3. Coordinate communication between modules
 */

#pragma once

#include <memory>
#include <string>
#include <cstdint>

// Forward declarations
class Window;
class Input;
class SceneRenderer;
class VulkanDevice;
class VulkanSwapChain;
class FrameResources;
class UIManager;
class ImGuiLayer;
class Camera;

namespace VulkanEngine {
    class Scene;
    class RenderSystem;
}

class Engine {
public:
    struct Config {
        std::string title = "Vulkan PBR Renderer";
        uint32_t width = 1280;
        uint32_t height = 720;
        bool enableValidation = true;
        bool enableUI = true;
    };

    Engine(const Config& config = Config{});
    ~Engine();

    // Non-copyable
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /**
     * @brief Run the engine main loop
     */
    void run();

    /**
     * @brief Request exit
     */
    void requestExit();

    // ========== Subsystem accessors ==========
    Window* getWindow() const { return m_window.get(); }
    Input* getInput() const { return m_input.get(); }
    SceneRenderer* getRenderer() const { return m_renderer.get(); }
    VulkanDevice* getDevice() const { return m_device.get(); }
    VulkanSwapChain* getSwapChain() const { return m_swapChain.get(); }
    VulkanEngine::Scene* getScene() const { return m_scene.get(); }
    VulkanEngine::RenderSystem* getRenderSystem() const { return m_renderSystem.get(); }
    UIManager* getUIManager() const { return m_uiManager.get(); }
    Camera* getCamera() const { return m_camera.get(); }

    // ========== Frame stats ==========
    float getDeltaTime() const { return m_deltaTime; }
    float getFPS() const { return m_fps; }
    uint64_t getFrameCount() const { return m_frameCount; }

private:
    void initializeSubsystems();
    void createDefaultScene();
    void setupInputCallbacks();
    void shutdownSubsystems();
    void mainLoop();
    void updateFrameStats();
    void recreateSwapChain();

    // Configuration
    Config m_config;
    bool m_running = false;

    // Subsystems (in dependency order)
    std::unique_ptr<Window> m_window;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<VulkanSwapChain> m_swapChain;
    std::unique_ptr<FrameResources> m_frameResources;
    std::unique_ptr<Input> m_input;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<VulkanEngine::Scene> m_scene;
    std::unique_ptr<VulkanEngine::RenderSystem> m_renderSystem;
    std::unique_ptr<SceneRenderer> m_renderer;
    std::unique_ptr<ImGuiLayer> m_imguiLayer;
    std::unique_ptr<UIManager> m_uiManager;

    // Frame stats
    float m_deltaTime = 0.0f;
    float m_lastFrameTime = 0.0f;
    float m_totalTime = 0.0f;
    float m_fps = 0.0f;
    uint64_t m_frameCount = 0;
    float m_fpsUpdateTimer = 0.0f;
    int m_fpsFrameCount = 0;

    // Feature toggles
    bool m_showUI = true;
    bool m_enableGPUCulling = false;
    bool m_enableNanite = false;
};