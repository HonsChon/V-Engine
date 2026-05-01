#pragma once

#include <memory>
#include <string>
#include <vector>

class RHIDevice;
class RHIPipeline;
class RHIBindingLayout;
class RHIBindingGroup;
class RHICommandBuffer;
class RHIBuffer;

/**
 * ComputePassBase - 计算通道基类 (Pure RHI)
 * 
 * 所有计算通道（如 GPU Culling、粒子更新等）的基类。
 * 提供通用的计算管线管理和资源绑定功能。
 * 只持有 RHIDevice*，禁止直接依赖 VulkanRHI 具体类型。
 */
class ComputePassBase {
public:
    ComputePassBase(RHIDevice* rhiDevice, const std::string& name);
    virtual ~ComputePassBase();

    ComputePassBase(const ComputePassBase&) = delete;
    ComputePassBase& operator=(const ComputePassBase&) = delete;

    virtual void init() = 0;
    virtual void record(RHICommandBuffer* cmd) = 0;
    virtual void cleanup();

    const std::string& getName() const { return name; }

protected:
    /**
     * 插入缓冲区屏障 (Pure RHI)
     */
    void insertBufferBarrier(RHICommandBuffer* cmd, RHIBuffer* buffer);

    RHIDevice* rhiDevice_ = nullptr;
    std::string name;
    
    // RHI resources
    std::unique_ptr<RHIPipeline>      pipeline_;
    std::unique_ptr<RHIBindingLayout> bindingLayout_;
};