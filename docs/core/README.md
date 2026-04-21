# Core 模块文档

> 本文档描述 V Engine 的 Vulkan 核心封装层，提供对 Vulkan API 的高级抽象。

---

## 模块概览

`src/core/` 目录包含 Vulkan API 的底层封装，是整个渲染引擎的基础设施层。

### 文件结构

```
src/core/
├── VulkanDevice.h/cpp       # 设备管理（Instance, PhysicalDevice, LogicalDevice）
├── VulkanSwapChain.h/cpp    # 交换链管理（Images, Views, Framebuffers）
├── VulkanBuffer.h/cpp       # GPU 缓冲区封装
├── VulkanTexture.h/cpp      # 纹理资源管理
├── VulkanPipeline.h/cpp     # 图形管线封装
└── Utils.h/cpp              # 工具函数
```

### 依赖关系

```
┌─────────────────────────────────────────────────────────────┐
│                    VulkanRenderer                           │
│                    (src/renderer/)                          │
└─────────────┬───────────────────────────────────────────────┘
              │ 使用
              ▼
┌─────────────────────────────────────────────────────────────┐
│  VulkanPipeline  │  VulkanSwapChain  │  VulkanTexture       │
│                  │                    │                      │
│                  │                    │  VulkanBuffer        │
└──────────────────┴────────────────────┴──────────────────────┘
              │ 依赖
              ▼
┌─────────────────────────────────────────────────────────────┐
│                      VulkanDevice                           │
│              (核心设备，所有资源的基础)                       │
└─────────────────────────────────────────────────────────────┘
              │ 依赖
              ▼
┌─────────────────────────────────────────────────────────────┐
│                      Utils                                  │
│                  (工具函数库)                                │
└─────────────────────────────────────────────────────────────┘
```

---

## 类详细说明

### 1. VulkanDevice

**文件**: `VulkanDevice.h`, `VulkanDevice.cpp`

Vulkan 设备管理类，负责 Vulkan 实例、物理设备、逻辑设备的创建和管理。是整个渲染系统的核心基础。

#### 数据结构

```cpp
// 队列族索引
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;  // 图形队列族
    std::optional<uint32_t> presentFamily;   // 呈现队列族
    
    bool isComplete();  // 检查是否找到所有必需的队列族
};

// 交换链支持详情
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;       // 表面能力
    std::vector<VkSurfaceFormatKHR> formats;     // 支持的格式
    std::vector<VkPresentModeKHR> presentModes;  // 支持的呈现模式
};
```

#### 公共接口

| 方法 | 说明 |
|------|------|
| `VulkanDevice(GLFWwindow* window)` | 构造函数，初始化所有 Vulkan 对象 |
| `getInstance()` | 获取 VkInstance |
| `getDevice()` | 获取 VkDevice（逻辑设备） |
| `getPhysicalDevice()` | 获取 VkPhysicalDevice |
| `getSurface()` | 获取 VkSurfaceKHR |
| `getGraphicsQueue()` | 获取图形队列 |
| `getPresentQueue()` | 获取呈现队列 |
| `getCommandPool()` | 获取命令池 |
| `findQueueFamilies(device)` | 查找队列族 |
| `querySwapChainSupport(device)` | 查询交换链支持 |
| `findMemoryType(typeFilter, properties)` | 查找合适的内存类型 |
| `findSupportedFormat(candidates, tiling, features)` | 查找支持的格式 |
| `findDepthFormat()` | 查找深度缓冲格式 |
| `createBuffer(...)` | 创建 GPU 缓冲区 |
| `copyBuffer(src, dst, size)` | 复制缓冲区 |
| `createImage(...)` | 创建图像资源 |
| `beginSingleTimeCommands()` | 开始单次命令缓冲 |
| `endSingleTimeCommands(cmd)` | 结束并提交单次命令 |

#### 初始化流程

```cpp
VulkanDevice::VulkanDevice(GLFWwindow* window) {
    createInstance();        // 1. 创建 Vulkan 实例
    setupDebugMessenger();   // 2. 设置调试回调（Debug 模式）
    createSurface();         // 3. 创建窗口表面
    pickPhysicalDevice();    // 4. 选择物理设备（显卡）
    createLogicalDevice();   // 5. 创建逻辑设备和队列
    createCommandPool();     // 6. 创建命令池
}
```

#### 验证层

```cpp
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"  // Khronos 标准验证层
};

// Debug 模式启用验证层，Release 模式禁用
#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif
```

#### 必需的设备扩展

```cpp
const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME  // 交换链扩展（必需）
};
```

---

### 2. VulkanSwapChain

