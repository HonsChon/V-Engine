/**
 * @file GPURayTracer.cpp
 * @brief GPU 硬件光追（DXR）实现 — 场景采集 / AS / 管线 / 每帧 pass 调度
 *
 * 采集逻辑镜像 CPU 版 SoftwareRaytracer::buildFromScene（网格按 meshPath 去重、
 * 材质贴图索引化、透明实体跳过、后备光源），但几何上传为一张合并大 VB/IB，
 * BLAS 引用其局部空间切片，TLAS 实例携带世界变换。
 *
 * Pass 绑定约定（set2）：输入 t0..（binding 0 起），输出 u10..（binding 10 起，
 * 避免同号 SRV/UAV 在绑定组查找中撞车）；push constants b0 space3（layout 数 = 3）。
 */

#include "GPURayTracer.h"

#include "Scene.h"
#include "Components.h"
#include "MeshManager.h"
#include "TextureManager.h"
#include "Mesh.h"
#include "AssetPath.h"

#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIAccelerationStructure.h"
#include "RHICommandBuffer.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>

namespace VEngine {
namespace GRT {

namespace {

constexpr uint32_t kComputeGroupSize = 8;

/// DXR 实例描述（D3D12_RAYTRACING_INSTANCE_DESC 的稳定 ABI 布局，64B）。
/// InstanceID/Mask 与 Contribution/Flags 各打包进一个 32bit 字（位域）。
/// 手工打包避免渲染器直接依赖 d3d12.h。
#pragma pack(push, 1)
struct DXRInstanceDesc {
    float    transform[3][4];         // 行主序（行向量约定：p' = p · M）
    uint32_t idMask;                  // 低 24 位 InstanceID，高 8 位 InstanceMask
    uint32_t contributionFlags;       // 低 24 位 ContributionToHitGroupIndex，高 8 位 Flags
    uint64_t accelerationStructure;
};
#pragma pack(pop)
static_assert(sizeof(DXRInstanceDesc) == 64, "DXRInstanceDesc ABI");

uint64_t alignTo(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

RHIBufferDesc makeStorageDesc(uint64_t size, uint32_t stride, const char* name) {
    RHIBufferDesc d;
    d.size = size;
    d.usage = RHIBufferUsage::Storage;
    d.memoryUsage = RHIMemoryUsage::GPUOnly;
    d.structStride = stride;
    d.debugName = name;
    return d;
}

} // namespace

// ============================================================
// 支持检测 / 生命周期
// ============================================================

bool GPURayTracer::isSupported(RHIDevice* device) {
    return device && device->queryRayTracingTier() >= 11;   // Tier 1.1: RayQuery 必需
}

GPURayTracer::GPURayTracer(RHIDevice* device)
    : device_(device)
{
    createStaticResources();
    createPipelines();
    initialized_ = true;
    std::cout << "[GPURayTracer] initialized (DXR Tier "
              << device->queryRayTracingTier() << ")" << std::endl;
}

GPURayTracer::~GPURayTracer() {
    if (device_) device_->waitIdle();
}

// ============================================================
// 静态资源：布局 / 采样器 / 帧常量 UBO 组
// ============================================================

void GPURayTracer::createStaticResources() {
    const auto rtStages = RHIShaderStage::RayTracingAll | RHIShaderStage::Compute;

    // ---- set0：帧常量 ----
    RHIBindingLayoutDesc l0;
    l0.addBinding(0, RHIDescriptorType::UniformBuffer, rtStages, 1);
    layoutFrame_ = device_->createBindingLayout(l0);

    for (uint32_t i = 0; i < 2; ++i) {
        RHIBufferDesc desc;
        desc.size = sizeof(FrameUBO);
        desc.usage = RHIBufferUsage::Uniform;
        desc.memoryUsage = RHIMemoryUsage::CPUToGPU;
        desc.debugName = "GRT FrameUBO";
        frameUBO_[i] = device_->createBuffer(desc);

        RHIBindingGroupDesc g;
        g.setBuffer(0, frameUBO_[i].get());
        frameGroups_[i] = device_->createBindingGroup(layoutFrame_.get(), g);
    }

    // ---- set1：场景（t0 TLAS, t1 VB, t2 IB, t3 材质, t4 实例, t5 灯光,
    //      t6 贴图数组 bindless, s7 采样器）----
    RHIBindingLayoutDesc l1;
    l1.addBinding(0, RHIDescriptorType::AccelerationStructure, rtStages, 1);
    l1.addBinding(1, RHIDescriptorType::StorageBufferReadOnly, rtStages, 1);
    l1.addBinding(2, RHIDescriptorType::StorageBufferReadOnly, rtStages, 1);
    l1.addBinding(3, RHIDescriptorType::StorageBufferReadOnly, rtStages, 1);
    l1.addBinding(4, RHIDescriptorType::StorageBufferReadOnly, rtStages, 1);
    l1.addBinding(5, RHIDescriptorType::StorageBufferReadOnly, rtStages, 1);
    l1.addBinding(6, RHIDescriptorType::SampledImageArray, rtStages, kMaxBindlessTextures);
    l1.addBinding(7, RHIDescriptorType::Sampler, rtStages, 1);
    layoutScene_ = device_->createBindingLayout(l1);

    // 采样器（bindless 贴图共用；RT 内无导数，SampleLevel 显式 LOD）
    RHISamplerDesc sdesc;
    sdesc.maxLod = 16.0f;
    linearSampler_ = device_->createSampler(sdesc);

    {
        auto dummy = device_->createBuffer(makeStorageDesc(256, 0, "GRT placeholder"));
        RHIBindingGroupDesc g;
        g.setBuffer(1, dummy.get());
        g.setBuffer(2, dummy.get());
        g.setBuffer(3, dummy.get());
        g.setBuffer(4, dummy.get());
        g.setBuffer(5, dummy.get());
        g.setTexture(7, nullptr, linearSampler_.get());   // s7 采样器
        sceneGroup_ = device_->createBindingGroup(layoutScene_.get(), g);
    }
}

// ============================================================
// 管线：2 RTPSO + 6 compute + SBT
// ============================================================

void GPURayTracer::createPipelines() {
    const auto kRtStage = RHIShaderStage::RayTracingAll;
    const auto kCsStage = RHIShaderStage::Compute;

    // ---- helper：pass 布局（输入 t0..，输出 u10..）----
    auto makePassLayout = [&](const std::vector<RHIDescriptorType>& inputs,
                              const std::vector<RHIDescriptorType>& outputs,
                              RHIShaderStage stages) {
        RHIBindingLayoutDesc l;
        for (uint32_t i = 0; i < inputs.size(); ++i)
            l.addBinding(i, inputs[i], stages, 1);
        for (uint32_t i = 0; i < outputs.size(); ++i)
            l.addBinding(10 + i, outputs[i], stages, 1);
        return device_->createBindingLayout(l);
    };

    // ---- [DispatchRays] RT-GBuffer：纯 UAV 布局（u0-u3 无冲突）----
    {
        RHIBindingLayoutDesc l;
        for (uint32_t i = 0; i < 4; ++i)
            l.addBinding(i, RHIDescriptorType::StorageImage, kRtStage, 1);
        auto layout = device_->createBindingLayout(l);

        auto b = device_->createRayTracingPipelineBuilder();
        b->addLibrary("shaders_dx12/raytrace/rt_gbuffer.dxil")
            .setRayGen("rgGBuffer")
            .addMiss("msSky", 0)
            .addHitGroup("hgGBuffer", "chGBuffer")
            .setMaxPayloadSize(32)
            .setMaxAttributeSize(8)
            .setMaxRecursionDepth(1)
            .addBindingLayout(layoutFrame_.get())
            .addBindingLayout(layoutScene_.get())
            .addBindingLayout(layout.get())
            .addPushConstant(kRtStage | kCsStage, 0, sizeof(FrameUBO));
        gbufferPipe_ = b->build();
        gbufferPass_.layout = layout;
    }

    // ---- [DispatchRays] 着色：t0-t4 GBuffer+蓄水池，u10 颜色 ----
    {
        auto layout = makePassLayout(
            { RHIDescriptorType::SampledImage, RHIDescriptorType::SampledImage,
              RHIDescriptorType::SampledImage, RHIDescriptorType::SampledImage,
              RHIDescriptorType::SampledImage },
            { RHIDescriptorType::StorageImage }, kRtStage);

        auto b = device_->createRayTracingPipelineBuilder();
        b->addLibrary("shaders_dx12/raytrace/rt_shade.dxil")
            .setRayGen("rgShade")
            .addMiss("msSkyShade", 0)
            .addHitGroup("hgShade", "chShade")
            .setMaxPayloadSize(32)
            .setMaxAttributeSize(8)
            .setMaxRecursionDepth(8)      // 间接光递归弹射上限
            .addBindingLayout(layoutFrame_.get())
            .addBindingLayout(layoutScene_.get())
            .addBindingLayout(layout.get())
            .addPushConstant(kRtStage | kCsStage, 0, sizeof(FrameUBO));
        shadePipe_ = b->build();
        shadePass_.layout = layout;
    }

    // ---- [compute] 工厂 ----
    auto makeCompute = [&](const char* path,
                           const std::vector<RHIDescriptorType>& inputs,
                           const std::vector<RHIDescriptorType>& outputs,
                           std::shared_ptr<RHIPipeline>* outPipe, PassResources* outPass) {
        auto layout = makePassLayout(inputs, outputs, kCsStage);
        auto b = device_->createComputePipelineBuilder();
        b->setComputeShader(path)
            .addBindingLayout(layoutFrame_.get())
            .addBindingLayout(layoutScene_.get())
            .addBindingLayout(layout.get())
            .addPushConstant(kCsStage, 0, sizeof(FrameUBO));
        *outPipe = b->build();
        outPass->layout = layout;
    };
    const auto SI = RHIDescriptorType::SampledImage;   // 输入纹理
    const auto SO = RHIDescriptorType::StorageImage;   // 输出纹理

    makeCompute("shaders_dx12/raytrace/rt_restir_initial.dxil",
                { SI, SI, SI, SI }, { SO }, &restirInitPipe_, &restirInitPass_);
    makeCompute("shaders_dx12/raytrace/rt_restir_spatial.dxil",
                { SI, SI, SI, SI, SI }, { SO }, &restirSpatialPipe_, &restirSpatialPass_);
    makeCompute("shaders_dx12/raytrace/rt_restir_temporal.dxil",
                { SI, SI, SI, SI, SI, SI, SI, SI }, { SO, SO, SO, SO },
                &restirTemporalPipe_, &restirTemporalPass_[0]);
    makeCompute("shaders_dx12/raytrace/rt_svgf_variance.dxil",
                { SI, SI, SI, SI }, { SO, SO }, &svgfVarPipe_, &svgfVarPass_[0]);
    makeCompute("shaders_dx12/raytrace/rt_svgf_atrous.dxil",
                { SI, SI, SI, SI }, { SO, SO }, &svgfAtrousPipe_, &atrousStep1_[0]);
    makeCompute("shaders_dx12/raytrace/rt_resolve.dxil",
                { SI }, { SO }, &resolvePipe_, &resolvePass_[0]);
    // 变体组共享布局
    restirTemporalPass_[1].layout = restirTemporalPass_[0].layout;
    svgfVarPass_[1].layout = svgfVarPass_[0].layout;
    atrousStep1_[1].layout = atrousStep1_[0].layout;
    atrousStep2_.layout = atrousStep1_[0].layout;
    atrousStep3_.layout = atrousStep1_[0].layout;
    resolvePass_[1].layout = resolvePass_[0].layout;

    // ---- SBT：[0]rgGBuffer [1]rgShade [2]msSky [3]msSkyShade [4]hgGBuffer [5]hgShade ----
    std::vector<uint8_t> sbt(size_t(kSBTRecordSize) * 6, 0);
    bool ok = true;
    ok &= writeSBTRecord(sbt, 0, gbufferPipe_.get(), "rgGBuffer");
    ok &= writeSBTRecord(sbt, 1, shadePipe_.get(), "rgShade");
    ok &= writeSBTRecord(sbt, 2, gbufferPipe_.get(), "msSky");
    ok &= writeSBTRecord(sbt, 3, shadePipe_.get(), "msSkyShade");
    ok &= writeSBTRecord(sbt, 4, gbufferPipe_.get(), "hgGBuffer");
    ok &= writeSBTRecord(sbt, 5, shadePipe_.get(), "hgShade");
    if (!ok) {
        throw std::runtime_error("[GPURayTracer] SBT: shader identifier lookup failed");
    }

    RHIBufferDesc sbtDesc;
    sbtDesc.size = sbt.size();
    sbtDesc.usage = RHIBufferUsage::RayTracing;
    sbtDesc.memoryUsage = RHIMemoryUsage::CPUToGPU;
    sbtDesc.debugName = "GRT SBT";
    sbtBuf_ = device_->createBuffer(sbtDesc);
    sbtBuf_->uploadData(sbt.data(), sbt.size());
}

bool GPURayTracer::writeSBTRecord(std::vector<uint8_t>& sbt, size_t recordIndex,
                                  RHIPipeline* pipe, const std::string& name) {
    const void* id = nullptr;
    uint32_t size = 0;
    if (!pipe->getShaderIdentifier(name, id, size) || !id) {
        std::cerr << "[GPURayTracer] shader identifier not found: " << name << std::endl;
        return false;
    }
    std::memcpy(sbt.data() + recordIndex * kSBTRecordSize, id, size);
    return true;
}

// ============================================================
// 场景采集（镜像 SoftwareRaytracer::buildFromScene）
// ============================================================

void GPURayTracer::buildFromScene(Scene* scene) {
    auto t0 = std::chrono::high_resolution_clock::now();

    meshIndexByPath_.clear();
    textureIndexByPath_.clear();
    blas_.clear();
    tlas_.reset();
    std::vector<RTMaterialGPU> materials;
    std::vector<RTInstanceGPU> instances;
    std::vector<RTLightGPU> lights;
    std::vector<DXRInstanceDesc> dxrInstances;
    std::vector<std::string> instanceMeshKeys;    // 实例 → meshPath（BLAS 回填）

    std::vector<uint8_t> vertexBytes;
    std::vector<uint8_t> indexBytes;
    struct MeshEntry { uint64_t vertexOffsetBytes, indexOffsetBytes; uint32_t vertexCount, indexCount; };
    std::unordered_map<std::string, MeshEntry> meshEntries;

    if (scene) {
        auto& registry = scene->getRegistry();

        auto meshView = registry.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : meshView) {
            auto& meshRenderer = meshView.get<MeshRendererComponent>(entity);
            if (!meshRenderer.visible) continue;

            auto gpuMesh = MeshManager::getInstance().getMesh(meshRenderer.meshPath);
            if (!gpuMesh || !gpuMesh->mesh) continue;
            if (gpuMesh->mesh->getIndices().size() < 3) continue;

            // 材质（透明物体跳过，与 CPU 版一致）
            bool transparent = false;
            RTMaterialGPU mat{};
            mat.albedoMetallic = glm::vec4(1.0f);
            mat.roughAoEmStrOp = glm::vec4(0.5f, 1.0f, 0.0f, 1.0f);   // rough/ao/emStr/opacity
            mat.texAlbNrmMR = glm::ivec4(-1);
            mat.texEmisExtra = glm::ivec4(-1);
            if (registry.all_of<PBRMaterialComponent>(entity)) {
                auto& src = registry.get<PBRMaterialComponent>(entity);
                mat.albedoMetallic = glm::vec4(src.albedo, src.metallic);
                mat.roughAoEmStrOp = glm::vec4(src.roughness, src.ao,
                                               src.emissiveStrength, src.opacity);
                mat.emissive = glm::vec4(src.emissive, 0.0f);
                transparent = src.isTransparent();

                auto loadTex = [&](const std::string& path) -> int {
                    if (path.empty()) return -1;
                    std::string resolved = resolveAssetPath(path);
                    auto it = textureIndexByPath_.find(resolved);
                    if (it != textureIndexByPath_.end()) return it->second;
                    auto tex = TextureManager::getInstance().getTexture(resolved);
                    if (!tex || !tex->isValid()) return -1;
                    int index = static_cast<int>(textureIndexByPath_.size());
                    textureIndexByPath_[resolved] = index;
                    sceneGroup_->updateTextureArrayElement(6, index, tex->getTexture());
                    return index;
                };
                mat.texAlbNrmMR.x = loadTex(src.albedoMap);
                mat.texAlbNrmMR.y = loadTex(src.normalMap);
                mat.texAlbNrmMR.z = loadTex(src.metallicMap);
                mat.texAlbNrmMR.w = loadTex(src.aoMap);
                mat.texEmisExtra.x = loadTex(src.emissiveMap);
            }
            if (transparent) continue;

            // 网格去重（按 meshPath）：合并几何
            std::string meshKey = resolveAssetPath(meshRenderer.meshPath);
            MeshEntry entry{};
            auto it = meshEntries.find(meshKey);
            if (it == meshEntries.end()) {
                const auto& vertices = gpuMesh->mesh->getVertices();
                const auto& indices = gpuMesh->mesh->getIndices();
                entry.vertexOffsetBytes = vertexBytes.size();
                entry.indexOffsetBytes = indexBytes.size();
                entry.vertexCount = static_cast<uint32_t>(vertices.size());
                entry.indexCount = static_cast<uint32_t>(indices.size());

                vertexBytes.resize(vertexBytes.size() + size_t(entry.vertexCount) * sizeof(Vertex));
                std::memcpy(vertexBytes.data() + entry.vertexOffsetBytes, vertices.data(),
                            size_t(entry.vertexCount) * sizeof(Vertex));
                indexBytes.resize(indexBytes.size() + size_t(entry.indexCount) * 4);
                std::memcpy(indexBytes.data() + entry.indexOffsetBytes, indices.data(),
                            size_t(entry.indexCount) * 4);
                meshEntries[meshKey] = entry;
            } else {
                entry = it->second;
            }

            // 实例（世界矩阵含父链）
            glm::mat4 world = computeWorldMatrix(registry, entity);
            glm::mat3 normalMat = glm::inverseTranspose(glm::mat3(world));

            uint32_t materialIndex = static_cast<uint32_t>(materials.size());
            materials.push_back(mat);

            RTInstanceGPU inst{};
            inst.materialIndex = materialIndex;
            inst.vertexOffsetBytes = static_cast<uint32_t>(entry.vertexOffsetBytes);
            inst.indexOffsetBytes = static_cast<uint32_t>(entry.indexOffsetBytes);
            // 法线矩阵行存储（world normal = row * local）：
            // HLSL transformNormal 用 dot(row_i, n)，row_i = normalMat 的第 i 行
            inst.nrmRow0 = glm::vec4(normalMat[0][0], normalMat[1][0], normalMat[2][0], 0.0f);
            inst.nrmRow1 = glm::vec4(normalMat[0][1], normalMat[1][1], normalMat[2][1], 0.0f);
            inst.nrmRow2 = glm::vec4(normalMat[0][2], normalMat[1][2], normalMat[2][2], 0.0f);
            instances.push_back(inst);

            DXRInstanceDesc dxr{};
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c)
                    dxr.transform[r][c] = world[c][r];   // 行主序 = 转置
            uint32_t instanceId = static_cast<uint32_t>(instances.size() - 1);
            dxr.idMask = (instanceId & 0xFFFFFFu) | (0xFFu << 24);   // mask = 0xFF
            dxr.contributionFlags = 0;                              // hit group 0，无 flags
            dxrInstances.push_back(dxr);
            instanceMeshKeys.push_back(meshKey);
        }

