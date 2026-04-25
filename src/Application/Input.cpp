/**
 * @file Input.cpp
 * @brief 输入管理模块实现
 */

#include "Input.h"
#include "Window.h"
#include "Camera.h"

Input::Input(Window* window)
    : m_window(window)
{
    // 设置默认键位映射
    m_keyBindings[GLFW_KEY_W] = InputAction::MoveForward;
    m_keyBindings[GLFW_KEY_S] = InputAction::MoveBackward;
    m_keyBindings[GLFW_KEY_A] = InputAction::MoveLeft;
    m_keyBindings[GLFW_KEY_D] = InputAction::MoveRight;
    m_keyBindings[GLFW_KEY_E] = InputAction::MoveUp;
    m_keyBindings[GLFW_KEY_Q] = InputAction::MoveDown;
    m_keyBindings[GLFW_KEY_U] = InputAction::ToggleUI;
    m_keyBindings[GLFW_KEY_T] = InputAction::ToggleWaterScene;
    m_keyBindings[GLFW_KEY_G] = InputAction::ToggleGPUCulling;
    m_keyBindings[GLFW_KEY_N] = InputAction::ToggleNanite;
    m_keyBindings[GLFW_KEY_M] = InputAction::TestNaniteClustering;
    m_keyBindings[GLFW_KEY_C] = InputAction::ToggleClusterVisualization;
    m_keyBindings[GLFW_KEY_V] = InputAction::CycleClusterDebugMode;
    m_keyBindings[GLFW_KEY_ESCAPE] = InputAction::Exit;

    // 注册 Window 回调
    if (m_window) {
        m_window->setKeyCallback([this](int key, int scancode, int action, int mods) {
            this->onKey(key, scancode, action, mods);
        });

        m_window->setCursorPosCallback([this](double xpos, double ypos) {
            this->onCursorPos(xpos, ypos);
        });

        m_window->setScrollCallback([this](double xoffset, double yoffset) {
            this->onScroll(xoffset, yoffset);
        });

        m_window->setMouseButtonCallback([this](int button, int action, int mods) {
            this->onMouseButton(button, action, mods);
        });
    }
}

Input::~Input() {
    // 清理回调
    if (m_window) {
        m_window->setKeyCallback(nullptr);
        m_window->setCursorPosCallback(nullptr);
        m_window->setScrollCallback(nullptr);
        m_window->setMouseButtonCallback(nullptr);
    }
}

void Input::update(float deltaTime) {
    // 重置每帧的鼠标增量
    m_mouseDelta = glm::vec2(0.0f);
    
    // 处理连续按键（如移动等
    processKeyboard(deltaTime);
}

void Input::processKeyboard(float deltaTime) {
    if (!m_camera || !m_window) return;

    GLFWwindow* glfwWindow = m_window->getNativeHandle();

    // 检查是否应该处理相机移动（非UI 模式时）
    if (m_mouseMode == MouseMode::Normal) {
        // 在正常模式下，只有按住右键才能移动相机
        // 如果需要其他逻辑，可以在这里修改
    }

    // 相机移动
    if (glfwGetKey(glfwWindow, GLFW_KEY_W) == GLFW_PRESS) {
        m_camera->processKeyboard(FORWARD, deltaTime);
    }
    if (glfwGetKey(glfwWindow, GLFW_KEY_S) == GLFW_PRESS) {
        m_camera->processKeyboard(BACKWARD, deltaTime);
    }
    if (glfwGetKey(glfwWindow, GLFW_KEY_A) == GLFW_PRESS) {
        m_camera->processKeyboard(LEFT, deltaTime);
    }
    if (glfwGetKey(glfwWindow, GLFW_KEY_D) == GLFW_PRESS) {
        m_camera->processKeyboard(RIGHT, deltaTime);
    }
    if (glfwGetKey(glfwWindow, GLFW_KEY_E) == GLFW_PRESS) {
        m_camera->processKeyboard(UP, deltaTime);
    }
    if (glfwGetKey(glfwWindow, GLFW_KEY_Q) == GLFW_PRESS) {
        m_camera->processKeyboard(DOWN, deltaTime);
    }
}

