#pragma once

#include "RenderPassBase.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

// Pure RHI forward declarations — no Vulkan headers
class GBufferPass;
class RHIDevice;
class RHITexture;
class RHISampler;
class RHIRenderPass;
class RHIFramebuffer;
class RHIPipeline;
class RHIBuffer;
class RHIBindingLayout;
class RHIBindingGroup;
class RHICommandBuffer;

/**
 * SSRPass - 屏幕空间反射渲染通道 (Pure RHI)
 */
class SSRPass : public RenderPassBase {
public:
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

    SSRPass(RHIDevice* rhiDevice,
            uint32_t width, uint32_t height);
    ~SSRPass();

    SSRPass(const SSRPass&) = delete;
    SSRPass& operator=(const SSRPass&) = delete;

    void resize(uint32_t width, uint32_t height);

    void updateParams(const glm::mat4& projection, const glm::mat4& view,
                      const glm::vec3& cameraPos, uint32_t frameIndex);

    void setMaxDistance(float distance) { params.maxDistance = distance; }
    void setThickness(float thickness) { params.thickness = thickness; }
    void setMaxSteps(float steps) { params.maxSteps = steps; }

    // Execute SSR pass (Pure RHI)
    void execute(RHICommandBuffer* cmd, GBufferPass* gbuffer,
                 RHITexture* sceneColorTexture, RHISampler* sceneColorSampler,
                 uint32_t frameIndex);

    // Output texture (Pure RHI)
    RHITexture* getOutputTexture() const { return outputTexture_.get(); }
    RHISampler* getOutputSampler() const { return outputSampler_.get(); }
    RHIRenderPass* getRHIRenderPass() const { return renderPass_.get(); }

private:
    void createOutputTexture();
    void createOutputSampler();
    void createRHIRenderPass();
    void createRHIFramebuffer();
    void createBindingLayout();
    void createPipeline();
    void createUniformBuffers();
    void createBindingGroups();
    void cleanup();

    RHIDevice* rhiDevice_ = nullptr;
    uint32_t width_;
    uint32_t height_;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    SSRParams params;

    std::shared_ptr<RHITexture>       outputTexture_;
    std::shared_ptr<RHISampler>       outputSampler_;
    std::shared_ptr<RHIRenderPass>    renderPass_;
    std::shared_ptr<RHIFramebuffer>   framebuffer_;
    std::shared_ptr<RHIPipeline>      pipeline_;
    std::shared_ptr<RHIBindingLayout> bindingLayout_;

    std::vector<std::shared_ptr<RHIBuffer>> uniformBuffers_;
    std::vector<std::shared_ptr<RHIBindingGroup>> bindingGroups_;
};