        // ---- 光源 ----
        auto lightView = registry.view<TransformComponent, LightComponent>();
        for (auto entity : lightView) {
            auto& transform = lightView.get<TransformComponent>(entity);
            auto& light = lightView.get<LightComponent>(entity);

            glm::mat4 world = computeWorldMatrix(registry, entity);
            RTLightGPU rt{};
            rt.type = static_cast<int32_t>(light.type);
            rt.color = light.color;
            rt.intensity = light.intensity;
            rt.position = glm::vec3(world[3]);
            rt.direction = glm::normalize(glm::mat3(world) * transform.getForward());
            rt.atten = glm::vec4(light.constantAttenuation, light.linearAttenuation,
                                 light.quadraticAttenuation, light.innerConeAngle);
            rt.range = light.range;
            rt.outerCone = light.outerConeAngle;
            lights.push_back(rt);
        }
    }

    // 后备光源（与 CPU/光栅化一致）
    if (lights.empty()) {
        RTLightGPU fallback{};
        fallback.type = 1;
        fallback.position = glm::vec3(5.0f, 3.0f, 5.0f);
        fallback.color = glm::vec3(1.0f);
        fallback.intensity = 300.0f;
        fallback.atten = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);   // 物理 1/d²
        fallback.range = 0.0f;
        lights.push_back(fallback);
    }

