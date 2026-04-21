/**
 * @file FrameResources.cpp
 * @brief 帧资源管理实�?
 */

#include "FrameResources.h"
#include "VulkanDevice.h"
#include <stdexcept>
#include <iostream>

FrameResources::FrameResources(VulkanDevice* device, uint32_t framesInFlight)
    : m_device(device)
    , m_framesInFlight(framesInFlight)
{
    m_frames.resize(m_framesInFlight);
    
    createCommandBuffers();
    createSyncObjects();
    
    std::cout << "[FrameResources] Initialized with " << m_framesInFlight << " frames in flight\n";
}

FrameResources::~FrameResources() {
    cleanup();
}

void FrameResources::createCommandBuffers() {
    if (!m_device) return;

    // 创建命令池（如果需要独立的池）
    // 这里使用设备的默认命令池
    VkCommandPool commandPool = m_device->getCommandPool();

    // 分配命令缓冲�?
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = m_framesInFlight;

    std::vector<VkCommandBuffer> commandBuffers(m_framesInFlight);
    
    if (vkAllocateCommandBuffers(m_device->getDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers!");
    }

    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        m_frames[i].commandBuffer = commandBuffers[i];
    }
}

void FrameResources::createSyncObjects() {
    if (!m_device) return;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // 初始�?signaled 状�?

    VkDevice device = m_device->getDevice();

    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_frames[i].imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_frames[i].renderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &m_frames[i].inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects for a frame!");
        }
    }
}

uint32_t FrameResources::beginFrame() {
    if (!m_device) return 0;

    VkDevice device = m_device->getDevice();
    FrameData& frame = m_frames[m_currentFrame];

    // 等待当前帧的 Fence
    vkWaitForFences(device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &frame.inFlightFence);

    // 重置命令缓冲�?
    vkResetCommandBuffer(frame.commandBuffer, 0);

    // 开始录制命�?
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(frame.commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer!");
    }

    // 更新帧计�?
    frame.frameNumber = m_totalFrameCount;

    return m_currentFrame;
}

void FrameResources::endFrame() {
    if (!m_device) return;

    FrameData& frame = m_frames[m_currentFrame];

    // 结束命令缓冲区录�?
    if (vkEndCommandBuffer(frame.commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer!");
    }

    // 移动到下一�?
    m_currentFrame = (m_currentFrame + 1) % m_framesInFlight;
    m_totalFrameCount++;
}

void FrameResources::submit(
    const std::vector<VkSemaphore>& waitSemaphores,
    const std::vector<VkPipelineStageFlags>& waitStages,
    const std::vector<VkSemaphore>& signalSemaphores)
{
    if (!m_device) return;

    FrameData& frame = m_frames[m_currentFrame];

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
    submitInfo.pWaitSemaphores = waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    if (vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, frame.inFlightFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }
}

void FrameResources::waitAllFrames() {
    if (!m_device) return;

    VkDevice device = m_device->getDevice();
    
    // 等待所有帧�?Fence
    std::vector<VkFence> fences;
    fences.reserve(m_framesInFlight);
    
    for (const auto& frame : m_frames) {
        fences.push_back(frame.inFlightFence);
    }
    
    vkWaitForFences(device, static_cast<uint32_t>(fences.size()), 
                    fences.data(), VK_TRUE, UINT64_MAX);
}

void FrameResources::cleanup() {
    if (!m_device) return;

    VkDevice device = m_device->getDevice();

    // 等待所有操作完�?
    vkDeviceWaitIdle(device);

    // 销毁同步对�?
    for (auto& frame : m_frames) {
        if (frame.imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, frame.imageAvailableSemaphore, nullptr);
            frame.imageAvailableSemaphore = VK_NULL_HANDLE;
        }
        if (frame.renderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, frame.renderFinishedSemaphore, nullptr);
            frame.renderFinishedSemaphore = VK_NULL_HANDLE;
        }
        if (frame.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(device, frame.inFlightFence, nullptr);
            frame.inFlightFence = VK_NULL_HANDLE;
        }
    }

    // 命令缓冲区会随命令池销毁而自动释�?
    // 如果创建了独立的命令池，在这里销毁它

    std::cout << "[FrameResources] Cleaned up\n";
}
