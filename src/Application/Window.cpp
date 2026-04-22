/**
 * @file Window.cpp
 * @brief 窗口管理模块实现
 */

#include "Window.h"
#include <stdexcept>
#include <iostream>

Window::Window(const Config& config) 
    : m_width(config.width), m_height(config.height) {
    
    // 初始化 GLFW（如果还没有初始化）
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // 配置窗口
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // 不使用 OpenGL
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

    // 创建窗口
    GLFWmonitor* monitor = config.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    m_window = glfwCreateWindow(
        static_cast<int>(config.width),
        static_cast<int>(config.height),
        config.title.c_str(),
        monitor,
        nullptr
    );

    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    // 设置用户指针以便在回调中访问 this
    glfwSetWindowUserPointer(m_window, this);

    // 设置回调
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
    glfwSetKeyCallback(m_window, keyCallbackStatic);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallbackStatic);
    glfwSetCursorPosCallback(m_window, cursorPosCallbackStatic);
    glfwSetScrollCallback(m_window, scrollCallbackStatic);
    glfwSetDropCallback(m_window, dropCallbackStatic);

    std::cout << "[Window] Created " << config.width << "x" << config.height 
              << " window: " << config.title << std::endl;
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
    std::cout << "[Window] Destroyed" << std::endl;
}

void Window::pollEvents() {
    glfwPollEvents();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::requestClose() {
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Window::waitEvents() {
    glfwWaitEvents();
}

VkSurfaceKHR Window::createSurface(VkInstance instance) {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, m_window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface");
    }
    return surface;
}

std::vector<const char*> Window::getRequiredInstanceExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    return std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
}

bool Window::isKeyPressed(int key) const {
    return glfwGetKey(m_window, key) == GLFW_PRESS;
}

bool Window::isMouseButtonPressed(int button) const {
    return glfwGetMouseButton(m_window, button) == GLFW_PRESS;
}

void Window::getCursorPos(double& x, double& y) const {
    glfwGetCursorPos(m_window, &x, &y);
}

void Window::setCursorMode(int mode) {
    glfwSetInputMode(m_window, GLFW_CURSOR, mode);
}

// ========== 静态回调实现 ==========

void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto* self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!self) return;

    self->m_width = static_cast<uint32_t>(width);
    self->m_height = static_cast<uint32_t>(height);
    self->m_framebufferResized = true;

    if (self->m_resizeCallback) {
        self->m_resizeCallback(self->m_width, self->m_height);
    }
}

void Window::keyCallbackStatic(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!self || !self->m_keyCallback) return;
    self->m_keyCallback(key, scancode, action, mods);
}

void Window::mouseButtonCallbackStatic(GLFWwindow* window, int button, int action, int mods) {
    auto* self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!self || !self->m_mouseButtonCallback) return;
    self->m_mouseButtonCallback(button, action, mods);
}

void Window::cursorPosCallbackStatic(GLFWwindow* window, double xpos, double ypos) {
    auto* self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!self || !self->m_cursorPosCallback) return;
    self->m_cursorPosCallback(xpos, ypos);
}

void Window::scrollCallbackStatic(GLFWwindow* window, double xoffset, double yoffset) {
    auto* self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!self || !self->m_scrollCallback) return;
    self->m_scrollCallback(xoffset, yoffset);
}

void Window::dropCallbackStatic(GLFWwindow* window, int count, const char** paths) {
    auto* self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!self || !self->m_dropCallback) return;
    self->m_dropCallback(count, paths);
}