    // ---- 上传合并几何 / 材质 / 实例 / 灯光 ----
    device_->waitIdle();   // 旧缓冲可能在途；场景重建不频繁

    if (!vertexBytes.empty()) {
        RHIBufferDesc vb;
        vb.size = vertexBytes.size();
        vb.usage = RHIBufferUsage::Vertex | RHIBufferUsage::Storage;
        vb.memoryUsage = RHIMemoryUsage::GPUOnly;
        vb.debugName = "GRT merged VB";
        mergedVB_ = device_->createBuffer(vb);
        mergedVB_->uploadData(vertexBytes.data(), vertexBytes.size());

        RHIBufferDesc ib;
        ib.size = indexBytes.size();
        ib.usage = RHIBufferUsage::Index | RHIBufferUsage::Storage;
        ib.memoryUsage = RHIMemoryUsage::GPUOnly;
        ib.debugName = "GRT merged IB";
        mergedIB_ = device_->createBuffer(ib);
        mergedIB_->uploadData(indexBytes.data(), indexBytes.size());
    } else {
        mergedVB_.reset();
        mergedIB_.reset();
    }

    auto uploadVec = [&](std::shared_ptr<RHIBuffer>& buf, const auto& vec,
                         uint32_t stride, const char* name) {
        if (vec.empty()) { buf.reset(); return; }
        buf = device_->createBuffer(makeStorageDesc(vec.size() * stride, stride, name));
        buf->uploadData(vec.data(), vec.size() * stride);
    };
    uploadVec(materialBuf_, materials, sizeof(RTMaterialGPU), "GRT materials");
    uploadVec(instanceBuf_, instances, sizeof(RTInstanceGPU), "GRT instances");
    uploadVec(lightBuf_, lights, sizeof(RTLightGPU), "GRT lights");
    lightCount_ = static_cast<uint32_t>(lights.size());

