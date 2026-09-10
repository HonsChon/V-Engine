#pragma once

#include "RHITypes.h"
#include <memory>

struct GLFWwindow;

// Forward declarations — RHI only (no VulkanDevice)
class RHIDevice;
class RHISwapChain;

/**
 * ImGuiLayer - ImGui 后端封装 (Vulkan / DX12 按 RHIDevice::getBackend() 选择)
 * 
 * 负责 ImGui 的初始化、资源管理和渲染集成。
 * 使用 ImGui 的 docking 分支支持窗口停靠功能。
 * 
 * 注意：两种后端都需要原始 native handle，
 * 通过 RHIDevice::getNative*() / RHICommandBuffer::getNativeHandle() 获取。
 * 此类不属于 Pass 层。
 */
class ImGuiLayer {
public:
    ImGuiLayer();

    /**
     * 带参数的构造函数 - 通过 RHI 获取原始 native handle
     * @param window GLFW 窗口句柄
     * @param rhiDevice RHI 设备（提供 native handle 访问与后端类型）
     * @param rhiSwapChain RHI 交换链（提供 renderPass / imageCount / 格式）
     */
    ImGuiLayer(GLFWwindow* window,
               RHIDevice* rhiDevice,
               RHISwapChain* rhiSwapChain);

    ~ImGuiLayer();

    // 禁止拷贝
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    /**
     * 初始化 ImGui（通过 RHI 设备，按后端选择 imgui_impl_vulkan / imgui_impl_dx12）
     */
    void init(GLFWwindow* window,
              RHIDevice* rhiDevice,
              RHISwapChain* rhiSwapChain);

    /**
     * 清理 ImGui 资源
     */
    void cleanup();

    /**
     * 开始新的 ImGui 帧
     * 必须在每帧渲染 UI 之前调用
     */
    void beginFrame();

    /**
     * 结束 ImGui 帧并录制渲染命令
     * @param commandBuffer 当前帧的命令缓冲区 (native handle cast to void*,
     *                      来自 RHICommandBuffer::getNativeHandle())
     */
    void endFrame(void* commandBuffer);

    /**
     * 处理窗口大小改变（交换链重建时调用）
     * @param width 新宽度
     * @param height 新高度
     */
    void onResize(uint32_t width, uint32_t height);

    /**
     * 检查是否已初始化
     */
    bool isInitialized() const;

    /**
     * 设置是否启用 Docking
     */
    void setDockingEnabled(bool enabled);

    /**
     * 设置是否显示 Demo 窗口（调试用）
     */
    void setShowDemoWindow(bool show);

    /**
     * 获取是否正在捕获鼠标（用于判断是否应该传递输入给场景）
     */
    bool wantCaptureMouse() const;

    /**
     * 获取是否正在捕获键盘
     */
    bool wantCaptureKeyboard() const;

private:
    void setupStyle();
    void initVulkanBackend();
#if defined(_WIN32)
    void initDX12Backend();
#endif

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
