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
#include <filesystem>

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

    // 相机覆盖（--cam x,y,z,yaw,pitch）：自动化验证用固定初始位姿
    bool camOverride = false;
    float camPos[3] = {0.0f, 0.0f, 5.0f};
    float camYaw = -90.0f;    // -90 = 面向 -Z（默认相机朝向）
    float camPitch = 0.0f;

    // 离线软光追（--raytrace）：主循环前渲染一帧并保存，随后退出
    bool raytraceOffline = false;
    std::string rtOutputPath = "raytrace_output.png";
    int rtSamples = 64;           // 每像素样本数
    int rtBounces = 5;            // 最大弹射次数
    int rtWidth = 1280;           // 输出分辨率
    int rtHeight = 720;
    bool rtPathTracing = false;   // false=Whitted, true=路径追踪
    bool rtRestir = false;        // PT + ReSTIR 直接光（覆盖 rtPathTracing）
    int rtLights = 0;             // >0: 注入 N 个网格测试点光源（多光源场景）
    int rtFrames = 1;             // 渲染帧数（时间复用需多帧，结果取平均）
    bool rtShadowInTarget = true; // ReSTIR 可见性计入目标函数（离线精度优先）
    float rtExposure = 1.0f;      // 曝光（tone map 前线性乘）
    bool rtDenoise = true;        // 输出降噪（GBuffer 引导双边滤波，ReSTIR 模式）
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

    // 场景操作（main.cpp 启动参数 / UI 菜单共用）
    // 路径统一用 std::filesystem::path（Windows 原生宽字符，无编码歧义）；
    // NFD 对话框经 u8path()、argv/内部字符串经 path(str) 转入
    void importModelFile(const std::string& filePath);   // 导入 .obj/.gltf/.glb
    bool openSceneFromFile(const std::filesystem::path& filePath); // 打开 .vscene（成功返回 true）
    bool openSceneFromFile(const std::string& filePath); // argv/内部字符串（ANSI 代码页）包装
    bool saveSceneToPath(const std::filesystem::path& filePath);   // 保存到指定路径（成功返回 true）

    // 离线软光追：CPU 渲染当前场景一帧并保存 PNG（返回是否成功）
    bool renderOfflineRaytrace();

    float getDeltaTime() const { return m_deltaTime; }
    float getFPS() const { return m_fps; }

private:
    void initializeSubsystems();
    void createDefaultScene();
    void setupInputCallbacks();
    void shutdownSubsystems();

    /// 注入 N 个网格排列点光源（--rt-lights，多光源/ReSTIR 测试用）
    void injectTestLights(int count);

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

    // Scene file & model import (File 菜单 / 拖拽 / 资源浏览器双击)
    void setupUICallbacks();           // 注入 UIManager 的场景动作回调
    void newScene();
    void openSceneDialog();            // NFD 选择文件
    void saveScene();                  // 无当前路径时弹另存为
    void saveSceneAs();                // NFD 选择保存位置
    void updateSceneTitle();           // 菜单栏显示 场景名*

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

    // 当前场景文件路径（空 = 未保存过；Ctrl+S 直接保存于此）
    std::filesystem::path m_currentScenePath;

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