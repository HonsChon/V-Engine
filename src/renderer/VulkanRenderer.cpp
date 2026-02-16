#include "VulkanRenderer.h"
#include "VulkanTexture.h"
#include "Mesh.h"
#include "GBufferPass.h"
#include "SSRPass.h"
#include "WaterPass.h"
#include "ForwardPass.h"
#include "../ui/panels/DebugPanel.h"
#include "../ui/panels/SceneHierarchyPanel.h"
#include "../ui/panels/InspectorPanel.h"
#include "../ui/panels/AssetBrowserPanel.h"
#include "../scene/RayPicker.h"
#include "../scene/SelectionManager.h"
#include "../scene/Scene.h"
#include "../scene/Entity.h"
#include "../scene/Components.h"
#include <imgui.h>
#include <iostream>
#include <stdexcept>
#include <array>
#include <chrono>
#include <thread>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>

// 静态回调函数实现
void VulkanRenderer::mouseCallback(GLFWwindow* window, double xposIn, double yposIn) {
    auto renderer = reinterpret_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window));
    if (!renderer || !renderer->mouseEnabled) return;
    
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);
    
    if (renderer->firstMouse) {
        renderer->lastMouseX = xpos;
        renderer->lastMouseY = ypos;
        renderer->firstMouse = false;
    }
    
    float xoffset = xpos - renderer->lastMouseX;
    float yoffset = renderer->lastMouseY - ypos; // Y轴反转
    
    renderer->lastMouseX = xpos;
    renderer->lastMouseY = ypos;
    
    renderer->handleMouseMovement(xoffset, yoffset);
}

void VulkanRenderer::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto renderer = reinterpret_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window));
    if (renderer) {
        renderer->handleMouseScroll(static_cast<float>(yoffset));
    }
}

void VulkanRenderer::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto renderer = reinterpret_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window));
    if (!renderer) return;
    
    // 快捷键处理
    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_5:
                // 切换水面场景模式
                if (renderer->renderMode == RenderMode::Normal) {
                    renderer->renderMode = RenderMode::WaterScene;
                    std::cout << "Switching to Water Scene mode (SSR enabled)" << std::endl;
                    // 初始化水面场景（如果还没有初始化）
                    if (!renderer->gbuffer) {
                        renderer->initWaterScene();
                    }
                } else {
                    renderer->renderMode = RenderMode::Normal;
                    std::cout << "Switching to Normal render mode" << std::endl;
                }
                break;
            case GLFW_KEY_F1:
                // 切换 UI 显示
                renderer->showUI = !renderer->showUI;
                std::cout << "UI " << (renderer->showUI ? "enabled" : "disabled") << std::endl;
                break;
        }
    }
}

void VulkanRenderer::dropCallback(GLFWwindow* window, int count, const char** paths) {
    auto renderer = reinterpret_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window));
    if (!renderer || count == 0 || !renderer->scene) return;
    
    // 获取第一个拖入的文件
    std::string filePath = paths[0];
    std::string extension = std::filesystem::path(filePath).extension().string();
    
    // 转换为小写进行比较
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    if (extension == ".obj") {
        std::cout << "Loading OBJ file via drag & drop: " << filePath << std::endl;
        // 通过 ECS 创建新实体来加载 OBJ
        auto newEntity = renderer->scene->createEntity("Dropped Model");
        newEntity.addComponent<VulkanEngine::MeshRendererComponent>(filePath, "default_material");
        std::cout << "Created new entity for dropped OBJ file" << std::endl;
    } else {
        std::cout << "Unsupported file format: " << extension << " (only .obj files are supported)" << std::endl;
    }
}

void VulkanRenderer::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto renderer = reinterpret_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window));
    if (!renderer) return;
    
    // 检查 ImGui 是否正在捕获鼠标输入
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;  // ImGui 正在使用鼠标，不处理场景交互
    }
    
    // ========================================
    // 左键 - 射线拾取选择物体
    // ========================================
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            renderer->handleMousePicking();
        }
    }
    
    // ========================================
    // 右键 - 相机旋转控制
    // ========================================
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            renderer->mouseEnabled = true;
            renderer->firstMouse = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else if (action == GLFW_RELEASE) {
            renderer->mouseEnabled = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }
    
    // ========================================
    // 中键 - 可扩展（如平移视图）
    // ========================================
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        // 预留给将来的中键功能（如平移视图）
        if (action == GLFW_PRESS) {
            // TODO: 实现中键平移
        }
    }
}

VulkanRenderer::VulkanRenderer() : window(nullptr), currentFrame(0), framebufferResized(false) {
    initWindow();
    initVulkan();
    createSyncObjects();
}

VulkanRenderer::~VulkanRenderer() {
    cleanup();
}

void VulkanRenderer::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan PBR Renderer", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    
    // 设置鼠标和滚轮回调
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);  // 统一处理鼠标按钮事件
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetDropCallback(window, dropCallback);
}

