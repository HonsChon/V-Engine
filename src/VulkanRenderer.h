#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <vector>
#include <array>

#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanPipeline.h"
#include "Mesh.h"
#include "Camera.h"
#include "Scene.h"

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 normalMatrix;
    alignas(16) glm::vec3 viewPos;
    alignas(16) glm::vec3 lightPos;
    alignas(16) glm::vec3 lightColor;
};

class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    void run();
    
    // 输入处理函数（供回调使用）
    void handleMouseMovement(float xoffset, float yoffset);
    void handleMouseScroll(float yoffset);

private:
    void initWindow();
    void initVulkan();
    void createSyncObjects();
    void createCommandBuffers();
    void createVertexBuffer();
    void createIndexBuffer();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void mainLoop();
    void cleanup();
    
    void drawFrame();
    void updateUniformBuffer(uint32_t currentImage);
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    void recreateSwapChain();
    void processKeyboardInput(float deltaTime);

    // Window
    GLFWwindow* window;
    const uint32_t WIDTH = 1280;
    const uint32_t HEIGHT = 720;

    // Vulkan core
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<VulkanSwapChain> swapChain;
    std::unique_ptr<VulkanPipeline> pbrPipeline;
    
    // Uniform Buffers (每帧一个)
    std::vector<std::unique_ptr<VulkanBuffer>> uniformBuffers;
    std::vector<void*> uniformBuffersMapped;
    
    // Descriptors
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    
    // Geometry
    std::unique_ptr<Mesh> cubeMesh;
    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;
    
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
    
    static const int MAX_FRAMES_IN_FLIGHT = 2;
};
