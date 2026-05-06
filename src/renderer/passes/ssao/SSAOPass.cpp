#include "SSAOPass.h"
#include "GBufferPass.h"

// Pure RHI headers — NO Vulkan includes
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIRenderPass.h"
#include "RHICommandBuffer.h"

#include <iostream>
#include <cstring>
#include <random>
#include <cmath>

// ============================================================
// Constructor / Destructor
// ============================================================

SSAOPass::SSAOPass(RHIDevice* rhiDevice,
                   uint32_t width, uint32_t height)
    : RenderPassBase(rhiDevice, width, height)
    , rhiDevice_(rhiDevice)
{
    passName = "SSAO Pass";
    init();
    std::cout << "[SSAOPass] Created (Pure RHI, " << width << "x" << height << ")\n";
}

SSAOPass::~SSAOPass() {
    cleanup();
}

void SSAOPass::init() {
    m_subWidth = (width + DEINTERLEAVE_FACTOR - 1) / DEINTERLEAVE_FACTOR;
    m_subHeight = (height + DEINTERLEAVE_FACTOR - 1) / DEINTERLEAVE_FACTOR;

    generateKernel();
    generateLayerRotations();
    createSamplers();
    createDeinterleavedTextures();
    createAOTextures();
    createDeinterleaveResources();
    createSSAOResources();
    createReinterleaveResources();
    createBlurResources();
}

void SSAOPass::cleanup() {
    if (!rhiDevice_) return;
    rhiDevice_->waitIdle();

    // Blur
    m_blurPipeline.reset();
    m_blurGroup.reset();
    m_blurLayout.reset();
    m_blurFramebuffer.reset();
    m_blurRenderPass.reset();

    // Reinterleave
    m_reinterleavePipeline.reset();
    m_reinterleaveGroup.reset();
    m_reinterleaveLayout.reset();

    // SSAO
    m_ssaoPipeline.reset();
    for (auto& g : m_ssaoGroups) g.reset();
    for (auto& u : m_ssaoUBOs) u.reset();
    m_ssaoLayout.reset();
    for (auto& fb : m_ssaoFramebuffers) fb.reset();
    m_ssaoRenderPass.reset();

    // Deinterleave
    m_deinterleavePipeline.reset();
    m_deinterleaveGroup.reset();
    m_deinterleaveLayout.reset();

    // Textures
    for (auto& v : m_aoLayerViews) v.reset();
    m_blurredAOTex.reset();
    m_fullAOTex.reset();
    m_aoArrayTex.reset();
    m_deinterleavedNorTex.reset();
    m_deinterleavedPosTex.reset();

    // Samplers
    m_aoSampler.reset();
    m_deinterleaveSampler.reset();
}

void SSAOPass::resize(uint32_t newWidth, uint32_t newHeight) {
    cleanup();
    width = newWidth;
    height = newHeight;
    init();
    std::cout << "[SSAOPass] Resized to " << newWidth << "x" << newHeight << "\n";
}

void SSAOPass::updateSettings(const SSAOSettings& settings) {
    m_settings = settings;
}

// ============================================================
// Kernel & Rotation Generation (same logic, pure CPU)
// ============================================================

void SSAOPass::generateKernel() {
    std::default_random_engine gen(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < KERNEL_SIZE; ++i) {
        // Alchemy AO disk directions
        float angle = 2.0f * 3.14159265f * dist(gen);
        float radius = dist(gen);
        // Scale: more samples closer to center
        float scale = static_cast<float>(i) / static_cast<float>(KERNEL_SIZE);
        scale = 0.1f + scale * scale * 0.9f;
        m_kernel[i] = glm::vec4(std::cos(angle), std::sin(angle), scale, 0.0f);
    }
}

void SSAOPass::generateLayerRotations() {
    for (int i = 0; i < NUM_LAYERS; ++i) {
        m_layerRotations[i] = static_cast<float>(i) * (2.0f * 3.14159265f / NUM_LAYERS);
    }
}

// ============================================================
// Resource Creation
// ============================================================

void SSAOPass::createSamplers() {
    RHISamplerDesc sd{};
    sd.minFilter = RHIFilter::Linear;
    sd.magFilter = RHIFilter::Linear;
    sd.addressModeU = RHIAddressMode::ClampToEdge;
    sd.addressModeV = RHIAddressMode::ClampToEdge;
    m_aoSampler = rhiDevice_->createSampler(sd);
    m_deinterleaveSampler = rhiDevice_->createSampler(sd);
}

