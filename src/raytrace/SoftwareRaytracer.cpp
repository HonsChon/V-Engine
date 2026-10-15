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

        // 材质（透明物体暂不支持，跳过）
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
                meshData.triangles.push_back({ a.pos, b.pos, c.pos,
                                               a.normal, b.normal, c.normal });
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
              << lights_.size() << " lights"
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
    }
}

void SoftwareRaytracer::resetAccumulation() {
    fb_.reset();
}

// ============================================================
// 渲染入口
// ============================================================

void SoftwareRaytracer::renderFrame(uint32_t frameIndex) {
    if (width_ == 0 || height_ == 0) return;

    auto startTime = std::chrono::high_resolution_clock::now();

    const int spp = std::max(1, settings.samplesPerPixel);

    // 空场景：直接累积天光（保持帧同步语义，避免 sampleCount 不一致）
    if (instances_.empty()) {
        for (uint32_t y = 0; y < height_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                glm::vec3 orig, dir;
                generateRay(invViewProj_, x, y, width_, height_, 0.5f, 0.5f, orig, dir);
                fb_.addSample(x, y, skyColor(dir));
            }
        }
        fb_.addSamplePass(spp);
        return;
    }

    uint32_t tilesX = (width_ + TILE_SIZE - 1) / TILE_SIZE;
    uint32_t tilesY = (height_ + TILE_SIZE - 1) / TILE_SIZE;

    for (uint32_t ty = 0; ty < tilesY; ++ty) {
        for (uint32_t tx = 0; tx < tilesX; ++tx) {
            uint32_t x0 = tx * TILE_SIZE;
            uint32_t y0 = ty * TILE_SIZE;
            uint32_t tw = std::min(TILE_SIZE, width_ - x0);
            uint32_t th = std::min(TILE_SIZE, height_ - y0);
            uint32_t tileSeed = pcgHash(tx * 1973u + ty * 9277u + frameIndex * 26699u);
            pool_->enqueue([this, x0, y0, tw, th, frameIndex, spp, tileSeed] {
                renderTile(x0, y0, tw, th, frameIndex, spp, tileSeed);
            });
        }
    }
    pool_->waitAll();

    fb_.addSamplePass(spp);

    auto endTime = std::chrono::high_resolution_clock::now();
    lastFrameMs_ = std::chrono::duration<float, std::milli>(endTime - startTime).count();
}

