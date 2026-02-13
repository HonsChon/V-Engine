#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

class VulkanBuffer;
class Mesh;

/**
 * RenderContext - 渲染上下文
 * 
 * 在各个渲染通道之间传递共享数据
 * 包括：相机矩阵、光照信息、当前帧索引、共享资源等
 */
struct RenderContext {
    // 帧信息
    uint32_t frameIndex = 0;
    uint32_t imageIndex = 0;
    float time = 0.0f;
    float deltaTime = 0.0f;
    
    // 屏幕尺寸
    uint32_t screenWidth = 0;
    uint32_t screenHeight = 0;
    
    // 相机矩阵
    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);
    glm::mat4 invViewMatrix = glm::mat4(1.0f);
    glm::mat4 invProjectionMatrix = glm::mat4(1.0f);
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    
    // 光照信息
    glm::vec3 lightPosition = glm::vec3(0.0f, 5.0f, 5.0f);
    glm::vec3 lightColor = glm::vec3(300.0f);
    
    // 场景网格（用于场景渲染）
    VkBuffer sceneVertexBuffer = VK_NULL_HANDLE;
    VkBuffer sceneIndexBuffer = VK_NULL_HANDLE;
    uint32_t sceneIndexCount = 0;
    
    // 模型矩阵（当前正在渲染的对象）
    glm::mat4 modelMatrix = glm::mat4(1.0f);
};
