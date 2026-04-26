/**
 * @file Window.h
 * @brief 窗口管理模块 - 封装 GLFW 窗口操作
 * 
 * 职责：
 * 1. 创建和销毁窗口
 * 2. 处理窗口事件（大小改变、关闭等）
 * 3. 提供 Vulkan Surface 创建所需的接口
 */

#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <string>
#include <functional>

struct WindowConfig {
    std::string title = "Vulkan Application";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool resizable = true;
    bool fullscreen = false;
};

class Window {
public:
    using Config = WindowConfig;

    // 回调函数类型
    using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;
    using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
    using CursorPosCallback = std::function<void(double xpos, double ypos)>;
    using ScrollCallback = std::function<void(double xoffset, double yoffset)>;
    using DropCallback = std::function<void(int count, const char** paths)>;

    Window(const Config& config = Config{});
    ~Window();

    // 禁止拷贝
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // ========== 核心功能 ==========
    
    /**
     * @brief 处理窗口事件
     */
    void pollEvents();

    /**
     * @brief 检查窗口是否应该关闭
     */
    bool shouldClose() const;

    /**
     * @brief 请求关闭窗口
     */
    void requestClose();

    /**
     * @brief 等待事件（用于最小化时节省 CPU）
     */
    void waitEvents();

    // ========== 属性访问 ==========

    GLFWwindow* getNativeHandle() const { return m_window; }
    uint32_t getWidth() const { return m_width; }
    uint32_t getHeight() const { return m_height; }
    float getAspectRatio() const { return static_cast<float>(m_width) / static_cast<float>(m_height); }
    bool isMinimized() const { return m_width == 0 || m_height == 0; }
    bool wasResized() const { return m_framebufferResized; }
    void resetResizedFlag() { m_framebufferResized = false; }

    // ========== Vulkan 相关 ==========

    /**
     * @brief 创建 Vulkan Surface
     */
    VkSurfaceKHR createSurface(VkInstance instance);

    /**
     * @brief 获取所需的 Vulkan 实例扩展
     */
    static std::vector<const char*> getRequiredInstanceExtensions();

    // ========== 输入状态查询 ==========

    bool isKeyPressed(int key) const;
    bool isMouseButtonPressed(int button) const;
    void getCursorPos(double& x, double& y) const;
    void setCursorMode(int mode); // GLFW_CURSOR_NORMAL, GLFW_CURSOR_DISABLED, etc.

    // ========== 回调设置 ==========

    void setResizeCallback(ResizeCallback callback) { m_resizeCallback = callback; }
    void setKeyCallback(KeyCallback callback) { m_keyCallback = callback; }
    void setMouseButtonCallback(MouseButtonCallback callback) { m_mouseButtonCallback = callback; }
    void setCursorPosCallback(CursorPosCallback callback) { m_cursorPosCallback = callback; }
    void setScrollCallback(ScrollCallback callback) { m_scrollCallback = callback; }
    void setDropCallback(DropCallback callback) { m_dropCallback = callback; }

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void keyCallbackStatic(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallbackStatic(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallbackStatic(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallbackStatic(GLFWwindow* window, double xoffset, double yoffset);
    static void dropCallbackStatic(GLFWwindow* window, int count, const char** paths);

    GLFWwindow* m_window = nullptr;
    uint32_t m_width;
    uint32_t m_height;
    bool m_framebufferResized = false;

    // 回调
    ResizeCallback m_resizeCallback;
    KeyCallback m_keyCallback;
    MouseButtonCallback m_mouseButtonCallback;
    CursorPosCallback m_cursorPosCallback;
    ScrollCallback m_scrollCallback;
    DropCallback m_dropCallback;
};
