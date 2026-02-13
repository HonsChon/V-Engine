/**
 * @file VulkanSwapChain.cpp
 * @brief Vulkan 交换链管理类实现
 * 
 * 交换链是 Vulkan 渲染管线与窗口系统之间的桥梁，负责管理用于屏幕显示的图像队列。
 * 本文件实现了交换链的创建、图像视图、渲染通道、深度缓冲和帧缓冲的完整生命周期管理。
 */

#include "VulkanSwapChain.h"
#include <iostream>
#include <algorithm>
#include <array>

// ═══════════════════════════════════════════════════════════════════════════════
// 构造函数与析构函数
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief 构造函数 - 初始化交换链及其所有关联资源
 * @param device Vulkan 设备的智能指针
 * @param width  窗口宽度（像素）
 * @param height 窗口高度（像素）
 * 
 * 按顺序创建：交换链 → 图像视图 → 渲染通道 → 深度资源 → 帧缓冲
 * 这个顺序很重要，因为后面的资源依赖前面创建的资源
 */
VulkanSwapChain::VulkanSwapChain(std::shared_ptr<VulkanDevice> device, int width, int height) 
    : device(device), width(width), height(height) {
    createSwapChain();       // 1. 创建交换链，获取用于显示的图像
    createImageViews();      // 2. 为每个交换链图像创建视图
    createRenderPass();      // 3. 定义渲染过程中附件的使用方式
    createDepthResources();  // 4. 创建深度缓冲（用于正确的3D遮挡）
    createFramebuffers();    // 5. 将图像视图和深度视图组合成帧缓冲
}

/**
 * @brief 析构函数 - 释放所有 Vulkan 资源
 */
