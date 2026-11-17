/**
 * @file SoftwareRaytracer.cpp
 * @brief CPU 软光追渲染器实现
 *
 * - 场景采集：EnTT 视图遍历 → 扁平化实例/材质/光源（网格按路径去重）
 * - 求交：逐实例逆变换射线到局部空间（方向不归一化 → t 直接对应世界空间）
 * - 积分：Whitted（直接光 + 镜面 + 折射）/ 路径追踪（NEE + 余弦采样 + RR）
 * - 并行：线程池按 32x32 分块
 */

#include "SoftwareRaytracer.h"
#include "ImageWriter.h"

#include "Scene.h"
#include "Components.h"
#include "MeshManager.h"
#include "AssetPath.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <chrono>
#include <iostream>

namespace VEngine {
namespace RT {

namespace {

constexpr uint32_t TILE_SIZE = 32;
constexpr float RAY_EPSILON = 1e-4f;

/// 生成相机光线（NDC → 世界，带亚像素抖动）
inline void generateRay(const glm::mat4& invViewProj,
                        uint32_t px, uint32_t py, uint32_t w, uint32_t h,
                        float jx, float jy,
                        glm::vec3& orig, glm::vec3& dir)
{
    float ndcX = 2.0f * (px + jx) / static_cast<float>(w) - 1.0f;
    float ndcY = 1.0f - 2.0f * (py + jy) / static_cast<float>(h);

    // 近/远平面两点反投影（比直接取相机位置更通用，兼容任意投影）
    glm::vec4 nearP = invViewProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farP  = invViewProj * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    nearP /= nearP.w;
    farP  /= farP.w;

    orig = glm::vec3(nearP);
    dir = glm::normalize(glm::vec3(farP) - glm::vec3(nearP));
}

} // anonymous namespace

// ============================================================
// 构造 / 析构
// ============================================================

SoftwareRaytracer::SoftwareRaytracer() {
    pool_ = std::make_unique<ThreadPool>();
}

SoftwareRaytracer::~SoftwareRaytracer() = default;

// ============================================================
// 场景采集
// ============================================================

void SoftwareRaytracer::buildFromScene(Scene* scene) {
    instances_.clear();
    lights_.clear();
    // 网格缓存（meshes_ + meshIndexByPath_）也重建——资产可能已变化
    meshes_.clear();
    meshIndexByPath_.clear();

    if (!scene) return;

    auto& registry = scene->getRegistry();

    // ---- 可渲染实例 ----
    auto meshView = registry.view<TransformComponent, MeshRendererComponent>();
    for (auto entity : meshView) {
        auto& meshRenderer = meshView.get<MeshRendererComponent>(entity);
        if (!meshRenderer.visible) continue;

        auto gpuMesh = MeshManager::getInstance().getMesh(meshRenderer.meshPath);
        if (!gpuMesh || !gpuMesh->mesh) continue;

        // 材质（透明物体暂不支持，跳过）+ 贴图加载
        RTMaterial material;
        if (registry.all_of<PBRMaterialComponent>(entity)) {
            auto& src = registry.get<PBRMaterialComponent>(entity);
            material.albedo = src.albedo;
            material.metallic = src.metallic;
            material.roughness = src.roughness;
            material.ao = src.ao;
            material.emissive = src.emissive;
            material.emissiveStrength = src.emissiveStrength;
            material.opacity = src.opacity;
            material.transparent = src.isTransparent();

            // 贴图（路径经 resolveAssetPath 归一化，跨平台反斜杠兼容）
            material.albedoTex = loadTextureIndex(src.albedoMap, true);
            material.normalTex = loadTextureIndex(src.normalMap, false);
            material.metallicRoughTex = loadTextureIndex(src.metallicMap, false);
            material.aoTex = loadTextureIndex(src.aoMap, false);
            material.emissiveTex = loadTextureIndex(src.emissiveMap, true);
        }
        if (material.transparent) continue;

        // 网格去重：按 meshPath 建一次三角形数组 + BVH
        auto it = meshIndexByPath_.find(meshRenderer.meshPath);
        uint32_t meshIndex;
        if (it == meshIndexByPath_.end()) {
            const auto& vertices = gpuMesh->mesh->getVertices();
            const auto& indices = gpuMesh->mesh->getIndices();
            if (indices.size() < 3) continue;

            RTMeshData meshData;
            meshData.triangles.reserve(indices.size() / 3);
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                const Vertex& a = vertices[indices[i]];
                const Vertex& b = vertices[indices[i + 1]];
                const Vertex& c = vertices[indices[i + 2]];
                RTTriangle tri;
                tri.v0 = a.pos; tri.v1 = b.pos; tri.v2 = c.pos;
                tri.n0 = a.normal; tri.n1 = b.normal; tri.n2 = c.normal;
                tri.uv0 = a.texCoord; tri.uv1 = b.texCoord; tri.uv2 = c.texCoord;
                tri.t0 = a.tangent; tri.t1 = b.tangent; tri.t2 = c.tangent;
                meshData.triangles.push_back(tri);
            }
            meshIndex = static_cast<uint32_t>(meshes_.size());
            meshData.bvh.build(meshData.triangles.data(),
                               static_cast<uint32_t>(meshData.triangles.size()));
            meshIndexByPath_[meshRenderer.meshPath] = meshIndex;
            meshes_.push_back(std::move(meshData));
        } else {
            meshIndex = it->second;
        }

        // 实例（世界矩阵含父链）
        glm::mat4 world = computeWorldMatrix(registry, entity);
        RTInstance inst;
        inst.meshIndex = meshIndex;
        inst.invWorld = glm::inverse(world);
        inst.normalMatrix = glm::inverseTranspose(glm::mat3(world));
        inst.material = material;
        instances_.push_back(std::move(inst));
    }

    // ---- 光源 ----
    auto lightView = registry.view<TransformComponent, LightComponent>();
    for (auto entity : lightView) {
        auto& transform = lightView.get<TransformComponent>(entity);
        auto& light = lightView.get<LightComponent>(entity);

        glm::mat4 world = computeWorldMatrix(registry, entity);
        RTLight rt;
        rt.type = static_cast<RTLightType>(static_cast<int>(light.type));
        rt.color = light.color;
        rt.intensity = light.intensity;
        rt.position = glm::vec3(world[3]);
        rt.direction = glm::normalize(glm::mat3(world) * transform.getForward());
        rt.constantAttenuation = light.constantAttenuation;
        rt.linearAttenuation = light.linearAttenuation;
        rt.quadraticAttenuation = light.quadraticAttenuation;
        rt.range = light.range;
        rt.innerConeAngle = light.innerConeAngle;
        rt.outerConeAngle = light.outerConeAngle;
        lights_.push_back(rt);
    }

