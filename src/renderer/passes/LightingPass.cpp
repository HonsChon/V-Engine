#include "LightingPass.h"
#include "VulkanDevice.h"

// RHI headers
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"

// Vulkan backend headers — for downcast to get native handles
#include "VulkanRHIDevice.h"
#include "VulkanRHIPipeline.h"
#include "VulkanRHIDescriptor.h"
#include "VulkanRHIBuffer.h"

#include <stdexcept>
#include <iostream>
#include <cstring>

// =============================================================================
// Constructor / Destructor
// =============================================================================

LightingPass::LightingPass(std::shared_ptr<VulkanDevice> device, RHIDevice* rhiDevice,
                           uint32_t width, uint32_t height,
                           VkRenderPass targetRenderPass, uint32_t maxFramesInFlight)
    : RenderPassBase(device, width, height)
    , rhiDevice_(rhiDevice)
    , vulkanDevice_(device)
    , width_(width)
    , height_(height)
    , maxFramesInFlight_(maxFramesInFlight)
    , targetRenderPass_(targetRenderPass)
{
    passName = "Lighting Pass";

    createBindingLayout();
    createUniformBuffers();
    createBindingGroups();
    createPipeline();
    createFullscreenQuad();

    std::cout << "LightingPass created (RHI)" << std::endl;
}

LightingPass::~LightingPass() {
    cleanup();
}

void LightingPass::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();

    quadVertexBuffer_.reset();
    quadIndexBuffer_.reset();
    uniformBuffers_.clear();
    nativeDescriptorSets_.clear();
    pipeline_.reset();
    bindingLayout_.reset();
}

// =============================================================================
// RHI Resource Creation
// =============================================================================

void LightingPass::createBindingLayout() {
    RHIBindingLayoutDesc desc;

    // binding 0: UBO
    desc.entries.push_back({0, RHIDescriptorType::UniformBuffer, RHIShaderStage::Fragment, 1});
    // binding 1: Position texture
    desc.entries.push_back({1, RHIDescriptorType::CombinedImageSampler, RHIShaderStage::Fragment, 1});
    // binding 2: Normal texture
    desc.entries.push_back({2, RHIDescriptorType::CombinedImageSampler, RHIShaderStage::Fragment, 1});
    // binding 3: Albedo texture
    desc.entries.push_back({3, RHIDescriptorType::CombinedImageSampler, RHIShaderStage::Fragment, 1});
    // binding 4: SSAO texture
    desc.entries.push_back({4, RHIDescriptorType::CombinedImageSampler, RHIShaderStage::Fragment, 1});

    bindingLayout_ = rhiDevice_->createBindingLayout(desc);
}

void LightingPass::createUniformBuffers() {
    uniformBuffers_.resize(maxFramesInFlight_);

    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        RHIBufferDesc desc{};
        desc.size = sizeof(LightingUBO);
        desc.usage = RHIBufferUsage::Uniform;
        desc.memoryUsage = RHIMemoryUsage::CPUToGPU;
        uniformBuffers_[i] = rhiDevice_->createBuffer(desc);
    }
}

void LightingPass::createBindingGroups() {
    // LightingPass receives GBuffer textures as raw VkImageView from other passes.
    // We allocate native VkDescriptorSets and write UBO + textures natively.
    auto* vkDevice = static_cast<VulkanRHIDevice*>(rhiDevice_);
    auto* vkLayout = static_cast<VulkanRHIBindingLayout*>(bindingLayout_.get());

    nativeDescriptorSets_.resize(maxFramesInFlight_);

    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        nativeDescriptorSets_[i] = vkDevice->allocateDescriptorSet(vkLayout->getVkDescriptorSetLayout());

        // Write UBO binding immediately
        auto* vkBuf = static_cast<VulkanRHIBuffer*>(uniformBuffers_[i].get());
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = vkBuf->getVkBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(LightingUBO);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = nativeDescriptorSets_[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(vkDevice->getVkDevice(), 1, &write, 0, nullptr);
    }
}

void LightingPass::createPipeline() {
    // Fullscreen quad: vec2 position + vec2 texCoord = 4 floats
    constexpr uint32_t vertexStride = sizeof(float) * 4;

    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    auto* vkBuilder = static_cast<VulkanGraphicsPipelineBuilder*>(builder.get());

    vkBuilder
        ->setVertexShader("shaders/deferred_lighting_vert.spv")
        .setFragmentShader("shaders/deferred_lighting_frag.spv")
        .addVertexBinding(0, vertexStride, RHIVertexInputRate::Vertex)
        .addVertexAttribute(0, 0, RHIFormat::R32G32_SFLOAT, 0)                     // Position
        .addVertexAttribute(0, 1, RHIFormat::R32G32_SFLOAT, sizeof(float) * 2)     // TexCoord
        .setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::None)      // Fullscreen quad — no culling
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(false, false)           // Lighting pass — no depth test
        .setSampleCount(RHISampleCount::Count1)
        .addBindingLayout(bindingLayout_.get());

    // Transitional: set native VkRenderPass (SwapChain render pass)
    vkBuilder->setNativeRenderPass(targetRenderPass_);

    pipeline_ = vkBuilder->build();

    std::cout << "LightingPass pipeline created (RHI Pipeline Builder)" << std::endl;
}

