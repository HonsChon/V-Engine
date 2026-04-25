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
 * SSAOPass - 屏幕空间环境遮蔽渲染通道
 * 
 * 采用 Deinterleaved Texturing 优化的 SSAO 实现。
 * 内部管理 4 个子阶段：
 * 1. Deinterleave: 将 GBuffer Position/Normal 按 4×4 拆分为 16 层 texture array
 * 2. SSAO 计算: 对 16 层子纹理分别执行法线导向半球 SSAO 采样
 * 3. Reinterleave: 将 16 层 AO 结果重组为全分辨率纹理
 * 4. Blur: 对全分辨率 AO 纹理执行 4×4 box blur
 * 
 * 对外只暴露最终模糊后的 AO 纹理。
 */
class SSAOPass : public RenderPassBase {
public:
    static constexpr int KERNEL_SIZE = 64;
    static constexpr int DEINTERLEAVE_FACTOR = 4;
    static constexpr int NUM_LAYERS = DEINTERLEAVE_FACTOR * DEINTERLEAVE_FACTOR; // 16

    // SSAO 参数（运行时可调）
    struct SSAOSettings {
        float radius = 0.5f;
        float bias = 0.025f;
        float power = 1.0f;
        int kernelSize = KERNEL_SIZE;
    };

    // SSAO UBO 结构（须与 shader 匹配）
    struct SSAOParamsUBO {
        alignas(16) glm::vec4 samples[KERNEL_SIZE];  // xyz: 采样偏移
        alignas(16) glm::mat4 projection;
        alignas(16) glm::mat4 view;
        alignas(4)  float radius;
        alignas(4)  float bias;
        alignas(4)  float power;
        alignas(4)  int kernelSize;
    };

    // Deinterleave push constants
    struct DeinterleavePushConstants {
        int fullWidth;
        int fullHeight;
    };

    // SSAO push constants
    struct SSAOPushConstants {
        int layerIndex;
        float rotationAngle;
        int subWidth;
        int subHeight;
    };

    // Reinterleave push constants
    struct ReinterleavePushConstants {
        int fullWidth;
        int fullHeight;
    };

    SSAOPass(std::shared_ptr<VulkanDevice> device, uint32_t width, uint32_t height);
    ~SSAOPass();

    // 禁止拷贝
    SSAOPass(const SSAOPass&) = delete;
    SSAOPass& operator=(const SSAOPass&) = delete;

    /**
     * 执行完整的 SSAO 流程
     * @param cmd 命令缓冲
     * @param gbuffer GBuffer Pass（提供 Position/Normal 纹理）
     * @param frameIndex 当前帧索引
     * @param projection 投影矩阵
     * @param view 视图矩阵
     */
    void execute(VkCommandBuffer cmd, GBufferPass* gbuffer, uint32_t frameIndex,
                 const glm::mat4& projection, const glm::mat4& view);

    /**
     * 重建分辨率相关资源
     */
    void resize(uint32_t newWidth, uint32_t newHeight) override;

    /**
     * 更新 SSAO 参数
     */
    void updateSettings(const SSAOSettings& settings);

    // 获取最终输出
    VkImageView getOutputAOView() const { return m_blurredAOView; }
    VkImage getOutputAOImage() const { return m_blurredAOImage; }
    VkSampler getOutputAOSampler() const { return m_aoSampler; }

    SSAOSettings& getSettings() { return m_settings; }
    const SSAOSettings& getSettings() const { return m_settings; }

private:
    // ========== 初始化方法 ==========
    void init();
    void cleanup();

    // 资源创建
    void createDeinterleavedTextures();
    void createAOTextures();
    void createSamplers();
    void generateKernel();
    void generateLayerRotations();

    // Deinterleave 阶段资源
    void createDeinterleaveResources();
    void createDeinterleaveDescriptorSetLayout();
    void createDeinterleaveDescriptorPool();
    void createDeinterleaveDescriptorSets();
    void createDeinterleavePipeline();

    // SSAO 计算阶段资源
    void createSSAOResources();
    void createSSAORenderPass();
    void createSSAOFramebuffers();
    void createSSAODescriptorSetLayout();
    void createSSAODescriptorPool();
    void createSSAODescriptorSets();
    void createSSAOPipeline();
    void createSSAOUniformBuffers();

    // Reinterleave 阶段资源
    void createReinterleaveResources();
    void createReinterleaveDescriptorSetLayout();
    void createReinterleaveDescriptorPool();
    void createReinterleaveDescriptorSets();
    void createReinterleavePipeline();

    // Blur 阶段资源
    void createBlurResources();
    void createBlurRenderPass();
    void createBlurFramebuffer();
    void createBlurDescriptorSetLayout();
    void createBlurDescriptorPool();
    void createBlurDescriptorSets();
    void createBlurPipeline();