void SSAOPass::createDeinterleavedTextures() {
    // Position array: 16 layers, R16G16B16A16_SFLOAT
    RHITextureDesc desc{};
    desc.width = m_subWidth;
    desc.height = m_subHeight;
    desc.arrayLayers = NUM_LAYERS;
    desc.format = RHIFormat::R16G16B16A16_SFLOAT;
    desc.usage = RHITextureUsage::Sampled | RHITextureUsage::Storage;
    m_deinterleavedPosTex = rhiDevice_->createTexture(desc);

    // Normal array: same format
    m_deinterleavedNorTex = rhiDevice_->createTexture(desc);
}

void SSAOPass::createAOTextures() {
    // AO array: 16 layers, R8_UNORM (render target + sampled)
    RHITextureDesc desc{};
    desc.width = m_subWidth;
    desc.height = m_subHeight;
    desc.arrayLayers = NUM_LAYERS;
    desc.format = RHIFormat::R8_UNORM;
    desc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled;
    m_aoArrayTex = rhiDevice_->createTexture(desc);

    // Per-layer views for framebuffers
    for (uint32_t i = 0; i < NUM_LAYERS; ++i) {
        m_aoLayerViews[i] = m_aoArrayTex->createLayerView(i);
    }

    // Full resolution AO (reinterleaved result)
    RHITextureDesc fullDesc{};
    fullDesc.width = width;
    fullDesc.height = height;
    fullDesc.format = RHIFormat::R8_UNORM;
    fullDesc.usage = RHITextureUsage::Sampled | RHITextureUsage::Storage;
    m_fullAOTex = rhiDevice_->createTexture(fullDesc);

    // Blurred AO (final output)
    RHITextureDesc blurDesc{};
    blurDesc.width = width;
    blurDesc.height = height;
    blurDesc.format = RHIFormat::R8_UNORM;
    blurDesc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled | RHITextureUsage::TransferDst;
    m_blurredAOTex = rhiDevice_->createTexture(blurDesc);
}

// ============================================================
// Deinterleave Resources (Compute)
// ============================================================

