#pragma once

#include "RenderPassBase.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <array>
#include <vector>
#include <string>

// RHI forward declarations
class RHIDevice;
class RHIBuffer;
class RHIPipeline;
class RHIBindingLayout;
class RHIBindingGroup;

class VulkanDevice;
class GBufferPass;

/**
 * LightingPass - 延迟渲染光照阶段 (RHI)
 * 
 * 使用 G-Buffer 中的几何信息进行光照计算：
 * 渲染一个全屏四边形，在片段着色器中完成所有光照运算。
 */
class LightingPass : public RenderPassBase {
public:
    // 光照 UBO 结构
    struct LightingUBO {
        alignas(16) glm::vec4 viewPos;      // 相机位置
        alignas(16) glm::vec4 lightPos;     // 光源位置
        alignas(16) glm::vec4 lightColor;   // 光源颜色 + 强度
        alignas(16) glm::vec4 ambientColor; // 环境光颜色+ 强度
        alignas(16) glm::vec4 screenSize;   // 屏幕尺寸
    };

    LightingPass(std::shared_ptr<VulkanDevice> device, RHIDevice* rhiDevice,
                 uint32_t width, uint32_t height,
                 VkRenderPass targetRenderPass, uint32_t maxFramesInFlight = 2);
    ~LightingPass();

    // 禁止拷贝
    LightingPass(const LightingPass&) = delete;
    LightingPass& operator=(const LightingPass&) = delete;

    // 设置 G-Buffer 输入（原生 Vulkan handles — 过渡期）
    void setGBufferInputs(VkImageView positionView, VkImageView normalView,
                          VkImageView albedoView, VkSampler sampler);

    // 设置 SSAO 纹理（原生 Vulkan handles — 过渡期）
    void setSSAOTexture(VkImageView ssaoView, VkSampler ssaoSampler);

    // 更新光照参数
    void updateUniforms(uint32_t frameIndex, const glm::vec3& viewPos,
                        const glm::vec3& lightPos, const glm::vec3& lightColor,
                        float lightIntensity = 1.0f);

    // 设置环境光
    void setAmbientLight(const glm::vec3& color, float intensity = 0.1f);

    // 录制渲染命令
    void recordCommands(VkCommandBuffer cmd, uint32_t frameIndex) override;
    void render(VkCommandBuffer cmd, uint32_t frameIndex);

    // 获取器 — 返回原生 handles（过渡期）
    VkPipeline getPipeline() const;
    VkPipelineLayout getPipelineLayout() const;

private:
    void createBindingLayout();
    void createUniformBuffers();
    void createBindingGroups();
    void createPipeline();
    void createFullscreenQuad();
    void cleanup();

    // RHI device references
    RHIDevice* rhiDevice_ = nullptr;
    std::shared_ptr<VulkanDevice> vulkanDevice_;
    uint32_t width_;
    uint32_t height_;
    uint32_t maxFramesInFlight_;
    VkRenderPass targetRenderPass_;

    // Pipeline (RHI)
    std::unique_ptr<RHIPipeline> pipeline_;

    // Binding layout (RHI)
    std::unique_ptr<RHIBindingLayout> bindingLayout_;

    // Uniform Buffers (RHI)
    std::vector<std::unique_ptr<RHIBuffer>> uniformBuffers_;

    // Binding Groups (RHI) — one per frame
    // Note: descriptor set updates for GBuffer/SSAO textures still use native Vulkan
    // because these textures come from other passes as raw VkImageViews
    std::vector<VkDescriptorSet> nativeDescriptorSets_;

    // Fullscreen quad (RHI buffers)
    std::unique_ptr<RHIBuffer> quadVertexBuffer_;
    std::unique_ptr<RHIBuffer> quadIndexBuffer_;

    // 缓存的 G-Buffer 视图
    VkImageView cachedPositionView = VK_NULL_HANDLE;
    VkImageView cachedNormalView = VK_NULL_HANDLE;
    VkImageView cachedAlbedoView = VK_NULL_HANDLE;
    VkSampler cachedSampler = VK_NULL_HANDLE;

    // SSAO 纹理
    VkImageView cachedSSAOView = VK_NULL_HANDLE;
    VkSampler cachedSSAOSampler = VK_NULL_HANDLE;

    // 光照参数
    glm::vec3 ambientColor = glm::vec3(0.03f);
    float ambientIntensity = 1.0f;
};