    // 后备光源：与光栅化路径（pbr.frag）一致 — (5,3,3+) 300 强度 1/d² 点光
    if (lights_.empty()) {
        RTLight fallback;
        fallback.type = RTLightType::Point;
        fallback.position = glm::vec3(5.0f, 3.0f, 5.0f);
        fallback.color = glm::vec3(1.0f);
        fallback.intensity = 300.0f;
        fallback.constantAttenuation = 0.0f;
        fallback.linearAttenuation = 0.0f;
        fallback.quadraticAttenuation = 1.0f;   // 物理 1/d²
        fallback.range = 0.0f;                  // 无限远
        lights_.push_back(fallback);
    }

    resetAccumulation();

    // 场景总包围盒（世界空间）
    glm::vec3 sceneMin(std::numeric_limits<float>::max());
    glm::vec3 sceneMax(std::numeric_limits<float>::lowest());
    for (const auto& mesh : meshes_) {
        sceneMin = glm::min(sceneMin, mesh.bvh.boundsMin());
        sceneMax = glm::max(sceneMax, mesh.bvh.boundsMax());
    }
    if (!meshes_.empty()) {
        std::cout << "[SoftwareRaytracer] Scene AABB: min("
                  << sceneMin.x << "," << sceneMin.y << "," << sceneMin.z << ") max("
                  << sceneMax.x << "," << sceneMax.y << "," << sceneMax.z << ")" << std::endl;
    }

    std::cout << "[SoftwareRaytracer] Scene built: " << instances_.size()
              << " instances, " << meshes_.size() << " meshes, "
              << totalTriangles() << " triangles, " << totalNodes() << " BVH nodes, "
              << lights_.size() << " lights, "
              << textures_.size() << " textures ("
              << (textureCache_.totalBytes() / (1024 * 1024)) << " MB)"
              << " (" << pool_->threadCount() << " threads)" << std::endl;
}

// ============================================================
// 相机
// ============================================================

void SoftwareRaytracer::setCamera(const glm::mat4& view, const glm::mat4& proj,
                                  uint32_t width, uint32_t height)
{
    invViewProj_ = glm::inverse(proj * view);
    // view 的逆平移即相机位置（透视投影下近平面中心即视点，用近点更稳妥——
    // 这里直接取 view 逆矩阵第 4 列）
    camPos_ = glm::vec3(glm::inverse(view)[3]);
    if (width_ != width || height_ != height) {
        width_ = width;
        height_ = height;
        fb_.resize(width, height);
        resizeRestirBuffers();
    }
}

void SoftwareRaytracer::resetAccumulation() {
    fb_.reset();
    restirHistoryValid_ = false;   // 相机/场景/设置变化 → 时间历史失效
}

// ============================================================
// 渲染入口
// ============================================================

void SoftwareRaytracer::renderFrame(uint32_t frameIndex) {
    if (width_ == 0 || height_ == 0) return;

    auto startTime = std::chrono::high_resolution_clock::now();

    const int spp = std::max(1, settings.samplesPerPixel);

    // ReSTIR 模式：多 phase 流水（无光源时退化为普通 PT）
    if (settings.algorithm == Algorithm::PathTracingRestir
        && !lights_.empty() && !instances_.empty()) {
        renderFrameRestir(frameIndex, spp);
        fb_.addSamplePass(spp);
        auto endTime = std::chrono::high_resolution_clock::now();
        lastFrameMs_ = std::chrono::duration<float, std::milli>(endTime - startTime).count();
        return;
    }

    // 空场景：直接累积天光（每像素 1 个样本，样本计数一致）
    if (instances_.empty()) {
        for (uint32_t y = 0; y < height_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                glm::vec3 orig, dir;
                generateRay(invViewProj_, x, y, width_, height_, 0.5f, 0.5f, orig, dir);
                fb_.addSample(x, y, skyColor(dir));
            }
        }
        fb_.addSamplePass(1);
        return;
    }

    dispatchTiles([this, frameIndex, spp](uint32_t x0, uint32_t y0,
                                          uint32_t tw, uint32_t th) {
        uint32_t tileSeed = pcgHash((x0 / TILE_SIZE) * 1973u
                                    + (y0 / TILE_SIZE) * 9277u
                                    + frameIndex * 26699u);
        renderTile(x0, y0, tw, th, frameIndex, spp, tileSeed);
    });

    fb_.addSamplePass(spp);

    auto endTime = std::chrono::high_resolution_clock::now();
    lastFrameMs_ = std::chrono::duration<float, std::milli>(endTime - startTime).count();
}

/// 分块派发工具：并行执行 fn(x0,y0,tw,th)，阻塞至全部完成
void SoftwareRaytracer::dispatchTiles(
    const std::function<void(uint32_t, uint32_t, uint32_t, uint32_t)>& fn)
{
    uint32_t tilesX = (width_ + TILE_SIZE - 1) / TILE_SIZE;
    uint32_t tilesY = (height_ + TILE_SIZE - 1) / TILE_SIZE;

    for (uint32_t ty = 0; ty < tilesY; ++ty) {
        for (uint32_t tx = 0; tx < tilesX; ++tx) {
            uint32_t x0 = tx * TILE_SIZE;
            uint32_t y0 = ty * TILE_SIZE;
            uint32_t tw = std::min(TILE_SIZE, width_ - x0);
            uint32_t th = std::min(TILE_SIZE, height_ - y0);
            pool_->enqueue([fn, x0, y0, tw, th] { fn(x0, y0, tw, th); });
        }
    }
    pool_->waitAll();
}

void SoftwareRaytracer::renderTile(uint32_t x0, uint32_t y0, uint32_t tileW, uint32_t tileH,
                                   uint32_t frameIndex, int spp, uint32_t tileSeed)
{
    for (uint32_t y = 0; y < tileH; ++y) {
        for (uint32_t x = 0; x < tileW; ++x) {
            uint32_t px = x0 + x;
            uint32_t py = y0 + y;

            for (int s = 0; s < spp; ++s) {
                uint32_t rng = pcgHash(px + py * width_ + s * 8191u
                                       + tileSeed + frameIndex * 104729u);
                glm::vec3 orig, dir;
                generateRay(invViewProj_, px, py, width_, height_,
                            rand01(rng), rand01(rng), orig, dir);

                // 逐样本直接累积（addSamplePass(spp) 与样本计数严格对应）
                switch (settings.algorithm) {
                    case Algorithm::Whitted:
                        fb_.addSample(px, py, traceWhitted(orig, dir, 0, rng));
                        break;
                    case Algorithm::PathTracing:
                    case Algorithm::PathTracingRestir:   // 无 ReSTIR phase 时按 PT 处理
                        fb_.addSample(px, py, tracePath(orig, dir, rng));
                        break;
                }
            }
        }
    }
}

// ============================================================
// 求交
// ============================================================

