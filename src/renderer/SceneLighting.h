/**
 * @file SceneLighting.h
 * @brief 光栅路径场景灯光采集 — 前向/延迟/反射共用
 *
 * 采集 ECS 中的 LightComponent 为紧凑 GPU 数组（语义与 GPU 光追的
 * RTLightGPU 完全一致：位置=世界平移、方向=mat3(world)×forward、
 * 衰减/锥角取组件原值）；场景无灯时写入后备点光
 * （(5,3,5)、强度 300、物理 1/d²）—— 与历史硬编码行为逐像素一致。
 */

#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace VEngine {

class Scene;

constexpr uint32_t kMaxRasterLights = 16;

/// 与 shader LightData 严格同构（5×vec4 = 80B；std140/cbuffer 无对齐歧义）
struct RasterLight {
    glm::vec4 colorType;        // rgb 颜色, a 类型(0 Directional / 1 Point / 2 Spot)
    glm::vec4 positionInt;      // xyz 世界位置, a 强度
    glm::vec4 directionRange;   // xyz 方向(归一化), a range
    glm::vec4 atten;            // x constant, y linear, z quadratic, w 内锥角(rad)
    glm::vec4 outerPad;         // x 外锥角(rad), yzw pad
};
static_assert(sizeof(RasterLight) == 80, "RasterLight must match shader LightData");

/// 采集场景灯到 outLights（容量 kMaxRasterLights，超出截断 + 一次性日志）。
/// @return 实际写入数量（场景无灯时为 1，内容为后备点光）
uint32_t collectSceneLights(Scene* scene, RasterLight* outLights);

} // namespace VEngine