**文件**: `VulkanSwapChain.h`, `VulkanSwapChain.cpp`

交换链管理类，负责帧缓冲、呈现和窗口大小变化处理。

#### 公共接口

| 方法 | 说明 |
|------|------|
| `VulkanSwapChain(device, width, height)` | 构造函数 |
| `recreate(width, height)` | 重建交换链（窗口大小变化时） |
| `getSwapChain()` | 获取 VkSwapchainKHR |
| `getImageFormat()` | 获取图像格式 |
| `getExtent()` | 获取交换链尺寸 |
| `getRenderPass()` | 获取默认渲染通道 |
| `getImages()` | 获取交换链图像数组 |
| `getImageViews()` | 获取图像视图数组 |
| `getFramebuffers()` | 获取帧缓冲数组 |
| `getImageCount()` | 获取图像数量 |

#### 内部资源

```cpp
// 交换链资源
VkSwapchainKHR swapChain;
std::vector<VkImage> swapChainImages;
std::vector<VkImageView> swapChainImageViews;
std::vector<VkFramebuffer> swapChainFramebuffers;
VkFormat swapChainImageFormat;
VkExtent2D swapChainExtent;
VkRenderPass renderPass;

// 深度资源
VkImage depthImage;
VkDeviceMemory depthImageMemory;
VkImageView depthImageView;
```

#### 格式选择策略

```cpp
// 优先选择 SRGB 格式
VkSurfaceFormatKHR chooseSwapSurfaceFormat(...) {
    // 优先: VK_FORMAT_B8G8R8A8_SRGB + VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
}

// 优先选择三重缓冲
VkPresentModeKHR chooseSwapPresentMode(...) {
    // 优先: VK_PRESENT_MODE_MAILBOX_KHR（三重缓冲）
    // 回退: VK_PRESENT_MODE_FIFO_KHR（垂直同步）
}
```

---

### 3. VulkanBuffer

**文件**: `VulkanBuffer.h`, `VulkanBuffer.cpp`

GPU 缓冲区封装类，用于顶点缓冲、索引缓冲、Uniform 缓冲等。

#### 公共接口

| 方法 | 说明 |
|------|------|
| `VulkanBuffer(device, size, usage, properties)` | 创建缓冲区 |
| `map(data, size, offset)` | 映射内存到 CPU 可访问 |
| `unmap()` | 取消映射 |
| `copyFrom(src, size)` | 从 CPU 内存复制数据 |
| `getBuffer()` | 获取 VkBuffer 句柄 |
| `getMemory()` | 获取 VkDeviceMemory 句柄 |
| `getSize()` | 获取缓冲区大小 |

#### 使用示例

```cpp
// 创建 Uniform 缓冲区
auto uniformBuffer = std::make_unique<VulkanBuffer>(
    device,
    sizeof(UniformBufferObject),
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
);

// 更新数据
void* data;
uniformBuffer->map(&data);
memcpy(data, &ubo, sizeof(ubo));
uniformBuffer->unmap();

// 或使用便捷方法
uniformBuffer->copyFrom(&ubo, sizeof(ubo));
```

#### 常用用途标志

| 用途 | Usage Flag | Memory Properties |
|------|------------|-------------------|
| 顶点缓冲 | `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT` | `DEVICE_LOCAL` |
| 索引缓冲 | `VK_BUFFER_USAGE_INDEX_BUFFER_BIT` | `DEVICE_LOCAL` |
| Uniform | `VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT` | `HOST_VISIBLE \| HOST_COHERENT` |
| 暂存缓冲 | `VK_BUFFER_USAGE_TRANSFER_SRC_BIT` | `HOST_VISIBLE \| HOST_COHERENT` |

---

### 4. VulkanTexture

**文件**: `VulkanTexture.h`, `VulkanTexture.cpp`

纹理资源管理类，支持从文件加载和创建默认纹理。

#### 公共接口

| 方法 | 说明 |
|------|------|
| `VulkanTexture(device)` | 默认构造（需后续加载） |
| `VulkanTexture(device, filepath)` | 从文件加载构造 |
| `loadFromFile(filepath)` | 从文件加载纹理 |
| `createDefaultTexture(r, g, b, a)` | 创建默认纯色纹理 |
| `createDefaultNormalTexture()` | 创建默认法线纹理 |
| `getImageView()` | 获取图像视图 |
| `getSampler()` | 获取采样器 |
| `getImage()` | 获取 VkImage |
| `getWidth()` / `getHeight()` | 获取尺寸 |

#### 静态工厂方法

```cpp
// 创建默认白色纹理
static std::unique_ptr<VulkanTexture> createDefaultTextureStatic(
    std::shared_ptr<VulkanDevice> device, 
    uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint8_t a = 255
);
```

