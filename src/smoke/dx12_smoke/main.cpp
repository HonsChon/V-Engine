// rhi_dx12_smoke (Phase 1 acceptance demo)
//
// Renders a textured triangle through the *complete* DX12 RHI chain:
//   device -> swapchain (2x FLIP + internal D32) -> render pass/framebuffer
//   -> PSO + root signature (descriptor tables + push constants)
//   -> binding groups (CBV + SRV + sampler) -> 2-frame submission loop.
//
// Self checks (printed on exit):
//   - UBO content read back through a GPUToCPU buffer matches what was uploaded
//   - D3D12 debug layer error count == 0 (retrieved via ID3D12InfoQueue)
//   - window resize does not crash (framebuffer-size callback path)
//
// Exit code: 0 = all checks passed.

#include "RHI.h"
#include "DX12/DX12RHIDevice.h"
#include "DX12/DX12RHICommandBuffer.h"

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

struct Vertex { float px, py, pz; float u, v; };

const Vertex kTriVertices[3] = {
    { -0.85f, -0.75f, 0.0f, 0.0f, 0.0f },
    {  0.85f, -0.75f, 0.0f, 1.0f, 0.0f },
    {  0.0f,   0.80f, 0.0f, 0.5f, 1.0f },
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

                cmd->draw(3, 1, 0, 0);
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

        exitCode = (validation.numErrors == 0 && readbackVerified) ? 0 : 4;
    } catch (const std::exception& e) {
        std::printf("[rhi_dx12_smoke] EXCEPTION: %s\n", e.what());
        exitCode = 2;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    std::printf("[rhi_dx12_smoke] exit code %d\n", exitCode);
    return exitCode;
}
