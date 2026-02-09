#include "VulkanRenderer.h"
#include "Mesh.h"
#include <iostream>
#include <stdexcept>
#include <array>
#include <chrono>
#include <thread>

// 静态回调函数实现
void VulkanRenderer::mouseCallback(GLFWwindow* window, double xposIn, double yposIn) {
    auto renderer = reinterpret_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window));
    if (!renderer || !renderer->mouseEnabled) return;
    
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);
    
    if (renderer->firstMouse) {
        renderer->lastMouseX = xpos;
        renderer->lastMouseY = ypos;
        renderer->firstMouse = false;
    }
    
    float xoffset = xpos - renderer->lastMouseX;
    float yoffset = renderer->lastMouseY - ypos; // Y轴反转
    
    renderer->lastMouseX = xpos;
    renderer->lastMouseY = ypos;
    
    renderer->handleMouseMovement(xoffset, yoffset);
}

void VulkanRenderer::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto renderer = reinterpret_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window));
    if (renderer) {
        renderer->handleMouseScroll(static_cast<float>(yoffset));
    }
}

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
    
    // 设置鼠标和滚轮回调
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);
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
    cubeMesh->createSphere(128);  // 创建球体，32段细分
    
    std::cout << "Created cube with " << cubeMesh->getVertices().size() << " vertices and " 
              << cubeMesh->getIndices().size() << " indices" << std::endl;
    
    // 创建顶点和索引缓冲区
    createVertexBuffer();
    createIndexBuffer();
    
    // 创建 Uniform Buffers
    createUniformBuffers();
    
    // 创建描述符池和描述符集
    createDescriptorPool();
    createDescriptorSets();
    
    // 创建命令缓冲
    createCommandBuffers();
    
    // 创建相机 - 位于 (0, 0, 5) 看向原点
    camera = std::make_unique<Camera>(glm::vec3(0.0f, 0.0f, 5.0f));
    
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
    std::cout << "Controls:" << std::endl;
    std::cout << "  WASD - Move camera" << std::endl;
    std::cout << "  Space/Shift - Move up/down" << std::endl;
    std::cout << "  Right mouse button - Enable mouse look" << std::endl;
    std::cout << "  Mouse scroll - Zoom in/out" << std::endl;
    std::cout << "  ESC - Exit" << std::endl;
    
    int frameCount = 0;
    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastFrameTime = startTime;
    
    while (!glfwWindowShouldClose(window)) {
        // 计算帧时间（deltaTime）
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;
        
        glfwPollEvents();
        
        // 检查窗口是否被意外关闭
        if (glfwWindowShouldClose(window)) {
            std::cout << "Window close requested after " << frameCount << " frames" << std::endl;
            break;
        }
        
        // 处理键盘输入
        processKeyboardInput(deltaTime);
        
        // 处理鼠标模式切换（右键按下时启用鼠标控制）
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            if (!mouseEnabled) {
                mouseEnabled = true;
                firstMouse = true;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
        } else {
            if (mouseEnabled) {
                mouseEnabled = false;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }
        
        drawFrame(); // 现在启用真正的渲染！
        
        frameCount++;
        if (frameCount % 60 == 0) {
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

void VulkanRenderer::processKeyboardInput(float deltaTime) {
    if (!camera) return;
    
    // WASD 移动
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera->processKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera->processKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera->processKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera->processKeyboard(RIGHT, deltaTime);
    
    // 空格/Shift 上下移动
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        camera->processKeyboard(UP, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        camera->processKeyboard(DOWN, deltaTime);
}

void VulkanRenderer::handleMouseMovement(float xoffset, float yoffset) {
    if (camera) {
        camera->processMouseMovement(xoffset, yoffset);
    }
}

void VulkanRenderer::handleMouseScroll(float yoffset) {
    if (camera) {
        camera->processMouseScroll(yoffset);
    }
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
    
    // 在重置 fence 之前更新 uniform buffer
    updateUniformBuffer(currentFrame);

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
    UniformBufferObject ubo{};
    
    // 计算时间
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
    
    // Model 矩阵 - 球体保持静止（单位矩阵）
    ubo.model = glm::mat4(1.0f);
    
    // View 矩阵 - 从相机获取
    if (camera) {
        ubo.view = camera->getViewMatrix();
    } else {
        ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), 
                               glm::vec3(0.0f, 0.0f, 0.0f), 
                               glm::vec3(0.0f, 1.0f, 0.0f));
    }
    
    // Projection 矩阵
    float fov = camera ? glm::radians(camera->getZoom()) : glm::radians(45.0f);
    float aspect = swapChain->getExtent().width / (float)swapChain->getExtent().height;
    ubo.proj = glm::perspective(fov, aspect, 0.1f, 100.0f);
    
    // Vulkan 的 Y 轴是反的（与 OpenGL 相反）
    ubo.proj[1][1] *= -1;
    
    // Normal 矩阵（用于法线变换）
    ubo.normalMatrix = glm::transpose(glm::inverse(ubo.model));
    
    // 相机位置（用于光照计算）- 使用 vec4，w 分量不使用
    glm::vec3 camPos = camera ? camera->getPosition() : glm::vec3(0.0f, 0.0f, 5.0f);
    ubo.viewPos = glm::vec4(camPos, 1.0f);
    
    // 光源绕球体旋转
    float lightRadius = 5.0f;  // 灯光距离球体中心的距离
    float lightSpeed = 0.5f;   // 旋转速度（每秒弧度数）
    float lightAngle = time * lightSpeed;
    
    // 灯光在 XZ 平面上绕 Y 轴旋转，同时有一定高度
    glm::vec3 lightPosition = glm::vec3(
        lightRadius * cos(lightAngle),   // X 坐标
        3.0f,                             // Y 坐标（高度）
        lightRadius * sin(lightAngle)    // Z 坐标
    );
    ubo.lightPos = glm::vec4(lightPosition, 1.0f);
    
    ubo.lightColor = glm::vec4(300.0f, 300.0f, 300.0f, 1.0f); // 高强度点光源
    
    // 复制到映射的内存
    memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
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
    
    // 绑定描述符集 (包含 MVP 矩阵和相机位置)
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                           pbrPipeline->getPipelineLayout(), 0, 1, 
                           &descriptorSets[currentFrame], 0, nullptr);
    
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

void VulkanRenderer::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    
    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        uniformBuffers[i] = std::make_unique<VulkanBuffer>(
            std::shared_ptr<VulkanDevice>(device.get(), [](VulkanDevice*){}),
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        
        // 持久映射
        vkMapMemory(device->getDevice(), uniformBuffers[i]->getMemory(), 0, bufferSize, 0, &uniformBuffersMapped[i]);
    }
    
    std::cout << "Created " << MAX_FRAMES_IN_FLIGHT << " uniform buffers" << std::endl;
}

void VulkanRenderer::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    
    if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
    
    std::cout << "Created descriptor pool" << std::endl;
}

void VulkanRenderer::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, pbrPipeline->getDescriptorSetLayout());
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();
    
    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }
    
    // 更新描述符集
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i]->getBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);
        
        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        
        vkUpdateDescriptorSets(device->getDevice(), 1, &descriptorWrite, 0, nullptr);
    }
    
    std::cout << "Created and updated " << MAX_FRAMES_IN_FLIGHT << " descriptor sets" << std::endl;
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