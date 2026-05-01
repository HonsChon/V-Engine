#include "LightingPass.h"

// Pure RHI headers — NO Vulkan backend headers
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIRenderPass.h"
#include "RHICommandBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"

#include <stdexcept>
#include <iostream>
#include <cstring>

// =============================================================================
// Constructor / Destructor
// =============================================================================

LightingPass::LightingPass(RHIDevice* rhiDevice,
                           uint32_t width, uint32_t height,
                           RHIRenderPass* externalRenderPass, uint32_t maxFramesInFlight)
    : RenderPassBase(rhiDevice, width, height)
    , rhiDevice_(rhiDevice)
    , width_(width)
    , height_(height)
    , maxFramesInFlight_(maxFramesInFlight)
    , externalRenderPass_(externalRenderPass)
{
    passName = "Lighting Pass";

    createBindingLayout();
    createUniformBuffers();
    createBindingGroups();
    createPipeline();
    createFullscreenQuad();

    std::cout << "LightingPass created (Pure RHI)" << std::endl;
}

LightingPass::~LightingPass() {
    cleanup();
}

void LightingPass::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();

    quadVertexBuffer_.reset();
    quadIndexBuffer_.reset();
    uniformBuffers_.clear();
    bindingGroups_.clear();
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
    // binding 1-3: G-Buffer textures (Position, Normal, Albedo)
    desc.entries.push_back({1, RHIDescriptorType::CombinedImageSampler, RHIShaderStage::Fragment, 1});
    desc.entries.push_back({2, RHIDescriptorType::CombinedImageSampler, RHIShaderStage::Fragment, 1});
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
    bindingGroups_.resize(maxFramesInFlight_);
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        // Allocate empty binding group, then set UBO
        bindingGroups_[i] = rhiDevice_->allocateBindingGroup(bindingLayout_.get());
        bindingGroups_[i]->updateBuffer(0, uniformBuffers_[i].get(), 0, sizeof(LightingUBO));
    }
}

void LightingPass::createPipeline() {
    constexpr uint32_t vertexStride = sizeof(float) * 4;

    auto builder = rhiDevice_->createGraphicsPipelineBuilder();

    builder->setVertexShader("shaders/deferred_lighting_vert.spv")
        .setFragmentShader("shaders/deferred_lighting_frag.spv")
        .addVertexBinding(0, vertexStride, RHIVertexInputRate::Vertex)
        .addVertexAttribute(0, 0, RHIFormat::R32G32_SFLOAT, 0)
        .addVertexAttribute(0, 1, RHIFormat::R32G32_SFLOAT, sizeof(float) * 2)
        .setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::None)
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(false, false)
        .setSampleCount(RHISampleCount::Count1)
        .addBindingLayout(bindingLayout_.get())
        .setRenderPass(externalRenderPass_);

    pipeline_ = builder->build();

    std::cout << "LightingPass pipeline created (Pure RHI)" << std::endl;
}

void LightingPass::createFullscreenQuad() {
    float quadVertices[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f
    };

    uint32_t quadIndices[] = { 0, 1, 2, 2, 3, 0 };

    {
        RHIBufferDesc desc{};
        desc.size = sizeof(quadVertices);
        desc.usage = RHIBufferUsage::Vertex;
        desc.memoryUsage = RHIMemoryUsage::GPUOnly;
        quadVertexBuffer_ = rhiDevice_->createBuffer(desc);
        quadVertexBuffer_->uploadData(quadVertices, sizeof(quadVertices));
    }
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
// GBuffer / SSAO Input (Pure RHI)
// =============================================================================

void LightingPass::setGBufferInputs(RHITexture* position, RHITexture* normal,
                                     RHITexture* albedo, RHISampler* sampler) {
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        bindingGroups_[i]->updateTexture(1, position, sampler);
        bindingGroups_[i]->updateTexture(2, normal, sampler);
        bindingGroups_[i]->updateTexture(3, albedo, sampler);
    }
}

void LightingPass::setSSAOTexture(RHITexture* ssaoTexture, RHISampler* ssaoSampler) {
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        bindingGroups_[i]->updateTexture(4, ssaoTexture, ssaoSampler);
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
// Render Commands (Pure RHI)
// =============================================================================

void LightingPass::render(RHICommandBuffer* cmd, uint32_t frameIndex) {
    cmd->setViewport(0, 0, static_cast<float>(width_), static_cast<float>(height_));
    cmd->setScissor(0, 0, width_, height_);

    cmd->bindGraphicsPipeline(pipeline_.get());

    if (frameIndex < bindingGroups_.size()) {
        cmd->setBindingGroup(0, bindingGroups_[frameIndex].get());
    }

    cmd->bindVertexBuffer(0, quadVertexBuffer_.get());
    cmd->bindIndexBuffer(quadIndexBuffer_.get(), 0, RHIIndexType::UInt32);

    cmd->drawIndexed(6);
}