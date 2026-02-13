
#include "ImGuiLayer.h"
#include "VulkanDevice.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <stdexcept>
#include <iostream>

ImGuiLayer::ImGuiLayer() {
}

ImGuiLayer::ImGuiLayer(GLFWwindow* window,
                       VkInstance instance,
                       VkPhysicalDevice physicalDevice,
                       VkDevice logicalDevice,
                       uint32_t queueFamily,
                       VkQueue queue,
                       VkRenderPass renderPass,
                       uint32_t imageCount)
    : logicalDevice_(logicalDevice)
{
    if (initialized) {
        std::cout << "ImGuiLayer already initialized" << std::endl;
        return;
    }

    // 创建 ImGui 专用的描述符池
    createDescriptorPoolDirect();

    // 初始化 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    
    if (dockingEnabled) {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }

    setupStyle();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForVulkan(window, true);

    // 初始化 Vulkan 后端 (适配 2025/09+ 版本的新 API)
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = logicalDevice;
    initInfo.QueueFamily = queueFamily;
    initInfo.Queue = queue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = imguiPool;
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = nullptr;
    
    // 新 API: RenderPass, Subpass, MSAASamples 移到 PipelineInfoMain 中
    initInfo.PipelineInfoMain.RenderPass = renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    
    // 多视口配置
    initInfo.PipelineInfoForViewports.Subpass = 0;
    initInfo.PipelineInfoForViewports.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);

    initialized = true;
    std::cout << "ImGui initialized successfully (direct constructor)" << std::endl;
}

ImGuiLayer::~ImGuiLayer() {
    cleanup();
}

void ImGuiLayer::init(GLFWwindow* window,
                      std::shared_ptr<VulkanDevice> deviceIn,
                      VkRenderPass renderPass,
                      uint32_t imageCount) {
    if (initialized) {
        std::cout << "ImGuiLayer already initialized" << std::endl;
        return;
    }

    device = deviceIn;

    // 创建 ImGui 专用的描述符池
    createDescriptorPool();

    // 初始化 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // 启用键盘导航
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // 启用手柄导航
    
    if (dockingEnabled) {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // 启用 Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // 启用多视口
    }

    // 设置样式
    setupStyle();

    // 如果启用了多视口，调整样式
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // 初始化 GLFW 后端
    ImGui_ImplGlfw_InitForVulkan(window, true);

    // 初始化 Vulkan 后端 (适配 2025/09+ 版本的新 API)
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = VK_API_VERSION_1_0;  // 或你的 Vulkan API 版本
    initInfo.Instance = device->getInstance();
    initInfo.PhysicalDevice = device->getPhysicalDevice();
    initInfo.Device = device->getDevice();
    initInfo.QueueFamily = device->getGraphicsQueueFamilyIndex();
    initInfo.Queue = device->getGraphicsQueue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = imguiPool;
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = nullptr;
    
    // 新 API: RenderPass, Subpass, MSAASamples 移到 PipelineInfoMain 中
    initInfo.PipelineInfoMain.RenderPass = renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    
    // 多视口配置
    initInfo.PipelineInfoForViewports.Subpass = 0;
    initInfo.PipelineInfoForViewports.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);

    // 注意：新版本的 ImGui Vulkan 后端会自动创建字体纹理
    // 不再需要手动调用 ImGui_ImplVulkan_CreateFontsTexture()

    initialized = true;
    std::cout << "ImGui initialized successfully" << std::endl;
}

void ImGuiLayer::cleanup() {
    if (!initialized) {
        return;
    }

    // 等待设备空闲
    VkDevice deviceToUse = VK_NULL_HANDLE;
    if (device) {
        deviceToUse = device->getDevice();
    } else if (logicalDevice_ != VK_NULL_HANDLE) {
        deviceToUse = logicalDevice_;
    }
    
    if (deviceToUse != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(deviceToUse);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (imguiPool != VK_NULL_HANDLE && deviceToUse != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(deviceToUse, imguiPool, nullptr);
        imguiPool = VK_NULL_HANDLE;
    }

    initialized = false;
    std::cout << "ImGui cleaned up" << std::endl;
}

void ImGuiLayer::beginFrame() {
    if (!initialized) return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 如果启用了 Docking，创建全屏 DockSpace
    if (dockingEnabled) {
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;
        
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        
        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
        windowFlags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        windowFlags |= ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        
        ImGui::Begin("DockSpace", nullptr, windowFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspaceId = ImGui::GetID("VEngineDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

        ImGui::End();
    }

    // 显示 Demo 窗口（调试用）
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }
}

void ImGuiLayer::endFrame(VkCommandBuffer commandBuffer) {
    if (!initialized) return;

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    // 处理多视口
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void ImGuiLayer::onResize(uint32_t width, uint32_t height) {
    // ImGui 会自动处理窗口大小改变
    // 这里可以添加额外的逻辑
}

bool ImGuiLayer::wantCaptureMouse() const {
    if (!initialized) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::wantCaptureKeyboard() const {
    if (!initialized) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

void ImGuiLayer::createDescriptorPool() {
    // 创建一个足够大的描述符池供 ImGui 使用
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &imguiPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool!");
    }
}

void ImGuiLayer::createDescriptorPoolDirect() {
    // 使用 logicalDevice_ 直接创建描述符池
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(logicalDevice_, &poolInfo, nullptr, &imguiPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool (direct)!");
    }
}

void ImGuiLayer::setupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // V Engine 主题 - 深色现代风格
    ImVec4* colors = style.Colors;
    
    // 背景色
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.94f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.94f);
    
    // 边框
    colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.35f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    
    // 帧背景
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.22f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.30f, 0.35f, 0.40f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.40f, 0.40f, 0.45f, 0.67f);
    
    // 标题栏
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    
    // 菜单栏
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    
    // 滚动条
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    
    // 复选框和单选框
    colors[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    
    // 滑块
    colors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.80f, 1.00f, 1.00f);
    
    // 按钮 - 蓝色主题
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.40f, 0.70f, 0.60f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.50f, 0.80f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.40f, 0.90f, 1.00f);
    
    // 头部（如 TreeNode, CollapsingHeader）
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.40f, 0.70f, 0.31f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.50f, 0.80f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.50f, 0.90f, 1.00f);
    
    // 分隔符
    colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.35f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.40f, 0.55f, 0.80f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.55f, 0.90f, 1.00f);
    
    // 调整大小手柄
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.50f, 0.80f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.30f, 0.50f, 0.80f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.30f, 0.50f, 0.90f, 0.95f);
    
    // Tab
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.18f, 0.86f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.50f, 0.80f, 0.80f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.40f, 0.70f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.12f, 0.12f, 0.15f, 0.97f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.18f, 0.30f, 0.50f, 1.00f);
    
    // Docking
    colors[ImGuiCol_DockingPreview] = ImVec4(0.30f, 0.50f, 0.80f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    
    // 文本
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    
    // 样式设置
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(5.0f, 3.0f);
    style.CellPadding = ImVec2(4.0f, 2.0f);
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;
    
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
}
