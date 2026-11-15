/**
 * @file Framebuffer.h
 * @brief CPU 帧缓冲 — HDR 累积 + ACES tone map + gamma 输出
 *
 * 路径追踪渐进式收敛：多次 renderFrame 累加颜色，resolve 时除以样本数。
 */

#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace VEngine {
namespace RT {

/// ACES filmic tone mapping 近似（Narkowicz）
inline glm::vec3 acesTonemap(glm::vec3 x) {
    x *= 0.6f;
    return glm::clamp(
        (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f),
        glm::vec3(0.0f), glm::vec3(1.0f));
}

class Framebuffer {
public:
    Framebuffer() = default;

    void resize(uint32_t width, uint32_t height) {
        width_ = width;
        height_ = height;
        acc_.assign(static_cast<size_t>(width) * height, glm::vec3(0.0f));
        samples_ = 0;
    }

    void reset() {
        std::fill(acc_.begin(), acc_.end(), glm::vec3(0.0f));
        samples_ = 0;
    }

    /// 累加一个样本（线程安全：不同像素由不同任务写，同像素单线程内写）
    void addSample(uint32_t x, uint32_t y, const glm::vec3& color) {
        acc_[static_cast<size_t>(y) * width_ + x] += color;
    }

    /// 增加已累积的每像素样本数（一次 renderFrame(spp) 后调用）
    void addSamplePass(int spp) { samples_ += static_cast<uint32_t>(spp); }

    /// 平均后的线性 HDR 颜色
    glm::vec3 pixel(uint32_t x, uint32_t y) const {
        if (samples_ == 0) return glm::vec3(0.0f);
        return acc_[static_cast<size_t>(y) * width_ + x] / static_cast<float>(samples_);
    }

    /// 解析为 RGBA8（top-down 行序，曝光 + ACES tone map + gamma 2.2）
    std::vector<uint8_t> resolveRGBA8(float exposure = 1.0f) const {
        std::vector<glm::vec3> hdr(static_cast<size_t>(width_) * height_);
        float invSamples = samples_ > 0 ? 1.0f / static_cast<float>(samples_) : 0.0f;
        for (size_t i = 0; i < hdr.size(); ++i) hdr[i] = acc_[i] * invSamples;
        return tonemapRGBA8(hdr, width_, height_, exposure);
    }

    /// 线性 HDR 图像 → RGBA8（曝光 + ACES tone map + gamma），降噪等外部处理后的输出共用
    static std::vector<uint8_t> tonemapRGBA8(const std::vector<glm::vec3>& hdr,
                                             uint32_t width, uint32_t height,
                                             float exposure) {
        std::vector<uint8_t> out(static_cast<size_t>(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                glm::vec3 c = hdr[static_cast<size_t>(y) * width + x] * exposure;
                c = acesTonemap(c);
                // gamma 2.2 编码
                c = glm::pow(glm::max(c, glm::vec3(0.0f)), glm::vec3(1.0f / 2.2f));
                size_t idx = (static_cast<size_t>(y) * width + x) * 4;
                out[idx + 0] = static_cast<uint8_t>(c.r * 255.0f + 0.5f);
                out[idx + 1] = static_cast<uint8_t>(c.g * 255.0f + 0.5f);
                out[idx + 2] = static_cast<uint8_t>(c.b * 255.0f + 0.5f);
                out[idx + 3] = 255;
            }
        }
        return out;
    }

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t sampleCount() const { return samples_; }

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::vector<glm::vec3> acc_;   // HDR 累积和
    uint32_t samples_ = 0;         // 每像素样本数
};

} // namespace RT
} // namespace VEngine