bool SoftwareRaytracer::intersectScene(const glm::vec3& orig, const glm::vec3& dir,
                                       float tMax, SceneHit& out) const
{
    rayCount_.fetch_add(1, std::memory_order_relaxed);

    RayHit best;
    bool anyHit = false;

    for (uint32_t i = 0; i < instances_.size(); ++i) {
        const RTInstance& inst = instances_[i];
        const RTMeshData& mesh = meshes_[inst.meshIndex];

        // 射线变换到局部空间：方向不归一化 → 局部 t 直接对应世界 t
        glm::vec3 localOrig = glm::vec3(inst.invWorld * glm::vec4(orig, 1.0f));
        glm::vec3 localDir  = glm::vec3(inst.invWorld * glm::vec4(dir, 0.0f));

        RayHit h;
        if (mesh.bvh.intersect(localOrig, localDir, best.t,
                               h.t, h.u, h.v, h.triangleIndex)) {
            h.instanceIndex = i;
            best = h;
            anyHit = true;
        }
    }

    if (!anyHit) return false;

    const RTInstance& inst = instances_[best.instanceIndex];
    const RTTriangle& tri = meshes_[inst.meshIndex].triangles[best.triangleIndex];

    // 重心坐标插值法线 → 世界空间（逆转置矩阵）
    float w = 1.0f - best.u - best.v;
    glm::vec3 localN = tri.n0 * w + tri.n1 * best.u + tri.n2 * best.v;
    glm::vec3 worldN = glm::normalize(inst.normalMatrix * localN);
    if (!std::isfinite(worldN.x) || !std::isfinite(worldN.y) || !std::isfinite(worldN.z)) {
        // 退化法线兜底：几何面法线
        worldN = glm::normalize(glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
    }
    // 双面：法线朝向射线来向一侧
    if (glm::dot(worldN, dir) > 0.0f) worldN = -worldN;

    out.hit = best;
    out.position = orig + dir * best.t;

    // 逐命中贴图采样（albedo/法线/metallicRoughness/ao/emissive）
    evaluateShadedMaterial(tri, inst, best.u, best.v, worldN, dir, out.shaded);
    return true;
}

// ============================================================
// 贴图 — 加载与逐命中材质求值
// ============================================================

int SoftwareRaytracer::loadTextureIndex(const std::string& path, bool srgb) {
    if (path.empty()) return -1;
    std::string resolved = resolveAssetPath(path);
    auto tex = textureCache_.load(resolved, srgb);
    if (!tex) return -1;

    // 去重下标（同一贴图可能被多材质引用）
    auto it = textureIndexByPath_.find(resolved);
    if (it != textureIndexByPath_.end()) return it->second;

    int index = static_cast<int>(textures_.size());
    textures_.push_back(tex);
    textureIndexByPath_[resolved] = index;
    return index;
}

void SoftwareRaytracer::evaluateShadedMaterial(const RTTriangle& tri, const RTInstance& inst,
                                               float u, float v, const glm::vec3& geomNormal,
                                               const glm::vec3& dir, ShadedMaterial& out) const
{
    const RTMaterial& mat = inst.material;
    float w = 1.0f - u - v;

    out.albedo = mat.albedo;
    out.metallic = mat.metallic;
    out.roughness = mat.roughness;
    out.ao = mat.ao;
    out.emissive = mat.emissive * mat.emissiveStrength;
    out.normal = geomNormal;

    bool anyTex = mat.albedoTex >= 0 || mat.normalTex >= 0 || mat.metallicRoughTex >= 0
               || mat.aoTex >= 0 || mat.emissiveTex >= 0;
    if (!anyTex) return;

    // UV 插值（世界无关，直接用局部 UV）
    glm::vec2 uv = tri.uv0 * w + tri.uv1 * u + tri.uv2 * v;

    if (mat.albedoTex >= 0) {
        out.albedo *= textures_[mat.albedoTex]->sampleRGB(uv.x, uv.y);
    }
    if (mat.metallicRoughTex >= 0) {
        // glTF metallicRoughness 打包：G=roughness B=metallic（因子相乘）
        const CpuTexture& tex = *textures_[mat.metallicRoughTex];
        out.metallic *= tex.sampleChannel(uv.x, uv.y, 2);
        out.roughness *= tex.sampleChannel(uv.x, uv.y, 1);
    }
    if (mat.aoTex >= 0) {
        out.ao *= textures_[mat.aoTex]->sampleChannel(uv.x, uv.y, 0);
    }
    if (mat.emissiveTex >= 0) {
        out.emissive *= textures_[mat.emissiveTex]->sampleRGB(uv.x, uv.y);
    }
    if (mat.normalTex >= 0) {
        // tangent-space 法线贴图：TBN 变换（T 用顶点切线插值，Gram-Schmidt 正交化）
        glm::vec3 localT = tri.t0 * w + tri.t1 * u + tri.t2 * v;
        glm::vec3 T = inst.normalMatrix * localT;
        // 与几何法线正交化（插值/法线贴图扰动后 T 可能不严格正交）
        T = T - geomNormal * glm::dot(geomNormal, T);
        float tLen = glm::length(T);
        if (tLen > 1e-5f) {
            T /= tLen;
            glm::vec3 B = glm::cross(geomNormal, T);

            glm::vec3 tn = textures_[mat.normalTex]->sampleNormal(uv.x, uv.y);
            glm::vec3 mapped = T * tn.x + B * tn.y + geomNormal * tn.z;
            mapped = glm::normalize(mapped);
            if (std::isfinite(mapped.x)) {
                // 钳制在几何法线半球内（视角无关）。原按射线来向翻转会把
                // 掠射视角下强扰动的法线翻到表面以下 → 阴影 ray 自交黑斑
                // 随相机移动（与 GPU 版 rt_common.hlsli 同步修复）。
                if (glm::dot(mapped, geomNormal) < 0.0f) {
                    mapped = mapped - 2.0f * geomNormal * glm::dot(mapped, geomNormal);
                    mapped = glm::normalize(mapped);
                }
                out.normal = mapped;
            }
        }
    }

    // 参数夹取（贴图数据可能越界）
    out.metallic = glm::clamp(out.metallic, 0.0f, 1.0f);
    out.roughness = glm::clamp(out.roughness, 0.03f, 1.0f);
    out.ao = glm::clamp(out.ao, 0.0f, 1.0f);
}

bool SoftwareRaytracer::occluded(const glm::vec3& orig, const glm::vec3& dir,
                                 float tMax) const
{
    rayCount_.fetch_add(1, std::memory_order_relaxed);

    for (const auto& inst : instances_) {
        const RTMeshData& mesh = meshes_[inst.meshIndex];
        glm::vec3 localOrig = glm::vec3(inst.invWorld * glm::vec4(orig, 1.0f));
        glm::vec3 localDir  = glm::vec3(inst.invWorld * glm::vec4(dir, 0.0f));
        if (mesh.bvh.occluded(localOrig, localDir, tMax)) return true;
    }
    return false;
}

// ============================================================
// 天光
// ============================================================

glm::vec3 SoftwareRaytracer::skyColor(const glm::vec3& dir) const {
    float t = 0.5f * (dir.y + 1.0f);
    glm::vec3 horizon(0.75f, 0.78f, 0.80f);
    glm::vec3 zenith(0.25f, 0.45f, 0.80f);
    return glm::mix(horizon, zenith, t) * settings.skyIntensity;
}

// ============================================================
// Whitted 积分器
// ============================================================

glm::vec3 SoftwareRaytracer::directLightingWhitted(const SceneHit& hit, uint32_t& rng) const {
    (void)rng;
    const ShadedMaterial& sm = hit.shaded;
    // 有效漫反射反照率：镜面金属无漫反射；粗糙金属（matte metal）保留 albedo 漫反射
    glm::vec3 diffuseAlbedo = sm.diffuseFactor();
    glm::vec3 result(0.0f);

    for (const auto& light : lights_) {
        glm::vec3 toLight;
        float distance;

        if (light.type == RTLightType::Directional) {
            toLight = -light.direction;
            distance = std::numeric_limits<float>::max();
        } else {
            glm::vec3 diff = light.position - hit.position;
            distance = glm::length(diff);
            if (distance < 1e-6f) continue;
            toLight = diff / distance;
        }

        float nDotL = glm::dot(sm.normal, toLight);
        if (nDotL <= 0.0f) continue;

        // 阴影射线
        if (settings.shadows) {
            float shadowT = distance - 2.0f * RAY_EPSILON;
            if (occluded(hit.position + sm.normal * RAY_EPSILON, toLight, shadowT)) {
                continue;
            }
        }

        result += diffuseAlbedo * (1.0f / 3.14159265f)
                * light.radiance(toLight, distance) * nDotL;
    }
    return result;
}

glm::vec3 SoftwareRaytracer::traceWhitted(const glm::vec3& orig, const glm::vec3& dir,
                                          int depth, uint32_t& rng) const
{
    // 深度限制
    if (depth >= settings.maxBounces) return glm::vec3(0.0f);

    SceneHit hit;
    if (!intersectScene(orig, dir, std::numeric_limits<float>::max(), hit)) {
        return skyColor(dir);
    }

    const ShadedMaterial& sm = hit.shaded;
    float gloss = 1.0f - sm.roughness;
    glm::vec3 result(0.0f);

    // 自发光（ShadedMaterial.emissive 已含贴图与强度）
    result += sm.emissive;

    // 直接光（Lambert 漫反射；粗糙金属保留漫反射分量）
    result += directLightingWhitted(hit, rng);

    // 环境泛光（天光按法线采样 × ao，模拟天光漫射）
    result += sm.albedo * (1.0f - sm.metallic * gloss) * sm.ao
            * skyColor(sm.normal) * 0.15f;

    glm::vec3 viewDir = -dir;
    float cosTheta = glm::dot(sm.normal, viewDir);
    glm::vec3 fresnel = schlickFresnel(sm.f0(), cosTheta);

    // 镜面判定：金属且足够光滑（粗糙金属走漫反射路径）
    if (sm.metallic > 0.5f && sm.roughness < 0.35f) {
        // 金属镜面：反射 + 粗糙度扰动，颜色随光泽度趋近 albedo
        glm::vec3 reflDir = glm::reflect(dir, sm.normal);
        float rough = sm.roughness * sm.roughness;
        if (rough > 1e-3f) {
            // 粗糙镜面：在反射方向附近抖动（cheap glossy）
            glm::vec3 jitter(randSym(rng), randSym(rng), randSym(rng));
            reflDir = glm::normalize(reflDir + jitter * rough * 0.5f);
            if (glm::dot(reflDir, sm.normal) <= 0.0f) reflDir = glm::reflect(dir, sm.normal);
        }
        glm::vec3 kS = glm::mix(fresnel, sm.albedo, gloss);
        result += kS * traceWhitted(
            hit.position + sm.normal * RAY_EPSILON, reflDir, depth + 1, rng);
    } else {
        // 光滑电介质：弱 Fresnel 天光反射（近似高光），无递归
        // （玻璃分支已移除：透明实体在场景采集时即跳过）
        glm::vec3 reflDir = glm::reflect(dir, sm.normal);
        result += fresnel * gloss * gloss * skyColor(reflDir) * 0.3f;
    }

    return result;
}

// ============================================================
// 路径追踪积分器
// ============================================================

glm::vec3 SoftwareRaytracer::estimateDirectLight(const SceneHit& hit, uint32_t& rng) const {
    if (lights_.empty()) return glm::vec3(0.0f);

    // 每次 NEE 采样 2 个光源取平均（方差减半，间接弹射降噪的关键）
    constexpr int NEE_LIGHT_SAMPLES = 2;
    glm::vec3 result(0.0f);

    for (int ls = 0; ls < NEE_LIGHT_SAMPLES; ++ls) {
        // 随机取一个光源（均匀，pdf = 1/N）
        uint32_t lightIdx = static_cast<uint32_t>(rand01(rng) * lights_.size());
        if (lightIdx >= lights_.size()) lightIdx = lights_.size() - 1;
        const RTLight& light = lights_[lightIdx];
        float lightPdf = 1.0f / static_cast<float>(lights_.size());

        glm::vec3 toLight;
        float distance;
        if (light.type == RTLightType::Directional) {
            toLight = -light.direction;
            distance = std::numeric_limits<float>::max();
        } else {
            glm::vec3 diff = light.position - hit.position;
            distance = glm::length(diff);
            if (distance < 1e-6f) continue;
            toLight = diff / distance;
        }

        float nDotL = glm::dot(hit.shaded.normal, toLight);
        if (nDotL <= 0.0f) continue;

        // 阴影射线
        if (settings.shadows) {
            float shadowT = distance - 2.0f * RAY_EPSILON;
            if (occluded(hit.position + hit.shaded.normal * RAY_EPSILON, toLight, shadowT)) {
                continue;
            }
        }

        // Lambert BRDF f = albedo/π；粗糙金属保留漫反射（镜面金属无）；
        // 解析光源（delta light）采样无需立体角 pdf，直接 f × Lo × cosθ / lightPdf
        glm::vec3 f = hit.shaded.diffuseFactor() * (1.0f / 3.14159265f);
        result += f * light.radiance(toLight, distance) * nDotL / lightPdf;
    }
    return result / static_cast<float>(NEE_LIGHT_SAMPLES);
}

glm::vec3 SoftwareRaytracer::tracePath(const glm::vec3& origIn, const glm::vec3& dirIn,
                                       uint32_t& rng) const
{
    SceneHit hit;
    if (!intersectScene(origIn, dirIn, std::numeric_limits<float>::max(), hit)) {
        return skyColor(dirIn);
    }
    return tracePathFrom(hit, dirIn, rng, nullptr);
}

/**
 * @brief 从已求交的主命中继续路径追踪
 *
 * bounce0 的直接光可由 directOverride 注入（ReSTIR 蓄水池估计结果，
 * 含可见性），后续弹射仍走 NEE；间接弹射结构不变。
 */
glm::vec3 SoftwareRaytracer::tracePathFrom(const SceneHit& firstHit, const glm::vec3& firstDir,
                                           uint32_t& rng, const glm::vec3* directOverride) const
{
    glm::vec3 throughput(1.0f);
    glm::vec3 radiance(0.0f);
    glm::vec3 dir = firstDir;
    SceneHit hit = firstHit;
    bool firstBounce = true;

    for (int bounce = 0; bounce < settings.maxBounces; ++bounce) {
        const ShadedMaterial& sm = hit.shaded;

        // 自发光（ShadedMaterial.emissive 已含贴图与强度）
        radiance += throughput * sm.emissive;

        // 直接光：bounce0 可由 ReSTIR 注入，其余弹射走 NEE
        if (firstBounce && directOverride) {
            radiance += throughput * (*directOverride);
        } else {
            radiance += throughput * estimateDirectLight(hit, rng);
        }
        firstBounce = false;

        // ---- BSDF 采样下一条光线 ----
        float cosTheta = glm::dot(sm.normal, -dir);
        glm::vec3 fresnel = schlickFresnel(sm.f0(), cosTheta);
        float gloss = 1.0f - sm.roughness;

        // 镜面概率 = Fresnel 亮度 × 光泽度（粗糙表面镜面瓣坍缩为漫反射），
        // clamp 防退化
        float specProb = glm::clamp(
            (0.2126f * fresnel.r + 0.7152f * fresnel.g + 0.0722f * fresnel.b)
            * (0.1f + 0.9f * gloss * gloss),
            0.05f, 0.95f);

        glm::vec3 newDir;
        if (rand01(rng) < specProb) {
            // 镜面瓣：反射 + 粗糙度扰动（GGX 简化）
            newDir = glm::reflect(dir, sm.normal);
            float rough = sm.roughness * sm.roughness;
            if (rough > 1e-3f) {
                glm::vec3 jitter(randSym(rng), randSym(rng), randSym(rng));
                newDir += jitter * rough;
            }
            newDir = glm::normalize(newDir);
            if (glm::dot(newDir, sm.normal) <= 0.0f) {
                break;   // 反射进表面 → 终止
            }
            // f × cos / pdf ≈ F / specProb（delta 镜面瓣）
            throughput *= fresnel / specProb;
        } else {
            // 漫反射瓣：余弦加权采样
            newDir = cosineSampleHemisphere(sm.normal, rng);
            // Lambert：f × cos / pdf = albedo（(albedo/π)(cos)/(cos/π)）；
            // 粗糙金属保留 albedo 漫反射
            throughput *= sm.albedo * (1.0f - sm.metallic * gloss) * sm.ao / (1.0f - specProb);
        }

        // 偏移起点避免自相交，继续弹射
        glm::vec3 orig = hit.position + sm.normal * RAY_EPSILON;

        SceneHit next;
        if (!intersectScene(orig, newDir, std::numeric_limits<float>::max(), next)) {
            radiance += throughput * skyColor(newDir);
            break;
        }
        hit = next;
        dir = newDir;

        // Russian Roulette：3 次弹射后按吞吐量概率终止
        if (bounce > 2) {
            float pCont = std::clamp(
                std::max(throughput.r, std::max(throughput.g, throughput.b)),
                0.05f, 0.95f);
            if (rand01(rng) > pCont) break;
            throughput /= pCont;
        }
    }
    return radiance;
}

// ============================================================
// ReSTIR — 目标函数与无偏合并
// ============================================================

glm::vec3 SoftwareRaytracer::rhoLightRGB(uint32_t lightIdx, const SurfaceInfo& surf) const {
    if (!surf.valid || lightIdx >= lights_.size()) return glm::vec3(0.0f);
    const RTLight& light = lights_[lightIdx];

    glm::vec3 toLight;
    float distance;
    if (light.type == RTLightType::Directional) {
        toLight = -light.direction;
        distance = std::numeric_limits<float>::max();
    } else {
        glm::vec3 diff = light.position - surf.position;
        distance = glm::length(diff);
        if (distance < 1e-6f) return glm::vec3(0.0f);
        toLight = diff / distance;
    }

    float nDotL = glm::dot(surf.normal, toLight);
    if (nDotL <= 0.0f) return glm::vec3(0.0f);

    // Lambert f = diffuseFactor/π × Le × cosθ（未遮挡）
    return surf.mat.diffuseFactor() * (1.0f / 3.14159265f)
         * light.radiance(toLight, distance) * nDotL;
}

float SoftwareRaytracer::rhoLight(uint32_t lightIdx, const SurfaceInfo& surf) const {
    glm::vec3 rgb = rhoLightRGB(lightIdx, surf);
    return 0.2126f * rgb.r + 0.7152f * rgb.g + 0.0722f * rgb.b;
}

bool SoftwareRaytracer::lightShadowed(uint32_t lightIdx, const SurfaceInfo& surf) const {
    if (lightIdx >= lights_.size()) return true;
    const RTLight& light = lights_[lightIdx];

    glm::vec3 toLight;
    float distance;
    if (light.type == RTLightType::Directional) {
        toLight = -light.direction;
        distance = std::numeric_limits<float>::max();
    } else {
        glm::vec3 diff = light.position - surf.position;
        distance = glm::length(diff);
        if (distance < 1e-6f) return false;
        toLight = diff / distance;
    }
    return occluded(surf.position + surf.normal * RAY_EPSILON, toLight,
                    distance - 2.0f * RAY_EPSILON);
}

/**
 * @brief 蓄水池合并（shift 校正池化，无偏）
 *
 * 输入 count 个独立蓄水池（各建在自己的表面 surfs[r] 上，输出样本 y_r），
 * 输出目标为 target 表面的合并蓄水池。
 *
 * 数学（候选池化）：蓄水池 r 的第 j 个候选权重为 ρ̂_r(x_j)/p(x_j)，
 * 移位到目标表面后等价于 ρ̂_t(x_j)/p(x_j) —— 恰为以 target 为目标函数的
 * 合法 RIS 权重。因此合并 = wSum 直接相加（各候选先乘 shift 因子）：
 *   w_r = wSum_r × ρ̂_t(y_r)/ρ̂_r(y_r)
 *   合并后 W = Σw_r / (M'·ρ̂_t(y'))，M' = ΣM_r
 * 同表面（shift=1）时退化为朴素池化 —— 无偏且方差最小。
 * 几何相似性拒绝在调用方完成（与样本值无关，不影响无偏性）。
 *
 * 注：GRIS pairwise-MIS（c_r 折扣双方 wSum）在同目标情形会使 W 减半
 * （M 翻倍而 wSum 被 1/2 缩放），链式时间合并导致估计几何衰减 —— 已弃用。
 */
void SoftwareRaytracer::gatherMergeReservoirs(const Reservoir* const* reservoirs,
                                              const SurfaceInfo* const* surfs,
                                              int count, const SurfaceInfo& target,
                                              Reservoir& out, uint32_t& rng) const
{
    if (count > 9) count = 9;
    out.reset();

    for (int r = 0; r < count; ++r) {
        const Reservoir& R = *reservoirs[r];
        if (!R.valid() || R.W <= 0.0f) continue;

        float rhoAtTarget = rhoLight(R.sample, target);
        if (rhoAtTarget <= 0.0f) continue;   // 目标表面无贡献 → 跳过
        float rhoOwn = rhoLight(R.sample, *surfs[r]);
        if (rhoOwn <= 0.0f) continue;

        // 移位后的候选权重和
        float w = R.wSum * (rhoAtTarget / rhoOwn);
        out.M += R.M;
        out.wSum += w;
        if (w > 0.0f && rand01(rng) < w / out.wSum) {
            out.sample = R.sample;
        }
    }

    // M 跨帧累积上限（防 float 溢出，行业惯例）
    if (out.M > 1e6f) out.M = 1e6f;

    if (out.valid()) {
        float rhoS = rhoLight(out.sample, target);
        out.W = out.wSum / (out.M * std::max(rhoS, 1e-20f));
    }
}

void SoftwareRaytracer::resizeRestirBuffers() {
    size_t n = static_cast<size_t>(width_) * height_;
    restirGBuffer_.assign(n, SurfaceInfo{});
    restirInitial_.assign(n, Reservoir{});
    restirSpatial_.assign(n, Reservoir{});
    restirFinal_.assign(n, Reservoir{});
    denoiseA_.assign(n, glm::vec3(0.0f));
    denoiseB_.assign(n, glm::vec3(0.0f));
    denoiseVar_.assign(n, 0.0f);
    restirPrev_.clear();
    restirPrevGBuffer_.clear();
    restirHistoryValid_ = false;
}

// ============================================================
// 降噪器 — à-trous 联合双边滤波（GBuffer 边缘保持，SVGF 简化版）
// ============================================================

std::vector<uint8_t> SoftwareRaytracer::resolveForOutput() {
    // 降噪仅 ReSTIR 模式可用（需逐像素 GBuffer）
    if (settings.restirDenoiser
        && settings.algorithm == Algorithm::PathTracingRestir
        && !instances_.empty() && !lights_.empty()) {
        denoiseHDR();
        return Framebuffer::tonemapRGBA8(denoiseA_, width_, height_, settings.exposure);
    }
    return fb_.resolveRGBA8(settings.exposure);
}

void SoftwareRaytracer::denoiseHDR() {
    // 初始化：HDR 平均值 + 噪声方差图（统计方差，随样本数自动收缩）
    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            size_t idx = static_cast<size_t>(y) * width_ + x;
            denoiseA_[idx] = fb_.pixel(x, y);
            denoiseVar_[idx] = fb_.pixelVariance(x, y);
        }
    }

    // 方差图 3×3 平滑（稳定估计，SVGF 惯例）→ denoiseVar_ 原地滤波
    std::vector<float> smoothed(denoiseVar_.size(), 0.0f);
    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            float sum = 0.0f;
            int n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = static_cast<int>(x) + dx;
                    int ny = static_cast<int>(y) + dy;
                    if (nx < 0 || ny < 0 || nx >= static_cast<int>(width_)
                        || ny >= static_cast<int>(height_)) continue;
                    sum += denoiseVar_[static_cast<size_t>(ny) * width_ + nx];
                    ++n;
                }
            }
            smoothed[static_cast<size_t>(y) * width_ + x] = sum / std::max(n, 1);
        }
    }
    denoiseVar_ = std::move(smoothed);

    // 3 层 à-trous：步长 1 → 2 → 4（平坦噪声区抹平，几何/颜色边缘保持）
    for (int step : {1, 2, 4}) {
        dispatchTiles([this, step](uint32_t x0, uint32_t y0, uint32_t tw, uint32_t th) {
            denoiseFilterBlock(denoiseA_, denoiseB_, x0, y0, tw, th, step);
        });
        std::swap(denoiseA_, denoiseB_);
    }
}