void VulkanRenderer::initVulkan() {
    std::cout << "Initializing Vulkan components..." << std::endl;
    
    // 创建 Vulkan 设备
    device = std::make_unique<VulkanDevice>(window);
    
    // 创建交换链
    swapChain = std::make_unique<VulkanSwapChain>(std::shared_ptr<VulkanDevice>(device.get(), [](VulkanDevice*){}), WIDTH, HEIGHT);
    
    // 创建命令缓冲
    createCommandBuffers();
    
    // 创建 ForwardPass（前向渲染）- 它会管理自己的 Pipeline、Descriptor Pool 和 UBO
    forwardPass = std::make_unique<ForwardPass>(
        std::shared_ptr<VulkanDevice>(device.get(), [](VulkanDevice*){}),
        swapChain->getRenderPass(),
        swapChain->getExtent().width,
        swapChain->getExtent().height,
        MAX_FRAMES_IN_FLIGHT
    );
    
    // 注意：新架构中，材质描述符由 RenderSystem::updateRenderables 自动分配
    // 每个实体/材质都会获得独立的描述符集
    std::cout << "ForwardPass initialized (Pipeline + Dual Descriptor Sets + UBO)" << std::endl;
    
    // 创建相机 - 位于 (0, 0, 5) 看向原点
    camera = std::make_unique<Camera>(glm::vec3(0.0f, 0.0f, 5.0f));
    
    // 初始化 ECS 场景
    scene = std::make_unique<VulkanEngine::Scene>();
    
    // 初始化多物体渲染系统
    auto deviceShared = std::shared_ptr<VulkanDevice>(device.get(), [](VulkanDevice*){});
    renderSystem = std::make_unique<VulkanEngine::RenderSystem>();
    renderSystem->init(deviceShared);
    std::cout << "RenderSystem initialized" << std::endl;
    
    // 创建一个代表当前网格的实体（球体）
    auto sphereEntity = scene->createEntity("Sphere");
    sphereEntity.addComponent<VulkanEngine::MeshRendererComponent>("sphere", "earth_material");
    // TransformComponent 已由 Scene::createEntity 自动添加
    
    // 为球体添加 PBR 材质组件（使用地球纹理）
    auto& sphereMaterial = sphereEntity.addComponent<VulkanEngine::PBRMaterialComponent>();
    sphereMaterial.albedoMap = "../../assets/Earth/Maps/Color Map.jpg";
    sphereMaterial.normalMap = "../../assets/Earth/Maps/Bump.jpg";
    sphereMaterial.metallicMap = "../../assets/Earth/Maps/Spec Mask.png";
    
    // 创建 UFO 实体
    auto ufoEntity = scene->createEntity("UFO");
    auto& ufoMesh = ufoEntity.addComponent<VulkanEngine::MeshRendererComponent>(
        "../../assets/UFO/UFO_Empty.obj", "ufo_material");
    // 设置 UFO 的位置和缩放
    auto& ufoTransform = ufoEntity.getComponent<VulkanEngine::TransformComponent>();
    ufoTransform.position = glm::vec3(3.0f, 0.0f, 0.0f);  // 放在球体右边
    ufoTransform.scale = glm::vec3(1.0f);  // 可能需要调整
    
    // 添加 UFO 的 PBR 材质组件
    auto& ufoMaterial = ufoEntity.addComponent<VulkanEngine::PBRMaterialComponent>();
    ufoMaterial.albedoMap = "../../assets/UFO/textures/UFO_color.jpg";
    ufoMaterial.normalMap = "../../assets/UFO/textures/UFO_nmap.jpg";
    ufoMaterial.metallicMap = "../../assets/UFO/textures/UFO_metalness.jpg";
    
    std::cout << "UFO entity created" << std::endl;

    //创建一个蓝色平面
    auto & planeEntity = scene->createEntity("Plane");
    planeEntity.addComponent<VulkanEngine::MeshRendererComponent>("plane", "plane_material");

    // 设置蓝色平面的位置和缩放
    auto & planeTransform = planeEntity.getComponent<VulkanEngine::TransformComponent>();
    planeTransform.position = glm::vec3(0.0f, -1.5f, 0.0f);  // 放在球体下方

    // 添加蓝色平面的 PBR 材质组件（使用默认纹理，通过 baseColor 控制颜色）
    auto& planeMaterial = planeEntity.addComponent<VulkanEngine::PBRMaterialComponent>();
    // 使用默认白色纹理，通过着色器中的 baseColor 设置蓝色
    // 如果没有指定纹理路径，RenderSystem 会自动使用默认纹理

    // 设置 SelectionManager 的场景引用
    VulkanEngine::SelectionManager::getInstance().setScene(scene.get());
    
    std::cout << "ECS Scene initialized with multiple entities" << std::endl;
    
    // 初始化 UI 系统
    initUI();
    
    std::cout << "Vulkan initialization complete!" << std::endl;
}