    // ---- BLAS（每 mesh 一个，局部空间切片）+ 实例 BLAS 回填 ----
    if (mergedVB_ && mergedIB_) {
        for (auto& [key, entry] : meshEntries) {
            RHIBLASDesc blasDesc;
            blasDesc.debugName = "GRT BLAS " + key;
            RHIRayTracingGeometryDesc geo{};
            geo.vertexBuffer = mergedVB_.get();
            geo.vertexOffset = entry.vertexOffsetBytes;
            geo.vertexStride = sizeof(Vertex);
            geo.vertexCount = entry.vertexCount;
            geo.vertexFormat = RHIFormat::R32G32B32_SFLOAT;
            geo.indexBuffer = mergedIB_.get();
            geo.indexOffset = entry.indexOffsetBytes;
            geo.indexCount = entry.indexCount;
            geo.indexType = RHIIndexType::UInt32;
            blasDesc.geometries.push_back(geo);
            blas_.push_back(device_->createBLAS(blasDesc));
            meshIndexByPath_[key] = static_cast<uint32_t>(blas_.size() - 1);
        }

        // 共享 scratch（每 BLAS 偏移 256 对齐累进）
        uint64_t scratchTotal = 0;
        for (auto& b : blas_) scratchTotal += alignTo(b->getScratchSize(), 256);
        if (scratchTotal > 0) {
            blasScratch_ = device_->createBuffer(makeStorageDesc(scratchTotal, 0, "GRT BLAS scratch"));
        }

        // 实例描述回填 BLAS 地址
        for (size_t i = 0; i < dxrInstances.size(); ++i) {
            auto mit = meshIndexByPath_.find(instanceMeshKeys[i]);
            if (mit == meshIndexByPath_.end()) continue;
            dxrInstances[i].accelerationStructure = blas_[mit->second]->getGPUAddress();
        }    } else {
        blasScratch_.reset();
    }

