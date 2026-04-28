#include "ForwardPass.h"
#include "VulkanDevice.h"
#include "Mesh.h"

// Vulkan backend headers — for downcast to get native handles (compatibility layer)
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

ForwardPass::ForwardPass(std::shared_ptr<VulkanDevice> device,
                         RHIDevice* rhiDevice,
                         VkRenderPass renderPass,
                         uint32_t width, uint32_t height,
                         uint32_t maxFramesInFlight)
    : RenderPassBase(device, width, height)
    , rhiDevice_(rhiDevice)
    , vulkanDevice_(device)
    , renderPass_(renderPass)
    , width_(width)
    , height_(height)
    , maxFramesInFlight_(maxFramesInFlight)
{
    passName = "Forward Pass";

    createBindingLayouts();
    createPipeline();
    createUniformBuffers();
    createGlobalBindingGroups();

    std::cout << "ForwardPass created (RHI): " << width_ << "x" << height_ << std::endl;
}

ForwardPass::~ForwardPass() {
    cleanup();
}

void ForwardPass::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();

    // RHI resources are automatically released via unique_ptr
    globalBindingGroups_.clear();
    uniformBuffers_.clear();
    materialDescriptorCache_.clear();
    pipeline_.reset();
    globalLayout_.reset();
    materialLayout_.reset();
}

void ForwardPass::recreate(VkRenderPass newRenderPass, uint32_t newWidth, uint32_t newHeight) {
    if (rhiDevice_) rhiDevice_->waitIdle();

    pipeline_.reset();
    renderPass_ = newRenderPass;
    width_ = newWidth;
    height_ = newHeight;

    createPipeline();
}

// =============================================================================
// RHI Resource Creation
// =============================================================================

void ForwardPass::createBindingLayouts() {
    // ========== Set 0: Global UBO ==========
    {
        RHIBindingLayoutDesc desc;
        desc.entries.push_back({
            0,                                  // binding
            RHIDescriptorType::UniformBuffer,   // type
            RHIShaderStage::Vertex | RHIShaderStage::Fragment,  // stageFlags
            1                                   // count
        });
        globalLayout_ = rhiDevice_->createBindingLayout(desc);
    }

    // ========== Set 1: Material Textures (3 combined image samplers) ==========
    {
        RHIBindingLayoutDesc desc;
        for (uint32_t i = 0; i < 3; ++i) {
            desc.entries.push_back({
                i,                                          // binding
                RHIDescriptorType::CombinedImageSampler,    // type
                RHIShaderStage::Fragment,                   // stageFlags
                1                                           // count
            });
        }
        materialLayout_ = rhiDevice_->createBindingLayout(desc);
    }

    std::cout << "ForwardPass binding layouts created (RHI)" << std::endl;
}

void ForwardPass::createPipeline() {
    // Vertex layout: pos(3) + normal(3) + texCoord(2) + tangent(3) = 11 floats
    constexpr uint32_t vertexStride = sizeof(float) * 11;

    // Use the Vulkan backend builder with setNativeRenderPass for compatibility
    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    auto* vkBuilder = static_cast<VulkanGraphicsPipelineBuilder*>(builder.get());

    vkBuilder
        ->setVertexShader("shaders/pbr_vert.spv")
        .setFragmentShader("shaders/pbr_frag.spv")
        .addVertexBinding(0, vertexStride, RHIVertexInputRate::Vertex)
        .addVertexAttribute(0, 0, RHIFormat::R32G32B32_SFLOAT, 0)                      // Position
        .addVertexAttribute(0, 1, RHIFormat::R32G32B32_SFLOAT, sizeof(float) * 3)       // Normal
        .addVertexAttribute(0, 2, RHIFormat::R32G32_SFLOAT,    sizeof(float) * 6)       // TexCoord
        .addVertexAttribute(0, 3, RHIFormat::R32G32B32_SFLOAT, sizeof(float) * 8)       // Tangent
        .setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::Back)
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(true, true, RHICompareOp::Less)
        .setSampleCount(RHISampleCount::Count1)
        .addBindingLayout(globalLayout_.get())
        .addBindingLayout(materialLayout_.get())
        .addPushConstant(RHIShaderStage::Vertex, 0, sizeof(PushConstantData));

    // Transitional: set native VkRenderPass directly (SwapChain not yet migrated)
    vkBuilder->setNativeRenderPass(renderPass_);

    pipeline_ = vkBuilder->build();

    std::cout << "ForwardPass pipeline created (RHI Pipeline Builder)" << std::endl;
}

void ForwardPass::createUniformBuffers() {
    uniformBuffers_.resize(maxFramesInFlight_);

    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        RHIBufferDesc desc{};
        desc.size = sizeof(UniformBufferObject);
        desc.usage = RHIBufferUsage::Uniform;
        desc.memoryUsage = RHIMemoryUsage::CPUToGPU;  // HOST_VISIBLE + HOST_COHERENT
        uniformBuffers_[i] = rhiDevice_->createBuffer(desc);
    }
}

void ForwardPass::createGlobalBindingGroups() {
    globalBindingGroups_.resize(maxFramesInFlight_);

    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        RHIBindingGroupDesc desc;
        RHIBindingGroupEntry entry{};
        entry.binding = 0;
        entry.kind = RHIBindingGroupEntry::Kind::Buffer;
        entry.bufferBinding.buffer = uniformBuffers_[i].get();
        entry.bufferBinding.offset = 0;
        entry.bufferBinding.range = sizeof(UniformBufferObject);
        desc.entries.push_back(entry);

        globalBindingGroups_[i] = rhiDevice_->createBindingGroup(globalLayout_.get(), desc);
    }

    std::cout << "Global binding groups created (RHI)" << std::endl;
}