void LightingPass::createFullscreenQuad() {
    // Fullscreen quad vertex data: position (x, y) + texCoord (u, v)
    float quadVertices[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f
    };

    uint32_t quadIndices[] = {
        0, 1, 2,
        2, 3, 0
    };

    // Vertex buffer (GPU-only, uploaded via staging)
    {
        RHIBufferDesc desc{};
        desc.size = sizeof(quadVertices);
        desc.usage = RHIBufferUsage::Vertex;
        desc.memoryUsage = RHIMemoryUsage::GPUOnly;
        quadVertexBuffer_ = rhiDevice_->createBuffer(desc);
        quadVertexBuffer_->uploadData(quadVertices, sizeof(quadVertices));
    }

    // Index buffer (GPU-only, uploaded via staging)
    {
        RHIBufferDesc desc{};
        desc.size = sizeof(quadIndices);
        desc.usage = RHIBufferUsage::Index;
        desc.memoryUsage = RHIMemoryUsage::GPUOnly;
        quadIndexBuffer_ = rhiDevice_->createBuffer(desc);
        quadIndexBuffer_->uploadData(quadIndices, sizeof(quadIndices));
    }
}

// =============================================================================
// GBuffer / SSAO Input (native Vulkan — transitional)
// =============================================================================

void LightingPass::setGBufferInputs(VkImageView positionView, VkImageView normalView,
                                     VkImageView albedoView, VkSampler sampler) {
    cachedPositionView = positionView;
    cachedNormalView = normalView;
    cachedAlbedoView = albedoView;
    cachedSampler = sampler;

    auto* vkDevice = static_cast<VulkanRHIDevice*>(rhiDevice_);

    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        std::array<VkDescriptorImageInfo, 3> imageInfos{};
        imageInfos[0] = { sampler, positionView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        imageInfos[1] = { sampler, normalView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        imageInfos[2] = { sampler, albedoView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        std::array<VkWriteDescriptorSet, 3> writes{};
        for (int j = 0; j < 3; ++j) {
            writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[j].dstSet = nativeDescriptorSets_[i];
            writes[j].dstBinding = j + 1;  // binding 1, 2, 3
            writes[j].dstArrayElement = 0;
            writes[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[j].descriptorCount = 1;
            writes[j].pImageInfo = &imageInfos[j];
        }

        vkUpdateDescriptorSets(vkDevice->getVkDevice(),
                               static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

void LightingPass::setSSAOTexture(VkImageView ssaoView, VkSampler ssaoSampler) {
    cachedSSAOView = ssaoView;
    cachedSSAOSampler = ssaoSampler;

    auto* vkDevice = static_cast<VulkanRHIDevice*>(rhiDevice_);

    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        VkDescriptorImageInfo ssaoInfo{};
        ssaoInfo.sampler = ssaoSampler;
        ssaoInfo.imageView = ssaoView;
        ssaoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = nativeDescriptorSets_[i];
        write.dstBinding = 4;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &ssaoInfo;

        vkUpdateDescriptorSets(vkDevice->getVkDevice(), 1, &write, 0, nullptr);
    }
}

// =============================================================================
// UBO Update
// =============================================================================

void LightingPass::updateUniforms(uint32_t frameIndex, const glm::vec3& viewPos,
                                   const glm::vec3& lightPos, const glm::vec3& lightColor,
                                   float lightIntensity) {
    LightingUBO ubo{};
    ubo.viewPos = glm::vec4(viewPos, 1.0f);
    ubo.lightPos = glm::vec4(lightPos, 1.0f);
    ubo.lightColor = glm::vec4(lightColor, lightIntensity);
    ubo.ambientColor = glm::vec4(ambientColor, ambientIntensity);
    ubo.screenSize = glm::vec4(static_cast<float>(width_), static_cast<float>(height_), 0.0f, 0.0f);

    void* ptr = uniformBuffers_[frameIndex]->map();
    memcpy(ptr, &ubo, sizeof(ubo));
    uniformBuffers_[frameIndex]->unmap();
}

void LightingPass::setAmbientLight(const glm::vec3& color, float intensity) {
    ambientColor = color;
    ambientIntensity = intensity;
}

// =============================================================================
// Native Handle Accessors (compatibility — transitional)
// =============================================================================

VkPipeline LightingPass::getPipeline() const {
    if (!pipeline_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHIPipeline*>(pipeline_.get())->getVkPipeline();
}

VkPipelineLayout LightingPass::getPipelineLayout() const {
    if (!pipeline_) return VK_NULL_HANDLE;
    return static_cast<VulkanRHIPipeline*>(pipeline_.get())->getVkPipelineLayout();
}

// =============================================================================
// Render Commands (VkCommandBuffer compatibility — transitional)
// =============================================================================

void LightingPass::recordCommands(VkCommandBuffer cmd, uint32_t frameIndex) {
    render(cmd, frameIndex);
}

void LightingPass::render(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width_);
    viewport.height = static_cast<float>(height_);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width_, height_};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getPipeline());

    // Bind descriptor set (native — because GBuffer textures are raw VkImageViews)
    if (frameIndex < nativeDescriptorSets_.size()) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getPipelineLayout(),
                                0, 1, &nativeDescriptorSets_[frameIndex], 0, nullptr);
    }

    // Bind fullscreen quad vertex/index buffers
    auto* vkVtxBuf = static_cast<VulkanRHIBuffer*>(quadVertexBuffer_.get());
    auto* vkIdxBuf = static_cast<VulkanRHIBuffer*>(quadIndexBuffer_.get());
    VkBuffer vertexBuffers[] = {vkVtxBuf->getVkBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, vkIdxBuf->getVkBuffer(), 0, VK_INDEX_TYPE_UINT32);

    // Draw fullscreen quad
    vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
}