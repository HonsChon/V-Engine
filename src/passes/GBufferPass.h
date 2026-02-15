#pragma once

#include "RenderPassBase.h"
#include "RenderContext.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <array>
#include <string>

class VulkanDevice;
class VulkanBuffer;

/**
 * GBufferPass - 几何缓冲区渲染通道
 * 
 * 用于延迟渲染的第一阶段，存储场景的几何信息：
 * - Position (RGB16F) - 世界空间位置
 * - Normal (RGB16F) - 世界空间法线
 * - Albedo (RGBA8) - 反照率 + 金属度
 * - Depth (D32F) - 深度缓冲
 */
class GBufferPass : public RenderPassBase {
public:
    // G-Buffer 附件索引
    enum Attachment {
        POSITION = 0,   // RGB16F - 世界空间位置
        NORMAL = 1,     // RGB16F - 世界空间法线  
        ALBEDO = 2,     // RGBA8 - 反照率(RGB) + 金属度(A)
        DEPTH = 3,      // D32F - 深度
        COUNT = 4
    };

    // Push Constants 结构体 - 每个物体独立的变换数据
    struct PushConstantData {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 normalMatrix;
    };

    GBufferPass(std::shared_ptr<VulkanDevice> device, uint32_t width, uint32_t height);
    ~GBufferPass();

    // 禁止拷贝
    GBufferPass(const GBufferPass&) = delete;
    GBufferPass& operator=(const GBufferPass&) = delete;

    // 重新创建 GBuffer（窗口大小改变时）
    void resize(uint32_t width, uint32_t height);

    // 获取器
    VkRenderPass getRenderPass() const { return renderPass; }
    VkFramebuffer getFramebuffer() const { return framebuffer; }
    
    VkImageView getPositionView() const { return attachmentViews[POSITION]; }
    VkImageView getNormalView() const { return attachmentViews[NORMAL]; }
    VkImageView getAlbedoView() const { return attachmentViews[ALBEDO]; }
    VkImageView getDepthView() const { return attachmentViews[DEPTH]; }
    
    VkImage getPositionImage() const { return attachmentImages[POSITION]; }
    VkImage getNormalImage() const { return attachmentImages[NORMAL]; }
    VkImage getAlbedoImage() const { return attachmentImages[ALBEDO]; }
    VkImage getDepthImage() const { return attachmentImages[DEPTH]; }
    
    VkSampler getSampler() const { return sampler; }
    
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }

    // 实现基类的录制命令方法
    void recordCommands(VkCommandBuffer cmd, uint32_t frameIndex) override;
    
    // 使用 RenderContext 录制命令（推荐方式）
    void recordCommands(VkCommandBuffer cmd, const RenderContext& context);
    
    // 设置描述符集（包含纹理等）
    void setDescriptorSet(VkDescriptorSet descriptorSet) { currentDescriptorSet = descriptorSet; }

    // 开始/结束 G-Buffer 渲染（供外部更细粒度控制使用）
    void beginRenderPass(VkCommandBuffer cmd);
    void endRenderPass(VkCommandBuffer cmd);
    
    // 获取清除值
    std::array<VkClearValue, 4> getClearValues() const;

    // G-Buffer Pipeline 相关
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
    
    // 描述符集相关
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const { 
        return descriptorSets[frameIndex]; 
    }
    
    // 绑定 Pipeline 和描述符集
    void bindPipeline(VkCommandBuffer cmd) const;
    void bindDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex) const;
    
    // 绘制网格
    void drawMesh(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkBuffer indexBuffer, uint32_t indexCount) const;
    
    // Push Constants - 推送每个物体的变换矩阵
    void pushModelMatrix(VkCommandBuffer cmd, const glm::mat4& model);
    
    // 创建描述符集和 Uniform Buffers
    void createDescriptorSets();
    
    // 更新纹理绑定
    void updateTextureBindings(VkImageView albedoView, VkImageView normalView, 
                               VkImageView specularView, VkSampler textureSampler);
    
    // UBO 结构体 - 全局共享的数据（相机、光照）
    struct UniformBufferObject {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::vec4 viewPos;
        alignas(16) glm::vec4 lightPos;
        alignas(16) glm::vec4 lightColor;
    };
    
    // 更新 UBO 数据
    void updateUniformBuffer(uint32_t frameIndex, const UniformBufferObject& ubo);

private:
    void createAttachments();
    void createRenderPass();
    void createFramebuffer();
    void createSampler();
    void cleanup();

    void createImage(VkFormat format, VkImageUsageFlags usage, 
                     VkImageAspectFlags aspect, uint32_t index);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    
    // Pipeline 创建
    void createDescriptorSetLayout();
    void createPipeline();
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);

    std::shared_ptr<VulkanDevice> device;
    
    uint32_t width;
    uint32_t height;

    // 附件资源
    std::array<VkImage, COUNT> attachmentImages = {};
    std::array<VkDeviceMemory, COUNT> attachmentMemories = {};
    std::array<VkImageView, COUNT> attachmentViews = {};
    
    // 附件格式
    std::array<VkFormat, COUNT> attachmentFormats = {
        VK_FORMAT_R16G16B16A16_SFLOAT,  // Position
        VK_FORMAT_R16G16B16A16_SFLOAT,  // Normal
        VK_FORMAT_R8G8B8A8_UNORM,       // Albedo
        VK_FORMAT_D32_SFLOAT            // Depth
    };

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    
    // G-Buffer Pipeline
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    
    // 当前渲染状态
    VkDescriptorSet currentDescriptorSet = VK_NULL_HANDLE;
    RenderContext currentContext;
    
    // 描述符资源
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> descriptorSets = {};
    
    // Uniform Buffers
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> uniformBuffers = {};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> uniformBuffersMemory = {};
    std::array<void*, MAX_FRAMES_IN_FLIGHT> uniformBuffersMapped = {};
    
    // 创建 Uniform Buffers
    void createUniformBuffers();
};
