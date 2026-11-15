/**
 * @file CpuTexture.h
 * @brief CPU 纹理 — 软光追专用贴图采样
 *
 * 与 GPU TextureManager 独立：内存保持 8-bit 原始数据（不做 float 展开，
 * 69 张 1024² 贴图 ≈ 288MB 而非 1.2GB），采样时经 LUT/字节转换。
 * 颜色贴图（albedo/emissive）为 sRGB 编码，采样时线性化；
 * 数据贴图（法线/metallicRoughness/AO）为线性数据，直接读值。
 */

#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace VEngine {
namespace RT {

class CpuTexture {
public:
    CpuTexture() = default;

    /**
     * @brief 从文件加载
     * @param srgb true = 颜色数据（采样时经 sRGB→线性 LUT）
     * @param maxResolution 边长上限（超限 box 降采样，0 = 不限制）
     */
    bool loadFromFile(const std::string& path, bool srgb, int maxResolution = 2048);

    bool valid() const { return width_ > 0 && height_ > 0; }
    int width() const { return width_; }
    int height() const { return height_; }

    /// 双线性采样 RGB（sRGB 贴图自动线性化），Repeat 寻址
    glm::vec3 sampleRGB(float u, float v) const;

    /// 单通道采样（数据贴图原始值），channel: 0=R 1=G 2=B
    float sampleChannel(float u, float v, int channel) const;

    /// 法线贴图采样（tangent-space，映射到 [-1,1]）
    glm::vec3 sampleNormal(float u, float v) const;

private:
    /// 双线性插值的 4 邻字节权重展开（Repeat 寻址）
    void bilinearCoords(float u, float v,
                        int& x0, int& y0, int& x1, int& y1,
                        float& fx, float& fy) const;

    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> data_;   // RGBA 4 通道 8-bit
    bool srgb_ = false;
};

/**
 * @brief CPU 纹理缓存 — 按路径去重加载
 */
class CpuTextureCache {
public:
    /// 加载（或命中缓存）纹理；路径为空或加载失败返回 nullptr
    std::shared_ptr<CpuTexture> load(const std::string& path, bool srgb);

    size_t count() const { return textures_.size(); }
    uint64_t totalBytes() const { return totalBytes_; }

private:
    std::unordered_map<std::string, std::shared_ptr<CpuTexture>> textures_;
    uint64_t totalBytes_ = 0;
};

} // namespace RT
} // namespace VEngine