void VulkanRenderer::createSyncObjects() {
    std::cout << "Creating synchronization objects..." << std::endl;
    
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device->getDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device->getDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device->getDevice(), &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

void VulkanRenderer::run() {
    mainLoop();
}

void VulkanRenderer::createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = device->getCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t) commandBuffers.size();

    if (vkAllocateCommandBuffers(device->getDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void VulkanRenderer::mainLoop() {
    std::cout << "Starting main loop..." << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  WASD - Move camera" << std::endl;
    std::cout << "  Space/Shift - Move up/down" << std::endl;
    std::cout << "  Right mouse button - Enable mouse look" << std::endl;
    std::cout << "  Mouse scroll - Zoom in/out" << std::endl;
    std::cout << "  5 - Toggle Water Scene (SSR reflection)" << std::endl;
    std::cout << "  F1 - Toggle UI" << std::endl;
    std::cout << "  Drag & Drop - Load OBJ file as new entity" << std::endl;
    std::cout << "  ESC - Exit" << std::endl;
    
    int frameCount = 0;
    auto startTime = std::chrono::high_resolution_clock::now();
    lastFrameTime = static_cast<float>(glfwGetTime());
    
    // FPS 计算
    float fpsUpdateTimer = 0.0f;
    int fpsFrameCount = 0;
    
    while (!glfwWindowShouldClose(window)) {
        // 计算帧时间（deltaTime）
        float currentTime = static_cast<float>(glfwGetTime());
        deltaTime = currentTime - lastFrameTime;
        lastFrameTime = currentTime;
        
        // 更新 FPS 统计
        fpsUpdateTimer += deltaTime;
        fpsFrameCount++;
        if (fpsUpdateTimer >= 1.0f) {
            fps = static_cast<float>(fpsFrameCount) / fpsUpdateTimer;
            fpsFrameCount = 0;
            fpsUpdateTimer = 0.0f;
        }
        
        glfwPollEvents();
        
        // 检查窗口是否被意外关闭
        if (glfwWindowShouldClose(window)) {
            std::cout << "Window close requested after " << frameCount << " frames" << std::endl;
            break;
        }
        
        // 处理键盘输入
        processKeyboardInput(deltaTime);
        
        // 注意：鼠标按钮事件（左键选择、右键旋转）现在由 mouseButtonCallback 统一处理
        
        drawFrame();
        
        frameCount++;
        if (frameCount % 60 == 0) {
            auto nowTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - startTime).count();
            // std::cout << "Rendered " << frameCount << " frames in " << duration << "ms" << std::endl;
        }
        
        // 临时：按ESC键退出
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            std::cout << "ESC pressed, exiting..." << std::endl;
            glfwSetWindowShouldClose(window, true);
            break;
        }
        
        // 在macOS上添加短暂延迟以防止过度消耗CPU
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    
    // 等待设备闲置
    vkDeviceWaitIdle(device->getDevice());
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    std::cout << "Exiting main loop after " << frameCount << " frames in " << totalDuration << "ms total" << std::endl;
    
    // 保持窗口打开几秒钟，让用户看到结果
    std::cout << "Keeping window open for 3 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
}

void VulkanRenderer::processKeyboardInput(float deltaTime) {
    if (!camera) return;
    
    // WASD 移动
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera->processKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera->processKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera->processKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera->processKeyboard(RIGHT, deltaTime);
    
    // 空格/Shift 上下移动
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        camera->processKeyboard(UP, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        camera->processKeyboard(DOWN, deltaTime);
}

void VulkanRenderer::handleMouseMovement(float xoffset, float yoffset) {
    if (camera) {
        camera->processMouseMovement(xoffset, yoffset);
    }
}

void VulkanRenderer::handleMouseScroll(float yoffset) {
    if (camera) {
        camera->processMouseScroll(yoffset);
    }
}

void VulkanRenderer::handleMousePicking() {
    if (!camera || !scene || !renderSystem) {
        std::cout << "[Picking] Missing components: camera=" << (camera ? "OK" : "NULL")
                  << ", scene=" << (scene ? "OK" : "NULL")
                  << ", renderSystem=" << (renderSystem ? "OK" : "NULL") << std::endl;
        return;
    }
    
    // 1. 获取鼠标位置
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    
    // 2. 获取窗口尺寸
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    
    std::cout << "[Picking] Mouse: (" << mouseX << ", " << mouseY << "), Window: " << width << "x" << height << std::endl;
    
    // 3. 获取视图和投影矩阵
    // 注意：这里不翻转 Y，因为 screenToWorldRay 已经处理了
    glm::mat4 viewMatrix = camera->getViewMatrix();
    float fov = glm::radians(camera->getZoom());
    float aspect = static_cast<float>(width) / static_cast<float>(height);
    glm::mat4 projMatrix = glm::perspective(fov, aspect, 0.1f, 100.0f);
    // 不需要 projMatrix[1][1] *= -1; 因为 RayPicker 使用的是标准 OpenGL 坐标
    
    // 4. 创建射线
    VulkanEngine::Ray ray = VulkanEngine::RayPicker::screenToWorldRay(
        static_cast<float>(mouseX),
        static_cast<float>(mouseY),
        static_cast<float>(width),
        static_cast<float>(height),
        viewMatrix,
        projMatrix
    );
    
    std::cout << "[Picking] Ray origin: (" << ray.origin.x << ", " << ray.origin.y << ", " << ray.origin.z << ")" << std::endl;
    std::cout << "[Picking] Ray direction: (" << ray.direction.x << ", " << ray.direction.y << ", " << ray.direction.z << ")" << std::endl;
    
    // 5. 遍历场景中所有有 MeshRendererComponent 的实体进行射线检测
    entt::entity hitEntity = entt::null;
    float closestT = std::numeric_limits<float>::max();
    
    auto& registry = scene->getRegistry();
    auto ecsView = registry.view<VulkanEngine::TransformComponent, VulkanEngine::MeshRendererComponent>();
    
    // 获取 MeshManager 来查询 AABB
    auto* meshManager = renderSystem->getMeshManager();
    
    int entityCount = 0;
    for (auto entity : ecsView) {
        entityCount++;
        auto& transform = ecsView.get<VulkanEngine::TransformComponent>(entity);
        auto& meshRenderer = ecsView.get<VulkanEngine::MeshRendererComponent>(entity);
        
        // 从 MeshManager 获取该实体网格的包围盒
        VulkanEngine::AABB meshAABB;
        if (meshManager) {
            meshAABB = meshManager->getMeshAABB(meshRenderer.meshPath);
        } else {
            // 如果没有 MeshManager，使用默认的单位立方体 AABB
            meshAABB.min = glm::vec3(-1.0f);
            meshAABB.max = glm::vec3(1.0f);
        }
        
        // 变换包围盒到世界空间
        glm::mat4 modelMatrix = transform.getTransform();
        VulkanEngine::AABB worldAABB = meshAABB.transform(modelMatrix);
        
        std::cout << "[Picking] Entity " << static_cast<uint32_t>(entity) 
                  << " (" << meshRenderer.meshPath << ")"
                  << " AABB: min(" << worldAABB.min.x << ", " << worldAABB.min.y << ", " << worldAABB.min.z << ")"
                  << " max(" << worldAABB.max.x << ", " << worldAABB.max.y << ", " << worldAABB.max.z << ")" << std::endl;
        
        // 射线-AABB 相交检测
        float tMin, tMax;
        if (VulkanEngine::RayPicker::rayIntersectsAABB(ray, worldAABB, tMin, tMax)) {
            std::cout << "[Picking] HIT! tMin=" << tMin << ", tMax=" << tMax << std::endl;
            if (tMin >= 0 && tMin < closestT) {
                closestT = tMin;
                hitEntity = entity;
            }
        }
    }
    
    std::cout << "[Picking] Checked " << entityCount << " entities" << std::endl;
    
    if (hitEntity != entt::null) {
        // 命中！选中这个实体
        glm::vec3 hitPoint = ray.getPoint(closestT);
        
        // 获取实体名称
        std::string entityName = "Unknown";
        if (registry.all_of<VulkanEngine::TagComponent>(hitEntity)) {
            entityName = registry.get<VulkanEngine::TagComponent>(hitEntity).tag;
        }
        
        std::cout << "Entity selected: " << entityName << " Hit at (" 
                  << hitPoint.x << ", " << hitPoint.y << ", " << hitPoint.z << ")" << std::endl;
        
        // 通过 SelectionManager 更新选择状态（使用 ECS 实体）
        VulkanEngine::SelectionManager::getInstance().select(hitEntity);
        
        // 同步到 UI 面板
        if (uiManager) {
            // 更新 SceneHierarchyPanel
            auto* hierarchy = uiManager->getSceneHierarchyPanel();
            if (hierarchy) {
                hierarchy->setSelectedEntity(hitEntity);
            }
            
            // 更新 InspectorPanel
            auto* inspector = uiManager->getInspectorPanel();
            if (inspector) {
                inspector->setScene(scene.get());  // 确保设置了场景引用
                inspector->setSelectedEntity(hitEntity);
            }
        }
    } else {
        // 未命中，清除选择
        std::cout << "No object selected" << std::endl;
        VulkanEngine::SelectionManager::getInstance().clearSelection();
        
        // 同步到 UI 面板
        if (uiManager) {
            // 更新 SceneHierarchyPanel
            auto* hierarchy = uiManager->getSceneHierarchyPanel();
            if (hierarchy) {
                hierarchy->setSelectedEntity(entt::null);
            }
            
            // 更新 InspectorPanel
            auto* inspector = uiManager->getInspectorPanel();
            if (inspector) {
                inspector->setSelectedEntity(entt::null);
            }
        }
    }
}

// calculateMeshAABB 已移至 MeshManager::getMeshAABB()
// 每个 GPUMesh 在加载时会自动计算其 AABB

void VulkanRenderer::drawFrame() {
    // 更新时间（用于水面动画）
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    totalTime = std::chrono::duration<float>(currentTime - startTime).count();
    
    vkWaitForFences(device->getDevice(), 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device->getDevice(), swapChain->getSwapChain(), UINT64_MAX,
        imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }
    
    // 在重置 fence 之前更新 uniform buffer
    updateUniformBuffer(currentFrame);
    
    // 如果是水面场景模式，更新水面相关的 uniform
    if (renderMode == RenderMode::WaterScene && waterPass) {
        updateWaterUniforms(currentFrame);
    }
    
    // 统一更新 RenderSystem 的可渲染数据（使用 RTTI 多态接口）
    if (renderSystem && scene) {
        std::vector<RenderPassBase*> passes;
        
        if (renderMode == RenderMode::WaterScene) {
            // 水面场景模式：同时需要 ForwardPass 和 GBufferPass 的材质描述符
            if (forwardPass) passes.push_back(forwardPass.get());
            if (gbuffer) passes.push_back(gbuffer.get());
        } else {
            // 普通模式：只需要 ForwardPass
            if (forwardPass) passes.push_back(forwardPass.get());
        }
        
        renderSystem->updateRenderables(scene.get(), passes);
    }

    vkResetFences(device->getDevice(), 1, &inFlightFences[currentFrame]);

    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    
    // 根据渲染模式选择不同的命令录制
    if (renderMode == RenderMode::WaterScene && waterPass) {
        recordWaterSceneCommandBuffer(commandBuffers[currentFrame], imageIndex);
    } else {
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(device->getGraphicsQueue(), 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapChain->getSwapChain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(device->getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::updateUniformBuffer(uint32_t currentImage) {
    if (!forwardPass) return;
    
    // 计算时间
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
    
    ForwardPass::UniformBufferObject ubo{};
    
    // View 矩阵 - 从相机获取
    if (camera) {
        ubo.view = camera->getViewMatrix();
    } else {
        ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), 
                               glm::vec3(0.0f, 0.0f, 0.0f), 
                               glm::vec3(0.0f, 1.0f, 0.0f));
    }
    
    // Projection 矩阵
    float fov = camera ? glm::radians(camera->getZoom()) : glm::radians(45.0f);
    float aspect = swapChain->getExtent().width / (float)swapChain->getExtent().height;
    ubo.proj = glm::perspective(fov, aspect, 0.1f, 100.0f);
    
    // Vulkan 的 Y 轴是反的（与 OpenGL 相反）
    ubo.proj[1][1] *= -1;
    
    // 相机位置（用于光照计算）- 使用 vec4，w 分量不使用
    glm::vec3 camPos = camera ? camera->getPosition() : glm::vec3(0.0f, 0.0f, 5.0f);
    ubo.viewPos = glm::vec4(camPos, 1.0f);
    
    // 光源绕球体旋转
    float lightRadius = 5.0f;  // 灯光距离球体中心的距离
    float lightSpeed = 0.5f;   // 旋转速度（每秒弧度数）
    float lightAngle = time * lightSpeed;
    
    // 灯光在 XZ 平面上绕 Y 轴旋转，同时有一定高度
    glm::vec3 lightPosition = glm::vec3(
        lightRadius * cos(lightAngle),   // X 坐标
        3.0f,                             // Y 坐标（高度）
        lightRadius * sin(lightAngle)    // Z 坐标
    );
    ubo.lightPos = glm::vec4(lightPosition, 1.0f);
    
    ubo.lightColor = glm::vec4(300.0f, 300.0f, 300.0f, 1.0f); // 高强度点光源
    
    // 更新 ForwardPass 的 UBO（不再包含 model 和 normalMatrix，这些通过 Push Constants 传递）
    forwardPass->updateUniformBuffer(currentImage, ubo);
}

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = swapChain->getRenderPass();
    renderPassInfo.framebuffer = swapChain->getFramebuffers()[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChain->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.1f, 0.2f, 0.4f, 1.0f}}; // 深蓝色背景
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // 使用 ForwardPass 进行渲染（每个 Pass 管理自己的 Pipeline 和 Descriptor）
    if (forwardPass && scene && renderSystem) {
        // 设置视口和裁剪
        forwardPass->begin(commandBuffer);
        
        // 绑定前向渲染管线
        forwardPass->bindPipeline(commandBuffer);
        
        // 使用统一的 RTTI 多态渲染接口
        // 注意：updateRenderables 已经在 drawFrame 中统一调用
        renderSystem->render(commandBuffer, forwardPass.get(), currentFrame);
    }

    // 更新并渲染 UI（在场景渲染之后，RenderPass 结束之前）
    updateUI();
    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void VulkanRenderer::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto app = reinterpret_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}

void VulkanRenderer::recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device->getDevice());

    swapChain->recreate(width, height);
    
    // 更新 ForwardPass 的尺寸和 RenderPass
    if (forwardPass) {
        forwardPass->recreate(swapChain->getRenderPass(), width, height);
    }
    
    // 通知 ImGui 窗口大小改变
    if (imguiLayer) {
        imguiLayer->onResize(static_cast<uint32_t>(width), 
                             static_cast<uint32_t>(height), 
                             swapChain->getRenderPass());
    }
}

