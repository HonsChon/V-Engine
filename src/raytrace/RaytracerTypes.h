/**
 * @file RaytracerTypes.h
 * @brief 软光追基础类型 — 求交结果、材质、三角形、光源、随机数
 *
 * CPU 软光追的数据层：从 ECS 场景采集扁平化的几何/材质/光照数据，
 * 供 BVH 与 SoftwareRaytracer 使用。数学类型复用 GLM。
 */

#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>

namespace VEngine {
namespace RT {

// ============================================================
// 随机数 — PCG hash（快、质量足够渲染用，每线程独立状态）
// ============================================================

inline uint32_t pcgHash(uint32_t v) {
    uint32_t state = v * 747796405u + 2891336453u;
    uint32_t word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

/// 返回 [0,1) 均匀随机数，推进 rng 状态
inline float rand01(uint32_t& rng) {
    rng = pcgHash(rng);
    return static_cast<float>(rng) * (1.0f / 4294967296.0f);
}

/// [-1,1) 均匀随机数
inline float randSym(uint32_t& rng) {
    return rand01(rng) * 2.0f - 1.0f;
}

// ============================================================
// 求交结果
// ============================================================

/**
 * @brief 射线命中信息（世界空间）
 * t 为世界空间射线参数（局部空间求交时不归一化方向，t 直接对应世界空间）
 */
struct RayHit {
    float t = std::numeric_limits<float>::max();
    float u = 0.0f;               // 重心坐标 (w, u, v)
    float v = 0.0f;
    uint32_t instanceIndex = 0;   // 命中的实例
    uint32_t triangleIndex = 0;   // 实例网格内的三角形索引

    bool isValid() const { return t != std::numeric_limits<float>::max(); }
};

// ============================================================
// 材质 — PBRMaterialComponent 的扁平化（不做纹理采样）
// ============================================================

struct RTMaterial {
    glm::vec3 albedo = glm::vec3(1.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    glm::vec3 emissive = glm::vec3(0.0f);
    float emissiveStrength = 0.0f;
    float opacity = 1.0f;
    bool transparent = false;     // 透明物体跳过求交（第一版不支持折射混合）

    /// 电介质 F0（Schlick 近似用）
    glm::vec3 f0() const {
        return glm::mix(glm::vec3(0.04f), albedo, metallic);
    }
};

// ============================================================
// 几何 — 网格局部空间三角形（位置 + 顶点法线，插值用）
// ============================================================

struct RTTriangle {
    glm::vec3 v0, v1, v2;
    glm::vec3 n0, n1, n2;

    glm::vec3 centroid() const { return (v0 + v1 + v2) * (1.0f / 3.0f); }
};

/**
 * @brief Möller–Trumbore 三角形求交（无剔除，双面）
 * @return 是否命中（t ∈ (eps, tMax)）
 */
inline bool intersectTriangle(
    const glm::vec3& orig, const glm::vec3& dir,
    const RTTriangle& tri,
    float tMax, float& t, float& u, float& v)
{
    const float EPSILON = 1e-6f;

    glm::vec3 edge1 = tri.v1 - tri.v0;
    glm::vec3 edge2 = tri.v2 - tri.v0;
    glm::vec3 h = glm::cross(dir, edge2);
    float a = glm::dot(edge1, h);
    if (a > -EPSILON && a < EPSILON) return false;  // 平行

    float f = 1.0f / a;
    glm::vec3 s = orig - tri.v0;
    u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(s, edge1);
    v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;

    t = f * glm::dot(edge2, q);
    return t > EPSILON && t < tMax;
}

// ============================================================
// 光源 — LightComponent 的扁平化
// ============================================================

enum class RTLightType : uint8_t {
    Directional,
    Point,
    Spot
};

struct RTLight {
    RTLightType type = RTLightType::Point;
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    glm::vec3 position = glm::vec3(0.0f);     // 世界空间
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);  // 世界空间（归一化）
    // 点光源/聚光灯衰减（与 LightComponent 一致）
    float constantAttenuation = 1.0f;
    float linearAttenuation = 0.09f;
    float quadraticAttenuation = 0.032f;
    float range = 10.0f;
    // 聚光灯锥角（弧度）
    float innerConeAngle = 0.2182f;   // ~12.5°
    float outerConeAngle = 0.2967f;   // ~17.5°

    /**
     * @brief 计算光源在给定方向的辐射强度（含距离衰减与锥角）
     * @param toLight 指向光源的单位向量（方向光时为 -direction）
     * @param distance 到光源距离（方向光忽略）
     * @return 辐射亮度 color * intensity * 各项衰减
     */
    glm::vec3 radiance(const glm::vec3& toLight, float distance) const {
        glm::vec3 result = color * intensity;
        switch (type) {
            case RTLightType::Directional:
                return result;
            case RTLightType::Point: {
                float att = 1.0f / (constantAttenuation
                    + linearAttenuation * distance
                    + quadraticAttenuation * distance * distance);
                // range 外平滑衰减到 0
                if (range > 0.0f && distance > range) {
                    att *= std::max(0.0f, 1.0f - (distance - range) / range);
                }
                return result * att;
            }
            case RTLightType::Spot: {
                float cosTheta = glm::dot(-toLight, direction);
                float cosOuter = std::cos(outerConeAngle);
                float cosInner = std::cos(innerConeAngle);
                float spot = std::clamp((cosTheta - cosOuter) / (cosInner - cosOuter), 0.0f, 1.0f);
                spot = spot * spot;  // 平滑过渡
                float att = 1.0f / (constantAttenuation
                    + linearAttenuation * distance
                    + quadraticAttenuation * distance * distance);
                return result * spot * att;
            }
        }
        return result;
    }
};

// ============================================================
// 采样工具 — 正交基与余弦半球采样（路径追踪用）
// ============================================================

/// 基于法线构建正交基 (w = n)
inline void orthoBasis(const glm::vec3& n, glm::vec3& b1, glm::vec3& b2) {
    glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    b1 = glm::normalize(glm::cross(up, n));
    b2 = glm::cross(n, b1);
}

/// 余弦加权半球采样（pdf = cosθ/π），返回半球内随机方向
inline glm::vec3 cosineSampleHemisphere(const glm::vec3& n, uint32_t& rng) {
    float r1 = rand01(rng);
    float r2 = rand01(rng);
    float phi = 6.2831853f * r1;
    float cosTheta = std::sqrt(1.0f - r2);
    float sinTheta = std::sqrt(r2);

    glm::vec3 b1, b2;
    orthoBasis(n, b1, b2);
    return glm::normalize(b1 * (sinTheta * std::cos(phi))
                        + b2 * (sinTheta * std::sin(phi))
                        + n * cosTheta);
}

/// Schlick Fresnel 近似
inline glm::vec3 schlickFresnel(const glm::vec3& f0, float cosTheta) {
    return f0 + (glm::vec3(1.0f) - f0)
        * std::pow(std::max(0.0f, 1.0f - cosTheta), 5.0f);
}

} // namespace RT
} // namespace VEngine
