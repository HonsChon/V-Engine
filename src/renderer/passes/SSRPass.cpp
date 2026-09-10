#include "SSRPass.h"
#include "GBufferPass.h"

// Pure RHI headers — NO Vulkan backend headers
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIRenderPass.h"
#include "RHICommandBuffer.h"

#include <stdexcept>
#include <iostream>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>

// =============================================================================
// Constructor / Destructor
// =============================================================================

SSRPass::SSRPass(RHIDevice* rhiDevice,
                 uint32_t width, uint32_t height)
    : RenderPassBase(rhiDevice, width, height)
    , rhiDevice_(rhiDevice)
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
    createBindingGroups();
    createPipeline();

    std::cout << "SSRPass created (Pure RHI): " << width_ << "x" << height_ << std::endl;
}

SSRPass::~SSRPass() {
    cleanup();
}

void SSRPass::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();
    bindingGroups_.clear();
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

    framebuffer_.reset();
    outputTexture_.reset();
    outputSampler_.reset();

    width_ = newWidth;
    height_ = newHeight;
    params.screenSize = glm::vec4(width_, height_, 1.0f / width_, 1.0f / height_);

    createOutputTexture();
    createOutputSampler();
    createRHIFramebuffer();

    std::cout << "SSRPass resized (Pure RHI): " << width_ << "x" << height_ << std::endl;
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
        RHILoadOp::Clear, RHIStoreOp::Store,
        RHIImageLayout::Undefined, RHIImageLayout::ShaderReadOnly
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
    RHIBindingLayoutDesc desc;
    // binding 0-4: CombinedImageSampler (Position, Normal, Albedo, Depth, SceneColor)
    for (uint32_t i = 0; i < 5; ++i) {
        desc.entries.push_back({i, RHIDescriptorType::CombinedImageSampler, RHIShaderStage::Fragment, 1});
    }
    // binding 5: UBO
    desc.entries.push_back({5, RHIDescriptorType::UniformBuffer, RHIShaderStage::Fragment, 1});
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

void SSRPass::createBindingGroups() {
    bindingGroups_.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        bindingGroups_[i] = rhiDevice_->allocateBindingGroup(bindingLayout_.get());
        bindingGroups_[i]->updateBuffer(5, uniformBuffers_[i].get(), 0, sizeof(SSRParams));
    }
}

void SSRPass::createPipeline() {
    auto builder = rhiDevice_->createGraphicsPipelineBuilder();

    builder->setVertexShader("shaders/ssr_vert.spv")
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

    pipeline_ = builder->build();
    std::cout << "SSR pipeline created (Pure RHI)" << std::endl;
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
// Execute (Pure RHI)
// =============================================================================

void SSRPass::execute(RHICommandBuffer* cmd, GBufferPass* gbuffer,
                      RHITexture* sceneColorTexture, RHISampler* sceneColorSampler,
                      uint32_t frameIndex) {
    // Update texture bindings for this frame
    bindingGroups_[frameIndex]->updateTexture(0, gbuffer->getPositionTexture(), gbuffer->getRHISampler());
    bindingGroups_[frameIndex]->updateTexture(1, gbuffer->getNormalTexture(), gbuffer->getRHISampler());
    bindingGroups_[frameIndex]->updateTexture(2, gbuffer->getAlbedoTexture(), gbuffer->getRHISampler());
    bindingGroups_[frameIndex]->updateTexture(3, gbuffer->getDepthTexture(), gbuffer->getRHISampler());
    bindingGroups_[frameIndex]->updateTexture(4, sceneColorTexture, sceneColorSampler);

    // Begin render pass
    cmd->beginRenderPass(renderPass_.get(), framebuffer_.get(),
                         {RHIClearValue::Color(0.0f, 0.0f, 0.0f, 0.0f)});

    cmd->setViewport(0, 0, static_cast<float>(width_), static_cast<float>(height_));
    cmd->setScissor(0, 0, width_, height_);

    cmd->bindGraphicsPipeline(pipeline_.get());
    cmd->setBindingGroup(0, bindingGroups_[frameIndex].get());

    // Fullscreen triangle
    cmd->draw(3);

    cmd->endRenderPass();
}