void SoftwareRaytracer::denoiseFilterBlock(const std::vector<glm::vec3>& src,
                                           std::vector<glm::vec3>& dst,
                                           uint32_t x0, uint32_t y0, uint32_t tw,
                                           uint32_t th, int step)
{
    // 5x5 二项式空间核（归一化，中心权重 36/256）
    static const float spatialKernel[5][5] = {
        { 1.0f / 256.0f, 4.0f / 256.0f,  6.0f / 256.0f, 4.0f / 256.0f, 1.0f / 256.0f },
        { 4.0f / 256.0f, 16.0f / 256.0f, 24.0f / 256.0f, 16.0f / 256.0f, 4.0f / 256.0f },
        { 6.0f / 256.0f, 24.0f / 256.0f, 36.0f / 256.0f, 24.0f / 256.0f, 6.0f / 256.0f },
        { 4.0f / 256.0f, 16.0f / 256.0f, 24.0f / 256.0f, 16.0f / 256.0f, 4.0f / 256.0f },
        { 1.0f / 256.0f, 4.0f / 256.0f,  6.0f / 256.0f, 4.0f / 256.0f, 1.0f / 256.0f },
    };

    for (uint32_t y = 0; y < th; ++y) {
        for (uint32_t x = 0; x < tw; ++x) {
            uint32_t px = x0 + x;
            uint32_t py = y0 + y;
            size_t idx = static_cast<size_t>(py) * width_ + px;

            const SurfaceInfo& s0 = restirGBuffer_[idx];
            if (!s0.valid) {
                dst[idx] = src[idx];   // 天空确定性 → 不滤
                continue;
            }

            // ---- SVGF 颜色权重（方差自适应 + 相对亮度上限钳制）----
            // 方差含像素内 AA 纹理展布（细节区被撑大 → σ_c 过大 → 糊），
            // 用局部亮度的固定比例钳制上限：保证 >3·cap·lum 的对比边缘
            // （地砖缝隙等真实细节）恒被拒绝，噪声（≤cap 量级）放行。
            // 参数可由环境变量临时校准（RT_SVGF_G / RT_SVGF_CAP / RT_SVGF_POW）
            float var = denoiseVar_[idx];
            float lum0 = luminance(src[idx]);
            static const float svgfG = [] {
                const char* e = getenv("RT_SVGF_G");
                return e ? static_cast<float>(atof(e)) : 1.33f;
            }();
            static const float svgfCap = [] {
                const char* e = getenv("RT_SVGF_CAP");
                return e ? static_cast<float>(atof(e)) : 0.15f;
            }();
            static const float svgfPow = [] {
                const char* e = getenv("RT_SVGF_POW");
                return e ? static_cast<float>(atof(e)) : 1.0f;
            }();
            float stepScale = std::pow(static_cast<float>(step), svgfPow);
            float capLum = svgfCap * std::max(lum0, 1e-6f);
            float sigmaC2 = std::min(svgfG * var * stepScale, capLum * capLum) + 1e-8f;
            float invTwoSigmaC2 = 1.0f / (2.0f * sigmaC2);

            // 深度容忍度随步长放大（远处梯度平缓）
            float sigmaDepth = 0.02f * s0.depth * static_cast<float>(step);
            float invTwoSigmaD2 = 1.0f / (2.0f * sigmaDepth * sigmaDepth + 1e-20f);

            glm::vec3 sum(0.0f);
            float wsum = 0.0f;

            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    int nx = static_cast<int>(px) + dx * step;
                    int ny = static_cast<int>(py) + dy * step;
                    if (nx < 0 || ny < 0 || nx >= static_cast<int>(width_)
                        || ny >= static_cast<int>(height_)) continue;

                    size_t nidx = static_cast<size_t>(ny) * width_ + nx;
                    const SurfaceInfo& s1 = restirGBuffer_[nidx];
                    if (!s1.valid) continue;   // 不跨天空边界

                    // 法线权重（幂次锐化，几何边缘保持）
                    float nDot = glm::max(0.0f, glm::dot(s0.normal, s1.normal));
                    float wn = std::pow(nDot, 32.0f);

                    // 深度权重（高斯）
                    float dd = s1.depth - s0.depth;
                    float wd = std::exp(-dd * dd * invTwoSigmaD2);

                    // 颜色权重（SVGF 核心）：亮度差按噪声尺度归一 —
                    // 真实纹理边缘 |Δl| >> σ_c → 拒绝；噪声 |Δl| ~ σ_c → 放行
                    float dl = luminance(src[nidx]) - lum0;
                    float wc = std::exp(-dl * dl * invTwoSigmaC2);

                    float w = spatialKernel[dy + 2][dx + 2] * wn * wd * wc;
                    if (w <= 0.0f) continue;

                    sum += src[nidx] * w;
                    wsum += w;
                }
            }

            dst[idx] = wsum > 0.0f ? sum / wsum : src[idx];
        }
    }
}