    // ---- TLAS ----
    if (!dxrInstances.empty()) {
        RHIBufferDesc idesc;
        idesc.size = dxrInstances.size() * sizeof(DXRInstanceDesc);
        idesc.usage = RHIBufferUsage::RayTracing;   // UPLOAD 堆禁止 UAV，不能加 Storage
        idesc.memoryUsage = RHIMemoryUsage::CPUToGPU;
        idesc.debugName = "GRT instance descs";
        instanceDescBuf_ = device_->createBuffer(idesc);
        instanceDescBuf_->uploadData(dxrInstances.data(), idesc.size);

        RHITLASDesc tdesc;
        tdesc.debugName = "GRT TLAS";
        for (size_t i = 0; i < dxrInstances.size(); ++i) {
            RHIRayTracingInstance ti{};
            auto mit = meshIndexByPath_.find(instanceMeshKeys[i]);
            ti.blas = (mit != meshIndexByPath_.end()) ? blas_[mit->second].get() : nullptr;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c)
                    ti.transform[r][c] = dxrInstances[i].transform[r][c];
            ti.instanceId = dxrInstances[i].idMask & 0xFFFFFFu;
            ti.hitGroupOffset = 0;
            ti.cullBackfaces = false;
            tdesc.instances.push_back(ti);
        }
        tlas_ = device_->createTLAS(tdesc);
        RHIBufferDesc tsd = makeStorageDesc(alignTo(tlas_->getScratchSize(), 256), 0,
                                            "GRT TLAS scratch");
        tlasScratch_ = device_->createBuffer(tsd);

        sceneGroup_->updateAccelerationStructure(0, tlas_.get());
    } else {
        instanceDescBuf_.reset();
        tlasScratch_.reset();
    }

    // 场景绑定组缓冲更新
    if (mergedVB_) sceneGroup_->updateBuffer(1, mergedVB_.get());
    if (mergedIB_) sceneGroup_->updateBuffer(2, mergedIB_.get());
    if (materialBuf_) sceneGroup_->updateBuffer(3, materialBuf_.get());
    if (instanceBuf_) sceneGroup_->updateBuffer(4, instanceBuf_.get());
    if (lightBuf_) sceneGroup_->updateBuffer(5, lightBuf_.get());

    // 统计
    stats_.instances = static_cast<uint32_t>(instances.size());
    stats_.meshes = static_cast<uint32_t>(blas_.size());
    stats_.lights = lightCount_;
    stats_.textures = static_cast<uint32_t>(textureIndexByPath_.size());
    stats_.triangles = 0;
    for (auto& [k, e] : meshEntries) stats_.triangles += e.indexCount / 3;

    auto t1 = std::chrono::high_resolution_clock::now();
    stats_.lastAsBuildMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "[GPURayTracer] Scene built: " << stats_.instances << " instances, "
              << stats_.meshes << " BLAS, " << stats_.triangles << " triangles, "
              << lightCount_ << " lights, " << stats_.textures << " textures ("
              << stats_.lastAsBuildMs << " ms)" << std::endl;

    asDirty_ = true;
    sceneBuilt_ = !instances.empty();
    resetHistory();
}

// ============================================================
// 相机 / 分辨率（GBuffer 与历史纹理重建 + pass 绑定组）
// ============================================================

void GPURayTracer::setCamera(const glm::mat4& view, const glm::mat4& proj,
                             uint32_t width, uint32_t height) {
    view_ = view;
    proj_ = proj;
    if (width == width_ && height == height_ && worldPosTex_) return;
    width_ = width;
    height_ = height;
    createPassTargets();
    resetHistory();
}

std::shared_ptr<RHITexture> GPURayTracer::makeTexture(RHIFormat format) {
    RHITextureDesc d;
    d.width = width_;
    d.height = height_;
    d.format = format;
    d.usage = RHITextureUsage::Sampled | RHITextureUsage::Storage;
    d.mipLevels = 1;
    return device_->createTexture(d);
}

