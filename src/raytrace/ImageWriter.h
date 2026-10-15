/**
 * @file ImageWriter.h
 * @brief 图像写出 — RGBA8 帧缓冲保存为 PNG
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace VEngine {
namespace RT {

/**
 * @brief 保存 RGBA8 数据为 PNG
 * @param path 输出路径（.png）
 * @param rgba top-down 行序 RGBA 数据，大小 = w*h*4
 * @return 是否成功
 */
bool savePNG(const std::string& path, const std::vector<uint8_t>& rgba,
             uint32_t width, uint32_t height);

} // namespace RT
} // namespace VEngine
