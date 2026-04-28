#include "WaterPass.h"
#include "GBufferPass.h"
#include "VulkanDevice.h"
#include "Mesh.h"
#include "MeshManager.h"
#include "Entity.h"
#include "Components.h"

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
#include <array>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

// =============================================================================
// Constructor / Destructor
// =============================================================================

WaterPass::WaterPass(std::shared_ptr<VulkanDevice> device, RHIDevice* rhiDevice,
                     uint32_t width, uint32_t height, VkRenderPass externalRenderPass)
    : RenderPassBase(device, width, height)
    , rhiDevice_(rhiDevice)
    , vulkanDevice_(device)
    , width_(width)
    , height_(height)
    , externalRenderPass_(externalRenderPass)
{
    passName = "Water Pass (Integrated SSR)";

    createWaterMesh();
    createVertexAndIndexBuffers();
    createBindingLayout();
    createUniformBuffers();
    createDescriptorSets();
    createPipeline();

    std::cout << "WaterPass created with integrated SSR (RHI): " << width_ << "x" << height_ << std::endl;
}

WaterPass::~WaterPass() {
    cleanup();
}

void WaterPass::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();

    descriptorSets_.clear();
    uniformBuffers_.clear();
    pipeline_.reset();
    bindingLayout_.reset();
    indexBuffer_.reset();
    vertexBuffer_.reset();
    waterMesh.reset();
}

void WaterPass::resize(uint32_t newWidth, uint32_t newHeight) {
    width_ = newWidth;
    height_ = newHeight;
}

void WaterPass::setWaterColor(const glm::vec3& color, float alpha) {
    waterColor = color;
    waterAlpha = alpha;
}

// =============================================================================
// Water Mesh Generation
// =============================================================================

