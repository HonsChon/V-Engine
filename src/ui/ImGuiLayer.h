#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>

// Forward declarations — RHI only (no VulkanDevice)
class RHIDevice;
class RHISwapChain;

/**
 * ImGuiLayer - ImGui Vulkan/GLFW 后端封装
 * 
 * 负责 ImGui 的初始化、资源管理和渲染集成。
 * 使用 ImGui 的docking 分支支持窗口停靠功能。
 * 
 * 注意：ImGui Vulkan 后端需要原始 Vk handle，
 * 通过 RHIDevice::getNative*() 获取。此类不属于 Pass 层。
 */
class ImGuiLayer {
public:
    ImGuiLayer();
    
    /**
     * 带参数的构造函数 - 通过 RHI 获取原始 Vulkan handle
     * @param window GLFW 窗口句柄
     * @param rhiDevice RHI 设备（提供 native handle 访问）
     * @param rhiSwapChain RHI 交换链（提供 renderPass 和 imageCount）
     */
    ImGuiLayer(GLFWwindow* window,
               RHIDevice* rhiDevice,
               RHISwapChain* rhiSwapChain);
    
    ~ImGuiLayer();

    // 禁止拷贝
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    /**
     * 初始化 ImGui（通过 RHI 设备）
     * @param window GLFW 窗口句柄
     * @param rhiDevice RHI 设备
     * @param rhiSwapChain RHI 交换链
     */
    void init(GLFWwindow* window, 
              RHIDevice* rhiDevice,
              RHISwapChain* rhiSwapChain);

    /**
     * 清理 ImGui 资源
     */
    void cleanup();

    /**
     * 开始新的ImGui 帧
     * 必须在每帧渲染UI 之前调用
     */
    void beginFrame();

    /**
     * 结束 ImGui 帧并录制渲染命令
     * @param commandBuffer 当前帧的命令缓冲区 (native VkCommandBuffer cast to void*)
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
    bool isInitialized() const { return initialized; }

    /**
     * 设置是否启用 Docking
     */
    void setDockingEnabled(bool enabled) { dockingEnabled = enabled; }

    /**
     * 设置是否显示 Demo 窗口（调试用）
     */
    void setShowDemoWindow(bool show) { showDemoWindow = show; }

    /**
     * 获取是否正在捕获鼠标（用于判断是否应该传递输入给场景）
     */
    bool wantCaptureMouse() const;

    /**
     * 获取是否正在捕获键盘
     */
    bool wantCaptureKeyboard() const;

private:
    void createDescriptorPool();
    void setupStyle();

    // Cached native Vulkan device handle (from RHIDevice)
    VkDevice logicalDevice_ = VK_NULL_HANDLE;
    
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;

    bool initialized = false;
    bool dockingEnabled = true;
    bool showDemoWindow = false;
};