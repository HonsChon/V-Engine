#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <vector>
#include <array>
#include <string>

#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
// VulkanPipeline.h 已不再需要，使用 ForwardPass 替代
#include "Mesh.h"
#include "Camera.h"
#include "Scene.h"
#include "GBuffer.h"
#include "SSRPass.h"
#include "WaterPass.h"
#include "ForwardPass.h"

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 normalMatrix;
    alignas(16) glm::vec4 viewPos;      // 使用 vec4 确保 std140 对齐
    alignas(16) glm::vec4 lightPos;     // 使用 vec4 确保 std140 对齐
    alignas(16) glm::vec4 lightColor;   // 使用 vec4 确保 std140 对齐
};

// 模型类型枚举
enum class MeshType {
    Sphere,
    Cube,
    Plane,
    OBJ
};

class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    void run();
    
    // 设置要加载的模型
    void setMeshType(MeshType type) { meshType = type; }
    void setOBJPath(const std::string& path) { objPath = path; meshType = MeshType::OBJ; }
    
    // 输入处理函数（供回调使用）
    void handleMouseMovement(float xoffset, float yoffset);
    void handleMouseScroll(float yoffset);

private:
    void initWindow();
    void initVulkan();
    void loadMesh();
    void createSyncObjects();
    void createCommandBuffers();
    void createVertexBuffer();
    void createIndexBuffer();
    void loadTextures();
    // createUniformBuffers、createDescriptorPool、createDescriptorSets 已移至 ForwardPass 类
    void mainLoop();
    void cleanup();
    
    void drawFrame();
    void updateUniformBuffer(uint32_t currentImage);
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void dropCallback(GLFWwindow* window, int count, const char** paths);
    void recreateSwapChain();
    void processKeyboardInput(float deltaTime);
    
    // 重新加载模型（用于拖拽加载）
    void reloadMesh();

    // Window
    GLFWwindow* window;
    const uint32_t WIDTH = 1280;
    const uint32_t HEIGHT = 720;

    // Vulkan core
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<VulkanSwapChain> swapChain;
    // 注意：旧的 pbrPipeline 已移除，使用 ForwardPass 替代
    
    // Geometry
    std::unique_ptr<Mesh> mesh;
    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;
    
    // Textures
    std::unique_ptr<VulkanTexture> albedoTexture;
    std::unique_ptr<VulkanTexture> normalTexture;
    std::unique_ptr<VulkanTexture> specularTexture;
    
    // 模型配置
    MeshType meshType = MeshType::Sphere;
    std::string objPath;
    std::string textureBasePath;  // 纹理目录路径
    bool needReloadMesh = false;
    
    // Scene
    std::unique_ptr<Camera> camera;
    std::unique_ptr<Scene> scene;
    
    // Rendering
    std::vector<VkCommandBuffer> commandBuffers;
    
    // Synchronization
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    
    uint32_t currentFrame = 0;
    bool framebufferResized = false;
    
    // 鼠标状态
    float lastMouseX = 640.0f;
    float lastMouseY = 360.0f;
    bool firstMouse = true;
    bool mouseEnabled = false;
    
    // ========== SSR 水面反射相关 ==========
    // 渲染模式
    enum class RenderMode {
        Normal,      // 普通 PBR 渲染
        WaterScene   // 水面反射场景
    };
    RenderMode renderMode = RenderMode::Normal;
    
    // G-Buffer (用于延迟渲染和 SSR)
    std::unique_ptr<GBuffer> gbuffer;
    
    // SSR Pass
    std::unique_ptr<SSRPass> ssrPass;
    
    // Water Pass
    std::unique_ptr<WaterPass> waterPass;
    
    // Forward Pass（前向渲染通道）
    std::unique_ptr<ForwardPass> forwardPass;
    
    // 场景颜色纹理 (用于 SSR 采样)
    VkImage sceneColorImage = VK_NULL_HANDLE;
    VkDeviceMemory sceneColorMemory = VK_NULL_HANDLE;
    VkImageView sceneColorView = VK_NULL_HANDLE;
    VkSampler sceneColorSampler = VK_NULL_HANDLE;
    
    // 时间 (用于水面动画)
    float totalTime = 0.0f;
    
    // 水面场景相关方法
    void initWaterScene();
    void cleanupWaterScene();
    void createSceneColorImage();
    void recordWaterSceneCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void updateWaterUniforms(uint32_t frameIndex);
    
    static const int MAX_FRAMES_IN_FLIGHT = 2;
};