// createVertexBuffer 和 createIndexBuffer 已迁移到 MeshManager
// 每个 GPUMesh 现在由 MeshManager 统一管理其顶点和索引缓冲区

// ============================================================
// UI 系统相关方法实现
// ============================================================

void VulkanRenderer::initUI() {
    std::cout << "Initializing UI system..." << std::endl;
    
    try {
        // 创建 ImGui 层
        imguiLayer = std::make_unique<ImGuiLayer>(
            window,
            device->getInstance(),
            device->getPhysicalDevice(),
            device->getDevice(),
            device->getGraphicsQueueFamily(),
            device->getGraphicsQueue(),
            swapChain->getRenderPass(),
            static_cast<uint32_t>(swapChain->getImageCount())
        );
        
        // 创建 UI 管理器
        uiManager = std::make_unique<UIManager>();
        
        // 设置资源浏览器的根目录
        if (uiManager->getAssetBrowserPanel()) {
            uiManager->getAssetBrowserPanel()->setRootPath("assets");
        }
        
        // 设置 InspectorPanel 的场景引用，启用 ECS 模式
        if (uiManager->getInspectorPanel() && scene) {
            uiManager->getInspectorPanel()->setScene(scene.get());
        }
        
        // 设置 SceneHierarchyPanel 的场景引用
        if (uiManager->getSceneHierarchyPanel() && scene) {
            uiManager->getSceneHierarchyPanel()->setScene(scene.get());
        }
        
        std::cout << "UI system initialized!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize UI: " << e.what() << std::endl;
        imguiLayer.reset();
        uiManager.reset();
    }
}