void SoftwareRaytracer::renderTile(uint32_t x0, uint32_t y0, uint32_t tileW, uint32_t tileH,
                                   uint32_t frameIndex, int spp, uint32_t tileSeed)
{
    for (uint32_t y = 0; y < tileH; ++y) {
        for (uint32_t x = 0; x < tileW; ++x) {
            uint32_t px = x0 + x;
            uint32_t py = y0 + y;
            glm::vec3 color(0.0f);

            for (int s = 0; s < spp; ++s) {
                uint32_t rng = pcgHash(px + py * width_ + s * 8191u
                                       + tileSeed + frameIndex * 104729u);
                glm::vec3 orig, dir;
                generateRay(invViewProj_, px, py, width_, height_,
                            rand01(rng), rand01(rng), orig, dir);

                switch (settings.algorithm) {
                    case Algorithm::Whitted:
                        color += traceWhitted(orig, dir, 0, rng);
                        break;
                    case Algorithm::PathTracing:
                        color += tracePath(orig, dir, rng);
                        break;
                }
            }
            color /= static_cast<float>(spp);
            fb_.addSample(px, py, color);
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
    out.normal = worldN;
    out.material = &inst.material;
    return true;
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
    const RTMaterial& m = *hit.material;
    // 有效漫反射反照率：镜面金属无漫反射；粗糙金属（matte metal）保留 albedo 漫反射
    // （Sponza 等资产常见 metallic=1 + roughness=1 的哑光金属，纯镜面处理会全黑）
    float gloss = 1.0f - m.roughness;
    glm::vec3 diffuseAlbedo = m.albedo * (1.0f - m.metallic * gloss) * m.ao;
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

        float nDotL = glm::dot(hit.normal, toLight);
        if (nDotL <= 0.0f) continue;

        // 阴影射线
        if (settings.shadows) {
            float shadowT = distance - 2.0f * RAY_EPSILON;
            if (occluded(hit.position + hit.normal * RAY_EPSILON, toLight, shadowT)) {
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

    const RTMaterial& m = *hit.material;
    float gloss = 1.0f - m.roughness;
    glm::vec3 result(0.0f);

    // 自发光
    result += m.emissive * m.emissiveStrength;

    // 直接光（Lambert 漫反射；粗糙金属保留漫反射分量）
    result += directLightingWhitted(hit, rng);

    // 环境泛光（天光按法线采样 × ao，模拟天光漫射）
    result += m.albedo * (1.0f - m.metallic * gloss) * m.ao
            * skyColor(hit.normal) * 0.15f;

    glm::vec3 viewDir = -dir;
    float cosTheta = glm::dot(hit.normal, viewDir);
    glm::vec3 fresnel = schlickFresnel(m.f0(), cosTheta);

    // 镜面判定：金属且足够光滑（粗糙金属走漫反射路径）
    if (m.metallic > 0.5f && m.roughness < 0.35f) {
        // 金属镜面：反射 + 粗糙度扰动，颜色随光泽度趋近 albedo
        glm::vec3 reflDir = glm::reflect(dir, hit.normal);
        float rough = m.roughness * m.roughness;
        if (rough > 1e-3f) {
            // 粗糙镜面：在反射方向附近抖动（cheap glossy）
            glm::vec3 jitter(randSym(rng), randSym(rng), randSym(rng));
            reflDir = glm::normalize(reflDir + jitter * rough * 0.5f);
            if (glm::dot(reflDir, hit.normal) <= 0.0f) reflDir = glm::reflect(dir, hit.normal);
        }
        glm::vec3 kS = glm::mix(fresnel, m.albedo, gloss);
        result += kS * traceWhitted(
            hit.position + hit.normal * RAY_EPSILON, reflDir, depth + 1, rng);
    } else if (m.opacity < 1.0f) {
        // 电介质玻璃：Fresnel 分裂反射/折射两路（各按权重递归）
        float eta = cosTheta > 0.0f ? 1.0f / 1.5f : 1.5f;   // 空气↔玻璃
        glm::vec3 refrDir = glm::refract(dir, hit.normal, eta);
        float kr = 0.04f + 0.96f * std::pow(1.0f - std::abs(cosTheta), 5.0f);
        glm::vec3 tint = glm::mix(glm::vec3(1.0f), m.albedo, 0.5f);

        if (glm::length(refrDir) > 0.5f) {
            result += tint * (1.0f - kr) * traceWhitted(
                hit.position - hit.normal * RAY_EPSILON, refrDir, depth + 1, rng);
        }
        glm::vec3 reflDir = glm::reflect(dir, hit.normal);
        result += tint * kr * traceWhitted(
            hit.position + hit.normal * RAY_EPSILON, reflDir, depth + 1, rng);
    } else {
        // 光滑电介质：弱 Fresnel 天光反射（近似高光），无递归
        glm::vec3 reflDir = glm::reflect(dir, hit.normal);
        result += fresnel * gloss * gloss * skyColor(reflDir) * 0.3f;
    }

    return result;
}

// ============================================================
// 路径追踪积分器
// ============================================================

glm::vec3 SoftwareRaytracer::estimateDirectLight(const SceneHit& hit, uint32_t& rng) const {
    if (lights_.empty()) return glm::vec3(0.0f);

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
        if (distance < 1e-6f) return glm::vec3(0.0f);
        toLight = diff / distance;
    }

    float nDotL = glm::dot(hit.normal, toLight);
    if (nDotL <= 0.0f) return glm::vec3(0.0f);

    // 阴影射线
    if (settings.shadows) {
        float shadowT = distance - 2.0f * RAY_EPSILON;
        if (occluded(hit.position + hit.normal * RAY_EPSILON, toLight, shadowT)) {
            return glm::vec3(0.0f);
        }
    }

    // Lambert BRDF f = albedo/π；粗糙金属保留漫反射（镜面金属无）；
    // 解析光源（delta light）采样无需立体角 pdf，直接 f × Lo × cosθ / lightPdf
    const RTMaterial& m = *hit.material;
    float gloss = 1.0f - m.roughness;
    glm::vec3 f = m.albedo * (1.0f / 3.14159265f) * (1.0f - m.metallic * gloss) * m.ao;
    return f * light.radiance(toLight, distance) * nDotL / lightPdf;
}

glm::vec3 SoftwareRaytracer::tracePath(const glm::vec3& origIn, const glm::vec3& dirIn,
                                       uint32_t& rng) const
{
    glm::vec3 throughput(1.0f);
    glm::vec3 radiance(0.0f);
    glm::vec3 orig = origIn;
    glm::vec3 dir = dirIn;

    for (int bounce = 0; bounce < settings.maxBounces; ++bounce) {
        SceneHit hit;
        if (!intersectScene(orig, dir, std::numeric_limits<float>::max(), hit)) {
            radiance += throughput * skyColor(dir);
            break;
        }

        const RTMaterial& m = *hit.material;

        // 自发光（解析光源不对应几何，无双重计数问题）
        radiance += throughput * m.emissive * m.emissiveStrength;

        // NEE 直接光
        radiance += throughput * estimateDirectLight(hit, rng);

        // ---- BSDF 采样下一条光线 ----
        float cosTheta = glm::dot(hit.normal, -dir);
        glm::vec3 fresnel = schlickFresnel(m.f0(), cosTheta);
        float gloss = 1.0f - m.roughness;

        // 镜面概率 = Fresnel 亮度 × 光泽度（粗糙表面镜面瓣坍缩为漫反射），
        // clamp 防退化
        float specProb = glm::clamp(
            (0.2126f * fresnel.r + 0.7152f * fresnel.g + 0.0722f * fresnel.b)
            * (0.1f + 0.9f * gloss * gloss),
            0.05f, 0.95f);

        glm::vec3 newDir;
        if (rand01(rng) < specProb) {
            // 镜面瓣：反射 + 粗糙度扰动（GGX 简化）
            newDir = glm::reflect(dir, hit.normal);
            float rough = m.roughness * m.roughness;
            if (rough > 1e-3f) {
                glm::vec3 jitter(randSym(rng), randSym(rng), randSym(rng));
                newDir += jitter * rough;
            }
            newDir = glm::normalize(newDir);
            if (glm::dot(newDir, hit.normal) <= 0.0f) {
                break;   // 反射进表面 → 终止
            }
            // f × cos / pdf ≈ F / specProb（delta 镜面瓣）
            throughput *= fresnel / specProb;
        } else {
            // 漫反射瓣：余弦加权采样
            newDir = cosineSampleHemisphere(hit.normal, rng);
            // Lambert：f × cos / pdf = albedo（(albedo/π)(cos)/(cos/π)）；
            // 粗糙金属保留 albedo 漫反射
            throughput *= m.albedo * (1.0f - m.metallic * gloss) * m.ao / (1.0f - specProb);
        }

        // 偏移起点避免自相交，继续弹射
        orig = hit.position + hit.normal * RAY_EPSILON;
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
