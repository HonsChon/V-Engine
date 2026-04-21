/**
 * @file Input.h
 * @brief 输入管理模块 - 处理键盘、鼠标输�?
 * 
 * 职责�?
 * 1. 管理输入状态（按键、鼠标位置等�?
 * 2. 提供高级输入抽象（如相机控制�?
 * 3. 处理输入事件分发
 */

#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <functional>
#include <unordered_map>
#include <memory>

// 前向声明
class Window;
class Camera;

namespace VulkanEngine {
    class Scene;
}

/**
 * @brief 输入动作类型
 */
enum class InputAction {
    // 相机控制
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    
    // 功能切换
    ToggleUI,
    ToggleWaterScene,
    ToggleGPUCulling,
    ToggleNanite,
    TestNaniteClustering,
    ToggleClusterVisualization,
    CycleClusterDebugMode,
    
    // 系统
    Exit
};

/**
 * @brief 鼠标模式
 */
enum class MouseMode {
    Normal,     // 正常模式（可以点�?UI�?
    Camera,     // 相机控制模式（鼠标被捕获�?
    Picking     // 拾取模式
};

class Input {
public:
    // 输入事件回调
    using ActionCallback = std::function<void()>;
    using MouseMoveCallback = std::function<void(float xoffset, float yoffset)>;
    using ScrollCallback = std::function<void(float yoffset)>;
    using PickingCallback = std::function<void(double x, double y)>;

    Input(Window* window);
    ~Input();

    // 禁止拷贝
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    /**
     * @brief 每帧更新输入状�?
     * @param deltaTime 帧时�?
     */
    void update(float deltaTime);

    /**
     * @brief 处理连续按键（如 WASD 移动�?
     * @param deltaTime 帧时�?
     */
    void processKeyboard(float deltaTime);

    // ========== 相机控制 ==========

    /**
     * @brief 设置要控制的相机
     */
    void setCamera(Camera* camera) { m_camera = camera; }

    // ========== 鼠标模式 ==========

    MouseMode getMouseMode() const { return m_mouseMode; }
    void setMouseMode(MouseMode mode);

    // ========== 回调绑定 ==========

    /**
     * @brief 绑定输入动作回调
     */
    void bindAction(InputAction action, ActionCallback callback);

    /**
     * @brief 绑定鼠标移动回调
     */
    void setMouseMoveCallback(MouseMoveCallback callback) { m_mouseMoveCallback = callback; }

    /**
     * @brief 绑定滚轮回调
     */
    void setScrollCallback(ScrollCallback callback) { m_scrollCallback = callback; }

    /**
     * @brief 绑定拾取回调
     */
    void setPickingCallback(PickingCallback callback) { m_pickingCallback = callback; }

    // ========== 状态查�?==========

    bool isKeyPressed(int key) const;
    bool isMouseButtonPressed(int button) const;
    glm::vec2 getMousePosition() const;
    glm::vec2 getMouseDelta() const { return m_mouseDelta; }

    // ========== 配置 ==========

    void setMouseSensitivity(float sensitivity) { m_mouseSensitivity = sensitivity; }
    float getMouseSensitivity() const { return m_mouseSensitivity; }

private:
    // 来自 Window 的回�?
    void onKey(int key, int scancode, int action, int mods);
    void onMouseButton(int button, int action, int mods);
    void onCursorPos(double xpos, double ypos);
    void onScroll(double xoffset, double yoffset);

    // 处理单次按键动作
    void processKeyAction(int key, int action, int mods);

    Window* m_window = nullptr;
    Camera* m_camera = nullptr;

    // 鼠标状�?
    MouseMode m_mouseMode = MouseMode::Normal;
    glm::vec2 m_lastMousePos = glm::vec2(0.0f);
    glm::vec2 m_mouseDelta = glm::vec2(0.0f);
    bool m_firstMouse = true;
    float m_mouseSensitivity = 0.1f;

    // 回调
    std::unordered_map<InputAction, ActionCallback> m_actionCallbacks;
    MouseMoveCallback m_mouseMoveCallback;
    ScrollCallback m_scrollCallback;
    PickingCallback m_pickingCallback;

    // 默认键位映射
    std::unordered_map<int, InputAction> m_keyBindings;
};
