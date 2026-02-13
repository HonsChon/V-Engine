
#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>

class VulkanDevice;

/**
 * ImGuiLayer - ImGui Vulkan/GLFW 后端封装
 * 
 * 负责 ImGui 的初始化、资源管理和渲染集成。
 * 使用 ImGui 的 docking 分支支持窗口停靠功能。
 */
class ImGuiLayer {
public:
    ImGuiLayer();
    
    /**
     * 带参数的构造函数 - 直接初始化 ImGui
     * @param window GLFW 窗口句柄
     * @param instance Vulkan 实例
     * @param physicalDevice Vulkan 物理设备
     * @param logicalDevice Vulkan 逻辑设备
     * @param queueFamily 图形队列族索引
     * @param queue 图形队列
     * @param renderPass 目标 RenderPass
     * @param imageCount SwapChain 图像数量
     */
    ImGuiLayer(GLFWwindow* window,
               VkInstance instance,
               VkPhysicalDevice physicalDevice,
               VkDevice logicalDevice,
               uint32_t queueFamily,
               VkQueue queue,
               VkRenderPass renderPass,
               uint32_t imageCount);
    
    ~ImGuiLayer();

    // 禁止拷贝
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    /**
     * 初始化 ImGui（使用 VulkanDevice 对象）
     * @param window GLFW 窗口句柄
     * @param device Vulkan 设备
     * @param renderPass 目标 RenderPass（通常是 SwapChain 的 RenderPass）
     * @param imageCount SwapChain 图像数量
     */
    void init(GLFWwindow* window, 
              std::shared_ptr<VulkanDevice> device,
              VkRenderPass renderPass,
              uint32_t imageCount);

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
     * @param commandBuffer 当前帧的命令缓冲区
     */
    void endFrame(VkCommandBuffer commandBuffer);

    /**
     * 处理窗口大小改变
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
    void createDescriptorPoolDirect();  // 使用原始 Vulkan 对象
    void setupStyle();

    // VulkanDevice 对象（用于 init() 方法）
    std::shared_ptr<VulkanDevice> device;
    
    // 原始 Vulkan 对象（用于带参数的构造函数）
    VkDevice logicalDevice_ = VK_NULL_HANDLE;
    
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;

    bool initialized = false;
    bool dockingEnabled = true;
    bool showDemoWindow = false;
};