void GPURayTracer::createPassTargets() {
    cleanupPassTargets();

    worldPosTex_ = makeTexture(RHIFormat::R32G32B32A32_SFLOAT);
    normalRoughTex_ = makeTexture(RHIFormat::R16G16B16A16_SFLOAT);
    albedoMetalTex_ = makeTexture(RHIFormat::R8G8B8A8_UNORM);
    emissiveAoTex_ = makeTexture(RHIFormat::R16G16B16A16_SFLOAT);
    reservoirInitTex_ = makeTexture(RHIFormat::R32G32B32A32_SFLOAT);
    reservoirSpatialTex_ = makeTexture(RHIFormat::R32G32B32A32_SFLOAT);
    reservoirFinalTex_ = makeTexture(RHIFormat::R32G32B32A32_SFLOAT);
    colorTex_ = makeTexture(RHIFormat::R32G32B32A32_SFLOAT);
    colorFilteredTex_ = makeTexture(RHIFormat::R32G32B32A32_SFLOAT);
    colorTmpTex_ = makeTexture(RHIFormat::R32G32B32A32_SFLOAT);
    varTex_ = makeTexture(RHIFormat::R32_SFLOAT);
    for (uint32_t i = 0; i < 2; ++i) {
        histPos_[i] = makeTexture(RHIFormat::R32G32B32A32_SFLOAT);
        histNormal_[i] = makeTexture(RHIFormat::R16G16B16A16_SFLOAT);
        histReservoir_[i] = makeTexture(RHIFormat::R32G32B32A32_SFLOAT);
        histColor_[i] = makeTexture(RHIFormat::R32G32B32A32_SFLOAT);
        histVar_[i] = makeTexture(RHIFormat::R32_SFLOAT);
    }
    {
        RHITextureDesc d;
        d.width = width_;
        d.height = height_;
        d.format = RHIFormat::R8G8B8A8_UNORM;
        d.usage = RHITextureUsage::Sampled | RHITextureUsage::Storage;
        d.mipLevels = 1;
        outputTex_ = device_->createTexture(d);
    }

    // ---- pass 绑定组（全部静态：ping-pong 用变体组，避免运行时
    //      updateTexture —— 多条目组的描述符改写曾出现不生效）----
    auto makeGroup = [](RHIDevice* dev, PassResources& pr, auto fill) {
        RHIBindingGroupDesc g;
        fill(g);
        pr.group = dev->createBindingGroup(pr.layout.get(), g);
    };

    makeGroup(device_, gbufferPass_, [&](RHIBindingGroupDesc& g) {
        g.setTexture(0, worldPosTex_.get());
        g.setTexture(1, normalRoughTex_.get());
        g.setTexture(2, albedoMetalTex_.get());
        g.setTexture(3, emissiveAoTex_.get());
    });
    makeGroup(device_, restirInitPass_, [&](RHIBindingGroupDesc& g) {
        g.setTexture(0, worldPosTex_.get());
        g.setTexture(1, normalRoughTex_.get());
        g.setTexture(2, albedoMetalTex_.get());
        g.setTexture(3, emissiveAoTex_.get());
        g.setTexture(10, reservoirInitTex_.get());
    });
    makeGroup(device_, restirSpatialPass_, [&](RHIBindingGroupDesc& g) {
        g.setTexture(0, worldPosTex_.get());
        g.setTexture(1, normalRoughTex_.get());
        g.setTexture(2, albedoMetalTex_.get());
        g.setTexture(3, emissiveAoTex_.get());
        g.setTexture(4, reservoirInitTex_.get());
        g.setTexture(10, reservoirSpatialTex_.get());
    });
    makeGroup(device_, shadePass_, [&](RHIBindingGroupDesc& g) {
        g.setTexture(0, worldPosTex_.get());
        g.setTexture(1, normalRoughTex_.get());
        g.setTexture(2, albedoMetalTex_.get());
        g.setTexture(3, emissiveAoTex_.get());
        g.setTexture(4, reservoirFinalTex_.get());
        g.setTexture(10, colorTex_.get());
    });
    // 时间复用变体（按 cur histIdx 静态绑定）；histIdx 翻转靠两组切换
    for (uint32_t cur = 0; cur < 2; ++cur) {
        const uint32_t out = 1 - cur;
        makeGroup(device_, restirTemporalPass_[cur], [&, cur, out](RHIBindingGroupDesc& g) {
            g.setTexture(0, worldPosTex_.get());
            g.setTexture(1, normalRoughTex_.get());
            g.setTexture(2, albedoMetalTex_.get());
            g.setTexture(3, emissiveAoTex_.get());
            g.setTexture(4, reservoirSpatialTex_.get());
            g.setTexture(5, histPos_[cur].get());
            g.setTexture(6, histNormal_[cur].get());
            g.setTexture(7, histReservoir_[cur].get());
            g.setTexture(10, reservoirFinalTex_.get());
            g.setTexture(11, histPos_[out].get());
            g.setTexture(12, histNormal_[out].get());
            g.setTexture(13, histReservoir_[out].get());
        });
    }
    // SVGF 方差变体（按 cur histIdx）
    for (uint32_t cur = 0; cur < 2; ++cur) {
        const uint32_t out = 1 - cur;
        makeGroup(device_, svgfVarPass_[cur], [&, cur, out](RHIBindingGroupDesc& g) {
            g.setTexture(0, colorTex_.get());
            g.setTexture(1, histColor_[cur].get());
            g.setTexture(2, histVar_[cur].get());
            g.setTexture(3, worldPosTex_.get());
            g.setTexture(10, histColor_[out].get());
            g.setTexture(11, histVar_[out].get());
        });
    }
    // à-trous 三步静态组：step1 输入 = histColor_[next]（按 nextIdx 两个变体）
    for (uint32_t next = 0; next < 2; ++next) {
        makeGroup(device_, atrousStep1_[next], [&, next](RHIBindingGroupDesc& g) {
            g.setTexture(0, histColor_[next].get());
            g.setTexture(1, histVar_[next].get());
            g.setTexture(2, worldPosTex_.get());
            g.setTexture(3, normalRoughTex_.get());
            g.setTexture(10, colorFilteredTex_.get());
            g.setTexture(11, varTex_.get());
        });
    }
    makeGroup(device_, atrousStep2_, [&](RHIBindingGroupDesc& g) {
        g.setTexture(0, colorFilteredTex_.get());
        g.setTexture(1, histVar_[0].get());
        g.setTexture(2, worldPosTex_.get());
        g.setTexture(3, normalRoughTex_.get());
        g.setTexture(10, colorTmpTex_.get());
        g.setTexture(11, varTex_.get());
    });
    makeGroup(device_, atrousStep3_, [&](RHIBindingGroupDesc& g) {
        g.setTexture(0, colorTmpTex_.get());
        g.setTexture(1, histVar_[0].get());
        g.setTexture(2, worldPosTex_.get());
        g.setTexture(3, normalRoughTex_.get());
        g.setTexture(10, colorFilteredTex_.get());
        g.setTexture(11, varTex_.get());
    });
    // resolve 变体：0 = denoiser 开（colorFiltered）/ 1 = 关（colorTex）
    makeGroup(device_, resolvePass_[0], [&](RHIBindingGroupDesc& g) {
        g.setTexture(0, colorFilteredTex_.get());
        g.setTexture(10, outputTex_.get());
    });
    makeGroup(device_, resolvePass_[1], [&](RHIBindingGroupDesc& g) {
        g.setTexture(0, colorTex_.get());
        g.setTexture(10, outputTex_.get());
    });
}