// =============================================================================
// UBO Update
// =============================================================================

void ForwardPass::updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo) {
    void* ptr = uniformBuffers_[currentFrame]->map();
    memcpy(ptr, &ubo, sizeof(ubo));
    uniformBuffers_[currentFrame]->unmap();
}

// =============================================================================
// Material Descriptor Management
// =============================================================================

ForwardPass::MaterialDescriptor* ForwardPass::allocateMaterialDescriptor(const std::string& materialId) {
    auto it = materialDescriptorCache_.find(materialId);
    if (it != materialDescriptorCache_.end()) {
        return &it->second;
    }

    // For now, we allocate empty descriptors — they will be updated via
    // updateMaterialTextures which still uses native Vulkan handles.
    // A full migration would use RHI textures, but the material system
    // hasn't been migrated yet.
    MaterialDescriptor descriptor;
    descriptor.groups.resize(maxFramesInFlight_);
    descriptor.nativeSets.resize(maxFramesInFlight_, VK_NULL_HANDLE);
    descriptor.valid = false;  // Will be set to true after updateMaterialTextures

    materialDescriptorCache_[materialId] = std::move(descriptor);

    std::cout << "Allocated material descriptor (RHI): " << materialId << std::endl;
    return &materialDescriptorCache_[materialId];
}

void ForwardPass::updateMaterialTextures(MaterialDescriptor* material,
                                          VkImageView albedoView, VkSampler albedoSampler,
                                          VkImageView normalView, VkSampler normalSampler,
                                          VkImageView specularView, VkSampler specularSampler) {
    if (!material) return;

    // COMPATIBILITY: Material system still provides raw Vulkan handles.
    // We use native Vulkan calls to write descriptor sets allocated from
    // the RHI device's auto-growing pool.
    auto* vkLayout = static_cast<VulkanRHIBindingLayout*>(materialLayout_.get());
    auto* vkDevice = static_cast<VulkanRHIDevice*>(rhiDevice_);

    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        VkDescriptorSet ds = vkDevice->allocateDescriptorSet(vkLayout->getVkDescriptorSetLayout());

        std::array<VkDescriptorImageInfo, 3> imageInfos{};
        imageInfos[0] = { albedoSampler,   albedoView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        imageInfos[1] = { normalSampler,   normalView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        imageInfos[2] = { specularSampler, specularView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        std::array<VkWriteDescriptorSet, 3> writes{};
        for (int j = 0; j < 3; ++j) {
            writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[j].dstSet = ds;
            writes[j].dstBinding = j;
            writes[j].dstArrayElement = 0;
            writes[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[j].descriptorCount = 1;
            writes[j].pImageInfo = &imageInfos[j];
        }

        vkUpdateDescriptorSets(vkDevice->getVkDevice(),
                               static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);

        // Store the native VkDescriptorSet for binding during render
        material->nativeSets[i] = ds;
        material->groups[i] = nullptr;  // No RHIBindingGroup wrapper for native sets yet
    }

    material->valid = true;
}

ForwardPass::MaterialDescriptor* ForwardPass::getMaterialDescriptor(const std::string& materialId) {
    auto it = materialDescriptorCache_.find(materialId);
    if (it != materialDescriptorCache_.end() && it->second.valid) {
        return &it->second;
    }
    return nullptr;
}

// =============================================================================
// Render Commands (VkCommandBuffer compatibility — transitional)
// =============================================================================

void ForwardPass::begin(VkCommandBuffer cmd) {
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
}

VkPipeline ForwardPass::getPipeline() const {
    if (!pipeline_) return VK_NULL_HANDLE;
    auto* vkPipeline = static_cast<VulkanRHIPipeline*>(pipeline_.get());
    return vkPipeline->getVkPipeline();
}

VkPipelineLayout ForwardPass::getPipelineLayout() const {
    if (!pipeline_) return VK_NULL_HANDLE;
    auto* vkPipeline = static_cast<VulkanRHIPipeline*>(pipeline_.get());
    return vkPipeline->getVkPipelineLayout();
}

void ForwardPass::bindPipeline(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getPipeline());
}

void ForwardPass::bindGlobalDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex) {
    auto* vkGroup = static_cast<VulkanRHIBindingGroup*>(globalBindingGroups_[frameIndex].get());
    VkDescriptorSet ds = vkGroup->getVkDescriptorSet();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getPipelineLayout(),
                            0, 1, &ds, 0, nullptr);
}

void ForwardPass::bindMaterialDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex,
                                             MaterialDescriptor* material) {
    if (!material || !material->valid || frameIndex >= material->nativeSets.size()) return;

    VkDescriptorSet ds = material->nativeSets[frameIndex];
    if (ds != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getPipelineLayout(),
                                1, 1, &ds, 0, nullptr);
    }
}

void ForwardPass::pushModelMatrix(VkCommandBuffer cmd, const glm::mat4& model) {
    PushConstantData pushData{};
    pushData.model = model;
    pushData.normalMatrix = glm::transpose(glm::inverse(model));

    vkCmdPushConstants(cmd, getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(PushConstantData), &pushData);
}

void ForwardPass::drawMesh(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkBuffer indexBuffer,
                            uint32_t indexCount) {
    if (vertexBuffer == VK_NULL_HANDLE || indexBuffer == VK_NULL_HANDLE) return;

    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}