// ============================================================
// ReSTIR — 渲染 phases
// ============================================================

void SoftwareRaytracer::renderFrameRestir(uint32_t frameIndex, int spp) {
    // Phase A: 主命中 GBuffer + 初始候选蓄水池
    dispatchTiles([this, frameIndex](uint32_t x0, uint32_t y0, uint32_t tw, uint32_t th) {
        restirPhaseInitial(x0, y0, tw, th, frameIndex);
    });

    // Phase B: 空间复用（读 initial + GBuffer，写 spatial）
    dispatchTiles([this, frameIndex](uint32_t x0, uint32_t y0, uint32_t tw, uint32_t th) {
        restirPhaseSpatial(x0, y0, tw, th, frameIndex);
    });

    // Phase C: 时间复用（读 spatial + prev，写 final）
    dispatchTiles([this, frameIndex](uint32_t x0, uint32_t y0, uint32_t tw, uint32_t th) {
        restirPhaseTemporal(x0, y0, tw, th, frameIndex);
    });

    // Phase D: 着色（直接光 = 蓄水池估计；间接光走路径追踪）
    dispatchTiles([this, frameIndex, spp](uint32_t x0, uint32_t y0, uint32_t tw, uint32_t th) {
        restirPhaseShade(x0, y0, tw, th, frameIndex, spp);
    });

    // Phase E: 保存历史（时间复用用）
    restirPrev_ = restirFinal_;
    restirPrevGBuffer_ = restirGBuffer_;
    prevViewProj_ = glm::inverse(invViewProj_);   // invViewProj = inverse(viewProj)
    prevCamPos_ = camPos_;
    restirHistoryValid_ = true;
}

