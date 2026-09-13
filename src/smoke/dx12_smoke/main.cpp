// rhi_dx12_smoke (Phase 1 acceptance demo; extended in Phase 5)
//
// Renders a textured triangle through the *complete* DX12 RHI chain:
//   device -> swapchain (2x FLIP + internal D32) -> render pass/framebuffer
//   -> PSO + root signature (descriptor tables + push constants)
//   -> binding groups (CBV + SRV + sampler) -> 2-frame submission loop.
//
// Phase 5 additions (4-frame rotation, same triangle on screen):
//   - frame%4==1: UInt16 index buffer + drawIndexed
//   - frame%4==2: drawIndirect (GPU-side args buffer, IndirectCommandRead barrier)
//   - frame%4==3: drawIndexedIndirectCount (args + count buffers)
//
// One-shot offscreen stencil test (after the readback warmup):
//   - left-half quad writes stencil=1 (Always/Replace, static ref),
//     full-screen quad only passes where stencil != 1 (NotEqual) ->
//     pixel-verified left=red / right=green through a native texture readback.
//
// Self checks (printed on exit):
//   - UBO content read back through a GPUToCPU buffer matches what was uploaded
//   - stencil pixel verification PASS
//   - D3D12 debug layer error count == 0 (retrieved via ID3D12InfoQueue)
//   - window resize does not crash (framebuffer-size callback path)
//
// Exit code: 0 = all checks passed.

#include "RHI.h"
#include "DX12/DX12RHIDevice.h"
#include "DX12/DX12RHICommandBuffer.h"
#include "DX12/DX12RHITexture.h"
#include "DX12/DX12RHIBuffer.h"

#include <GLFW/glfw3.h>

#include <directx/d3d12.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <crtdbg.h>
#include <memory>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr int    kWindowWidth  = 960;
constexpr int    kWindowHeight = 600;
constexpr UINT   kFramesInFlight = 2;
constexpr size_t kUboSize        = 64;   // one mat4
constexpr UINT   kStencilRTSize  = 64;   // offscreen stencil test target

struct Vertex { float px, py, pz; float u, v; };

const Vertex kTriVertices[3] = {
    { -0.85f, -0.75f, 0.0f, 0.0f, 0.0f },
    {  0.85f, -0.75f, 0.0f, 1.0f, 0.0f },
    {  0.0f,   0.80f, 0.0f, 0.5f, 1.0f },
};

// RHI indirect command layouts (see RHICommandBuffer.h for the convention).
struct DrawArgsDirect     { uint32_t vertexCount, instanceCount, firstVertex, firstInstance; };
struct DrawIndexedArgsInd { uint32_t indexCount, instanceCount, firstIndex;
                            int32_t vertexOffset; uint32_t firstInstance; };
static_assert(sizeof(DrawArgsDirect) == 16, "VkDrawIndirectCommand layout");
static_assert(sizeof(DrawIndexedArgsInd) == 20, "VkDrawIndexedIndirectCommand layout");

// Stencil-test geometry: [0..6) left-half quad (writes stencil), [6..12) full-screen quad.
// All UVs (0,0) -> checker texel (255,60,60) so tint math is exact.
const Vertex kStencilQuads[12] = {
    { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f, 0.0f, 0.0f },
    {  0.0f,  1.0f, 0.0f, 0.0f, 0.0f }, { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f },
    {  0.0f,  1.0f, 0.0f, 0.0f, 0.0f }, { -1.0f,  1.0f, 0.0f, 0.0f, 0.0f },
    { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f },
    {  1.0f,  1.0f, 0.0f, 0.0f, 0.0f }, { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f },
    {  1.0f,  1.0f, 0.0f, 0.0f, 0.0f }, { -1.0f,  1.0f, 0.0f, 0.0f, 0.0f },
};

// Identity matrix uploaded to the UBO (16 floats, column-major mat4).
const float kIdentity[16] = {
    1,0,0,0,
    0,1,0,0,
    0,0,1,0,
    0,0,0,1,
};

// 2x2 checkerboard (RGBA8).
const uint8_t kChecker[16] = {
    255,  60,  60, 255,   40,  90, 255, 255,
     40,  90, 255, 255,  255,  60,  60, 255,
};

struct ValidationMonitor {
    ComPtr<ID3D12InfoQueue> queue;
    uint64_t numErrors = 0;
    uint64_t lastDrained = 0;
    int      printed = 0;

    void init(ID3D12Device* device) {
        device->QueryInterface(IID_PPV_ARGS(&queue));
    }

