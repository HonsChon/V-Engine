/**
 * @file Restir.h
 * @brief ReSTIR 数据结构 — 蓄水池采样（WRS）与表面缓存
 *
 * 无偏版实现（Generalized Resampled Importance Sampling, Bitterli 2022）：
 * 蓄水池合并采用 balance-heuristic MIS + 目标函数 shift，
 * 重采样过程无偏；可见性默认走最终 1 条阴影射线（RTXDI 惯例，
 * 角落处存在轻微偏差，可用 shadowInTarget 模式改善）。
 */

#pragma once

#include "RaytracerTypes.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace VEngine {
namespace RT {

/**
 * @brief 蓄水池（Weighted Reservoir Sampling）
 *
 * sample  — 蓄水池当前采中的样本（点光源场景 = 光源索引）
 * wSum    — 流经的全部候选权重和 Σ w_i（w_i = ρ̂(x_i)/p(x_i)）
 * M       — 已处理的候选计数
 * W       — RIS 权重估计 = wSum / (M·ρ̂(sample))，
 *           近似 1/(输出 pdf)，MIS 合并时用于估计 m_r(x)
 */
struct Reservoir {
    uint32_t sample = 0;      // 采中的光源索引
    float wSum = 0.0f;        // 权重和
    float M = 0.0f;           // 候选计数（时间复用可跨帧累积）
    float W = 0.0f;           // RIS 权重

    bool valid() const { return wSum > 0.0f && M > 0.0f; }
    void reset() { sample = 0; wSum = 0.0f; M = 0.0f; W = 0.0f; }

    /// 流式并入一个候选（权重 w ≥ 0）
    void update(uint32_t s, float w, uint32_t& rng) {
        M += 1.0f;
        wSum += w;
        if (w > 0.0f && rand01(rng) < w / wSum) {
            sample = s;
        }
    }
};

/**
 * @brief 逐像素主命中表面缓存（ReSTIR 复用的"表面"）
 *
 * 目标函数 ρ_r(x) = luminance( diffuseFactor/π × Le_x × atten × NdotL )，
 * 在 r 自己的表面上求值；shift 到其它像素需对方的表面信息。
 */
struct SurfaceInfo {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    float depth = 0.0f;                          // 到相机的距离（时间复用验证用）
    uint32_t instanceIndex = 0;                  // 材质取回（material 指针帧内稳定但不跨帧）
    bool valid = false;                          // 是否命中几何
    ShadedMaterial mat;                          // 逐命中贴图采样结果（mat.normal == normal）
};

} // namespace RT
} // namespace VEngine