void WaterPass::createWaterMesh() {
    waterMesh = std::make_unique<Mesh>();

    float size = 50.0f;
    int resolution = 32;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float step = size / resolution;
    float halfSize = size / 2.0f;

    for (int z = 0; z <= resolution; z++) {
        for (int x = 0; x <= resolution; x++) {
            Vertex vertex;
            vertex.pos = glm::vec3(-halfSize + x * step, waterHeight, -halfSize + z * step);
            vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            vertex.texCoord = glm::vec2(
                static_cast<float>(x) / resolution,
                static_cast<float>(z) / resolution
            );
            vertices.push_back(vertex);
        }
    }

    for (int z = 0; z < resolution; z++) {
        for (int x = 0; x < resolution; x++) {
            int topLeft = z * (resolution + 1) + x;
            int topRight = topLeft + 1;
            int bottomLeft = (z + 1) * (resolution + 1) + x;
            int bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    waterMesh->setVertices(vertices);
    waterMesh->setIndices(indices);
    indexCount_ = static_cast<uint32_t>(indices.size());
}

void WaterPass::createVertexAndIndexBuffers() {
    const auto& vertices = waterMesh->getVertices();
    const auto& indices = waterMesh->getIndices();

    // Vertex buffer
    {
        RHIBufferDesc desc{};
        desc.size = sizeof(Vertex) * vertices.size();
        desc.usage = RHIBufferUsage::Vertex;
        desc.memoryUsage = RHIMemoryUsage::GPUOnly;
        vertexBuffer_ = rhiDevice_->createBuffer(desc);
        vertexBuffer_->uploadData(vertices.data(), desc.size);
    }

    // Index buffer
    {
        RHIBufferDesc desc{};
        desc.size = sizeof(uint32_t) * indices.size();
        desc.usage = RHIBufferUsage::Index;
        desc.memoryUsage = RHIMemoryUsage::GPUOnly;
        indexBuffer_ = rhiDevice_->createBuffer(desc);
        indexBuffer_->uploadData(indices.data(), desc.size);
    }
}

// =============================================================================
// RHI Resource Creation
// =============================================================================

void WaterPass::createBindingLayout() {
    // binding 0: Water UBO (Vertex + Fragment)
    // binding 1-4: G-Buffer textures + Scene Color (Fragment)
    RHIBindingLayoutDesc desc;
    desc.entries.push_back({
        0,                    // Binding point
        RHIDescriptorType::UniformBuffer,
        RHIShaderStage::Vertex | RHIShaderStage::Fragment,
        1
    });
    for (uint32_t i = 1; i <= 4; ++i) {
        desc.entries.push_back({
            i,
            RHIDescriptorType::CombinedImageSampler,
            RHIShaderStage::Fragment,
            1
        });
    }
    bindingLayout_ = rhiDevice_->createBindingLayout(desc);
}

void WaterPass::createUniformBuffers() {
    uniformBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        RHIBufferDesc desc{};
        desc.size = sizeof(WaterUBO);
        desc.usage = RHIBufferUsage::Uniform;
        desc.memoryUsage = RHIMemoryUsage::CPUToGPU;
        uniformBuffers_[i] = rhiDevice_->createBuffer(desc);
    }
}

void WaterPass::createDescriptorSets() {
    auto* vkDevice = static_cast<VulkanRHIDevice*>(rhiDevice_);
    auto* vkLayout = static_cast<VulkanRHIBindingLayout*>(bindingLayout_.get());

    descriptorSets_.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        descriptorSets_[i] = vkDevice->allocateDescriptorSet(vkLayout->getVkDescriptorSetLayout());
    }

    // Initialize UBO binding for each frame
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto* vkUBO = static_cast<VulkanRHIBuffer*>(uniformBuffers_[i].get());
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = vkUBO->getVkBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(WaterUBO);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets_[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(vkDevice->getVkDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

void WaterPass::createPipeline() {
    constexpr uint32_t vertexStride = sizeof(Vertex);

    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    auto* vkBuilder = static_cast<VulkanGraphicsPipelineBuilder*>(builder.get());

    // Alpha blend attachment
    RHIColorBlendAttachment blendAttachment{};
    blendAttachment.blendEnable = true;
    blendAttachment.srcColorFactor = RHIBlendFactor::SrcAlpha;
    blendAttachment.dstColorFactor = RHIBlendFactor::OneMinusSrcAlpha;
    blendAttachment.colorBlendOp = RHIBlendOp::Add;
    blendAttachment.srcAlphaFactor = RHIBlendFactor::One;
    blendAttachment.dstAlphaFactor = RHIBlendFactor::Zero;
    blendAttachment.alphaBlendOp = RHIBlendOp::Add;

    // setNativeRenderPass is VulkanGraphicsPipelineBuilder-specific, call it first
    vkBuilder->setNativeRenderPass(externalRenderPass_, 0);

    vkBuilder
        ->setVertexShader("shaders/water_vert.spv")
        .setFragmentShader("shaders/water_frag.spv")
        .addVertexBinding(0, vertexStride, RHIVertexInputRate::Vertex)
        .addVertexAttribute(0, 0, RHIFormat::R32G32B32_SFLOAT, offsetof(Vertex, pos))
        .addVertexAttribute(0, 1, RHIFormat::R32G32B32_SFLOAT, offsetof(Vertex, normal))
        .addVertexAttribute(0, 2, RHIFormat::R32G32_SFLOAT,    offsetof(Vertex, texCoord))
        .addVertexAttribute(0, 3, RHIFormat::R32G32B32_SFLOAT, offsetof(Vertex, tangent))
        .setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::None)  // 双面渲染水面
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(false, false, RHICompareOp::Less)
        .setSampleCount(RHISampleCount::Count1)
        .addColorBlendAttachment(blendAttachment)
        .addBindingLayout(bindingLayout_.get());

    pipeline_ = vkBuilder->build();

    std::cout << "Water pipeline created (RHI Pipeline Builder)" << std::endl;
}

// =============================================================================
// UBO Update
// =============================================================================

void WaterPass::updateUniforms(const glm::mat4& view, const glm::mat4& projection,
                                const glm::vec3& cameraPos, float time, uint32_t frameIndex) {
    WaterUBO ubo{};

    ubo.model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, waterHeight, 0.0f));
    ubo.view = view;
    ubo.projection = projection;
    ubo.invView = glm::inverse(view);
    ubo.invProjection = glm::inverse(projection);
    ubo.cameraPos = glm::vec4(cameraPos, 1.0f);
    ubo.waterColor = glm::vec4(waterColor, waterAlpha);
    ubo.waterParams = glm::vec4(waveSpeed, waveStrength, time, refractionStrength);
    ubo.screenSize = glm::vec4(width_, height_, 0.1f, 100.0f);
    ubo.ssrParams = glm::vec4(ssrMaxDistance, ssrMaxSteps, ssrThickness, 0.0f);

    void* ptr = uniformBuffers_[frameIndex]->map();
    memcpy(ptr, &ubo, sizeof(WaterUBO));
    uniformBuffers_[frameIndex]->unmap();
}

