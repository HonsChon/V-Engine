#pragma once

#include "RenderPassBase.h"
#include "RenderContext.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>

class VulkanDevice;
class VulkanBuffer;
class GBufferPass;

/**
 * SSRPass - 屏幕空间反射渲染通道
 * 
 * 基于 G-Buffer 信息进行光线步进，计算屏幕空间反射
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
        alignas(4)  float resolution;         // 分辨率因子
        alignas(4)  float thickness;          // 厚度阈值（线性深度空间）
        alignas(4)  float maxSteps;           // 最大步进次数
        alignas(4)  float nearPlane;          // 近平面距离（用于线性深度计算）
        alignas(4)  float farPlane;           // 远平面距离（用于线性深度计算）
        alignas(8)  float padding[2];         // 对齐填充
    };

    SSRPass(std::shared_ptr<VulkanDevice> device, uint32_t width, uint32_t height);
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

    // 执行 SSR Pass（需要 GBufferPass 和场景颜色作为输入）
    void execute(VkCommandBuffer cmd, GBufferPass* gbuffer, 
                 VkImageView sceneColorView, uint32_t frameIndex);

    // 获取输出纹理
    VkImageView getOutputView() const { return outputImageView; }
    VkImage getOutputImage() const { return outputImage; }
    
    VkRenderPass getRenderPass() const { return renderPass; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

private:
    void createOutputImage();
    void createRenderPass();
    void createFramebuffer();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createPipeline();
    void createUniformBuffers();
    void cleanup();
    
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    std::shared_ptr<VulkanDevice> device;
    
    uint32_t width;
    uint32_t height;

    // SSR 参数
    SSRParams params;

    // 输出图像
    VkImage outputImage = VK_NULL_HANDLE;
    VkDeviceMemory outputImageMemory = VK_NULL_HANDLE;
    VkImageView outputImageView = VK_NULL_HANDLE;
    VkSampler outputSampler = VK_NULL_HANDLE;

    // Vulkan 资源
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    
    // 描述符
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    
    // Uniform Buffers
    std::vector<std::unique_ptr<VulkanBuffer>> uniformBuffers;
    std::vector<void*> uniformBuffersMapped;
    
    static const int MAX_FRAMES_IN_FLIGHT = 2;
};
