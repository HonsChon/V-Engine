#pragma once

#include "RenderPassBase.h"
#include "RenderContext.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class VulkanDevice;
class GBufferPass;
class RHIDevice;
class RHITexture;
class RHISampler;
class RHIRenderPass;
class RHIFramebuffer;
class RHIPipeline;
class RHIBuffer;
class RHIBindingLayout;

/**
 * SSRPass - 屏幕空间反射渲染通道 (RHI)
 * 
 * 基于 G-Buffer 信息进行光线步进，计算屏幕空间反射
 * 使用 Hybrid Descriptor 策略：UBO 使用 RHI，G-Buffer 纹理使用 native 描述符写入
 */
class SSRPass : public RenderPassBase {
public:
    // SSR 参数结构
    struct SSRParams {
        alignas(16) glm::mat4 projection;
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 invProjection;
        alignas(16) glm::mat4 invView;
        alignas(16) glm::vec4 cameraPos;
        alignas(16) glm::vec4 screenSize;     // xy: 屏幕尺寸, zw: 1/屏幕尺寸
        alignas(4)  float maxDistance;        // 最大光线步进距离
        alignas(4)  float resolution;         // 分辨率因存
        alignas(4)  float thickness;          // 厚度阈值（线性深度空间）
        alignas(4)  float maxSteps;           // 最大步进次数
        alignas(4)  float nearPlane;          // 近平面距离（用于线性深度计算）
        alignas(4)  float farPlane;           // 远平面距离（用于线性深度计算）
        alignas(8)  float padding[2];         // 对齐填充
    };

    SSRPass(std::shared_ptr<VulkanDevice> device, RHIDevice* rhiDevice,
            uint32_t width, uint32_t height);
    ~SSRPass();

    // 禁止拷贝
    SSRPass(const SSRPass&) = delete;
    SSRPass& operator=(const SSRPass&) = delete;

    // 重新调整大小
    void resize(uint32_t width, uint32_t height);

    // 更新 SSR 参数
    void updateParams(const glm::mat4& projection, const glm::mat4& view,
                      const glm::vec3& cameraPos, uint32_t frameIndex);

    // 设置 SSR 参数
    void setMaxDistance(float distance) { params.maxDistance = distance; }
    void setThickness(float thickness) { params.thickness = thickness; }
    void setMaxSteps(float steps) { params.maxSteps = steps; }

    // 执行 SSR Pass（需要GBufferPass 和场景颜色作为输入）
    void execute(VkCommandBuffer cmd, GBufferPass* gbuffer, 
                 VkImageView sceneColorView, uint32_t frameIndex);

    // 获取输出纹理 — native handle accessors (compatibility)
    VkImageView getOutputView() const;
    VkImage getOutputImage() const;
    VkSampler getOutputSampler() const;
    
    VkRenderPass getRenderPass() const;
    VkPipeline getPipeline() const;
    VkPipelineLayout getPipelineLayout() const;

private:
    void createOutputTexture();
    void createOutputSampler();
    void createRHIRenderPass();
    void createRHIFramebuffer();
    void createBindingLayout();
    void createPipeline();
    void createUniformBuffers();
    void createDescriptorSets();
    void cleanup();

    // RHI device
    RHIDevice* rhiDevice_ = nullptr;
    std::shared_ptr<VulkanDevice> vulkanDevice_;

    uint32_t width_;
    uint32_t height_;
    
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    // SSR 参数
    SSRParams params;

    // RHI resources
    std::unique_ptr<RHITexture>     outputTexture_;
    std::unique_ptr<RHISampler>     outputSampler_;
    std::unique_ptr<RHIRenderPass>  renderPass_;
    std::unique_ptr<RHIFramebuffer> framebuffer_;
    std::unique_ptr<RHIPipeline>    pipeline_;
    std::unique_ptr<RHIBindingLayout> bindingLayout_;

    // Per-frame UBOs (RHI)
    std::vector<std::unique_ptr<RHIBuffer>> uniformBuffers_;

    // Descriptor sets (native Vulkan — hybrid approach for G-Buffer textures)
    std::vector<VkDescriptorSet> descriptorSets_;
};