/// 逐像素 rng 种子（各 phase 一致）
static inline uint32_t pixelRngSeed(uint32_t px, uint32_t py, uint32_t w,
                                    uint32_t frameIndex) {
    return pcgHash(px + py * w * 7919u + frameIndex * 104729u);
}

void SoftwareRaytracer::restirPhaseInitial(uint32_t x0, uint32_t y0, uint32_t tw,
                                           uint32_t th, uint32_t frameIndex)
{
    const int M = std::max(1, settings.restirCandidates);
    const uint32_t lightCount = static_cast<uint32_t>(lights_.size());

    for (uint32_t y = 0; y < th; ++y) {
        for (uint32_t x = 0; x < tw; ++x) {
            uint32_t px = x0 + x;
            uint32_t py = y0 + y;
            size_t idx = static_cast<size_t>(py) * width_ + px;

            uint32_t rng = pixelRngSeed(px, py, width_, frameIndex);
            float jx = rand01(rng);
            float jy = rand01(rng);

            SurfaceInfo& surf = restirGBuffer_[idx];
            Reservoir& res = restirInitial_[idx];
            res.reset();

            glm::vec3 orig, dir;
            generateRay(invViewProj_, px, py, width_, height_, jx, jy, orig, dir);

            SceneHit hit;
            if (!intersectScene(orig, dir, std::numeric_limits<float>::max(), hit)) {
                surf = SurfaceInfo{};   // valid=false → 着色 phase 出天光
                continue;
            }

            surf.position = hit.position;
            surf.normal = hit.shaded.normal;
            surf.mat = hit.shaded;          // 含贴图采样结果（mat.normal == surf.normal）
            surf.depth = glm::length(hit.position - orig);
            surf.instanceIndex = hit.hit.instanceIndex;
            surf.valid = true;

            // 初始候选：均匀采样光源 p=1/N，w = ρ̂/p
            for (int c = 0; c < M; ++c) {
                uint32_t li = std::min(lightCount - 1,
                    static_cast<uint32_t>(rand01(rng) * lightCount));
                float w = rhoLight(li, surf) * static_cast<float>(lightCount);
                if (w > 0.0f && settings.restirShadowInTarget && lightShadowed(li, surf)) {
                    w = 0.0f;   // 可见性计入目标函数（贵但阴影更准）
                }
                res.update(li, w, rng);
            }

            if (res.valid()) {
                float rhoS = rhoLight(res.sample, surf);
                res.W = res.wSum / (res.M * std::max(rhoS, 1e-20f));
            }
        }
    }
}