    void poll(const char* stage) {
        if (!queue) {
            return;
        }
        const UINT64 total = queue->GetNumStoredMessages();
        if (total <= lastDrained) {
            return;
        }
        for (UINT64 i = lastDrained; i < total; ++i) {
            SIZE_T size = 0;
            if (FAILED(queue->GetMessage(i, nullptr, &size))) {
                continue;
            }
            std::vector<uint8_t> buf(size);
            D3D12_MESSAGE* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (FAILED(queue->GetMessage(i, msg, &size))) {
                continue;
            }
            if (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                ++numErrors;
                if (printed < 5) {
                    std::printf("[validation] ERROR (%s): %s\n", stage, msg->pDescription);
                    ++printed;
                }
            }
        }
        lastDrained = total;
    }
};

bool g_resized = false;
int  g_fbWidth  = kWindowWidth;
int  g_fbHeight = kWindowHeight;

void onFramebufferSize(GLFWwindow* /*window*/, int width, int height) {
    g_fbWidth = width;
    g_fbHeight = height;
    g_resized = true;
}

int assertToStderrHook(int reportType, char* message, int* returnValue) {
    (void)reportType; (void)returnValue;
    std::fprintf(stderr, "[CRT-ASSERT] %s\n", message);
    std::fflush(stderr);
    __debugbreak();   // stop here so a debugger can capture the call stack
    return TRUE;
}

/// Native readback of an RGBA8 texture through a GPUToCPU buffer and a
/// single-time command list. The RHI has no copyTextureToBuffer (engine
/// never needs it); this smoke-only helper bridges to D3D12 directly while
/// keeping the texture's tracked state consistent.
/// @param subresource  mip/layer subresource to read (mipgen reads mip > 0).
std::vector<uint8_t> readbackTextureRGBA8(DX12RHIDevice* device, RHITexture* texture,
                                          UINT subresource = 0) {
    auto* dxTex = static_cast<DX12RHITexture*>(texture);
    const UINT mip = subresource % dxTex->getMipLevels();
    UINT width = dxTex->getWidth() >> mip;  if (width == 0) width = 1;
    UINT height = dxTex->getHeight() >> mip; if (height == 0) height = 1;
    const UINT rowPitch = (width * 4 + 255u) & ~255u;

    RHIBufferDesc rbDesc;
    rbDesc.setSize(static_cast<uint64_t>(rowPitch) * height)
          .setUsage(RHIBufferUsage::TransferDst)
          .setMemoryUsage(RHIMemoryUsage::GPUToCPU);
    std::shared_ptr<RHIBuffer> readback = device->createBuffer(rbDesc);
    auto* dxRB = static_cast<DX12RHIBuffer*>(readback.get());

    const D3D12_RESOURCE_STATES prior = dxTex->getCurrentState();
    ID3D12GraphicsCommandList* list = static_cast<ID3D12GraphicsCommandList*>(
        device->beginSingleTimeCommands());

    auto barrier = [&](D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = dxTex->getD3D12Resource();
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        b.Transition.Subresource = subresource;
        list->ResourceBarrier(1, &b);
    };
    barrier(prior, D3D12_RESOURCE_STATE_COPY_SOURCE);
    dxTex->setCurrentState(D3D12_RESOURCE_STATE_COPY_SOURCE);

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = dxTex->getD3D12Resource();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = subresource;

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = dxRB->getD3D12Resource();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Offset = 0;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = width;
    dst.PlacedFootprint.Footprint.Height = height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    barrier(D3D12_RESOURCE_STATE_COPY_SOURCE, prior);
    dxTex->setCurrentState(prior);

    device->endSingleTimeCommands(list);

    std::vector<uint8_t> pixels(static_cast<size_t>(rowPitch) * height);
    uint8_t* mapped = static_cast<uint8_t*>(readback->map());
    std::memcpy(pixels.data(), mapped, pixels.size());
    readback->unmap();
    return pixels;
}

} // namespace