void GPURayTracer::cleanupPassTargets() {
    worldPosTex_.reset(); normalRoughTex_.reset(); albedoMetalTex_.reset();
    emissiveAoTex_.reset(); reservoirInitTex_.reset(); reservoirSpatialTex_.reset();
    reservoirFinalTex_.reset(); colorTex_.reset(); colorFilteredTex_.reset();
    colorTmpTex_.reset(); varTex_.reset();
    for (uint32_t i = 0; i < 2; ++i) {
        histPos_[i].reset(); histNormal_[i].reset(); histReservoir_[i].reset();
        histColor_[i].reset(); histVar_[i].reset();
    }
    outputTex_.reset();
    gbufferPass_.group.reset(); restirInitPass_.group.reset();
    restirSpatialPass_.group.reset(); shadePass_.group.reset();
    for (uint32_t i = 0; i < 2; ++i) {
        restirTemporalPass_[i].group.reset();
        svgfVarPass_[i].group.reset();
        atrousStep1_[i].group.reset();
        resolvePass_[i].group.reset();
    }
    atrousStep2_.group.reset(); atrousStep3_.group.reset();
}

// ============================================================
// 每帧调度
// ============================================================

void GPURayTracer::dispatchCompute(RHICommandBuffer* cmd, uint32_t frameSlot,
                                   RHIPipeline* pipe, RHIBindingGroup* group,
                                   uint32_t gx, uint32_t gy,
                                   const void* pushData, uint32_t pushSize) {
    cmd->bindComputePipeline(pipe);
    cmd->setBindingGroup(0, frameGroups_[frameSlot].get());
    cmd->setBindingGroup(1, sceneGroup_.get());
    cmd->setBindingGroup(2, group);
    if (pushData && pushSize > 0) {
        cmd->pushConstants(RHIShaderStage::Compute, 0, pushSize, pushData);
    }
    cmd->dispatch(gx, gy, 1);
}

