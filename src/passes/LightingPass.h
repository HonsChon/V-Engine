#pragma once

#include "RenderPassBase.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <array>
#include <vector>
#include <string>

class VulkanDevice;
class GBufferPass;

/**
 * LightingPass - 延迟渲染光照阶段
 * 
 * 使用 G-Buffer 中的几何信息进行光照计算，
 * 渲染一个全屏四边形，在片段着色器中完成所有光照运算。
 */
class LightingPass : public RenderPassBase {
public:
    // 光照 UBO 结构
    struct LightingUBO {
        alignas(16) glm::vec4 viewPos;      // 相机位置
        alignas(16) glm::vec4 lightPos;     // 光源位置
        alignas(16) glm::vec4 lightColor;   // 光源颜色 + 强度
        alignas(16) glm::vec4 ambientColor; // 环境光颜色 + 强度
        alignas(16) glm::vec4 screenSize;   // 屏幕尺寸
    };

    LightingPass(std::shared_ptr<VulkanDevice> device, uint32_t width, uint32_t height,
                 VkRenderPass targetRenderPass);
    ~LightingPass();

    // 禁止拷贝
    LightingPass(const LightingPass&) = delete;
    LightingPass& operator=(const LightingPass&) = delete;

    // 设置 G-Buffer 输入
    void setGBufferInputs(VkImageView positionView, VkImageView normalView,
                          VkImageView albedoView, VkSampler sampler);

    // 更新光照参数
    void updateUniforms(uint32_t frameIndex, const glm::vec3& viewPos,
                        const glm::vec3& lightPos, const glm::vec3& lightColor,
                        float lightIntensity = 1.0f);

    // 设置环境光
    void setAmbientLight(const glm::vec3& color, float intensity = 0.1f);

    // 录制渲染命令（渲染全屏四边形）
    void recordCommands(VkCommandBuffer cmd, uint32_t frameIndex) override;

    // 渲染（简化接口）
    void render(VkCommandBuffer cmd, uint32_t frameIndex);

    // 获取器
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createUniformBuffers();
    void createPipeline();
    void createFullscreenQuad();
    void cleanup();

    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);

    VkRenderPass targetRenderPass;

    // Pipeline
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

    // 描述符
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> descriptorSets = {};

    // Uniform Buffers
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> uniformBuffers = {};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> uniformBuffersMemory = {};
    std::array<void*, MAX_FRAMES_IN_FLIGHT> uniformBuffersMapped = {};

    // 全屏四边形
    VkBuffer quadVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory quadVertexMemory = VK_NULL_HANDLE;
    VkBuffer quadIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory quadIndexMemory = VK_NULL_HANDLE;

    // 缓存的 G-Buffer 视图
    VkImageView cachedPositionView = VK_NULL_HANDLE;
    VkImageView cachedNormalView = VK_NULL_HANDLE;
    VkImageView cachedAlbedoView = VK_NULL_HANDLE;
    VkSampler cachedSampler = VK_NULL_HANDLE;

    // 光照参数
    glm::vec3 ambientColor = glm::vec3(0.03f);
    float ambientIntensity = 1.0f;
};
