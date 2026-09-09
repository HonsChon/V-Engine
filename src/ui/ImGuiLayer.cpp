#include "ImGuiLayer.h"
#include "RHIDevice.h"
#include "RHISwapChain.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#if defined(_WIN32)
#include "imgui_impl_dx12.h"
#include "RHI/DX12/DX12TypeConversions.h"
#endif

#include <stdexcept>
#include <iostream>

#if defined(_WIN32)
// Trivial bump allocator over the ImGui-owned shader-visible SRV heap.
// ImGui's DX12 backend currently allocates few descriptors (font + textures)
// and never frees them while the heap is alive, so Free is a no-op.
struct DX12SrvAllocator {
    ID3D12DescriptorHeap* heap = nullptr;
    UINT increment = 0;
    UINT capacity = 0;
    UINT cursor = 0;
};
#endif

struct ImGuiLayer::Impl {
    RHIDevice*   rhiDevice    = nullptr;
    RHISwapChain* rhiSwapChain = nullptr;
    GLFWwindow*  window       = nullptr;

    RHIBackend backend = RHIBackend::Vulkan;

    bool initialized     = false;
    bool dockingEnabled  = true;
    bool showDemoWindow  = false;

    // ---- Vulkan backend state ----
    VkDevice         vkDevice         = VK_NULL_HANDLE;
    VkDescriptorPool vkDescriptorPool = VK_NULL_HANDLE;
    VkRenderPass     vkRenderPass     = VK_NULL_HANDLE;

#if defined(_WIN32)
    // ---- DX12 backend state ----
    ID3D12Device*         dxDevice = nullptr;   // raw; owned by the RHI device
    ID3D12DescriptorHeap* dxSrvHeap = nullptr;  // owned here (released in cleanup)
    DX12SrvAllocator      dxSrvAlloc;
#endif
};

ImGuiLayer::ImGuiLayer() : m_impl(std::make_unique<Impl>()) {
}

ImGuiLayer::ImGuiLayer(GLFWwindow* window,
                       RHIDevice* rhiDevice,
                       RHISwapChain* rhiSwapChain)
    : ImGuiLayer()
{
    init(window, rhiDevice, rhiSwapChain);
}

ImGuiLayer::~ImGuiLayer() {
    cleanup();
}

// ============================================================
// 初始化
// ============================================================

void ImGuiLayer::init(GLFWwindow* window,
                      RHIDevice* rhiDevice,
                      RHISwapChain* rhiSwapChain) {
    Impl& d = *m_impl;
    if (d.initialized) {
        std::cout << "ImGuiLayer already initialized" << std::endl;
        return;
    }

    if (!rhiDevice || !rhiSwapChain) {
        throw std::runtime_error("ImGuiLayer::init: rhiDevice/rhiSwapChain is null");
    }

    d.window = window;
    d.rhiDevice = rhiDevice;
    d.rhiSwapChain = rhiSwapChain;
    d.backend = rhiDevice->getBackend();

    // 创建 ImGui 上下文（与后端无关的公共部分）
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    if (d.dockingEnabled) {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }

    setupStyle();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    switch (d.backend) {
    case RHIBackend::Vulkan:
        initVulkanBackend();
        break;
    case RHIBackend::DX12:
#if defined(_WIN32)
        initDX12Backend();
#else
        throw std::runtime_error("ImGuiLayer: DX12 backend requires Windows");
#endif
        break;
    default:
        throw std::runtime_error("ImGuiLayer: unknown RHI backend");
    }

    d.initialized = true;
    std::cout << "ImGui initialized successfully (via RHI, backend="
              << (d.backend == RHIBackend::Vulkan ? "Vulkan" : "DX12") << ")" << std::endl;
}

void ImGuiLayer::initVulkanBackend() {
    Impl& d = *m_impl;

    // Get native handles from RHI
    VkInstance instance = static_cast<VkInstance>(d.rhiDevice->getNativeInstance());
    VkPhysicalDevice physicalDevice = static_cast<VkPhysicalDevice>(d.rhiDevice->getNativePhysicalDevice());
    d.vkDevice = static_cast<VkDevice>(d.rhiDevice->getNativeDevice());
    uint32_t queueFamily = d.rhiDevice->getGraphicsQueueFamilyIndex();
    VkQueue queue = static_cast<VkQueue>(d.rhiDevice->getNativeGraphicsQueue());
    d.vkRenderPass = static_cast<VkRenderPass>(d.rhiSwapChain->getNativeRenderPass());
    uint32_t imageCount = d.rhiSwapChain->getImageCount();

    // 创建 ImGui 专用的描述符池
    {
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

        if (vkCreateDescriptorPool(d.vkDevice, &poolInfo, nullptr, &d.vkDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ImGui descriptor pool!");
        }
    }

    ImGui_ImplGlfw_InitForVulkan(d.window, true);

    // 初始化 Vulkan 后端
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = d.vkDevice;
    initInfo.QueueFamily = queueFamily;
    initInfo.Queue = queue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = d.vkDescriptorPool;
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = nullptr;

    initInfo.PipelineInfoMain.RenderPass = d.vkRenderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    // 多视口配置
    initInfo.PipelineInfoForViewports.Subpass = 0;
    initInfo.PipelineInfoForViewports.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);
}

