#include "SSRPass.h"
#include "GBufferPass.h"
#include "VulkanDevice.h"

// RHI headers
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIRenderPass.h"

// Vulkan backend headers — for downcast to get native handles
#include "VulkanRHIDevice.h"
#include "VulkanRHIPipeline.h"
#include "VulkanRHIDescriptor.h"
#include "VulkanRHIBuffer.h"
#include "VulkanRHITexture.h"
#include "VulkanRHISampler.h"
#include "VulkanRHIRenderPass.h"

#include <stdexcept>
#include <iostream>
#include <array>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>

// =============================================================================
// Constructor / Destructor
// =============================================================================

SSRPass::SSRPass(std::shared_ptr<VulkanDevice> device, RHIDevice* rhiDevice,
                 uint32_t width, uint32_t height)
    : RenderPassBase(device, width, height)
    , rhiDevice_(rhiDevice)
    , vulkanDevice_(device)
    , width_(width)
    , height_(height)
{
    passName = "SSR Pass";

    // 初始化默认参数
    params.maxDistance = 50.0f;
    params.resolution = 1.0f;
    params.thickness = 0.01f;
    params.maxSteps = 64.0f;
    params.screenSize = glm::vec4(width_, height_, 1.0f / width_, 1.0f / height_);
    params.nearPlane = 0.1f;
    params.farPlane = 100.0f;

    createOutputTexture();
    createOutputSampler();
    createRHIRenderPass();
    createRHIFramebuffer();
    createBindingLayout();
    createUniformBuffers();
    createDescriptorSets();
    createPipeline();

    std::cout << "SSRPass created (RHI): " << width_ << "x" << height_ << std::endl;
}

SSRPass::~SSRPass() {
    cleanup();
}

void SSRPass::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();

    descriptorSets_.clear();
    uniformBuffers_.clear();
    pipeline_.reset();
    bindingLayout_.reset();
    framebuffer_.reset();
    renderPass_.reset();
    outputSampler_.reset();
    outputTexture_.reset();
}

void SSRPass::resize(uint32_t newWidth, uint32_t newHeight) {
    if (newWidth == width_ && newHeight == height_) return;

    if (rhiDevice_) rhiDevice_->waitIdle();

    // Release resolution-dependent resources
    framebuffer_.reset();
    renderPass_.reset();
    outputTexture_.reset();
    outputSampler_.reset();

    width_ = newWidth;
    height_ = newHeight;
    params.screenSize = glm::vec4(width_, height_, 1.0f / width_, 1.0f / height_);

    createOutputTexture();
    createOutputSampler();
    createRHIRenderPass();
    createRHIFramebuffer();

    std::cout << "SSRPass resized (RHI): " << width_ << "x" << height_ << std::endl;
}

// =============================================================================
// RHI Resource Creation
// =============================================================================

void SSRPass::createOutputTexture() {
    RHITextureDesc desc{};
    desc.width = width_;
    desc.height = height_;
    desc.format = RHIFormat::R16G16B16A16_SFLOAT;
    desc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled;
    outputTexture_ = rhiDevice_->createTexture(desc);
}

void SSRPass::createOutputSampler() {
    RHISamplerDesc desc{};
    desc.magFilter = RHIFilter::Linear;
    desc.minFilter = RHIFilter::Linear;
    desc.addressModeU = RHIAddressMode::ClampToEdge;
    desc.addressModeV = RHIAddressMode::ClampToEdge;
    desc.addressModeW = RHIAddressMode::ClampToEdge;
    desc.anisotropyEnable = false;
    desc.maxAnisotropy = 1.0f;
    desc.mipMapFilter = RHIFilter::Linear;
    desc.minLod = 0.0f;
    desc.maxLod = 1.0f;
    outputSampler_ = rhiDevice_->createSampler(desc);
}

void SSRPass::createRHIRenderPass() {
    RHIRenderPassDesc desc{};
    desc.addColorAttachment(
        RHIFormat::R16G16B16A16_SFLOAT,
        RHILoadOp::Clear,
        RHIStoreOp::Store,
        RHIImageLayout::Undefined,
        RHIImageLayout::ShaderReadOnly
    );
    renderPass_ = rhiDevice_->createRenderPass(desc);
}

void SSRPass::createRHIFramebuffer() {
    RHIFramebufferDesc desc{};
    desc.renderPass = renderPass_.get();
    desc.width = width_;
    desc.height = height_;
    desc.attachments.push_back(outputTexture_.get());
    framebuffer_ = rhiDevice_->createFramebuffer(desc);
}

void SSRPass::createBindingLayout() {
    // All 6 bindings in a single set:
    // 0-4: CombinedImageSampler (Position, Normal, Albedo, Depth, SceneColor)
    // 5: UniformBuffer (SSR params)
    RHIBindingLayoutDesc desc;
    for (uint32_t i = 0; i < 5; ++i) {
        desc.entries.push_back({
            i,                                          // binding
            RHIDescriptorType::CombinedImageSampler,    // type
            RHIShaderStage::Fragment,                    // stageFlags
            1                                           // count
        });
    }
    desc.entries.push_back({
        5,                                  // binding
        RHIDescriptorType::UniformBuffer,   // type
        RHIShaderStage::Fragment,           // stageFlags
        1                                   // count
    });
    bindingLayout_ = rhiDevice_->createBindingLayout(desc);
}

void SSRPass::createUniformBuffers() {
    uniformBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        RHIBufferDesc desc{};
        desc.size = sizeof(SSRParams);
        desc.usage = RHIBufferUsage::Uniform;
        desc.memoryUsage = RHIMemoryUsage::CPUToGPU;
        uniformBuffers_[i] = rhiDevice_->createBuffer(desc);
    }
}

