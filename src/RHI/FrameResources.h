/**
 * @file FrameResources.h
 * @brief 帧资源管理- 管理多帧渲染所需的同步原语和命令缓冲区
 * 
 * 职责：
 * 1. 管理每帧的Command Buffer
 * 2. 管理同步原语（Semaphore、Fence：
 * 3. 实现多帧并行（Flight Frames：
 * 4. 提供资源获取接口
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <memory>
#include <functional>

class VulkanDevice;

/**
 * @brief 单帧资源数据
 */
struct FrameData {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    
    // 同步原语
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
    
    // 帧计数器（用于调试分析：
    uint64_t frameNumber = 0;
};

/**
 * @brief 帧资源管理器
 * 
 * 实现 Triple Buffering 的资源管理：
 * - 一帧在 GPU 上执行
 * - 一帧在 CPU 上录制命令
 * - 一帧等待使用
 */
class FrameResources {
public:
    // 默认使用 2 帧缓冲（双缓冲）
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    FrameResources(VulkanDevice* device, uint32_t framesInFlight = MAX_FRAMES_IN_FLIGHT);
    ~FrameResources();

    // 禁止拷贝
    FrameResources(const FrameResources&) = delete;
    FrameResources& operator=(const FrameResources&) = delete;

    /**
     * @brief 开始新帧
     * 
     * 等待当前帧的 Fence，重置命令缓冲区
     * @return 当前帧索引
     */
    uint32_t beginFrame();

    /**
     * @brief 结束当前帧
     * 
     * 提交命令缓冲区
     */
    void endFrame();

    /**
     * @brief 提交命令缓冲区
     * 
     * @param waitSemaphores 等待的信号量
     * @param waitStages 等待的管线阶段
     * @param signalSemaphores 要触发的信号量
     */
    void submit(
        const std::vector<VkSemaphore>& waitSemaphores,
        const std::vector<VkPipelineStageFlags>& waitStages,
        const std::vector<VkSemaphore>& signalSemaphores
    );

    /**
     * @brief 等待所有帧完成
     */
    void waitAllFrames();

    // ========== Getter ==========

    uint32_t getCurrentFrameIndex() const { return m_currentFrame; }
    uint64_t getTotalFrameCount() const { return m_totalFrameCount; }

    const FrameData& getCurrentFrameData() const { 
        return m_frames[m_currentFrame]; 
    }

    FrameData& getCurrentFrameData() { 
        return m_frames[m_currentFrame]; 
    }

    VkCommandBuffer getCurrentCommandBuffer() const {
        return m_frames[m_currentFrame].commandBuffer;
    }

    VkSemaphore getImageAvailableSemaphore() const {
        return m_frames[m_currentFrame].imageAvailableSemaphore;
    }

    VkSemaphore getRenderFinishedSemaphore() const {
        return m_frames[m_currentFrame].renderFinishedSemaphore;
    }

    VkFence getInFlightFence() const {
        return m_frames[m_currentFrame].inFlightFence;
    }

    uint32_t getFramesInFlight() const { return m_framesInFlight; }

private:
    void createCommandBuffers();
    void createSyncObjects();
    void cleanup();

    VulkanDevice* m_device = nullptr;
    
    std::vector<FrameData> m_frames;
    uint32_t m_framesInFlight = MAX_FRAMES_IN_FLIGHT;
    uint32_t m_currentFrame = 0;
    uint64_t m_totalFrameCount = 0;
    
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
};
