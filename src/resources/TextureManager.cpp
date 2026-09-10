/**
 * @file TextureManager.cpp
 * @brief TextureManager implementation — loads images via stbi, creates RHI textures
 */

#include "TextureManager.h"
#include "RHIDevice.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>

namespace VulkanEngine {

// ============================================================
// Helper: create GPUTexture from raw pixels
// ============================================================

std::shared_ptr<GPUTexture> TextureManager::createFromPixels(
    const unsigned char* pixels, uint32_t w, uint32_t h, RHIFormat format)
{
    if (!m_rhiDevice || !pixels) return nullptr;

    auto gpuTex = std::make_shared<GPUTexture>();
    gpuTex->width = w;
    gpuTex->height = h;

    // Create RHI texture (Sampled + TransferDst for upload)
    RHITextureDesc desc{};
    desc.width = w;
    desc.height = h;
    desc.format = format;
    desc.usage = RHITextureUsage::Sampled | RHITextureUsage::TransferDst;
    gpuTex->texture = m_rhiDevice->createTexture(desc);

    // Upload pixel data
    uint64_t dataSize = static_cast<uint64_t>(w) * h * 4; // RGBA
    gpuTex->texture->uploadPixels(pixels, dataSize);

    // Create sampler (linear filtering, repeat addressing)
    RHISamplerDesc samplerDesc{};
    samplerDesc.minFilter = RHIFilter::Linear;
    samplerDesc.magFilter = RHIFilter::Linear;
    samplerDesc.addressModeU = RHIAddressMode::Repeat;
    samplerDesc.addressModeV = RHIAddressMode::Repeat;
    samplerDesc.addressModeW = RHIAddressMode::Repeat;
    gpuTex->sampler = m_rhiDevice->createSampler(samplerDesc);

    return gpuTex;
}

// ============================================================
// Default Textures
// ============================================================

void TextureManager::createDefaultTextures() {
    if (!m_rhiDevice) return;

    // Default white texture (1x1, RGBA = 255,255,255,255)
    {
        unsigned char white[4] = { 255, 255, 255, 255 };
        m_defaultWhiteTexture = createFromPixels(white, 1, 1, RHIFormat::R8G8B8A8_UNORM);
    }

    // Default normal texture (1x1, RGBA = 128,128,255,255 → normal pointing +Z)
    {
        unsigned char normal[4] = { 128, 128, 255, 255 };
        m_defaultNormalTexture = createFromPixels(normal, 1, 1, RHIFormat::R8G8B8A8_UNORM);
    }

    // Default black texture (1x1, RGBA = 0,0,0,255)
    {
        unsigned char black[4] = { 0, 0, 0, 255 };
        m_defaultBlackTexture = createFromPixels(black, 1, 1, RHIFormat::R8G8B8A8_UNORM);
    }

    std::cout << "[TextureManager] Default textures created (RHI)" << std::endl;
}

// ============================================================
// Load Texture from File
// ============================================================

std::shared_ptr<GPUTexture> TextureManager::loadTexture(const std::string& texturePath) {
    if (!m_rhiDevice) {
        std::cerr << "[TextureManager] Error: RHI Device not initialized!" << std::endl;
        return nullptr;
    }

    // Load via stbi (always request RGBA)
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(texturePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        std::cerr << "[TextureManager] Failed to load texture: " << texturePath << std::endl;
        return nullptr;
    }

    uint32_t w = static_cast<uint32_t>(texWidth);
    uint32_t h = static_cast<uint32_t>(texHeight);

    std::cout << "Loaded texture: " << texturePath << " (" << w << "x" << h << ")" << std::endl;

#ifdef __APPLE__
    // macOS (Metal/MoltenVK) max texture size = 16384
    const uint32_t maxTextureSize = 16384;
    stbi_uc* resizedPixels = nullptr;

    if (w > maxTextureSize || h > maxTextureSize) {
        float scale = std::min(static_cast<float>(maxTextureSize) / w,
                               static_cast<float>(maxTextureSize) / h);
        uint32_t newWidth = std::max(static_cast<uint32_t>(w * scale), 1u);
        uint32_t newHeight = std::max(static_cast<uint32_t>(h * scale), 1u);

        std::cout << "[macOS] Texture exceeds Metal limit, resizing from "
                  << w << "x" << h << " to " << newWidth << "x" << newHeight << std::endl;

        resizedPixels = static_cast<stbi_uc*>(malloc(newWidth * newHeight * 4));
        float xRatio = static_cast<float>(w) / newWidth;
        float yRatio = static_cast<float>(h) / newHeight;

        for (uint32_t y = 0; y < newHeight; ++y) {
            for (uint32_t x = 0; x < newWidth; ++x) {
                uint32_t srcX = std::min(static_cast<uint32_t>(x * xRatio), w - 1);
                uint32_t srcY = std::min(static_cast<uint32_t>(y * yRatio), h - 1);
                uint32_t srcIdx = (srcY * w + srcX) * 4;
                uint32_t dstIdx = (y * newWidth + x) * 4;
                resizedPixels[dstIdx + 0] = pixels[srcIdx + 0];
                resizedPixels[dstIdx + 1] = pixels[srcIdx + 1];
                resizedPixels[dstIdx + 2] = pixels[srcIdx + 2];
                resizedPixels[dstIdx + 3] = pixels[srcIdx + 3];
            }
        }

        w = newWidth;
        h = newHeight;
    }

    const unsigned char* finalPixels = resizedPixels ? resizedPixels : pixels;
#else
    const unsigned char* finalPixels = pixels;
#endif

    // Use SRGB for color textures (default)
    auto gpuTex = createFromPixels(finalPixels, w, h, RHIFormat::R8G8B8A8_SRGB);

#ifdef __APPLE__
    free(resizedPixels);
#endif
    stbi_image_free(pixels);

    if (gpuTex) {
        std::cout << "[TextureManager] Loaded texture: " << texturePath << std::endl;
    }

    return gpuTex;
}

} // namespace VulkanEngine
