#include "VulkanRenderer.h"
#include "Mesh.h"
#include <iostream>
#include <stdexcept>
#include <array>
#include <chrono>
#include <thread>

VulkanRenderer::VulkanRenderer() : window(nullptr), currentFrame(0), framebufferResized(false) {
    initWindow();
    initVulkan();
    createSyncObjects();
}

VulkanRenderer::~VulkanRenderer() {
    cleanup();
}

void VulkanRenderer::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan PBR Renderer", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void VulkanRenderer::initVulkan() {
    std::cout << "Initializing Vulkan components..." << std::endl;
    
    // 创建Vulkan设备 - 现在是真正的实现！
    device = std::make_unique<VulkanDevice>(window);
    
    // 创建交换链 - 现在也是真正的实现！
    swapChain = std::make_unique<VulkanSwapChain>(std::shared_ptr<VulkanDevice>(device.get(), [](VulkanDevice*){}), WIDTH, HEIGHT);
    
    // 创建PBR管线
    pbrPipeline = std::make_unique<VulkanPipeline>(std::shared_ptr<VulkanDevice>(device.get(), [](VulkanDevice*){}), 
                                                  std::shared_ptr<VulkanSwapChain>(swapChain.get(), [](VulkanSwapChain*){}));
    
    // 创建几何体
    cubeMesh = std::make_unique<Mesh>();
    cubeMesh->createCube();  // 创建立方体
    
    std::cout << "Created cube with " << cubeMesh->getVertices().size() << " vertices and " 
              << cubeMesh->getIndices().size() << " indices" << std::endl;
    
    // 创建顶点和索引缓冲区
    createVertexBuffer();
    createIndexBuffer();
    
    // 创建命令缓冲
    createCommandBuffers();
    
    std::cout << "Vulkan initialization complete!" << std::endl;
}

void VulkanRenderer::createSyncObjects() {
    std::cout << "Creating synchronization objects..." << std::endl;
    
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device->getDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device->getDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device->getDevice(), &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

void VulkanRenderer::run() {
    mainLoop();
}

void VulkanRenderer::createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = device->getCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t) commandBuffers.size();

    if (vkAllocateCommandBuffers(device->getDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void VulkanRenderer::mainLoop() {
    std::cout << "Starting main loop..." << std::endl;
    
    int frameCount = 0;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        // 检查窗口是否被意外关闭
        if (glfwWindowShouldClose(window)) {
            std::cout << "Window close requested after " << frameCount << " frames" << std::endl;
            break;
        }
        
        drawFrame(); // 现在启用真正的渲染！
        
        frameCount++;
        if (frameCount % 60 == 0) {
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
            std::cout << "Rendered " << frameCount << " frames in " << duration << "ms" << std::endl;
        }
        
        // 临时：按ESC键退出
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            std::cout << "ESC pressed, exiting..." << std::endl;
            glfwSetWindowShouldClose(window, true);
            break;
        }
        
        // 在macOS上添加短暂延迟以防止过度消耗CPU
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    
    // 等待设备闲置
    vkDeviceWaitIdle(device->getDevice());
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    std::cout << "Exiting main loop after " << frameCount << " frames in " << totalDuration << "ms total" << std::endl;
    
    // 保持窗口打开几秒钟，让用户看到结果
    std::cout << "Keeping window open for 3 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
}

void VulkanRenderer::drawFrame() {
    vkWaitForFences(device->getDevice(), 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device->getDevice(), swapChain->getSwapChain(), UINT64_MAX,
        imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    vkResetFences(device->getDevice(), 1, &inFlightFences[currentFrame]);

    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(device->getGraphicsQueue(), 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapChain->getSwapChain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(device->getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::updateUniformBuffer(uint32_t currentImage) {
    // 更新uniform buffer - 临时空实现
}

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = swapChain->getRenderPass();
    renderPassInfo.framebuffer = swapChain->getFramebuffers()[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChain->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.1f, 0.2f, 0.4f, 1.0f}}; // 深蓝色背景
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // 绑定PBR图形管线
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pbrPipeline->getPipeline());
    
    // 设置视口和裁剪矩形
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChain->getExtent().width);
    viewport.height = static_cast<float>(swapChain->getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChain->getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    // 绑定顶点缓冲区
    if (vertexBuffer) {
        VkBuffer vertexBuffers[] = {vertexBuffer->getBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    }
    
    // 绑定索引缓冲区
    if (indexBuffer) {
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
    }
    
    // 渲染立方体
    if (indexBuffer && vertexBuffer) {
        const auto& indices = cubeMesh->getIndices();
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    } else {
        // 如果缓冲区创建失败，回退到硬编码三角形
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void VulkanRenderer::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto app = reinterpret_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}

void VulkanRenderer::recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device->getDevice());

    swapChain->recreate(width, height);
}

void VulkanRenderer::createVertexBuffer() {
    const auto& vertices = cubeMesh->getVertices();
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    // 为了简单测试，直接使用主机可见内存创建顶点缓冲区
    vertexBuffer = std::make_unique<VulkanBuffer>(
        std::shared_ptr<VulkanDevice>(device.get(), [](VulkanDevice*){}),
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    // 将顶点数据复制到缓冲区
    vertexBuffer->copyFrom(vertices.data(), bufferSize);
}

void VulkanRenderer::createIndexBuffer() {
    const auto& indices = cubeMesh->getIndices();
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    // 为了简单测试，直接使用主机可见内存创建索引缓冲区
    indexBuffer = std::make_unique<VulkanBuffer>(
        std::shared_ptr<VulkanDevice>(device.get(), [](VulkanDevice*){}),
        bufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    // 将索引数据复制到缓冲区
    indexBuffer->copyFrom(indices.data(), bufferSize);
}

void VulkanRenderer::cleanup() {
    // 清理同步对象
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device->getDevice(), renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device->getDevice(), imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device->getDevice(), inFlightFences[i], nullptr);
    }

    if (window) {
        glfwDestroyWindow(window);
        glfwTerminate();
    }
}