// =============================================================================
// Descriptor Set Update (Hybrid — G-Buffer textures via native Vulkan)
// =============================================================================

void WaterPass::updateDescriptorSets(GBufferPass* gbuffer, VkImageView sceneColorView, VkSampler sampler) {
    auto* vkDevice = static_cast<VulkanRHIDevice*>(rhiDevice_);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        std::array<VkDescriptorImageInfo, 4> imageInfos{};

        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[0].imageView = gbuffer->getPositionView();
        imageInfos[0].sampler = sampler;

        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].imageView = gbuffer->getNormalView();
        imageInfos[1].sampler = sampler;

        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfos[2].imageView = gbuffer->getDepthView();
        imageInfos[2].sampler = sampler;

        imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[3].imageView = sceneColorView;
        imageInfos[3].sampler = sampler;

        std::array<VkWriteDescriptorSet, 4> descriptorWrites{};

        for (int j = 0; j < 4; ++j) {
            descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j].dstSet = descriptorSets_[i];
            descriptorWrites[j].dstBinding = j + 1;  // 从binding 1 开始
            descriptorWrites[j].dstArrayElement = 0;
            descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[j].descriptorCount = 1;
            descriptorWrites[j].pImageInfo = &imageInfos[j];
        }

        vkUpdateDescriptorSets(vkDevice->getVkDevice(),
                               static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }
}

// =============================================================================
// Render
// =============================================================================

void WaterPass::render(VkCommandBuffer cmd, uint32_t frameIndex) {
    auto* vkPipeline = static_cast<VulkanRHIPipeline*>(pipeline_.get());
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->getVkPipeline());

    VkBuffer vb;
    VkBuffer ib;
    uint32_t drawIndexCount;

    if (useExternalMesh && externalMesh && externalMesh->isValid()) {
        vb = externalMesh->getVertexBufferHandle();
        ib = externalMesh->getIndexBufferHandle();
        drawIndexCount = externalMesh->getIndexCount();
    } else {
        auto* vkVB = static_cast<VulkanRHIBuffer*>(vertexBuffer_.get());
        auto* vkIB = static_cast<VulkanRHIBuffer*>(indexBuffer_.get());
        vb = vkVB->getVkBuffer();
        ib = vkIB->getVkBuffer();
        drawIndexCount = indexCount_;
    }

    VkBuffer vertexBuffers[] = { vb };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vkPipeline->getVkPipelineLayout(),
                            0, 1, &descriptorSets_[frameIndex], 0, nullptr);

    vkCmdDrawIndexed(cmd, drawIndexCount, 1, 0, 0, 0);
}

// =============================================================================
// External Mesh
// =============================================================================

bool WaterPass::setWaterEntity(const VulkanEngine::Entity& entity) {
    if (!entity) {
        std::cerr << "[WaterPass] Invalid entity provided!" << std::endl;
        return false;
    }

    if (!entity.hasComponent<VulkanEngine::MeshRendererComponent>()) {
        std::cerr << "[WaterPass] Entity does not have MeshRendererComponent!" << std::endl;
        return false;
    }

    const auto& meshRenderer = entity.getComponent<VulkanEngine::MeshRendererComponent>();

    if (meshRenderer.meshPath.empty()) {
        std::cerr << "[WaterPass] MeshRendererComponent has empty meshPath!" << std::endl;
        return false;
    }

    auto gpuMesh = VulkanEngine::MeshManager::getInstance().getMesh(meshRenderer.meshPath);

    if (!gpuMesh || !gpuMesh->isValid()) {
        std::cerr << "[WaterPass] Failed to get mesh: " << meshRenderer.meshPath << std::endl;
        return false;
    }

    return setWaterMesh(gpuMesh);
}

bool WaterPass::setWaterMesh(std::shared_ptr<VulkanEngine::GPUMesh> gpuMesh) {
    if (!gpuMesh || !gpuMesh->isValid()) {
        std::cerr << "[WaterPass] Invalid GPUMesh provided!" << std::endl;
        return false;
    }

    externalMesh = gpuMesh;
    useExternalMesh = true;

    std::cout << "[WaterPass] Using external mesh with "
              << gpuMesh->getVertexCount() << " vertices, "
              << gpuMesh->getIndexCount() << " indices" << std::endl;

    return true;
}

void WaterPass::clearExternalMesh() {
    externalMesh.reset();
    useExternalMesh = false;
    std::cout << "[WaterPass] Cleared external mesh, using built-in water mesh" << std::endl;
}