void VulkanRenderer::cleanupUI() {
    if (imguiLayer) {
        imguiLayer->cleanup();
        imguiLayer.reset();
    }
    uiManager.reset();
}

void VulkanRenderer::updateUI() {
    if (!uiManager || !camera) return;
    
    // 更新调试面板信息
    auto* debugPanel = uiManager->getDebugPanel();
    if (debugPanel) {
        debugPanel->setFPS(fps);
        debugPanel->setFrameTime(deltaTime * 1000.0f); // 转换为毫秒
        
        // 更新相机信息
        debugPanel->setCameraPosition(camera->getPosition());
        debugPanel->setCameraFOV(camera->getZoom());
        
        // 从 RenderSystem 获取渲染器统计信息
        uint32_t vertexCount = 0;
        uint32_t triangleCount = 0;
        uint32_t drawCalls = 0;
        if (renderSystem) {
            vertexCount = renderSystem->getTotalVertexCount();
            triangleCount = renderSystem->getTotalTriangleCount();
            drawCalls = renderSystem->getDrawCallCount();
        }
        debugPanel->setVertices(vertexCount);
        debugPanel->setTriangles(triangleCount);
        debugPanel->setDrawCalls(drawCalls);
    }
    
    // SceneHierarchyPanel 现在会自动从 ECS 场景获取实体列表
    // 不再需要手动添加示例对象
}

