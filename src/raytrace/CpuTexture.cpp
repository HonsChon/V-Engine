/**
 * @file CpuTexture.cpp
 * @brief CPU 纹理实现 — stbi 加载 + 双线性采样 + sRGB LUT
 *
 * 注意：stb_image 的实现宏在 TextureManager.cpp 中定义，此处仅包含头文件。
 */

#include "CpuTexture.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace VEngine {
namespace RT {

namespace {

/// sRGB→线性 LUT（256 项，首次访问初始化）
const float* srgbToLinearLUT() {
    static const std::vector<float> lut = [] {
        std::vector<float> v(256);
        for (int i = 0; i < 256; ++i) {
            float c = static_cast<float>(i) / 255.0f;
            // sRGB 官方分段转换（暗部线性段保证精度）
            v[i] = (c <= 0.04045f) ? c / 12.92f
                                   : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return v;
    }();
    return lut.data();
}

inline float srgbToLinear(uint8_t v) {
    return srgbToLinearLUT()[v];
}

/// box 降采样（尺寸超限时）
std::vector<uint8_t> downsampleRGBA(const std::vector<uint8_t>& src,
                                    int srcW, int srcH, int dstW, int dstH)
{
    std::vector<uint8_t> dst(static_cast<size_t>(dstW) * dstH * 4);
    int boxX = srcW / dstW;
    int boxY = srcH / dstH;
    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            uint32_t r = 0, g = 0, b = 0, a = 0;
            uint32_t n = 0;
            for (int by = 0; by < boxY; ++by) {
                for (int bx = 0; bx < boxX; ++bx) {
                    size_t idx = (static_cast<size_t>(y * boxY + by) * srcW
                                  + x * boxX + bx) * 4;
                    r += src[idx]; g += src[idx + 1];
                    b += src[idx + 2]; a += src[idx + 3];
                    ++n;
                }
            }
            size_t dstIdx = (static_cast<size_t>(y) * dstW + x) * 4;
            dst[dstIdx + 0] = static_cast<uint8_t>(r / n);
            dst[dstIdx + 1] = static_cast<uint8_t>(g / n);
            dst[dstIdx + 2] = static_cast<uint8_t>(b / n);
            dst[dstIdx + 3] = static_cast<uint8_t>(a / n);
        }
    }
    return dst;
}

} // anonymous namespace

// ============================================================
// CpuTexture
// ============================================================

bool CpuTexture::loadFromFile(const std::string& path, bool srgb, int maxResolution) {
    int w = 0, h = 0, c = 0;
    // 统一请求 4 通道（ stride 一致，采样路径无分支）
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &c, STBI_rgb_alpha);
    if (!pixels || w <= 0 || h <= 0) {
        std::cout << "[CpuTexture] Failed to load: " << path << std::endl;
        return false;
    }

    data_.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    width_ = w;
    height_ = h;
    srgb_ = srgb;

    // 尺寸安全阀（box 降采样）
    if (maxResolution > 0 && (w > maxResolution || h > maxResolution)) {
        int scale = std::max(1, (std::max(w, h) + maxResolution - 1) / maxResolution);
        int dw = std::max(1, w / scale);
        int dh = std::max(1, h / scale);
        data_ = downsampleRGBA(data_, w, h, dw, dh);
        width_ = dw;
        height_ = dh;
    }
    return true;
}

void CpuTexture::bilinearCoords(float u, float v,
                                int& x0, int& y0, int& x1, int& y1,
                                float& fx, float& fy) const
{
    // Repeat 寻址
    u = u - std::floor(u);
    v = v - std::floor(v);

    float px = u * width_ - 0.5f;
    float py = v * height_ - 0.5f;

    x0 = static_cast<int>(std::floor(px));
    y0 = static_cast<int>(std::floor(py));
    fx = px - x0;
    fy = py - y0;

    auto wrap = [](int x, int n) {
        int r = x % n;
        return r < 0 ? r + n : r;
    };
    x0 = wrap(x0, width_);
    y0 = wrap(y0, height_);
    x1 = wrap(x0 + 1, width_);
    y1 = wrap(y0 + 1, height_);
}

glm::vec3 CpuTexture::sampleRGB(float u, float v) const {
    if (!valid()) return glm::vec3(1.0f);

    int x0, y0, x1, y1;
    float fx, fy;
    bilinearCoords(u, v, x0, y0, x1, y1, fx, fy);

    auto fetch = [&](int x, int y) {
        const uint8_t* p = &data_[(static_cast<size_t>(y) * width_ + x) * 4];
        if (srgb_) {
            return glm::vec3(srgbToLinear(p[0]), srgbToLinear(p[1]), srgbToLinear(p[2]));
        }
        return glm::vec3(p[0], p[1], p[2]) * (1.0f / 255.0f);
    };

    glm::vec3 c00 = fetch(x0, y0), c10 = fetch(x1, y0);
    glm::vec3 c01 = fetch(x0, y1), c11 = fetch(x1, y1);
    return glm::mix(glm::mix(c00, c10, fx), glm::mix(c01, c11, fx), fy);
}

float CpuTexture::sampleChannel(float u, float v, int channel) const {
    if (!valid()) return 1.0f;

    int x0, y0, x1, y1;
    float fx, fy;
    bilinearCoords(u, v, x0, y0, x1, y1, fx, fy);

    auto fetch = [&](int x, int y) -> float {
        return data_[(static_cast<size_t>(y) * width_ + x) * 4 + channel] * (1.0f / 255.0f);
    };

    float c00 = fetch(x0, y0), c10 = fetch(x1, y0);
    float c01 = fetch(x0, y1), c11 = fetch(x1, y1);
    return glm::mix(glm::mix(c00, c10, fx), glm::mix(c01, c11, fx), fy);
}

glm::vec3 CpuTexture::sampleNormal(float u, float v) const {
    // 数据贴图（无 sRGB 转换），[-1,1] 映射
    glm::vec3 rgb(sampleChannel(u, v, 0), sampleChannel(u, v, 1), sampleChannel(u, v, 2));
    return rgb * 2.0f - 1.0f;
}

// ============================================================
// CpuTextureCache
// ============================================================

std::shared_ptr<CpuTexture> CpuTextureCache::load(const std::string& path, bool srgb) {
    if (path.empty()) return nullptr;

    auto it = textures_.find(path);
    if (it != textures_.end()) return it->second;

    auto tex = std::make_shared<CpuTexture>();
    if (!tex->loadFromFile(path, srgb)) {
        return nullptr;   // 不缓存失败，便于问题修复后重试
    }
    totalBytes_ += static_cast<uint64_t>(tex->width()) * tex->height() * 4;
    textures_[path] = tex;
    return tex;
}

} // namespace RT
} // namespace VEngine