    // 执行子阶段
    void executeDeinterleave(VkCommandBuffer cmd, GBufferPass* gbuffer);
    void executeSSAO(VkCommandBuffer cmd, uint32_t frameIndex);
    void executeReinterleave(VkCommandBuffer cmd);
    void executeBlur(VkCommandBuffer cmd);

    // 工具方法
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void createImage2D(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage,
                       VkImage& image, VkDeviceMemory& memory);
    void createImage2DArray(uint32_t w, uint32_t h, uint32_t layers, VkFormat format,
                            VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);
    VkImageView createImageView2D(VkImage image, VkFormat format, VkImageAspectFlags aspect);
    VkImageView createImageView2DArray(VkImage image, VkFormat format, uint32_t layers, VkImageAspectFlags aspect);
    VkImageView createImageView2DArraySingleLayer(VkImage image, VkFormat format, uint32_t layer, VkImageAspectFlags aspect);
    void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                               VkImageLayout newLayout, VkImageAspectFlags aspect,
                               uint32_t layerCount = 1);

    // ========== 成员变量 ==========
    SSAOSettings m_settings;
    
    // 子纹理尺寸
    uint32_t m_subWidth = 0;
    uint32_t m_subHeight = 0;

    // 采样核心
    std::array<glm::vec4, KERNEL_SIZE> m_kernel;
    std::array<float, NUM_LAYERS> m_layerRotations;

    // ---- Deinterleaved Textures ----
    // Position array (16 layers, R16G16B16A16_SFLOAT)
    VkImage m_deinterleavedPositionImage = VK_NULL_HANDLE;
    VkDeviceMemory m_deinterleavedPositionMemory = VK_NULL_HANDLE;
    VkImageView m_deinterleavedPositionView = VK_NULL_HANDLE;  // full array view

    // Normal array (16 layers, R16G16B16A16_SFLOAT)
    VkImage m_deinterleavedNormalImage = VK_NULL_HANDLE;
    VkDeviceMemory m_deinterleavedNormalMemory = VK_NULL_HANDLE;
    VkImageView m_deinterleavedNormalView = VK_NULL_HANDLE;  // full array view

    // ---- AO Texture Array ----
    // AO output array (16 layers, R8_UNORM)
    VkImage m_aoArrayImage = VK_NULL_HANDLE;
    VkDeviceMemory m_aoArrayMemory = VK_NULL_HANDLE;
    VkImageView m_aoArrayView = VK_NULL_HANDLE;  // full array view
    std::array<VkImageView, NUM_LAYERS> m_aoArrayLayerViews = {};  // per-layer views

    // ---- Full Resolution AO ----
    // Reinterleaved AO (R8_UNORM, full res)
    VkImage m_fullAOImage = VK_NULL_HANDLE;
    VkDeviceMemory m_fullAOMemory = VK_NULL_HANDLE;
    VkImageView m_fullAOView = VK_NULL_HANDLE;

    // Blurred AO (R8_UNORM, full res) — final output
    VkImage m_blurredAOImage = VK_NULL_HANDLE;
    VkDeviceMemory m_blurredAOMemory = VK_NULL_HANDLE;
    VkImageView m_blurredAOView = VK_NULL_HANDLE;

    // ---- Samplers ----
    VkSampler m_aoSampler = VK_NULL_HANDLE;           // clamp-to-edge for AO
    VkSampler m_deinterleaveSampler = VK_NULL_HANDLE;  // clamp-to-edge for deinterleaved

    // ---- Deinterleave Pipeline ----
    VkPipelineLayout m_deinterleavePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_deinterleavePipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_deinterleaveDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_deinterleaveDescPool = VK_NULL_HANDLE;
    VkDescriptorSet m_deinterleaveDescSet = VK_NULL_HANDLE;

    // ---- SSAO Pipeline ----
    VkRenderPass m_ssaoRenderPass = VK_NULL_HANDLE;
    std::array<VkFramebuffer, NUM_LAYERS> m_ssaoFramebuffers = {};
    VkPipelineLayout m_ssaoPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_ssaoPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_ssaoDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_ssaoDescPool = VK_NULL_HANDLE;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_ssaoDescSets = {};
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_ssaoUBOs = {};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> m_ssaoUBOMemory = {};
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_ssaoUBOMapped = {};

    // ---- Reinterleave Pipeline ----
    VkPipelineLayout m_reinterleavePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_reinterleavePipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_reinterleaveDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_reinterleaveDescPool = VK_NULL_HANDLE;
    VkDescriptorSet m_reinterleaveDescSet = VK_NULL_HANDLE;

    // ---- Blur Pipeline ----
    VkRenderPass m_blurRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_blurFramebuffer = VK_NULL_HANDLE;
    VkPipelineLayout m_blurPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_blurPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_blurDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_blurDescPool = VK_NULL_HANDLE;
    VkDescriptorSet m_blurDescSet = VK_NULL_HANDLE;
};