void VulkanRenderer::renderUI(VkCommandBuffer commandBuffer) {
    if (!imguiLayer || !uiManager || !showUI) return;
    
    // 开始新的 ImGui 帧
    imguiLayer->beginFrame();
    
    // 渲染所有 UI 面板
    uiManager->render();
    
    // 结束 ImGui 帧并记录渲染命令
    imguiLayer->endFrame(commandBuffer);
}

// createDescriptorPool、createDescriptorSets、loadTextures 已移至各个 Pass 类和 TextureManager 中管理

void VulkanRenderer::cleanup() {
    // 等待设备闲置
    vkDeviceWaitIdle(device->getDevice());
    
    // 清理 UI 系统
    cleanupUI();
    
    // 清理水面场景资源
    cleanupWaterScene();
    
    // 清理同步对象
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device->getDevice(), renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device->getDevice(), imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device->getDevice(), inFlightFences[i], nullptr);
    }

    if (window) {
        glfwDestroyWindow(window);
        glfwTerminate();
    }
}

// ============================================================
// 水面场景相关方法实现
// ============================================================

void VulkanRenderer::initWaterScene() {
    std::cout << "Initializing water scene with SSR..." << std::endl;
    
    auto devicePtr = std::shared_ptr<VulkanDevice>(device.get(), [](VulkanDevice*){});
    uint32_t width = swapChain->getExtent().width;
    uint32_t height = swapChain->getExtent().height;
    
    try {
        // 1. 创建 G-Buffer
        gbuffer = std::make_unique<GBufferPass>(devicePtr, width, height);
        std::cout << "  G-Buffer created" << std::endl;
        
        // 2. 创建 SSR Pass
        ssrPass = std::make_unique<SSRPass>(devicePtr, width, height);
        std::cout << "  SSR Pass created" << std::endl;
        
        // 3. 创建 Water Pass（使用内置水面网格）
        waterPass = std::make_unique<WaterPass>(devicePtr, width, height, swapChain->getRenderPass());
        waterPass->setWaterHeight(-1.5f);  // 水面在 Y = -1.5 位置
        waterPass->setWaterColor(glm::vec3(0.0f, 0.4f, 0.6f), 0.7f);
        std::cout << "  Water Pass created (using built-in water mesh)" << std::endl;
        
        // 4. 创建场景颜色纹理（用于 SSR 采样）
        createSceneColorImage();
        std::cout << "  Scene color image created" << std::endl;
        
        // 5. 为 GBuffer 创建描述符集（拥有独立的 UBO）
        if (gbuffer) {
            gbuffer->createDescriptorSets();
            std::cout << "  GBuffer descriptor sets created" << std::endl;
            
            // GBuffer 纹理绑定将在 recordWaterSceneCommandBuffer 中
            // 根据每个实体的 PBRMaterialComponent 动态设置
            std::cout << "  GBuffer texture bindings will be set per-entity" << std::endl;
        }
        
        // 6. 创建 LightingPass（延迟渲染光照阶段）
        lightingPass = std::make_unique<LightingPass>(devicePtr, width, height, swapChain->getRenderPass());
        lightingPass->setAmbientLight(glm::vec3(0.03f), 1.0f);
        std::cout << "  LightingPass created" << std::endl;
        
        // 7. 设置 LightingPass 的 G-Buffer 输入
        if (gbuffer) {
            lightingPass->setGBufferInputs(
                gbuffer->getPositionView(),
                gbuffer->getNormalView(),
                gbuffer->getAlbedoView(),
                gbuffer->getSampler()
            );
            std::cout << "  LightingPass G-Buffer inputs set" << std::endl;
        }
        
        // 8. 更新 WaterPass 的描述符集（绑定 G-Buffer 用于内置 SSR）
        if (gbuffer) {
            waterPass->updateDescriptorSets(
                gbuffer.get(),                   // G-Buffer（Position, Normal, Depth）
                sceneColorView,                  // 场景颜色（用于反射和折射）
                sceneColorSampler                // 采样器
            );
            std::cout << "  Water Pass descriptors updated (integrated SSR)" << std::endl;
        }
        
        std::cout << "Water scene initialization complete! (Deferred Shading enabled)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize water scene: " << e.what() << std::endl;
        cleanupWaterScene();
        renderMode = RenderMode::Normal;
    }
}