int main(int argc, char** argv) {
    // Optional: "--frames N" exits after N presented frames (automation).
    uint64_t maxFrames = 0;
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0) {
            maxFrames = static_cast<uint64_t>(std::strtoull(argv[i + 1], nullptr, 10));
        }
    }

    std::puts("[rhi_dx12_smoke] Phase 1 demo start");

    // Redirect CRT assertions to stderr (with file+line) instead of a modal dialog.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(assertToStderrHook);
    if (!glfwInit()) {
        std::fputs("glfwInit failed\n", stderr);
        return 2;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight,
                                          "RHI DX12 Smoke (Phase 1)", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 2;
    }
    glfwSetFramebufferSizeCallback(window, onFramebufferSize);

    int exitCode = 1;
    bool readbackVerified = false;
    bool stencilVerified = false;
    bool mipsVerified = false;
    try {
        std::shared_ptr<RHIDevice> device = std::make_shared<DX12RHIDevice>(window);
        auto* dx12 = static_cast<DX12RHIDevice*>(device.get());

        ValidationMonitor validation;
        validation.init(dx12->getDevice());

        // ---- Swapchain (2x FLIP + internal D32 depth) ----
        RHISwapChainDesc scDesc;
        scDesc.setWidth(kWindowWidth).setHeight(kWindowHeight)
              .setFormat(RHIFormat::R8G8B8A8_UNORM)
              .setBufferCount(2)
              .setPresentMode(RHIPresentMode::VSync);
        std::shared_ptr<RHISwapChain> swapChain = device->createSwapChain(scDesc);
        RHIRenderPass* swapChainPass = swapChain->getRHIRenderPass();
        std::printf("[rhi_dx12_smoke] swapchain created (format %d, %ux%u, %u buffers)\n",
                    static_cast<int>(swapChain->getFormat()),
                    swapChain->getExtent().width, swapChain->getExtent().height,
                    swapChain->getImageCount());

        // ---- 2-frame sync objects (Engine.cpp pattern) ----
        std::vector<void*> fences(kFramesInFlight);
        for (UINT i = 0; i < kFramesInFlight; ++i) {
            fences[i] = device->createFence(/*signaled=*/true);
        }
        std::vector<void*> commandBuffers = device->allocateCommandBuffers(kFramesInFlight);

        // ---- Resources ----
        RHIBufferDesc uboDesc;
        uboDesc.setSize(kUboSize)
              .setUsage(RHIBufferUsage::Uniform)
              .setMemoryUsage(RHIMemoryUsage::CPUToGPU);
        std::shared_ptr<RHIBuffer> ubo = device->createBuffer(uboDesc);
        {
            void* uboPtr = ubo->map();
            std::memcpy(uboPtr, kIdentity, sizeof(kIdentity));
            ubo->unmap();
        }

        RHIBufferDesc vbDesc;
        vbDesc.setSize(sizeof(kTriVertices))
              .setUsage(RHIBufferUsage::Vertex)
              .setMemoryUsage(RHIMemoryUsage::CPUToGPU);
        std::shared_ptr<RHIBuffer> vb = device->createBuffer(vbDesc);
        vb->uploadData(kTriVertices, sizeof(kTriVertices), 0);

        RHIBufferDesc readbackDesc;
        readbackDesc.setSize(kUboSize)
                    .setUsage(RHIBufferUsage::TransferDst)
                    .setMemoryUsage(RHIMemoryUsage::GPUToCPU);
        std::shared_ptr<RHIBuffer> readback = device->createBuffer(readbackDesc);

        RHITextureDesc texDesc;
        texDesc.width = 2;
        texDesc.height = 2;
        texDesc.format = RHIFormat::R8G8B8A8_UNORM;
        texDesc.usage  = RHITextureUsage::Sampled | RHITextureUsage::TransferDst;
        std::shared_ptr<RHITexture> texture = device->createTexture(texDesc);
        texture->uploadPixels(kChecker, sizeof(kChecker));

        RHISamplerDesc samplerDesc;
        samplerDesc.minFilter = RHIFilter::Linear;
        samplerDesc.magFilter = RHIFilter::Linear;
        samplerDesc.mipMapFilter = RHIFilter::Linear;
        samplerDesc.addressModeU = RHIAddressMode::ClampToEdge;
        samplerDesc.addressModeV = RHIAddressMode::ClampToEdge;
        samplerDesc.addressModeW = RHIAddressMode::ClampToEdge;
        std::shared_ptr<RHISampler> sampler = device->createSampler(samplerDesc);

        // ---- Binding layouts (engine ForwardPass shape: UBO set0 + texture set1) ----
        RHIBindingLayoutDesc layout0Desc;
        layout0Desc.addBinding(0, RHIDescriptorType::UniformBuffer,
                               RHIShaderStage::Vertex, 1);
        std::shared_ptr<RHIBindingLayout> layout0 = device->createBindingLayout(layout0Desc);

        RHIBindingLayoutDesc layout1Desc;
        layout1Desc.addBinding(0, RHIDescriptorType::CombinedImageSampler,
                               RHIShaderStage::Fragment, 1);
        std::shared_ptr<RHIBindingLayout> layout1 = device->createBindingLayout(layout1Desc);

        // Binding groups: set 0 (UBO) and set 1 (texture + sampler).
        std::shared_ptr<RHIBindingGroup> uboGroup = device->allocateBindingGroup(layout0.get());
        uboGroup->updateBuffer(0, ubo.get(), 0, kUboSize);

        std::shared_ptr<RHIBindingGroup> texGroup = device->allocateBindingGroup(layout1.get());
        texGroup->updateTexture(0, texture.get(), sampler.get());

        // ---- Pipeline ----
        auto builder = device->createGraphicsPipelineBuilder();
        builder->setVertexShader("shaders_dx12/demo_vert.dxil");
        builder->setFragmentShader("shaders_dx12/demo_frag.dxil");
        builder->addVertexBinding(0, sizeof(Vertex), RHIVertexInputRate::Vertex);
        builder->addVertexAttribute(0, 0, RHIFormat::R32G32B32_SFLOAT, 0);
        builder->addVertexAttribute(0, 1, RHIFormat::R32G32_SFLOAT, offsetof(Vertex, u));
        builder->setTopology(RHIPrimitiveTopology::TriangleList);
        builder->setCullMode(RHICullMode::None);
        builder->setFrontFace(RHIFrontFace::CounterClockwise);
        builder->setPolygonMode(RHIPolygonMode::Fill);
        builder->setDepthTest(false, false);
        builder->setSampleCount(RHISampleCount::Count1);
        builder->addBindingLayout(layout0.get());
        builder->addBindingLayout(layout1.get());
        builder->addPushConstant(RHIShaderStage::Fragment, 0, 16);
        builder->setRenderPass(swapChainPass, 0);
        std::shared_ptr<RHIPipeline> pipeline = builder->build();
        std::puts("[rhi_dx12_smoke] pipeline built (PSO + root signature)");

        // ---- Phase 5: UInt16 IB + indirect args/count buffers ----
        RHIBufferDesc ibDesc;
        ibDesc.setSize(sizeof(uint16_t) * 3)
              .setUsage(RHIBufferUsage::Index)
              .setMemoryUsage(RHIMemoryUsage::CPUToGPU);
        std::shared_ptr<RHIBuffer> ibU16 = device->createBuffer(ibDesc);
        const uint16_t kIndicesU16[3] = { 0, 1, 2 };
        ibU16->uploadData(kIndicesU16, sizeof(kIndicesU16), 0);

        RHIBufferDesc drawArgsDesc;
        drawArgsDesc.setSize(sizeof(DrawArgsDirect))
                   .setUsage(RHIBufferUsage::Indirect)
                   .setMemoryUsage(RHIMemoryUsage::GPUOnly);
        std::shared_ptr<RHIBuffer> drawArgs = device->createBuffer(drawArgsDesc);
        const DrawArgsDirect kDrawArgs = { 3, 1, 0, 0 };
        drawArgs->uploadData(&kDrawArgs, sizeof(kDrawArgs), 0);

        RHIBufferDesc drawIndexedArgsDesc;
        drawIndexedArgsDesc.setSize(sizeof(DrawIndexedArgsInd))
                          .setUsage(RHIBufferUsage::Indirect)
                          .setMemoryUsage(RHIMemoryUsage::GPUOnly);
        std::shared_ptr<RHIBuffer> drawIndexedArgs = device->createBuffer(drawIndexedArgsDesc);
        const DrawIndexedArgsInd kDrawIndexedArgs = { 3, 1, 0, 0, 0 };
        drawIndexedArgs->uploadData(&kDrawIndexedArgs, sizeof(kDrawIndexedArgs), 0);

        RHIBufferDesc drawCountDesc;
        drawCountDesc.setSize(4)
                    .setUsage(RHIBufferUsage::Indirect)
                    .setMemoryUsage(RHIMemoryUsage::GPUOnly);
        std::shared_ptr<RHIBuffer> drawCount = device->createBuffer(drawCountDesc);
        const uint32_t kDrawCount = 1;
        drawCount->uploadData(&kDrawCount, sizeof(kDrawCount), 0);

        // ---- Phase 5: offscreen stencil test scene ----
        RHITextureDesc stencilRTDesc;
        stencilRTDesc.width = kStencilRTSize;
        stencilRTDesc.height = kStencilRTSize;
        stencilRTDesc.format = RHIFormat::R8G8B8A8_UNORM;
        stencilRTDesc.usage  = RHITextureUsage::ColorAttachment;
        std::shared_ptr<RHITexture> stencilRT = device->createTexture(stencilRTDesc);

        RHITextureDesc stencilDSDesc;
        stencilDSDesc.width = kStencilRTSize;
        stencilDSDesc.height = kStencilRTSize;
        stencilDSDesc.format = RHIFormat::D24_UNORM_S8_UINT;
        stencilDSDesc.usage  = RHITextureUsage::DepthStencilAttachment;
        std::shared_ptr<RHITexture> stencilDS = device->createTexture(stencilDSDesc);

        RHIRenderPassDesc stencilRPDesc;
        stencilRPDesc.addColorAttachment(RHIFormat::R8G8B8A8_UNORM, RHILoadOp::Clear,
                                         RHIStoreOp::Store, RHIImageLayout::Undefined,
                                         RHIImageLayout::ColorAttachment);
        stencilRPDesc.setDepthAttachment(RHIFormat::D24_UNORM_S8_UINT, RHILoadOp::Clear,
                                         RHIStoreOp::Store, RHIImageLayout::Undefined,
                                         RHIImageLayout::DepthStencilAttachment);
        stencilRPDesc.depthAttachment.stencilLoadOp = RHILoadOp::Clear;
        stencilRPDesc.depthAttachment.stencilStoreOp = RHIStoreOp::Store;
        std::shared_ptr<RHIRenderPass> stencilPass = device->createRenderPass(stencilRPDesc);

        RHIFramebufferDesc stencilFBDesc;
        stencilFBDesc.renderPass = stencilPass.get();
        stencilFBDesc.attachments = { stencilRT.get(), stencilDS.get() };
        stencilFBDesc.width = kStencilRTSize;
        stencilFBDesc.height = kStencilRTSize;
        std::shared_ptr<RHIFramebuffer> stencilFB = device->createFramebuffer(stencilFBDesc);

        RHIBufferDesc stencilVBDesc;
        stencilVBDesc.setSize(sizeof(kStencilQuads))
                    .setUsage(RHIBufferUsage::Vertex)
                    .setMemoryUsage(RHIMemoryUsage::CPUToGPU);
        std::shared_ptr<RHIBuffer> stencilVB = device->createBuffer(stencilVBDesc);
        stencilVB->uploadData(kStencilQuads, sizeof(kStencilQuads), 0);

        // Pipeline A: stencil Always + Replace(ref 1) — writes the mask.
        RHIStencilOpState stWrite;
        stWrite.compareOp = RHICompareOp::Always;
        stWrite.passOp    = RHIStencilOp::Replace;
        stWrite.reference = 1;
        auto builderA = device->createGraphicsPipelineBuilder();
        builderA->setVertexShader("shaders_dx12/demo_vert.dxil");
        builderA->setFragmentShader("shaders_dx12/demo_frag.dxil");
        builderA->addVertexBinding(0, sizeof(Vertex), RHIVertexInputRate::Vertex);
        builderA->addVertexAttribute(0, 0, RHIFormat::R32G32B32_SFLOAT, 0);
        builderA->addVertexAttribute(0, 1, RHIFormat::R32G32_SFLOAT, offsetof(Vertex, u));
        builderA->setTopology(RHIPrimitiveTopology::TriangleList);
        builderA->setCullMode(RHICullMode::None);
        builderA->setDepthTest(false, false);
        builderA->setSampleCount(RHISampleCount::Count1);
        builderA->addBindingLayout(layout0.get());
        builderA->addBindingLayout(layout1.get());
        builderA->addPushConstant(RHIShaderStage::Fragment, 0, 16);
        builderA->setStencilTest(true, stWrite, stWrite);
        builderA->setRenderPass(stencilPass.get(), 0);
        std::shared_ptr<RHIPipeline> stencilWritePipeline = builderA->build();

        // Pipeline B: stencil NotEqual(ref 1) — draws only where stencil != 1.
        RHIStencilOpState stMask;
        stMask.compareOp = RHICompareOp::NotEqual;
        stMask.reference = 1;
        auto builderB = device->createGraphicsPipelineBuilder();
        builderB->setVertexShader("shaders_dx12/demo_vert.dxil");
        builderB->setFragmentShader("shaders_dx12/demo_frag.dxil");
        builderB->addVertexBinding(0, sizeof(Vertex), RHIVertexInputRate::Vertex);
        builderB->addVertexAttribute(0, 0, RHIFormat::R32G32B32_SFLOAT, 0);
        builderB->addVertexAttribute(0, 1, RHIFormat::R32G32_SFLOAT, offsetof(Vertex, u));
        builderB->setTopology(RHIPrimitiveTopology::TriangleList);
        builderB->setCullMode(RHICullMode::None);
        builderB->setDepthTest(false, false);
        builderB->setSampleCount(RHISampleCount::Count1);
        builderB->addBindingLayout(layout0.get());
        builderB->addBindingLayout(layout1.get());
        builderB->addPushConstant(RHIShaderStage::Fragment, 0, 16);
        builderB->setStencilTest(true, stMask, stMask);
        builderB->setRenderPass(stencilPass.get(), 0);
        std::shared_ptr<RHIPipeline> stencilMaskPipeline = builderB->build();
        std::puts("[rhi_dx12_smoke] stencil pipelines built");

        // ---- Frame loop ----
        double lastPrint = glfwGetTime();
        uint64_t frameCount = 0;

        while (!glfwWindowShouldClose(window)) {
            if (maxFrames > 0 && frameCount >= maxFrames) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            glfwPollEvents();

            if (g_resized) {
                g_resized = false;
                if (g_fbWidth > 0 && g_fbHeight > 0) {
                    std::printf("[rhi_dx12_smoke] recreating swapchain %dx%d\n",
                                g_fbWidth, g_fbHeight);
                    std::fflush(stdout);
                    swapChain->recreate(static_cast<uint32_t>(g_fbWidth),
                                        static_cast<uint32_t>(g_fbHeight));
                    std::printf("[rhi_dx12_smoke] swapchain recreated to %ux%u (images %u)\n",
                                swapChain->getExtent().width, swapChain->getExtent().height,
                                swapChain->getImageCount());
                    std::fflush(stdout);
                    validation.poll("resize");
                    continue;
                }
            }

            const UINT frame = static_cast<UINT>(frameCount % kFramesInFlight);

            // ---- Engine-style frame ----
            device->waitForFence(fences[frame]);

            uint32_t imageIndex = 0;
            RHISwapChainResult acquireResult =
                swapChain->acquireNextImage(nullptr, &imageIndex);
            if (acquireResult != RHISwapChainResult::Success) {
                continue;
            }

            std::shared_ptr<RHICommandBuffer> cmd =
                device->wrapCommandBuffer(commandBuffers[frame]);
            cmd->begin();   // resets per-frame allocator + list, starts recording

            // Indirect-args barriers (outside the render pass; buffers start in
            // COMMON and transition to INDIRECT_ARGUMENT).
            const bool useDrawIndirect = (frameCount % 4 == 2);
            const bool useDrawIndirectCount = (frameCount % 4 == 3);
            if (useDrawIndirect) {
                cmd->bufferBarrier(drawArgs.get(), drawArgs->getSize(),
                                   RHIPipelineStage::AllCommands, RHIPipelineStage::DrawIndirect,
                                   RHIAccessFlags::IndirectCommandRead,
                                   RHIAccessFlags::IndirectCommandRead);
            }
            if (useDrawIndirectCount) {
                cmd->bufferBarrier(drawIndexedArgs.get(), drawIndexedArgs->getSize(),
                                   RHIPipelineStage::AllCommands, RHIPipelineStage::DrawIndirect,
                                   RHIAccessFlags::IndirectCommandRead,
                                   RHIAccessFlags::IndirectCommandRead);
                cmd->bufferBarrier(drawCount.get(), drawCount->getSize(),
                                   RHIPipelineStage::AllCommands, RHIPipelineStage::DrawIndirect,
                                   RHIAccessFlags::IndirectCommandRead,
                                   RHIAccessFlags::IndirectCommandRead);
            }

            std::vector<RHIClearValue> clears;
            clears.push_back(RHIClearValue::Color(0.10f, 0.16f, 0.30f, 1.0f));
            clears.push_back(RHIClearValue::DepthStencil(1.0f, 0));

            cmd->beginRenderPass(swapChainPass,
                                 swapChain->getRHIFramebuffer(imageIndex), clears);
            {
                cmd->bindGraphicsPipeline(pipeline.get());
                cmd->setBindingGroup(0, uboGroup.get());
                cmd->setBindingGroup(1, texGroup.get());

                RHIExtent2D ext = swapChain->getExtent();
                cmd->setViewport(0, 0, static_cast<float>(ext.width),
                                 static_cast<float>(ext.height));
                cmd->setScissor(0, 0, ext.width, ext.height);

                cmd->bindVertexBuffer(0, vb.get(), 0);

                const float t = static_cast<float>(frameCount) * 0.02f;
                const float tint[4] = { 1.0f, 0.25f + 0.15f * std::sin(t), 0.6f, 1.0f };
                cmd->pushConstants(RHIShaderStage::Fragment, 0, sizeof(tint), tint);

                // 4-way rotation: direct / UInt16 indexed / indirect / indirect-count.
                // All paths render the same triangle.
                switch (frameCount % 4) {
                case 0:
                    cmd->draw(3, 1, 0, 0);
                    break;
                case 1:
                    cmd->bindIndexBuffer(ibU16.get(), 0, RHIIndexType::UInt16);
                    cmd->drawIndexed(3, 1, 0, 0, 0);
                    break;
                case 2:
                    cmd->drawIndirect(drawArgs.get(), 0, 1, sizeof(DrawArgsDirect));
                    break;
                case 3:
                    cmd->bindIndexBuffer(ibU16.get(), 0, RHIIndexType::UInt16);
                    cmd->drawIndexedIndirectCount(drawIndexedArgs.get(), 0,
                                                  drawCount.get(), 0, 1,
                                                  sizeof(DrawIndexedArgsInd));
                    break;
                }
            }
            cmd->endRenderPass();
            cmd->end();     // Close the command list (was: dx12->getCommandList(frame)->Close())
            cmd.reset();

            device->submitGraphicsQueue({}, { commandBuffers[frame] }, {}, fences[frame]);
            swapChain->present(nullptr, imageIndex);
            ++frameCount;

            // GPU -> CPU readback check (after the pipeline is warmed up).
            if (!readbackVerified && frameCount > kFramesInFlight + 2) {
                device->waitIdle();
                void* srcCmd = device->beginSingleTimeCommands();
                std::shared_ptr<RHICommandBuffer> copyCmd =
                    device->wrapCommandBuffer(srcCmd);
                copyCmd->copyBuffer(ubo.get(), readback.get(), kUboSize, 0, 0);
                copyCmd.reset();
                device->endSingleTimeCommands(srcCmd);

                uint8_t* data = static_cast<uint8_t*>(readback->map());
                const bool ok = std::memcmp(data, kIdentity, sizeof(kIdentity)) == 0;
                readback->unmap();
                std::printf("[rhi_dx12_smoke] readback verify: %s\n", ok ? "PASS" : "FAIL");
                readbackVerified = ok;
            }

            // Phase 5 one-shot stencil test (offscreen render + pixel verify).
            if (readbackVerified && !stencilVerified) {
                device->waitIdle();
                void* stencilCmdPtr = device->beginSingleTimeCommands();
                std::shared_ptr<RHICommandBuffer> scmd =
                    device->wrapCommandBuffer(stencilCmdPtr);

                std::vector<RHIClearValue> stencilClears;
                stencilClears.push_back(RHIClearValue::Color(0.0f, 0.0f, 0.0f, 1.0f));
                stencilClears.push_back(RHIClearValue::DepthStencil(1.0f, 0));

                scmd->beginRenderPass(stencilPass.get(), stencilFB.get(), stencilClears);
                {
                    // A: left-half quad writes stencil=1 (Always/Replace, tint red).
                    scmd->bindGraphicsPipeline(stencilWritePipeline.get());
                    scmd->setBindingGroup(0, uboGroup.get());
                    scmd->setBindingGroup(1, texGroup.get());
                    scmd->setViewport(0, 0, static_cast<float>(kStencilRTSize),
                                      static_cast<float>(kStencilRTSize));
                    scmd->setScissor(0, 0, kStencilRTSize, kStencilRTSize);
                    scmd->bindVertexBuffer(0, stencilVB.get(), 0);
                    const float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
                    scmd->pushConstants(RHIShaderStage::Fragment, 0, sizeof(red), red);
                    scmd->draw(6, 1, 0, 0);

                    // B: full-screen quad, only where stencil != 1 (tint green:
                    // 60/255 checker texel * 255/60 tint == full green).
                    scmd->bindGraphicsPipeline(stencilMaskPipeline.get());
                    scmd->setBindingGroup(0, uboGroup.get());
                    scmd->setBindingGroup(1, texGroup.get());
                    const float green[4] = { 0.0f, 255.0f / 60.0f, 0.0f, 1.0f };
                    scmd->pushConstants(RHIShaderStage::Fragment, 0, sizeof(green), green);
                    scmd->draw(6, 1, 6, 0);
                }
                scmd->endRenderPass();
                scmd.reset();
                device->endSingleTimeCommands(stencilCmdPtr);

                const std::vector<uint8_t> px =
                    readbackTextureRGBA8(dx12, stencilRT.get());
                const UINT pitch = (kStencilRTSize * 4 + 255u) & ~255u;
                auto pixel = [&](int x, int y) {
                    const size_t i = static_cast<size_t>(y) * pitch + x * 4;
                    return std::make_tuple(px[i], px[i + 1], px[i + 2]);
                };
                const auto left  = pixel(16, kStencilRTSize / 2);
                const auto right = pixel(48, kStencilRTSize / 2);
                const bool leftRed   = std::get<0>(left) > 200 && std::get<1>(left) < 50;
                const bool rightGreen = std::get<1>(right) > 200 && std::get<0>(right) < 50;
                stencilVerified = leftRed && rightGreen;
                std::printf("[rhi_dx12_smoke] stencil verify: left(%u,%u,%u) right(%u,%u,%u): %s\n",
                            std::get<0>(left), std::get<1>(left), std::get<2>(left),
                            std::get<0>(right), std::get<1>(right), std::get<2>(right),
                            stencilVerified ? "PASS" : "FAIL");
            }

            // Phase 5 one-shot mip-generation test: 64x64 half red / half green
            // texture with a full auto mip chain (mipLevels = 0). mip 1 must be
            // the 32x32 box downsample (still half red / half green).
            if (readbackVerified && stencilVerified && !mipsVerified) {
                constexpr UINT kMipTexSize = 64;
                std::vector<uint8_t> mipData(kMipTexSize * kMipTexSize * 4);
                for (UINT y = 0; y < kMipTexSize; ++y) {
                    for (UINT x = 0; x < kMipTexSize; ++x) {
                        uint8_t* p = &mipData[(static_cast<size_t>(y) * kMipTexSize + x) * 4];
                        if (x < kMipTexSize / 2) { p[0]=255; p[1]=0;   p[2]=0;   p[3]=255; }
                        else                      { p[0]=0;   p[1]=255; p[2]=0;   p[3]=255; }
                    }
                }

                RHITextureDesc mipTexDesc;
                mipTexDesc.width = kMipTexSize;
                mipTexDesc.height = kMipTexSize;
                mipTexDesc.format = RHIFormat::R8G8B8A8_UNORM;
                mipTexDesc.usage  = RHITextureUsage::Sampled | RHITextureUsage::TransferDst;
                mipTexDesc.mipLevels = 0;   // 0 = automatic full chain (7 levels)
                std::shared_ptr<RHITexture> mipped = device->createTexture(mipTexDesc);
                mipped->uploadPixels(mipData.data(), mipData.size());   // uploads mip 0 + generates chain

                const bool levelOk = mipped->getMipLevels() == 7;

                const std::vector<uint8_t> mip1 =
                    readbackTextureRGBA8(dx12, mipped.get(), /*subresource=*/1);
                const UINT w1 = kMipTexSize / 2;
                const UINT pitch1 = (w1 * 4 + 255u) & ~255u;
                const uint8_t* pl = &mip1[static_cast<size_t>(w1 / 4) * 4];
                const uint8_t* pr = &mip1[(static_cast<size_t>(w1 / 2) + w1 / 4) * 4];
                const bool colorOk = pl[0] > 200 && pl[1] < 50 && pr[1] > 200 && pr[0] < 50;

                mipsVerified = levelOk && colorOk;
                std::printf("[rhi_dx12_smoke] mipgen verify: levels=%u (expect 7), "
                            "mip1 left(%u,%u) right(%u,%u): %s\n",
                            mipped->getMipLevels(), pl[0], pl[1], pr[0], pr[1],
                            mipsVerified ? "PASS" : "FAIL");
            }

            validation.poll("frame");

            if (glfwGetTime() - lastPrint > 1.0) {
                std::printf("[rhi_dx12_smoke] rendered %llu frames\n",
                            static_cast<unsigned long long>(frameCount));
                lastPrint = glfwGetTime();
            }
        }

        // ---- Teardown ----
        device->waitIdle();
        validation.poll("teardown");

        std::printf("[rhi_dx12_smoke] validation errors: %llu\n",
                    static_cast<unsigned long long>(validation.numErrors));

        for (void* f : fences) {
            device->destroyFence(f);
        }

        exitCode = (validation.numErrors == 0 && readbackVerified && stencilVerified && mipsVerified) ? 0 : 4;
    } catch (const std::exception& e) {
        std::printf("[rhi_dx12_smoke] EXCEPTION: %s\n", e.what());
        exitCode = 2;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    std::printf("[rhi_dx12_smoke] exit code %d\n", exitCode);
    return exitCode;
}