void SoftwareRaytracer::restirPhaseSpatial(uint32_t x0, uint32_t y0, uint32_t tw,
                                           uint32_t th, uint32_t frameIndex)
{
    const int K = std::clamp(settings.restirSpatialNeighbors, 0, 8);

    for (uint32_t y = 0; y < th; ++y) {
        for (uint32_t x = 0; x < tw; ++x) {
            uint32_t px = x0 + x;
            uint32_t py = y0 + y;
            size_t idx = static_cast<size_t>(py) * width_ + px;
            uint32_t rng = pixelRngSeed(px, py, width_, frameIndex * 31u + 7u);

            Reservoir& out = restirSpatial_[idx];
            out.reset();
            const SurfaceInfo& surf = restirGBuffer_[idx];
            if (!surf.valid || !restirInitial_[idx].valid()) continue;

            // 候选集合：自身 + K 个随机邻居（读相邻像素 → 需与写入分离，本 phase 只写自己）
            const Reservoir* cands[9];
            const SurfaceInfo* surfs[9];
            int n = 0;
            cands[n] = &restirInitial_[idx];
            surfs[n] = &surf;
            ++n;

            for (int k = 0; k < K; ++k) {
                // 偏移 ±4..±12 px（小半径提高曲面接受率）
                int dx = static_cast<int>(rand01(rng) * 9.0f) + 4;
                int dy = static_cast<int>(rand01(rng) * 9.0f) + 4;
                if (rand01(rng) < 0.5f) dx = -dx;
                if (rand01(rng) < 0.5f) dy = -dy;
                int nx = static_cast<int>(px) + dx;
                int ny = static_cast<int>(py) + dy;
                if (nx < 0 || ny < 0 || nx >= static_cast<int>(width_)
                    || ny >= static_cast<int>(height_)) continue;

                size_t nidx = static_cast<size_t>(ny) * width_ + nx;
                const Reservoir& nr = restirInitial_[nidx];
                const SurfaceInfo& ns = restirGBuffer_[nidx];
                if (!ns.valid || !nr.valid()) continue;

                // 几何相似性拒绝（与样本值无关 → 不影响力学无偏性）
                // 平面距离测试容忍斜面深度梯度；相对深度兜底
                glm::vec3 dp = ns.position - surf.position;
                float planeDist = std::abs(glm::dot(surf.normal, dp));
                bool depthOK = (std::abs(ns.depth - surf.depth) < 0.2f * surf.depth)
                            || (planeDist < 0.1f * surf.depth);
                if (!depthOK) continue;
                if (glm::dot(ns.normal, surf.normal) < 0.75f) continue;

                cands[n] = &nr;
                surfs[n] = &ns;
                ++n;
            }

            gatherMergeReservoirs(cands, surfs, n, surf, out, rng);
        }
    }
}