void VulkanRenderer::cleanupWaterScene() {
    vkDeviceWaitIdle(device->getDevice());
    
    // 清理场景颜色纹理
    if (sceneColorSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device->getDevice(), sceneColorSampler, nullptr);
        sceneColorSampler = VK_NULL_HANDLE;
    }
    if (sceneColorView != VK_NULL_HANDLE) {
        vkDestroyImageView(device->getDevice(), sceneColorView, nullptr);
        sceneColorView = VK_NULL_HANDLE;
    }
    if (sceneColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(device->getDevice(), sceneColorImage, nullptr);
        sceneColorImage = VK_NULL_HANDLE;
    }
    if (sceneColorMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device->getDevice(), sceneColorMemory, nullptr);
        sceneColorMemory = VK_NULL_HANDLE;
    }
    
    // 清理渲染通道
    waterPass.reset();
    ssrPass.reset();
    lightingPass.reset();
    gbuffer.reset();
}

void VulkanRenderer::createSceneColorImage() {
    uint32_t width = swapChain->getExtent().width;
    uint32_t height = swapChain->getExtent().height;
    
    // 创建图像
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device->getDevice(), &imageInfo, nullptr, &sceneColorImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create scene color image!");
    }
    
    // 分配内存
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device->getDevice(), sceneColorImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    
    // 查找合适的内存类型
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(device->getPhysicalDevice(), &memProperties);
    
    uint32_t memoryTypeIndex = 0;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    
    if (vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &sceneColorMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate scene color image memory!");
    }
    
    vkBindImageMemory(device->getDevice(), sceneColorImage, sceneColorMemory, 0);
    
    // 创建图像视图
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = sceneColorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device->getDevice(), &viewInfo, nullptr, &sceneColorView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create scene color image view!");
    }
    
    // 创建采样器
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    
    if (vkCreateSampler(device->getDevice(), &samplerInfo, nullptr, &sceneColorSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create scene color sampler!");
    }
}

void VulkanRenderer::updateWaterUniforms(uint32_t frameIndex) {
    if (!waterPass || !camera) return;
    
    glm::mat4 view = camera->getViewMatrix();
    float fov = glm::radians(camera->getZoom());
    float aspect = swapChain->getExtent().width / (float)swapChain->getExtent().height;
    glm::mat4 projection = glm::perspective(fov, aspect, 0.1f, 100.0f);
    projection[1][1] *= -1;  // Vulkan Y 轴翻转
    
    waterPass->updateUniforms(view, projection, camera->getPosition(), totalTime, frameIndex);
    
    // 更新 SSR Pass 参数
    if (ssrPass) {
        ssrPass->updateParams(projection, view, camera->getPosition(), frameIndex);
    }
}

