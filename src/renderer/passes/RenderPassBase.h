#pragma once

#include <memory>
#include <string>
#include <cstdint>

class RHIDevice;
class RHICommandBuffer;

/**
 * RenderPassBase - 渲染通道基类
 * 
 * 所有渲染通道的抽象基类，定义统一接口。
 * 所有 Pass 只能持有 RHIDevice*，禁止直接依赖 VulkanRHI 具体类。
 */
class RenderPassBase {
public:
    RenderPassBase(RHIDevice* rhiDevice, uint32_t width, uint32_t height)
        : rhiDevice_(rhiDevice), width(width), height(height) {}
    
    virtual ~RenderPassBase() = default;
    
    // 禁止拷贝
    RenderPassBase(const RenderPassBase&) = delete;
    RenderPassBase& operator=(const RenderPassBase&) = delete;
    
    /**
     * 录制渲染命令（RHI 接口）
     */
    virtual void recordCommands(RHICommandBuffer* cmd, uint32_t frameIndex) {
        (void)cmd;
        (void)frameIndex;
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
    RHIDevice* rhiDevice_ = nullptr;
    uint32_t width;
    uint32_t height;
    std::string passName = "Unnamed Pass";
    bool enabled = true;
};