#### 支持的格式

- **颜色贴图**: `VK_FORMAT_R8G8B8A8_SRGB`（自动 gamma 校正）
- **法线贴图**: `VK_FORMAT_R8G8B8A8_UNORM`（线性空间）

#### 图像布局转换

```cpp
void transitionImageLayout(VkImage image, VkFormat format, 
                          VkImageLayout oldLayout, 
                          VkImageLayout newLayout);
```

支持的转换：
- `UNDEFINED` → `TRANSFER_DST_OPTIMAL`（准备写入）
- `TRANSFER_DST_OPTIMAL` → `SHADER_READ_ONLY_OPTIMAL`（准备着色器读取）

---

### 5. VulkanPipeline

**文件**: `VulkanPipeline.h`, `VulkanPipeline.cpp`

图形管线封装类（默认管线，主要用于参考）。

#### 公共接口

| 方法 | 说明 |
|------|------|
| `VulkanPipeline(device, swapChain)` | 构造函数 |
| `getPipeline()` | 获取 VkPipeline |
| `getPipelineLayout()` | 获取 VkPipelineLayout |
| `getDescriptorSetLayout()` | 获取描述符集布局 |
| `createShaderModule(device, code)` | 静态：创建着色器模块 |

#### 着色器模块创建

```cpp
// 静态方法，供其他类（如 RenderPass）使用
static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code);
```

**注意**: 实际项目中，各个 RenderPass 会创建自己的管线，此类主要作为参考实现。

---

### 6. Utils

**文件**: `Utils.h`, `Utils.cpp`

通用工具函数集合。

#### 公共接口

| 方法 | 说明 |
|------|------|
| `readFile(filename)` | 读取二进制文件（用于加载 SPIR-V） |
| `debugCallback(...)` | Vulkan 调试消息回调 |
| `populateDebugMessengerCreateInfo(...)` | 填充调试信使创建信息 |
| `CreateDebugUtilsMessengerEXT(...)` | 创建调试信使 |
| `DestroyDebugUtilsMessengerEXT(...)` | 销毁调试信使 |
| `radians(degrees)` | 角度转弧度 |
| `degrees(radians)` | 弧度转角度 |

#### 调试回调

```cpp
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
);
```

消息严重级别：
- `VERBOSE`: 诊断信息
- `INFO`: 信息性消息
- `WARNING`: 警告（可能是 bug）
- `ERROR`: 错误（无效操作）

---

## 资源生命周期

### 创建顺序

```
1. VulkanDevice（最先创建）
   ↓
2. VulkanSwapChain（依赖 Device）
   ↓
3. VulkanPipeline（依赖 Device, SwapChain）
   ↓
4. VulkanBuffer / VulkanTexture（按需创建）
```

### 销毁顺序（与创建相反）

```
1. VulkanBuffer / VulkanTexture
   ↓
2. VulkanPipeline
   ↓
3. VulkanSwapChain
   ↓
4. VulkanDevice（最后销毁）
```

---

## 使用示例

### 初始化核心组件

```cpp
// 1. 创建设备
auto device = std::make_shared<VulkanDevice>(window);

// 2. 创建交换链
auto swapChain = std::make_shared<VulkanSwapChain>(device, width, height);

// 3. 创建缓冲区
auto vertexBuffer = std::make_unique<VulkanBuffer>(
    device,
    sizeof(vertices[0]) * vertices.size(),
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
);

// 4. 加载纹理
auto texture = std::make_unique<VulkanTexture>(device, "assets/textures/albedo.png");
```

### 处理窗口大小变化

```cpp
void onWindowResize(int width, int height) {
    vkDeviceWaitIdle(device->getDevice());
    swapChain->recreate(width, height);
    // 重建依赖交换链的资源...
}
```

### 单次命令执行

```cpp
// 执行一次性 GPU 操作（如复制缓冲区）
VkCommandBuffer cmd = device->beginSingleTimeCommands();

// 记录命令...
vkCmdCopyBuffer(cmd, srcBuffer, dstBuffer, 1, &copyRegion);

device->endSingleTimeCommands(cmd);  // 提交并等待完成
```

---

## 设计原则

1. **RAII**: 所有资源在构造函数中创建，在析构函数中销毁
2. **shared_ptr**: Device 使用 shared_ptr 共享，确保资源正确释放顺序
3. **封装**: 隐藏 Vulkan 复杂性，提供简洁接口
4. **可扩展**: 各类可独立使用，支持自定义管线和渲染通道

---

*文档版本: v0.9.1*
*最后更新: 2024/02/25*
