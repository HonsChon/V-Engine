#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <string>

class VulkanDevice;

/**
 * RenderPassBase - 渲染通道基类
 * 
 * 所有渲染通道的抽象基类，定义统一接口：
 * - recordCommands: 录制命令到命令缓冲
 * - resize: 窗口大小改变时重建资源
 */
class RenderPassBase {
public:
    RenderPassBase(std::shared_ptr<VulkanDevice> device, uint32_t width, uint32_t height)
        : device(device), width(width), height(height) {}
    
    virtual ~RenderPassBase() = default;
    
    // 禁止拷贝
    RenderPassBase(const RenderPassBase&) = delete;
    RenderPassBase& operator=(const RenderPassBase&) = delete;
    
    /**
     * 录制渲染命令（可选实现）
     * 
     * 注意：并非所有 Pass 都适合这个简单接口。
     * 例如 SSRPass 需要 GBuffer 和 SceneColor 作为输入，
     * 应该使用它自己的 execute() 方法。
     * 
     * @param cmd 命令缓冲
     * @param frameIndex 当前帧索引
     */
    virtual void recordCommands(VkCommandBuffer cmd, uint32_t frameIndex) {
        (void)cmd;
        (void)frameIndex;
        // 默认空实现，子类可以选择是否重写
    }
    
    /**
     * 重建资源（窗口大小改变时调用）
     * @param newWidth 新宽度
     * @param newHeight 新高度
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

protected:
    std::shared_ptr<VulkanDevice> device;
    uint32_t width;
    uint32_t height;
    std::string passName = "Unnamed Pass";
    bool enabled = true;
};