#if defined(_WIN32)
namespace {
void imguiDX12AllocSrv(ImGui_ImplDX12_InitInfo* info,
                       D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                       D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
    auto* alloc = static_cast<DX12SrvAllocator*>(info->UserData);
    if (alloc->cursor + 1 > alloc->capacity) {
        throw std::runtime_error("ImGuiLayer: DX12 SRV descriptor heap exhausted");
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = alloc->heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = alloc->heap->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(alloc->cursor) * alloc->increment;
    gpu.ptr += static_cast<UINT64>(alloc->cursor) * alloc->increment;
    *outCpu = cpu;
    *outGpu = gpu;
    ++alloc->cursor;
}

void imguiDX12FreeSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {
    // no-op: bump allocator, heap lives for the app lifetime
}
} // namespace

void ImGuiLayer::initDX12Backend() {
    Impl& d = *m_impl;

    ID3D12Device* device = static_cast<ID3D12Device*>(d.rhiDevice->getNativeDevice());
    ID3D12CommandQueue* queue = static_cast<ID3D12CommandQueue*>(d.rhiDevice->getNativeGraphicsQueue());
    if (!device || !queue) {
        throw std::runtime_error("ImGuiLayer: DX12 device/queue native handles missing");
    }
    d.dxDevice = device;

    // Dedicated shader-visible CBV/SRV/UAV heap for ImGui resources
    const UINT kSrvHeapSize = 1024;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = kSrvHeapSize;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&d.dxSrvHeap)))) {
        throw std::runtime_error("ImGuiLayer: failed to create DX12 SRV descriptor heap");
    }

    d.dxSrvAlloc.heap = d.dxSrvHeap;
    d.dxSrvAlloc.increment =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    d.dxSrvAlloc.capacity = kSrvHeapSize;
    d.dxSrvAlloc.cursor = 0;

    ImGui_ImplGlfw_InitForOther(d.window, true);

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = device;
    initInfo.CommandQueue = queue;
    initInfo.NumFramesInFlight = static_cast<int>(d.rhiSwapChain->getImageCount());
    initInfo.RTVFormat = DX12TypeConversions::toDXGIFormat(d.rhiSwapChain->getFormat());
    initInfo.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    initInfo.UserData = &d.dxSrvAlloc;
    initInfo.SrvDescriptorHeap = d.dxSrvHeap;
    initInfo.SrvDescriptorAllocFn = imguiDX12AllocSrv;
    initInfo.SrvDescriptorFreeFn = imguiDX12FreeSrv;

    if (!ImGui_ImplDX12_Init(&initInfo)) {
        throw std::runtime_error("ImGuiLayer: ImGui_ImplDX12_Init failed");
    }
}
#endif // _WIN32

void ImGuiLayer::cleanup() {
    Impl& d = *m_impl;
    if (!d.initialized) {
        return;
    }

    if (d.backend == RHIBackend::Vulkan) {
        if (d.vkDevice != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(d.vkDevice);
        }
        ImGui_ImplVulkan_Shutdown();

        if (d.vkDescriptorPool != VK_NULL_HANDLE && d.vkDevice != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(d.vkDevice, d.vkDescriptorPool, nullptr);
            d.vkDescriptorPool = VK_NULL_HANDLE;
        }
        d.vkDevice = VK_NULL_HANDLE;
    }
#if defined(_WIN32)
    else if (d.backend == RHIBackend::DX12) {
        // Drain any pending upload work before ImGui releases its objects.
        if (d.rhiDevice) d.rhiDevice->waitIdle();
        ImGui_ImplDX12_Shutdown();

        if (d.dxSrvHeap) {
            d.dxSrvHeap->Release();
            d.dxSrvHeap = nullptr;
        }
        d.dxDevice = nullptr;
        d.dxSrvAlloc = DX12SrvAllocator{};
    }
#endif

    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    d.initialized = false;
    std::cout << "ImGui cleaned up" << std::endl;
}

// ============================================================
// 帧循环
// ============================================================

void ImGuiLayer::beginFrame() {
    Impl& d = *m_impl;
    if (!d.initialized) return;

    if (d.backend == RHIBackend::Vulkan) {
        ImGui_ImplVulkan_NewFrame();
    }
#if defined(_WIN32)
    else if (d.backend == RHIBackend::DX12) {
        ImGui_ImplDX12_NewFrame();
    }
#endif
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 如果启用了Docking，创建全屏DockSpace
    if (d.dockingEnabled) {
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
    if (d.showDemoWindow) {
        ImGui::ShowDemoWindow(&d.showDemoWindow);
    }
}

void ImGuiLayer::endFrame(void* commandBuffer) {
    Impl& d = *m_impl;
    if (!d.initialized) return;

    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();

    if (d.backend == RHIBackend::Vulkan) {
        ImGui_ImplVulkan_RenderDrawData(drawData, static_cast<VkCommandBuffer>(commandBuffer));
    }
#if defined(_WIN32)
    else if (d.backend == RHIBackend::DX12) {
        ImGui_ImplDX12_RenderDrawData(drawData, static_cast<ID3D12GraphicsCommandList*>(commandBuffer));
    }
#endif

    // 处理多视口
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void ImGuiLayer::onResize(uint32_t width, uint32_t height) {
    Impl& d = *m_impl;
    if (!d.initialized) return;

    // 当窗口大小为 0 时（最小化），不做任何操作
    if (width == 0 || height == 0) return;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
}

// ============================================================
// 查询
// ============================================================

bool ImGuiLayer::isInitialized() const {
    return m_impl->initialized;
}

void ImGuiLayer::setDockingEnabled(bool enabled) {
    m_impl->dockingEnabled = enabled;
}

void ImGuiLayer::setShowDemoWindow(bool show) {
    m_impl->showDemoWindow = show;
}

bool ImGuiLayer::wantCaptureMouse() const {
    if (!m_impl->initialized) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::wantCaptureKeyboard() const {
    if (!m_impl->initialized) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

// ============================================================
// 样式
// ============================================================

void ImGuiLayer::setupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    // V Engine 主题 - 深色现代风格
    ImVec4* colors = style.Colors;

    // 背景色
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.94f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.14f, 0.94f);

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

    // 分隔线
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