void Input::setMouseMode(MouseMode mode) {
    if (m_mouseMode == mode) return;

    m_mouseMode = mode;

    if (!m_window) return;
    GLFWwindow* glfwWindow = m_window->getNativeHandle();

    switch (mode) {
        case MouseMode::Normal:
            glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            break;
        case MouseMode::Camera:
            glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            m_firstMouse = true; // 重置鼠标位置，避免跳动
            break;
        case MouseMode::Picking:
            glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            break;
    }
}

void Input::bindAction(InputAction action, ActionCallback callback) {
    m_actionCallbacks[action] = callback;
}

bool Input::isKeyPressed(int key) const {
    if (!m_window) return false;
    return glfwGetKey(m_window->getNativeHandle(), key) == GLFW_PRESS;
}

bool Input::isMouseButtonPressed(int button) const {
    if (!m_window) return false;
    return glfwGetMouseButton(m_window->getNativeHandle(), button) == GLFW_PRESS;
}

glm::vec2 Input::getMousePosition() const {
    if (!m_window) return glm::vec2(0.0f);
    
    double xpos, ypos;
    glfwGetCursorPos(m_window->getNativeHandle(), &xpos, &ypos);
    return glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));
}

// ========== 私有回调处理 ==========

void Input::onKey(int key, int scancode, int action, int mods) {
    // 处理单次按键动作
    if (action == GLFW_PRESS || action == GLFW_RELEASE) {
        processKeyAction(key, action, mods);
    }
}

void Input::processKeyAction(int key, int action, int mods) {
    // 只处理按下事件
    if (action != GLFW_PRESS) return;

    // 查找键位映射
    auto it = m_keyBindings.find(key);
    if (it != m_keyBindings.end()) {
        InputAction inputAction = it->second;
        
        // 触发回调
        auto callbackIt = m_actionCallbacks.find(inputAction);
        if (callbackIt != m_actionCallbacks.end() && callbackIt->second) {
            callbackIt->second();
        }
    }
}

void Input::onMouseButton(int button, int action, int mods) {
    if (!m_window) return;

    // 右键控制相机模式
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            setMouseMode(MouseMode::Camera);
        } else if (action == GLFW_RELEASE) {
            setMouseMode(MouseMode::Normal);
        }
    }

    // 左键处理拾取
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (m_mouseMode == MouseMode::Picking || m_mouseMode == MouseMode::Normal) {
            double xpos, ypos;
            glfwGetCursorPos(m_window->getNativeHandle(), &xpos, &ypos);
            
            if (m_pickingCallback) {
                m_pickingCallback(xpos, ypos);
            }
        }
    }
}

void Input::onCursorPos(double xpos, double ypos) {
    float x = static_cast<float>(xpos);
    float y = static_cast<float>(ypos);

    if (m_firstMouse) {
        m_lastMousePos.x = x;
        m_lastMousePos.y = y;
        m_firstMouse = false;
    }

    float xoffset = x - m_lastMousePos.x;
    float yoffset = m_lastMousePos.y - y; // Y 反转

    m_lastMousePos.x = x;
    m_lastMousePos.y = y;

    m_mouseDelta.x = xoffset;
    m_mouseDelta.y = yoffset;

    // 只在相机模式下处理鼠标移动
    if (m_mouseMode == MouseMode::Camera) {
        xoffset *= m_mouseSensitivity;
        yoffset *= m_mouseSensitivity;

        // 更新相机
        if (m_camera) {
            m_camera->processMouseMovement(xoffset, yoffset);
        }

        // 触发回调
        if (m_mouseMoveCallback) {
            m_mouseMoveCallback(xoffset, yoffset);
        }
    }
}

void Input::onScroll(double xoffset, double yoffset) {
    // 更新相机 FOV
    if (m_camera) {
        m_camera->processMouseScroll(static_cast<float>(yoffset));
    }

    // 触发回调
    if (m_scrollCallback) {
        m_scrollCallback(static_cast<float>(yoffset));
    }
}