void VulkanRenderer::recordWaterSceneCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    // 完整的多 Pass 渲染流程：
    // Pass 1: G-Buffer Pass - 渲染场景到 G-Buffer（使用 ForwardPass 的 Pipeline）
    // Pass 2: SSR Pass - 计算屏幕空间反射
    // Pass 3: Final Pass - 渲染场景 + 水面（使用 SSR 结果）
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    uint32_t width = swapChain->getExtent().width;
    uint32_t height = swapChain->getExtent().height;
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChain->getExtent();

    // ========================================
    // Pass 1: G-Buffer Pass - 使用 GBuffer 自己的 Pipeline 渲染场景
    // ========================================
    if (gbuffer && scene) {
        // 更新 GBuffer 的 UBO（全局数据，不包含 model 和 normalMatrix）
        GBufferPass::UniformBufferObject gbufferUBO{};
        
        // 从相机获取 View/Projection 矩阵
        if (camera) {
            gbufferUBO.view = camera->getViewMatrix();
            float fov = glm::radians(camera->getZoom());
            float aspect = swapChain->getExtent().width / (float)swapChain->getExtent().height;
            gbufferUBO.proj = glm::perspective(fov, aspect, 0.1f, 100.0f);
            gbufferUBO.proj[1][1] *= -1;  // Vulkan Y 轴翻转
            gbufferUBO.viewPos = glm::vec4(camera->getPosition(), 1.0f);
        } else {
            gbufferUBO.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), 
                                          glm::vec3(0.0f, 0.0f, 0.0f), 
                                          glm::vec3(0.0f, 1.0f, 0.0f));
            gbufferUBO.proj = glm::perspective(glm::radians(45.0f), 
                swapChain->getExtent().width / (float)swapChain->getExtent().height, 0.1f, 100.0f);
            gbufferUBO.proj[1][1] *= -1;
            gbufferUBO.viewPos = glm::vec4(0.0f, 0.0f, 5.0f, 1.0f);
        }
        
        // 光源参数（与前向渲染保持一致）
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float>(currentTime - startTime).count();
        
        float lightRadius = 5.0f;
        float lightSpeed = 0.5f;
        float lightAngle = time * lightSpeed;
        glm::vec3 lightPosition = glm::vec3(
            lightRadius * cos(lightAngle),
            3.0f,
            lightRadius * sin(lightAngle)
        );
        gbufferUBO.lightPos = glm::vec4(lightPosition, 1.0f);
        gbufferUBO.lightColor = glm::vec4(300.0f, 300.0f, 300.0f, 1.0f);
        
        // 更新 GBuffer 的 UBO（只包含全局数据）
        gbuffer->updateUniformBuffer(currentFrame, gbufferUBO);
        
        // 开始 GBuffer RenderPass
        gbuffer->beginRenderPass(commandBuffer);
        
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        
        // 绑定 Pipeline
        gbuffer->bindPipeline(commandBuffer);
        
        // 使用新的 RTTI 多态接口渲染
        if (renderSystem) {
            // 注意：updateRenderables 应该在主渲染循环中调用一次，包含所有需要的 Pass
            // 这里直接使用统一的 render 接口
            renderSystem->render(commandBuffer, gbuffer.get(), currentFrame);
        }
        
        gbuffer->endRenderPass(commandBuffer);
    }

    // ========================================
    // Pass 1.5: 复制 GBuffer Albedo 到 sceneColorImage
    // ========================================
    if (gbuffer && sceneColorImage != VK_NULL_HANDLE) {
        // 转换 sceneColorImage 布局为传输目标
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = sceneColorImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // 执行 Blit（从 GBuffer Albedo 复制到 sceneColorImage）
        VkImageBlit blitRegion{};
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.mipLevel = 0;
        blitRegion.srcSubresource.baseArrayLayer = 0;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};
        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.mipLevel = 0;
        blitRegion.dstSubresource.baseArrayLayer = 0;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.dstOffsets[0] = {0, 0, 0};
        blitRegion.dstOffsets[1] = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};

        vkCmdBlitImage(commandBuffer,
            gbuffer->getAlbedoImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            sceneColorImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blitRegion, VK_FILTER_LINEAR);

        // 转换 sceneColorImage 布局为着色器可读
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // ========================================
    // Pass 2: SSR Pass - 计算屏幕空间反射
    // ========================================
    if (ssrPass && gbuffer && sceneColorView) {
        ssrPass->execute(commandBuffer, gbuffer.get(), sceneColorView, currentFrame);
    }

    // ========================================
    // Pass 3: Final Pass - 渲染到交换链
    // ========================================
    {
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = swapChain->getRenderPass();
        renderPassInfo.framebuffer = swapChain->getFramebuffers()[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapChain->getExtent();

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.02f, 0.05f, 0.1f, 1.0f}}; // 深蓝色夜空背景
        clearValues[1].depthStencil = {1.0f, 0};

        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // 使用 LightingPass 进行延迟光照渲染（从 G-Buffer 读取数据）
        if (lightingPass && gbuffer) {
            // 计算光源位置（与 ForwardPass 保持一致）
            static auto startTime = std::chrono::high_resolution_clock::now();
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float>(currentTime - startTime).count();
            
            float lightRadius = 5.0f;
            float lightSpeed = 0.5f;
            float lightAngle = time * lightSpeed;
            glm::vec3 lightPosition = glm::vec3(
                lightRadius * cos(lightAngle),
                3.0f,
                lightRadius * sin(lightAngle)
            );
            
            // 更新 LightingPass 的 Uniform
            glm::vec3 camPos = camera ? camera->getPosition() : glm::vec3(0.0f, 0.0f, 5.0f);
            lightingPass->updateUniforms(currentFrame, camPos, lightPosition, 
                                         glm::vec3(300.0f, 300.0f, 300.0f), 1.0f);
            
            // 渲染全屏光照四边形
            lightingPass->render(commandBuffer, currentFrame);
        }

        // 渲染水面（使用 SSR 反射结果）
        if (waterPass) {
            waterPass->render(commandBuffer, currentFrame);
        }

        // 更新并渲染 UI（在场景渲染之后，RenderPass 结束之前）
        updateUI();
        renderUI(commandBuffer);

        vkCmdEndRenderPass(commandBuffer);
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}
