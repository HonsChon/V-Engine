/**
 * @file ImageWriter.cpp
 * @brief 图像写出实现 — stb_image_write
 */

#include "ImageWriter.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace VEngine {
namespace RT {

bool savePNG(const std::string& path, const std::vector<uint8_t>& rgba,
             uint32_t width, uint32_t height)
{
    if (rgba.size() != static_cast<size_t>(width) * height * 4) return false;
    int result = stbi_write_png(path.c_str(), static_cast<int>(width),
                                static_cast<int>(height), 4, rgba.data(),
                                static_cast<int>(width) * 4);
    return result != 0;
}

} // namespace RT
} // namespace VEngine