void SSRPass::createDescriptorSets() {
    // Allocate native Vulkan descriptor sets via RHI device
    auto* vkDevice = static_cast<VulkanRHIDevice*>(rhiDevice_);
    auto* vkLayout = static_cast<VulkanRHIBindingLayout*>(bindingLayout_.get());

    descriptorSets_.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        descriptorSets_[i] = vkDevice->allocateDescriptorSet(vkLayout->getVkDescriptorSetLayout());
    }
    // Actual texture bindings are updated per-frame in execute()
}

void SSRPass::createPipeline() {
    // Fullscreen triangle — no vertex input
    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    auto* vkBuilder = static_cast<VulkanGraphicsPipelineBuilder*>(builder.get());

    vkBuilder
        ->setVertexShader("shaders/ssr_vert.spv")
        .setFragmentShader("shaders/ssr_frag.spv")
        .setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::None)
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(false, false, RHICompareOp::Less)
        .setSampleCount(RHISampleCount::Count1)
        .setColorAttachmentCount(1)
        .addBindingLayout(bindingLayout_.get())
        .setRenderPass(renderPass_.get());

    pipeline_ = vkBuilder->build();

    std::cout << "SSR pipeline created (RHI Pipeline Builder)" << std::endl;
}

// =============================================================================
// UBO Update
// =============================================================================

void SSRPass::updateParams(const glm::mat4& projection, const glm::mat4& view,
                           const glm::vec3& cameraPos, uint32_t frameIndex) {
    params.projection = projection;
    params.view = view;
    params.invProjection = glm::inverse(projection);
    params.invView = glm::inverse(view);
    params.cameraPos = glm::vec4(cameraPos, 1.0f);

    void* ptr = uniformBuffers_[frameIndex]->map();
    memcpy(ptr, &params, sizeof(SSRParams));
    uniformBuffers_[frameIndex]->unmap();
}

// =============================================================================
// Execute
// =============================================================================

void SSRPass::execute(VkCommandBuffer cmd, GBufferPass* gbuffer,
                      VkImageView sceneColorView, uint32_t frameIndex) {
    auto* vkDevice = static_cast<VulkanRHIDevice*>(rhiDevice_);

    // --- Update descriptor set with G-Buffer textures (native Vulkan) ---
    std::array<VkDescriptorImageInfo, 5> imageInfos{};

    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = gbuffer->getPositionView();
    imageInfos[0].sampler = gbuffer->getSampler();

    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].imageView = gbuffer->getNormalView();
    imageInfos[1].sampler = gbuffer->getSampler();

    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[2].imageView = gbuffer->getAlbedoView();
    imageInfos[2].sampler = gbuffer->getSampler();

    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    imageInfos[3].imageView = gbuffer->getDepthView();
    imageInfos[3].sampler = gbuffer->getSampler();

    imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[4].imageView = sceneColorView;
    imageInfos[4].sampler = gbuffer->getSampler();

    // UBO binding
    auto* vkUBO = static_cast<VulkanRHIBuffer*>(uniformBuffers_[frameIndex].get());
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = vkUBO->getVkBuffer();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(SSRParams);

    std::array<VkWriteDescriptorSet, 6> descriptorWrites{};

    for (int i = 0; i < 5; ++i) {
        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = descriptorSets_[frameIndex];
        descriptorWrites[i].dstBinding = i;
        descriptorWrites[i].dstArrayElement = 0;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].pImageInfo = &imageInfos[i];
    }

    descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[5].dstSet = descriptorSets_[frameIndex];
    descriptorWrites[5].dstBinding = 5;
    descriptorWrites[5].dstArrayElement = 0;
    descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[5].descriptorCount = 1;
    descriptorWrites[5].pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(vkDevice->getVkDevice(),
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);

    // --- Begin render pass ---
    VkClearValue clearValue{};
    clearValue.color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};

    auto* vkRenderPass = static_cast<VulkanRHIRenderPass*>(renderPass_.get());
    auto* vkFramebuffer = static_cast<VulkanRHIFramebuffer*>(framebuffer_.get());

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = vkRenderPass->getVkRenderPass();
    renderPassInfo.framebuffer = vkFramebuffer->getVkFramebuffer();
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = { width_, height_ };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    auto* vkPipeline = static_cast<VulkanRHIPipeline*>(pipeline_.get());
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->getVkPipeline());

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width_);
    viewport.height = static_cast<float>(height_);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = { width_, height_ };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vkPipeline->getVkPipelineLayout(),
                            0, 1, &descriptorSets_[frameIndex], 0, nullptr);

    // Fullscreen triangle
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}

// =============================================================================
// Native Handle Accessors (compatibility — transitional)
// =============================================================================

VkImageView SSRPass::getOutputView() const {
    if (!outputTexture_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHITexture*>(outputTexture_.get())->getVkImageView();
}

VkImage SSRPass::getOutputImage() const {
    if (!outputTexture_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHITexture*>(outputTexture_.get())->getVkImage();
}

VkSampler SSRPass::getOutputSampler() const {
    if (!outputSampler_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHISampler*>(outputSampler_.get())->getVkSampler();
}

VkRenderPass SSRPass::getRenderPass() const {
    if (!renderPass_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHIRenderPass*>(renderPass_.get())->getVkRenderPass();
}

VkPipeline SSRPass::getPipeline() const {
    if (!pipeline_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHIPipeline*>(pipeline_.get())->getVkPipeline();
}

VkPipelineLayout SSRPass::getPipelineLayout() const {
    if (!pipeline_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHIPipeline*>(pipeline_.get())->getVkPipelineLayout();
}