void GPURayTracer::renderFrame(RHICommandBuffer* cmd, uint32_t frameSlot,
                               const GRTSettings& settings) {
    if (!sceneBuilt_ || !tlas_ || width_ == 0 || height_ == 0) return;
    auto t0 = std::chrono::high_resolution_clock::now();

    // 调试：GRT_PHASES 位掩码（bit0 AS / bit1 gbuffer / bit2 initial / bit3 spatial /
    // bit4 temporal / bit5 shade / bit6 svgf / bit7 resolve），默认全开
    static const uint32_t phaseMask = []() -> uint32_t {
        const char* e = std::getenv("GRT_PHASES");
        return e ? static_cast<uint32_t>(strtoul(e, nullptr, 0)) : 0xFFu;
    }();
    const bool wantAs = phaseMask & 0x01, wantGBuffer = phaseMask & 0x02;
    const bool wantInit = phaseMask & 0x04, wantSpatial = phaseMask & 0x08;
    const bool wantTemporal = phaseMask & 0x10, wantShade = phaseMask & 0x20;
    const bool wantSvgf = phaseMask & 0x40, wantResolve = phaseMask & 0x80;

    const uint32_t groupsX = (width_ + kComputeGroupSize - 1) / kComputeGroupSize;
    const uint32_t groupsY = (height_ + kComputeGroupSize - 1) / kComputeGroupSize;
    const uint32_t nextIdx = 1 - histIdx_;

    // ---- 帧常量（root constants 通路；matrix 等仍同步写 UBO 备用）----
    {
        glm::mat4 viewProj = proj_ * view_;
        glm::vec3 camPos = glm::inverse(view_)[3];

        FrameUBO& ubo = frameData_;
        ubo = FrameUBO{};
        ubo.invViewProj = glm::inverse(viewProj);
        ubo.prevViewProj = lastFrameViewProj_;
        ubo.camPos = camPos;
        ubo.frameIndex = static_cast<uint32_t>(frameIndex_ & 0xFFFFFF);
        ubo.resolution = glm::vec2(width_, height_);
        ubo.lightCount = lightCount_;
        ubo.skyIntensity = settings.skyIntensity;
        ubo.maxBounces = static_cast<uint32_t>(std::max(1, settings.maxBounces));
        ubo.rayEpsilon = 1e-4f;
        ubo.restirCandidates = static_cast<uint32_t>(std::max(1, settings.restirCandidates));
        ubo.restirSpatialNeighbors = static_cast<uint32_t>(std::max(0, settings.restirSpatialNeighbors));
        ubo.restirTemporal = settings.restirTemporal ? 1u : 0u;
        ubo.shadows = settings.shadows ? 1u : 0u;
        ubo.shadowInTarget = settings.restirShadowInTarget ? 1u : 0u;
        ubo.prevCamPos = lastFrameCamPos_;
        ubo.historyValid = historyValid_ ? 1u : 0u;
        ubo.exposure = settings.exposure;

        frameUBO_[frameSlot]->uploadData(&ubo, sizeof(ubo));
        frameGroups_[frameSlot]->updateBuffer(0, frameUBO_[frameSlot].get());
    }

    // ---- AS（重建后首帧录制构建）----
    if (asDirty_ && wantAs) {
        uint64_t scratchOffset = 0;
        for (auto& b : blas_) {
            cmd->buildBLAS(b.get(), blasScratch_.get(), scratchOffset, false);
            scratchOffset += alignTo(b->getScratchSize(), 256);
        }
        cmd->buildTLAS(tlas_.get(), instanceDescBuf_.get(), tlasScratch_.get(), 0, false);
        asDirty_ = false;
    }

    // 纹理状态便捷：写前 → UAV，写后 → 只读
    auto toUAV = [&](RHITexture* t) {
        cmd->transitionImageLayout(t, RHIImageLayout::Undefined, RHIImageLayout::General);
    };
    auto toRead = [&](RHITexture* t) {
        cmd->transitionImageLayout(t, RHIImageLayout::Undefined, RHIImageLayout::ShaderReadOnly);
    };

    // ---- 1. RT-GBuffer（DispatchRays）----
    if (wantGBuffer) {
        toUAV(worldPosTex_.get()); toUAV(normalRoughTex_.get());
        toUAV(albedoMetalTex_.get()); toUAV(emissiveAoTex_.get());

        frameData_.step = 0; frameData_.reset = 0;
        cmd->bindRayTracingPipeline(gbufferPipe_.get());
        cmd->setBindingGroup(0, frameGroups_[frameSlot].get());
        cmd->setBindingGroup(1, sceneGroup_.get());
        cmd->setBindingGroup(2, gbufferPass_.group.get());
        cmd->pushConstants(RHIShaderStage::RayTracingAll, 0, sizeof(frameData_), &frameData_);
        cmd->traceRays(gbufferPipe_.get(), sbtBuf_.get(),
                       0 * kSBTRecordSize, kSBTRecordSize,           // raygen
                       2 * kSBTRecordSize, kSBTRecordSize, 1,        // miss: msSky
                       4 * kSBTRecordSize, kSBTRecordSize, 2,        // hit groups
                       width_, height_, 1);

        toRead(worldPosTex_.get()); toRead(normalRoughTex_.get());
        toRead(albedoMetalTex_.get()); toRead(emissiveAoTex_.get());
    }

    // ---- 2. ReSTIR 初始候选 ----
    if (wantInit) {
        toUAV(reservoirInitTex_.get());
        frameData_.step = 0; frameData_.reset = 0;
        dispatchCompute(cmd, frameSlot, restirInitPipe_.get(), restirInitPass_.group.get(),
                        groupsX, groupsY, &frameData_, sizeof(frameData_));
        toRead(reservoirInitTex_.get());
    }

    // ---- 3. ReSTIR 空间复用 ----
    if (wantSpatial) {
        toUAV(reservoirSpatialTex_.get());
        frameData_.step = 0; frameData_.reset = 0;
        dispatchCompute(cmd, frameSlot, restirSpatialPipe_.get(), restirSpatialPass_.group.get(),
                        groupsX, groupsY, &frameData_, sizeof(frameData_));
        toRead(reservoirSpatialTex_.get());
    }

    // ---- 4. ReSTIR 时间复用（ping-pong 历史读写；静态变体组）----
    if (wantTemporal) {
        toUAV(reservoirFinalTex_.get());
        toUAV(histPos_[nextIdx].get()); toUAV(histNormal_[nextIdx].get());
        toUAV(histReservoir_[nextIdx].get());

        frameData_.step = 0; frameData_.reset = 0;
        dispatchCompute(cmd, frameSlot, restirTemporalPipe_.get(),
                        restirTemporalPass_[histIdx_].group.get(),
                        groupsX, groupsY, &frameData_, sizeof(frameData_));

        toRead(reservoirFinalTex_.get());
        toRead(histPos_[nextIdx].get()); toRead(histNormal_[nextIdx].get());
        toRead(histReservoir_[nextIdx].get());
    }

    // ---- 5. 着色（DispatchRays：ReSTIR 直接光 + PT 间接光）----
    if (wantShade) {
        toUAV(colorTex_.get());
        frameData_.step = 0; frameData_.reset = 0;
        cmd->bindRayTracingPipeline(shadePipe_.get());
        cmd->setBindingGroup(0, frameGroups_[frameSlot].get());
        cmd->setBindingGroup(1, sceneGroup_.get());
        cmd->setBindingGroup(2, shadePass_.group.get());
        cmd->pushConstants(RHIShaderStage::RayTracingAll, 0, sizeof(frameData_), &frameData_);
        cmd->traceRays(shadePipe_.get(), sbtBuf_.get(),
                       1 * kSBTRecordSize, kSBTRecordSize,           // raygen: rgShade
                       3 * kSBTRecordSize, kSBTRecordSize, 1,        // miss: msSkyShade
                       4 * kSBTRecordSize, kSBTRecordSize, 2,        // hit groups
                       width_, height_, 1);
        toRead(colorTex_.get());
    }

    // ---- 6. SVGF：方差估计（ping-pong 颜色/方差历史；静态变体组）----
    if (wantSvgf) {
        toUAV(histColor_[nextIdx].get()); toUAV(histVar_[nextIdx].get());
        frameData_.step = 0; frameData_.reset = historyValid_ ? 0u : 1u;
        dispatchCompute(cmd, frameSlot, svgfVarPipe_.get(),
                        svgfVarPass_[histIdx_].group.get(),
                        groupsX, groupsY, &frameData_, sizeof(frameData_));
        toRead(histColor_[nextIdx].get()); toRead(histVar_[nextIdx].get());
    }

    // ---- 7. SVGF：3 层 à-trous（步长 1/2/4，乒乓；静态组）----
    if (settings.denoiser && wantSvgf) {
        struct StepCfg { RHIBindingGroup* group; RHITexture* output; };
        const StepCfg steps[3] = {
            { atrousStep1_[nextIdx].group.get(), colorFilteredTex_.get() },
            { atrousStep2_.group.get(), colorTmpTex_.get() },
            { atrousStep3_.group.get(), colorFilteredTex_.get() },
        };
        for (int i = 0; i < 3; ++i) {
            toUAV(steps[i].output);
            frameData_.step = static_cast<uint32_t>(1 << i);
            frameData_.reset = 0;
            dispatchCompute(cmd, frameSlot, svgfAtrousPipe_.get(), steps[i].group,
                            groupsX, groupsY, &frameData_, sizeof(frameData_));
            toRead(steps[i].output);
        }
    }

    // ---- 8. Resolve：ACES tonemap → 输出（静态变体：denoiser 开/关）----
    if (wantResolve) {
        const bool denoised = settings.denoiser && wantSvgf;
        toUAV(outputTex_.get());
        frameData_.step = 0; frameData_.reset = 0;
        dispatchCompute(cmd, frameSlot, resolvePipe_.get(),
                        resolvePass_[denoised ? 0 : 1].group.get(),
                        groupsX, groupsY, &frameData_, sizeof(frameData_));
        toRead(outputTex_.get());
    }

    // ---- 帧尾：历史推进 ----
    {
        glm::mat4 viewProj = proj_ * view_;
        glm::vec3 camPos = glm::inverse(view_)[3];
        lastFrameViewProj_ = viewProj;
        lastFrameCamPos_ = camPos;
        histIdx_ = nextIdx;
        historyValid_ = true;
        ++frameIndex_;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    stats_.lastFrameMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
}

} // namespace GRT
} // namespace VEngine