void SSAOPass::createDeinterleaveResources() {
    // Layout: binding 0,1 = sampled images (position, normal from GBuffer)
    //         binding 2,3 = storage images (output arrays)
    RHIBindingLayoutDesc layoutDesc;
    layoutDesc.entries.push_back({0, RHIDescriptorType::CombinedImageSampler,
                                  RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({1, RHIDescriptorType::CombinedImageSampler,
                                  RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({2, RHIDescriptorType::StorageImage,
                                  RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({3, RHIDescriptorType::StorageImage,
                                  RHIShaderStage::Compute, 1});
    m_deinterleaveLayout = rhiDevice_->createBindingLayout(layoutDesc);
    m_deinterleaveGroup = rhiDevice_->allocateBindingGroup(m_deinterleaveLayout.get());

    // Update storage image bindings (deinterleaved outputs — always the same)
    m_deinterleaveGroup->updateTexture(2, m_deinterleavedPosTex.get(), nullptr);
    m_deinterleaveGroup->updateTexture(3, m_deinterleavedNorTex.get(), nullptr);

    // Compute pipeline
    auto builder = rhiDevice_->createComputePipelineBuilder();
    builder->setComputeShader("shaders/ssao/ssao_deinterleave_comp.spv")
        .addBindingLayout(m_deinterleaveLayout.get())
        .addPushConstant(RHIShaderStage::Compute, 0, sizeof(DeinterleavePushConstants));
    m_deinterleavePipeline = builder->build();

    std::cout << "[SSAOPass] Deinterleave pipeline created\n";
}

// ============================================================
// SSAO Resources (Graphics, per-layer)
// ============================================================

void SSAOPass::createSSAOResources() {
    // Render pass: single R8_UNORM color attachment
    RHIRenderPassDesc rpDesc;
    rpDesc.addColorAttachment(RHIFormat::R8_UNORM, RHILoadOp::Clear, RHIStoreOp::Store,
                              RHIImageLayout::Undefined, RHIImageLayout::ShaderReadOnly);
    m_ssaoRenderPass = rhiDevice_->createRenderPass(rpDesc);

    // Per-layer framebuffers
    for (uint32_t i = 0; i < NUM_LAYERS; ++i) {
        RHIFramebufferDesc fbDesc{};
        fbDesc.renderPass = m_ssaoRenderPass.get();
        fbDesc.attachments.push_back(m_aoLayerViews[i].get());
        fbDesc.width = m_subWidth;
        fbDesc.height = m_subHeight;
        m_ssaoFramebuffers[i] = rhiDevice_->createFramebuffer(fbDesc);
    }

    // Binding layout: binding 0 = UBO, binding 1 = deinterleaved position (array sampler),
    //                 binding 2 = deinterleaved normal (array sampler)
    RHIBindingLayoutDesc layoutDesc;
    layoutDesc.entries.push_back({0, RHIDescriptorType::UniformBuffer,
                                  RHIShaderStage::Fragment, 1});
    layoutDesc.entries.push_back({1, RHIDescriptorType::CombinedImageSampler,
                                  RHIShaderStage::Fragment, 1});
    layoutDesc.entries.push_back({2, RHIDescriptorType::CombinedImageSampler,
                                  RHIShaderStage::Fragment, 1});
    m_ssaoLayout = rhiDevice_->createBindingLayout(layoutDesc);

    // UBOs and binding groups (per frame)
    for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f) {
        RHIBufferDesc bufDesc{};
        bufDesc.size = sizeof(SSAOParamsUBO);
        bufDesc.usage = RHIBufferUsage::Uniform;
        bufDesc.memoryUsage = RHIMemoryUsage::CPUToGPU;
        m_ssaoUBOs[f] = rhiDevice_->createBuffer(bufDesc);

        m_ssaoGroups[f] = rhiDevice_->allocateBindingGroup(m_ssaoLayout.get());
        m_ssaoGroups[f]->updateBuffer(0, m_ssaoUBOs[f].get(), 0, sizeof(SSAOParamsUBO));
        m_ssaoGroups[f]->updateTexture(1, m_deinterleavedPosTex.get(), m_deinterleaveSampler.get());
        m_ssaoGroups[f]->updateTexture(2, m_deinterleavedNorTex.get(), m_deinterleaveSampler.get());
    }

    // Graphics pipeline (fullscreen triangle, no vertex buffer)
    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    builder->setVertexShader("shaders/ssao/ssao_vert.spv")
        .setFragmentShader("shaders/ssao/ssao_frag.spv")
        .setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::None)
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(false, false, RHICompareOp::Always)
        .setSampleCount(RHISampleCount::Count1)
        .setColorAttachmentCount(1)
        .addBindingLayout(m_ssaoLayout.get())
        .addPushConstant(RHIShaderStage::Fragment, 0, sizeof(SSAOPushConstants))
        .setRenderPass(m_ssaoRenderPass.get());
    m_ssaoPipeline = builder->build();

    std::cout << "[SSAOPass] SSAO pipeline created (16 framebuffers)\n";
}

// ============================================================
// Reinterleave Resources (Compute)
// ============================================================

void SSAOPass::createReinterleaveResources() {
    // Layout: binding 0 = AO array (sampled), binding 1 = full AO (storage out)
    RHIBindingLayoutDesc layoutDesc;
    layoutDesc.entries.push_back({0, RHIDescriptorType::CombinedImageSampler,
                                  RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({1, RHIDescriptorType::StorageImage,
                                  RHIShaderStage::Compute, 1});
    m_reinterleaveLayout = rhiDevice_->createBindingLayout(layoutDesc);
    m_reinterleaveGroup = rhiDevice_->allocateBindingGroup(m_reinterleaveLayout.get());

    m_reinterleaveGroup->updateTexture(0, m_aoArrayTex.get(), m_aoSampler.get());
    m_reinterleaveGroup->updateTexture(1, m_fullAOTex.get(), nullptr);

    // Compute pipeline
    auto builder = rhiDevice_->createComputePipelineBuilder();
    builder->setComputeShader("shaders/ssao/ssao_reinterleave_comp.spv")
        .addBindingLayout(m_reinterleaveLayout.get())
        .addPushConstant(RHIShaderStage::Compute, 0, sizeof(ReinterleavePushConstants));
    m_reinterleavePipeline = builder->build();

    std::cout << "[SSAOPass] Reinterleave pipeline created\n";
}

// ============================================================
// Blur Resources (Graphics, fullscreen)
// ============================================================

void SSAOPass::createBlurResources() {
    // Render pass: single R8_UNORM color attachment
    RHIRenderPassDesc rpDesc;
    rpDesc.addColorAttachment(RHIFormat::R8_UNORM, RHILoadOp::Clear, RHIStoreOp::Store,
                              RHIImageLayout::Undefined, RHIImageLayout::ShaderReadOnly);
    m_blurRenderPass = rhiDevice_->createRenderPass(rpDesc);

    // Framebuffer
    RHIFramebufferDesc fbDesc{};
    fbDesc.renderPass = m_blurRenderPass.get();
    fbDesc.attachments.push_back(m_blurredAOTex.get());
    fbDesc.width = width;
    fbDesc.height = height;
    m_blurFramebuffer = rhiDevice_->createFramebuffer(fbDesc);

    // Binding layout: binding 0 = full AO input (sampled)
    RHIBindingLayoutDesc layoutDesc;
    layoutDesc.entries.push_back({0, RHIDescriptorType::CombinedImageSampler,
                                  RHIShaderStage::Fragment, 1});
    m_blurLayout = rhiDevice_->createBindingLayout(layoutDesc);
    m_blurGroup = rhiDevice_->allocateBindingGroup(m_blurLayout.get());
    m_blurGroup->updateTexture(0, m_fullAOTex.get(), m_aoSampler.get());

    // Graphics pipeline
    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    builder->setVertexShader("shaders/ssao/ssao_blur_vert.spv")
        .setFragmentShader("shaders/ssao/ssao_blur_frag.spv")
        .setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::None)
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(false, false, RHICompareOp::Always)
        .setSampleCount(RHISampleCount::Count1)
        .setColorAttachmentCount(1)
        .addBindingLayout(m_blurLayout.get())
        .setRenderPass(m_blurRenderPass.get());
    m_blurPipeline = builder->build();

    std::cout << "[SSAOPass] Blur pipeline created\n";
}

// ============================================================
// Execute (Pure RHI)
// ============================================================

void SSAOPass::execute(RHICommandBuffer* cmd, GBufferPass* gbuffer, uint32_t frameIndex,
                       const glm::mat4& projection, const glm::mat4& view) {
    if (!gbuffer || !cmd) return;

    // Update UBO
    SSAOParamsUBO ubo{};
    for (int i = 0; i < KERNEL_SIZE; ++i) ubo.samples[i] = m_kernel[i];
    ubo.projection = projection;
    ubo.view = view;
    ubo.radius = m_settings.radius;
    ubo.bias = m_settings.bias;
    ubo.power = m_settings.power;
    ubo.amount = m_settings.amount;
    ubo.kernelSize = m_settings.kernelSize;

    void* mapped = m_ssaoUBOs[frameIndex]->map();
    memcpy(mapped, &ubo, sizeof(ubo));
    m_ssaoUBOs[frameIndex]->unmap();

    // Stage 1: Deinterleave (compute)
    executeDeinterleave(cmd, gbuffer);

    // Stage 2: SSAO (graphics, 16 layers)
    executeSSAO(cmd, frameIndex);

    // Stage 3: Reinterleave (compute)
    executeReinterleave(cmd);

    // Stage 4: Blur (graphics)
    executeBlur(cmd);
}

void SSAOPass::executeDeinterleave(RHICommandBuffer* cmd, GBufferPass* gbuffer) {
    // Update deinterleave group with current GBuffer textures
    m_deinterleaveGroup->updateTexture(0, gbuffer->getPositionTexture(), m_aoSampler.get());
    m_deinterleaveGroup->updateTexture(1, gbuffer->getNormalTexture(), m_aoSampler.get());

    // Transition deinterleaved outputs to GENERAL for compute write
    cmd->transitionImageLayout(m_deinterleavedPosTex.get(),
        RHIImageLayout::Undefined, RHIImageLayout::General,
        RHIPipelineStage::TopOfPipe, RHIPipelineStage::ComputeShader);
    cmd->transitionImageLayout(m_deinterleavedNorTex.get(),
        RHIImageLayout::Undefined, RHIImageLayout::General,
        RHIPipelineStage::TopOfPipe, RHIPipelineStage::ComputeShader);

    // Dispatch deinterleave compute
    cmd->bindComputePipeline(m_deinterleavePipeline.get());
    cmd->setBindingGroup(0, m_deinterleaveGroup.get());

    DeinterleavePushConstants pc{};
    pc.fullWidth = static_cast<int>(width);
    pc.fullHeight = static_cast<int>(height);
    cmd->pushConstants(RHIShaderStage::Compute, 0, sizeof(pc), &pc);

    uint32_t groupsX = (m_subWidth + 7) / 8;
    uint32_t groupsY = (m_subHeight + 7) / 8;
    cmd->dispatch(groupsX, groupsY, NUM_LAYERS);

    // Transition deinterleaved to SHADER_READ for SSAO fragment shader
    cmd->transitionImageLayout(m_deinterleavedPosTex.get(),
        RHIImageLayout::General, RHIImageLayout::ShaderReadOnly,
        RHIPipelineStage::ComputeShader, RHIPipelineStage::FragmentShader);
    cmd->transitionImageLayout(m_deinterleavedNorTex.get(),
        RHIImageLayout::General, RHIImageLayout::ShaderReadOnly,
        RHIPipelineStage::ComputeShader, RHIPipelineStage::FragmentShader);
}

void SSAOPass::executeSSAO(RHICommandBuffer* cmd, uint32_t frameIndex) {
    RHIClearValue clearValue = RHIClearValue::Color(1.0f, 1.0f, 1.0f, 1.0f);  // No occlusion by default

    for (uint32_t layer = 0; layer < NUM_LAYERS; ++layer) {
        cmd->beginRenderPass(m_ssaoRenderPass.get(), m_ssaoFramebuffers[layer].get(),
                             {clearValue});

        cmd->setViewport(0, 0, static_cast<float>(m_subWidth), static_cast<float>(m_subHeight));
        cmd->setScissor(0, 0, m_subWidth, m_subHeight);
        cmd->bindGraphicsPipeline(m_ssaoPipeline.get());
        cmd->setBindingGroup(0, m_ssaoGroups[frameIndex].get());

        SSAOPushConstants pc{};
        pc.layerIndex = static_cast<int>(layer);
        pc.rotationAngle = m_layerRotations[layer];
        pc.subWidth = static_cast<int>(m_subWidth);
        pc.subHeight = static_cast<int>(m_subHeight);
        cmd->pushConstants(RHIShaderStage::Fragment, 0, sizeof(pc), &pc);

        // Fullscreen triangle (3 vertices, no vertex buffer)
        cmd->draw(3, 1, 0, 0);

        cmd->endRenderPass();
    }
}

void SSAOPass::executeReinterleave(RHICommandBuffer* cmd) {
    // Transition full AO to GENERAL for compute write
    cmd->transitionImageLayout(m_fullAOTex.get(),
        RHIImageLayout::Undefined, RHIImageLayout::General,
        RHIPipelineStage::TopOfPipe, RHIPipelineStage::ComputeShader);

    // Dispatch
    cmd->bindComputePipeline(m_reinterleavePipeline.get());
    cmd->setBindingGroup(0, m_reinterleaveGroup.get());

    ReinterleavePushConstants pc{};
    pc.fullWidth = static_cast<int>(width);
    pc.fullHeight = static_cast<int>(height);
    cmd->pushConstants(RHIShaderStage::Compute, 0, sizeof(pc), &pc);

    uint32_t groupsX = (width + 7) / 8;
    uint32_t groupsY = (height + 7) / 8;
    cmd->dispatch(groupsX, groupsY, 1);

    // Transition to SHADER_READ for blur
    cmd->transitionImageLayout(m_fullAOTex.get(),
        RHIImageLayout::General, RHIImageLayout::ShaderReadOnly,
        RHIPipelineStage::ComputeShader, RHIPipelineStage::FragmentShader);
}

void SSAOPass::executeBlur(RHICommandBuffer* cmd) {
    RHIClearValue clearValue = RHIClearValue::Color(1.0f, 1.0f, 1.0f, 1.0f);

    cmd->beginRenderPass(m_blurRenderPass.get(), m_blurFramebuffer.get(),
                         {clearValue});

    cmd->setViewport(0, 0, static_cast<float>(width), static_cast<float>(height));
    cmd->setScissor(0, 0, width, height);
    cmd->bindGraphicsPipeline(m_blurPipeline.get());
    cmd->setBindingGroup(0, m_blurGroup.get());

    // Fullscreen triangle
    cmd->draw(3, 1, 0, 0);

    cmd->endRenderPass();
}