void SoftwareRaytracer::restirPhaseTemporal(uint32_t x0, uint32_t y0, uint32_t tw,
                                            uint32_t th, uint32_t frameIndex)
{
    for (uint32_t y = 0; y < th; ++y) {
        for (uint32_t x = 0; x < tw; ++x) {
            uint32_t px = x0 + x;
            uint32_t py = y0 + y;
            size_t idx = static_cast<size_t>(py) * width_ + px;
            uint32_t rng = pixelRngSeed(px, py, width_, frameIndex * 131u + 17u);

            Reservoir& out = restirFinal_[idx];
            const Reservoir& spatial = restirSpatial_[idx];
            if (!spatial.valid()) {
                out.reset();
                continue;
            }

            // 无历史 / 关闭时间复用 → 直通
            if (!settings.restirTemporal || !restirHistoryValid_
                || restirPrev_.empty() || restirPrevGBuffer_.empty())
            {
                out = spatial;
                continue;
            }

            const SurfaceInfo& surf = restirGBuffer_[idx];
            if (!surf.valid) {
                out = spatial;
                continue;
            }

            // 重投影：当前表面点 → 上一帧屏幕坐标
            glm::vec4 clip = prevViewProj_ * glm::vec4(surf.position, 1.0f);
            if (clip.w <= 0.0f) { out = spatial; continue; }
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) {
                out = spatial;
                continue;
            }
            float fx = (ndc.x + 1.0f) * 0.5f * width_;
            float fy = (1.0f - ndc.y) * 0.5f * height_;   // GL Y-up → 行序 top-down
            int nx = std::clamp(static_cast<int>(fx), 0, static_cast<int>(width_) - 1);
            int ny = std::clamp(static_cast<int>(fy), 0, static_cast<int>(height_) - 1);
            size_t nidx = static_cast<size_t>(ny) * width_ + nx;

            const SurfaceInfo& ps = restirPrevGBuffer_[nidx];
            const Reservoir& pr = restirPrev_[nidx];
            if (!ps.valid || !pr.valid()) { out = spatial; continue; }

            // 深度/法线连续性验证（拒绝不连续表面，避免拖影）
            float dPrev = glm::length(surf.position - prevCamPos_);
            if (std::abs(dPrev - ps.depth) > 0.1f * ps.depth) { out = spatial; continue; }
            if (glm::dot(ps.normal, surf.normal) < 0.9f) { out = spatial; continue; }

            // GRIS 合并：本帧空间结果 + 上一帧蓄水池（目标 = 当前表面）
            const Reservoir* cands[2] = { &spatial, &pr };
            const SurfaceInfo* surfs[2] = { &surf, &ps };
            gatherMergeReservoirs(cands, surfs, 2, surf, out, rng);
            if (!out.valid()) out = spatial;
        }
    }
}

void SoftwareRaytracer::restirPhaseShade(uint32_t x0, uint32_t y0, uint32_t tw,
                                         uint32_t th, uint32_t frameIndex, int spp)
{
    for (uint32_t y = 0; y < th; ++y) {
        for (uint32_t x = 0; x < tw; ++x) {
            uint32_t px = x0 + x;
            uint32_t py = y0 + y;
            size_t idx = static_cast<size_t>(py) * width_ + px;

            // 复现 Phase A 的主光线（同一种子的前两次 rand01 即抖动）
            uint32_t rng = pixelRngSeed(px, py, width_, frameIndex);
            float jx = rand01(rng);
            float jy = rand01(rng);
            glm::vec3 orig, dir;
            generateRay(invViewProj_, px, py, width_, height_, jx, jy, orig, dir);

            const SurfaceInfo& surf = restirGBuffer_[idx];
            if (!surf.valid) {
                fb_.addSample(px, py, skyColor(dir));
                continue;
            }

            // ---- ReSTIR 直接光：W × g(y) × V(y)（1 条阴影射线）----
            // 贡献钳制（firefly 控制，行业惯例）：单样本直接光亮度上限
            const Reservoir& res = restirFinal_[idx];
            glm::vec3 direct(0.0f);
            if (res.valid() && res.W > 0.0f) {
                glm::vec3 rgb = rhoLightRGB(res.sample, surf);
                if (glm::dot(rgb, rgb) > 0.0f
                    && (!settings.shadows || !lightShadowed(res.sample, surf))) {
                    direct = res.W * rgb;
                    float lum = 0.2126f * direct.r + 0.7152f * direct.g + 0.0722f * direct.b;
                    const float DIRECT_CLAMP = 8.0f;
                    if (lum > DIRECT_CLAMP) direct *= DIRECT_CLAMP / lum;
                }
            }

            // ---- 间接光 + 后续弹射（bounce0 直接光已注入）----
            // spp 个间接样本，直接光随每个样本计入（相关但无偏，均值正确）
            SceneHit hit;
            hit.hit.instanceIndex = surf.instanceIndex;
            hit.hit.t = surf.depth;
            hit.position = surf.position;
            hit.shaded = surf.mat;      // 含贴图采样结果
            hit.shaded.normal = surf.normal;

            glm::vec3 total(0.0f);
            for (int s = 0; s < spp; ++s) {
                uint32_t rngS = pcgHash(pixelRngSeed(px, py, width_, frameIndex)
                                         + static_cast<uint32_t>(s) * 8191u);
                fb_.addSample(px, py, tracePathFrom(hit, dir, rngS, &direct));
            }
        }
    }
}

// ============================================================
// 统计
// ============================================================

uint32_t SoftwareRaytracer::totalTriangles() const {
    uint32_t total = 0;
    for (const auto& mesh : meshes_) total += mesh.bvh.triangleCount();
    return total;
}

uint32_t SoftwareRaytracer::totalNodes() const {
    uint32_t total = 0;
    for (const auto& mesh : meshes_) total += mesh.bvh.nodeCount();
    return total;
}

} // namespace RT
} // namespace VEngine