VulkanSwapChain::~VulkanSwapChain() {
    cleanup();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 交换链重建
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief 重建交换链
 * @param width  新的窗口宽度
 * @param height 新的窗口高度
 * 
 * 当窗口大小改变时调用此函数。需要重建交换链及其依赖的所有资源。
 * 注意：RenderPass 也需要重建，因为 cleanup() 会销毁它。
 */
void VulkanSwapChain::recreate(int width, int height) {
    this->width = width;
    this->height = height;
    
    cleanup();               // 先清理旧资源（包括 RenderPass）
    createSwapChain();       // 重建交换链（新分辨率）
    createImageViews();      // 重建图像视图
    createRenderPass();      // 重建渲染通道（cleanup 销毁了它）
    createDepthResources();  // 重建深度缓冲（需要匹配新分辨率）
    createFramebuffers();    // 重建帧缓冲
}

// ═══════════════════════════════════════════════════════════════════════════════
// 交换链创建
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief 创建 Vulkan 交换链
 * 
 * 交换链管理用于屏幕显示的图像队列，实现双缓冲/三缓冲机制，避免画面撕裂。
 * 创建过程：查询硬件能力 → 选择最佳配置 → 创建交换链 → 获取图像句柄
 */
void VulkanSwapChain::createSwapChain() {
    // 查询物理设备支持的交换链能力（格式、呈现模式、分辨率范围等）
    SwapChainSupportDetails swapChainSupport = device->querySwapChainSupport(device->getPhysicalDevice());

    // 从支持的选项中选择最佳配置
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);  // 颜色格式
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);   // 呈现模式
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);                    // 分辨率

    // 确定图像数量：请求比最小值多1张，实现三缓冲（如果支持）
    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    // 确保不超过最大限制（maxImageCount = 0 表示无限制）
    if (swapChainSupport.capabilities.maxImageCount > 0 && 
        imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    // 填充交换链创建信息
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = device->getSurface();           // 绑定的窗口表面
    createInfo.minImageCount = imageCount;               // 图像数量
    createInfo.imageFormat = surfaceFormat.format;       // 像素格式（如 B8G8R8A8）
    createInfo.imageColorSpace = surfaceFormat.colorSpace;  // 颜色空间（如 SRGB）
    createInfo.imageExtent = extent;                     // 图像分辨率
    createInfo.imageArrayLayers = 1;                     // 图像层数（非VR为1）
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;  // 用作渲染目标

    // 处理队列族共享模式
    QueueFamilyIndices indices = device->findQueueFamilies(device->getPhysicalDevice());
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
        // 图形队列和呈现队列不同：使用并发模式，允许多队列访问
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        // 图形队列和呈现队列相同：使用独占模式，性能最佳
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;  // 保持当前变换
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;  // 不透明，不与其他窗口混合
    createInfo.presentMode = presentMode;                // 呈现模式
    createInfo.clipped = VK_TRUE;                        // 裁剪被遮挡的像素
    createInfo.oldSwapchain = VK_NULL_HANDLE;            // 旧交换链（首次创建为空）

    // 创建交换链
    if (vkCreateSwapchainKHR(device->getDevice(), &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    // 获取交换链图像句柄（两阶段调用）
    vkGetSwapchainImagesKHR(device->getDevice(), swapChain, &imageCount, nullptr);  // 获取数量
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device->getDevice(), swapChain, &imageCount, swapChainImages.data());  // 获取句柄

    // 保存格式和分辨率供后续使用
    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 图像视图创建
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief 为每个交换链图像创建图像视图
 * 
 * VkImage 只是原始图像数据，不能直接使用。
 * VkImageView 定义了如何访问图像（2D/3D、格式、颜色通道映射、mipmap范围等）。
 * 类比：VkImage 是一块内存，VkImageView 是"如何解读这块内存"的说明书。
 */
void VulkanSwapChain::createImageViews() {
    // 为每个交换链图像创建一个对应的视图
    swapChainImageViews.resize(swapChainImages.size());

    for (size_t i = 0; i < swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];           // 关联的图像
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;     // 2D 纹理视图
        createInfo.format = swapChainImageFormat;        // 像素格式
        
        // 颜色通道映射（IDENTITY = 保持原样，不做重映射）
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        
        // 子资源范围：指定访问图像的哪个部分
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;  // 颜色数据
        createInfo.subresourceRange.baseMipLevel = 0;    // 第一个 mipmap 级别
        createInfo.subresourceRange.levelCount = 1;      // mipmap 级别数量
        createInfo.subresourceRange.baseArrayLayer = 0;  // 第一个数组层
        createInfo.subresourceRange.layerCount = 1;      // 数组层数量

        if (vkCreateImageView(device->getDevice(), &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image views!");
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 渲染通道创建
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief 创建渲染通道（Render Pass）
 * 
 * 渲染通道定义了渲染操作中使用的附件（颜色、深度等）及其在渲染前后的状态转换。
 * 它不执行实际渲染，而是描述"渲染会用到什么、会产生什么"的蓝图。
 * 
 * 本函数创建一个包含颜色附件和深度附件的渲染通道：
 * - 颜色附件：存储渲染结果，最终呈现到屏幕
 * - 深度附件：存储深度值，用于正确的3D遮挡关系
 */
void VulkanSwapChain::createRenderPass() {
    // ─────────────────────────────────────────────────────────────────────────
    // 颜色附件描述
    // ─────────────────────────────────────────────────────────────────────────
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;       // 像素格式（与交换链一致）
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;     // 采样数（1 = 不使用多重采样）
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // 渲染前：清除为指定颜色
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // 渲染后：保存结果
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;   // 模板数据：不关心
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;         // 渲染前布局：未定义（内容可丢弃）
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;     // 渲染后布局：准备呈现

    // ─────────────────────────────────────────────────────────────────────────
    // 深度附件描述
    // ─────────────────────────────────────────────────────────────────────────
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = device->findDepthFormat();  // 深度格式（如 D32_SFLOAT）
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;    // 渲染前：清除为最远深度
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // 渲染后：不需要保存
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // ─────────────────────────────────────────────────────────────────────────
    // 附件引用（在子通道中使用）
    // ─────────────────────────────────────────────────────────────────────────
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;   // 引用附件数组中的第 0 个（颜色附件）
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;  // 子通道中的布局

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;   // 引用附件数组中的第 1 个（深度附件）
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // ─────────────────────────────────────────────────────────────────────────
    // 子通道描述
    // ─────────────────────────────────────────────────────────────────────────
    // 一个渲染通道可以包含多个子通道（用于多通道渲染），这里只用一个
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;  // 绑定到图形管线
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;      // 颜色输出目标
    subpass.pDepthStencilAttachment = &depthAttachmentRef; // 深度测试目标

    // ─────────────────────────────────────────────────────────────────────────
    // 子通道依赖
    // ─────────────────────────────────────────────────────────────────────────
    // 定义子通道之间的执行顺序和内存依赖
    // VK_SUBPASS_EXTERNAL 表示渲染通道之外的操作（如上一帧的呈现）
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;  // 依赖来源：外部操作
    dependency.dstSubpass = 0;                     // 依赖目标：子通道 0
    // 源阶段：颜色输出 + 早期深度测试 必须完成
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    // 目标阶段：颜色输出 + 早期深度测试 才能开始
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    // 目标访问：写入颜色和深度附件
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | 
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // ─────────────────────────────────────────────────────────────────────────
    // 创建渲染通道
    // ─────────────────────────────────────────────────────────────────────────
    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device->getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 深度资源创建
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief 创建深度缓冲资源
 * 
 * 深度缓冲用于存储每个像素的深度值（到相机的距离），
 * 实现正确的 3D 遮挡关系（近的物体遮挡远的物体）。
 * 
 * 创建内容：
 * - VkImage: 存储深度数据的图像
 * - VkDeviceMemory: 图像使用的 GPU 内存
 * - VkImageView: 访问深度图像的视图
 */
void VulkanSwapChain::createDepthResources() {
    // 查找硬件支持的深度格式（如 D32_SFLOAT、D32_SFLOAT_S8_UINT、D24_UNORM_S8_UINT）
    VkFormat depthFormat = device->findDepthFormat();

    // 创建深度图像和分配内存
    // - TILING_OPTIMAL: GPU 优化布局，性能最佳
    // - USAGE_DEPTH_STENCIL_ATTACHMENT_BIT: 用作深度/模板附件
    // - DEVICE_LOCAL_BIT: 存储在 GPU 专用内存中
    device->createImage(swapChainExtent.width, swapChainExtent.height, depthFormat,
                       VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);

    // 创建深度图像视图
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;  // 深度数据（非颜色）
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device->getDevice(), &viewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture image view!");
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 帧缓冲创建
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief 为每个交换链图像创建帧缓冲
 * 
 * 帧缓冲（Framebuffer）将渲染通道与具体的图像视图绑定在一起。
 * 渲染通道定义了"需要什么类型的附件"，帧缓冲则提供"实际的图像视图"。
 * 
 * 每个帧缓冲包含：
 * - 一个颜色附件（来自交换链图像）
 * - 一个深度附件（共享同一个深度缓冲）
 */
void VulkanSwapChain::createFramebuffers() {
    // 为每个交换链图像创建一个帧缓冲
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        // 附件数组：颜色 + 深度
        // 顺序必须与渲染通道中定义的附件顺序一致
        std::array<VkImageView, 2> attachments = {
            swapChainImageViews[i],  // 颜色附件：每帧不同
            depthImageView           // 深度附件：所有帧共享
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;         // 关联的渲染通道
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapChainExtent.width;   // 帧缓冲宽度
        framebufferInfo.height = swapChainExtent.height; // 帧缓冲高度
        framebufferInfo.layers = 1;                      // 层数（非数组纹理为1）

        if (vkCreateFramebuffer(device->getDevice(), &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 资源清理
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief 清理所有交换链相关的 Vulkan 资源
 * 
 * 销毁顺序是创建顺序的逆序（依赖关系）：
 * 帧缓冲 → 深度资源 → 渲染通道 → 图像视图 → 交换链
 * 
 * 注意：交换链图像（swapChainImages）由交换链自动管理，不需要手动销毁
 */
void VulkanSwapChain::cleanup() {
    // 销毁深度资源
    vkDestroyImageView(device->getDevice(), depthImageView, nullptr);
    vkDestroyImage(device->getDevice(), depthImage, nullptr);
    vkFreeMemory(device->getDevice(), depthImageMemory, nullptr);

    // 销毁所有帧缓冲
    for (auto framebuffer : swapChainFramebuffers) {
        vkDestroyFramebuffer(device->getDevice(), framebuffer, nullptr);
    }

    // 销毁渲染通道
    vkDestroyRenderPass(device->getDevice(), renderPass, nullptr);

    // 销毁所有图像视图
    for (auto imageView : swapChainImageViews) {
        vkDestroyImageView(device->getDevice(), imageView, nullptr);
    }

    // 销毁交换链（会自动释放 swapChainImages）
    vkDestroySwapchainKHR(device->getDevice(), swapChain, nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 辅助函数：选择最佳配置
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief 选择最佳的表面格式（颜色格式 + 颜色空间）
 * @param availableFormats 硬件支持的格式列表
 * @return 选中的表面格式
 * 
 * 优先选择：B8G8R8A8_SRGB + SRGB_NONLINEAR
 * - B8G8R8A8: 每通道8位，总共32位颜色
 * - SRGB: 标准 RGB 颜色空间，颜色更准确，符合人眼感知
 */
VkSurfaceFormatKHR VulkanSwapChain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && 
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;  // 找到理想格式
        }
    }
    // 未找到理想格式，返回第一个可用格式
    return availableFormats[0];
}

/**
 * @brief 选择最佳的呈现模式
 * @param availablePresentModes 硬件支持的呈现模式列表
 * @return 选中的呈现模式
 * 
 * 呈现模式说明：
 * - IMMEDIATE: 立即提交，可能导致画面撕裂
 * - FIFO: 先进先出（垂直同步），无撕裂但可能有延迟
 * - FIFO_RELAXED: 类似 FIFO，但队列为空时立即显示
 * - MAILBOX: 三缓冲，队列满时替换旧帧，低延迟且无撕裂（最佳选择）
 */
VkPresentModeKHR VulkanSwapChain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;  // 优先选择 Mailbox
        }
    }
    // FIFO 是 Vulkan 规范强制支持的，作为后备选项
    return VK_PRESENT_MODE_FIFO_KHR;
}

/**
 * @brief 选择交换链分辨率（交换范围）
 * @param capabilities 表面能力信息
 * @return 选中的分辨率
 * 
 * 大多数情况下直接使用 currentExtent（窗口实际大小）。
 * 但某些窗口系统会设置 currentExtent.width = UINT32_MAX，
 * 表示可以自定义分辨率，此时使用窗口的实际像素大小，并限制在允许范围内。
 */
VkExtent2D VulkanSwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    // 如果 currentExtent 有效，直接使用
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        // 使用窗口大小，但限制在硬件支持的范围内
        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        // 限制宽度在 [minWidth, maxWidth] 范围内
        actualExtent.width = std::clamp(actualExtent.width, 
                                       capabilities.minImageExtent.width, 
                                       capabilities.maxImageExtent.width);
        // 限制高度在 [minHeight, maxHeight] 范围内
        actualExtent.height = std::clamp(actualExtent.height, 
                                        capabilities.minImageExtent.height, 
                                        capabilities.maxImageExtent.height);

        return actualExtent;
    }
}