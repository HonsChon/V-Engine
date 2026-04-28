#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <string>

class VulkanDevice;
class RHIDevice;
class RHICommandBuffer;

/**
 * RenderPassBase - 渲染通道基类
 * 
 * 所有渲染通道的抽象基类，定义统一接口。
 * 
 * 兼容层说明：
 * - 同时持有 VulkanDevice (旧) 和 RHIDevice* (新) 引用
 * - recordCommands 提供 VkCommandBuffer 和 RHICommandBuffer* 两套接口
 * - 各 Pass 逐步迁移到 RHI 接口后，旧接口将被删除
 */
class RenderPassBase {
public:
    // 旧构造函数（兼容现有代码）
    RenderPassBase(std::shared_ptr<VulkanDevice> device, uint32_t width, uint32_t height)
        : device(device), rhiDevice_(nullptr), width(width), height(height) {}

    // 新构造函数（使用 RHIDevice）
    RenderPassBase(RHIDevice* rhiDevice, uint32_t width, uint32_t height)
        : device(nullptr), rhiDevice_(rhiDevice), width(width), height(height) {}
    
    virtual ~RenderPassBase() = default;
    
    // 禁止拷贝
    RenderPassBase(const RenderPassBase&) = delete;
    RenderPassBase& operator=(const RenderPassBase&) = delete;
    
    /**
     * 录制渲染命令（旧接口 — VkCommandBuffer）
     * 已迁移的 Pass 可以不重写此方法
     */
    virtual void recordCommands(VkCommandBuffer cmd, uint32_t frameIndex) {
        (void)cmd;
        (void)frameIndex;
    }

    /**
     * 录制渲染命令（新接口 — RHICommandBuffer）
     * 正在迁移中的 Pass 应重写此方法
     */
    virtual void recordCommands(RHICommandBuffer* cmd, uint32_t frameIndex) {
        (void)cmd;
        (void)frameIndex;
        // 默认空实现，子类可以选择是否重写
    }
    
    /**
     * 重建资源（窗口大小改变时调用）
     */
    virtual void resize(uint32_t newWidth, uint32_t newHeight) {
        width = newWidth;
        height = newHeight;
    }
    
    // 获取器
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }
    const std::string& getName() const { return passName; }
    
    // 是否启用
    bool isEnabled() const { return enabled; }
    void setEnabled(bool enable) { enabled = enable; }

    // RHI 访问器
    RHIDevice* getRHIDevice() const { return rhiDevice_; }

protected:
    std::shared_ptr<VulkanDevice> device;      // 旧：Vulkan 设备
    RHIDevice* rhiDevice_ = nullptr;           // 新：RHI 设备
    uint32_t width;
    uint32_t height;
    std::string passName = "Unnamed Pass";
    bool enabled = true;
};