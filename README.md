# Vulkan PBR 渲染项目

这是一个基于Vulkan API的物理渲染（PBR）项目，实现了Cook-Torrance BRDF着色模型。

[TOC]



## 项目特性

- **Vulkan API**: 使用现代的Vulkan图形API进行高性能渲染
- **Cook-Torrance BRDF**: 实现了工业标准的物理渲染模型
- **OBJ 模型加载**: 支持导入 Wavefront OBJ 格式的 3D 模型
- **材质系统**: 支持albedo、normal、metallic、roughness和AO贴图
- **相机控制**: 支持鼠标和键盘控制的第一人称相机
- **拖拽加载**: 支持拖拽 OBJ 文件到窗口直接加载
- **场景管理**: 简单的场景图系统，支持多个物体和变换

## 系统要求

- macOS 10.15+/Windows (Catalina或更高版本)
- Xcode Command Line Tools
- CMake 3.16+
- Vulkan SDK
- MoltenVK (macOS上的Vulkan实现)

## 依赖库

- **Vulkan**: 图形API
- **GLFW**: 窗口管理和输入处理
- **GLM**: 数学库

## 安装依赖（MacOS）

### 1. 安装Vulkan SDK

从[LunarG官网](https://vulkan.lunarg.com/sdk/home)下载并安装Vulkan SDK for macOS。

### 2. 使用Homebrew安装其他依赖

```bash
# 安装Homebrew (如果还没有安装)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 安装依赖
brew install cmake
brew install glfw
brew install glm
```

### 3. 设置环境变量

将以下内容添加到你的 `~/.zshrc` 或 `~/.bash_profile`:

```bash
export VULKAN_SDK_PATH="/Users/$USER/VulkanSDK/最新版本/macOS"
export PATH="$VULKAN_SDK_PATH/bin:$PATH"
export DYLD_LIBRARY_PATH="$VULKAN_SDK_PATH/lib:$DYLD_LIBRARY_PATH"
export VK_ICD_FILENAMES="$VULKAN_SDK_PATH/share/vulkan/icd.d/MoltenVK_icd.json"
export VK_LAYER_PATH="$VULKAN_SDK_PATH/share/vulkan/explicit_layer.d"
```

## 构建项目

1. 创建构建目录并配置项目：
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

2. 编译项目：
```bash
make -j$(sysctl -n hw.ncpu)
```

## 在Visual Studio Code中运行（Windows)

1. 打开项目文件夹
2. 安装推荐的扩展：
   - C/C++ (Microsoft)
   - CMake Tools (Microsoft)
   - CMake (twxs)

3. 按 `Cmd+Shift+P` 打开命令面板，运行：
   - `CMake: Configure`
   - `CMake: Build`

4. 使用 `F5` 开始调试，或 `Ctrl+F5` 运行而不调试

## 控制说明

- **WASD**: 相机移动
- **空格/Shift**: 相机上升/下降
- **鼠标右键 + 移动**: 相机旋转
- **鼠标滚轮**: 调整视野
- **1**: 切换到球体模型
- **2**: 切换到立方体模型
- **3**: 切换到平面模型
- **拖拽 OBJ 文件**: 加载自定义 3D 模型
- **ESC**: 退出程序

## 项目结构

```
PBR/
├── CMakeLists.txt              # CMake 构建配置
├── README.md                   # 项目说明文档
├── compile_shaders.sh          # 着色器编译脚本
├── setup_and_run.sh            # 构建运行脚本
│
├── src/                        # 源代码目录
│   ├── main.cpp                # 程序入口
│   ├── VulkanRenderer.*        # 主渲染器（核心协调类）
│   ├── VulkanDevice.*          # Vulkan 设备管理
│   ├── VulkanSwapChain.*       # 交换链管理
│   ├── VulkanPipeline.*        # 图形管线
│   ├── VulkanBuffer.*          # GPU 缓冲区
│   ├── VulkanTexture.*         # 纹理资源
│   ├── Mesh.*                  # 网格几何体
│   ├── Material.*              # PBR 材质
│   ├── Camera.*                # 相机控制
│   ├── Scene.*                 # 场景管理
│   └── Utils.*                 # 工具函数
│
├── shaders/                    # GLSL 着色器源码
│   ├── pbr.vert                # PBR 顶点着色器
│   ├── pbr.frag                # PBR 片段着色器
│   ├── simple.vert/frag        # 简单测试着色器
│   └── mesh.vert/frag          # 网格着色器
│
├── assets/                     # 资源文件（纹理、模型等）
│
└── build/                      # 构建输出目录
    └── bin/                    # 可执行文件和编译后的着色器
```

---

## 架构详解

### 渲染流程图

```
┌─────────────────────────────────────────────────────────────────────┐
│                         VulkanRenderer (主渲染器)                     │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                        主循环 mainLoop()                       │   │
│  │  ┌─────────────┐    ┌─────────────┐    ┌─────────────────┐   │   │
│  │  │ 处理输入     │ -> │ 更新 UBO    │ -> │ 录制/提交命令    │   │   │
│  │  │ 键盘/鼠标   │    │ MVP/光源    │    │ drawFrame()     │   │   │
│  │  └─────────────┘    └─────────────┘    └─────────────────┘   │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                   │                                  │
│          ┌────────────────────────┼────────────────────────┐        │
│          ▼                        ▼                        ▼        │
│  ┌──────────────┐        ┌──────────────┐        ┌──────────────┐  │
│  │ VulkanDevice │        │VulkanSwapChain│       │VulkanPipeline │  │
│  │  设备/队列   │        │ 交换链/帧缓冲 │        │ 着色器/管线  │  │
│  └──────────────┘        └──────────────┘        └──────────────┘  │
│          │                        │                        │        │
│          ▼                        ▼                        ▼        │
│  ┌──────────────┐        ┌──────────────┐        ┌──────────────┐  │
│  │ VulkanBuffer │        │VulkanTexture │        │    Mesh      │  │
│  │ 顶点/索引/UBO│        │  纹理采样    │        │  几何数据    │  │
│  └──────────────┘        └──────────────┘        └──────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
                    ┌─────────────────────────────┐
                    │      GPU 渲染管线            │
                    │  pbr.vert -> pbr.frag       │
                    │  (Cook-Torrance BRDF)       │
                    └─────────────────────────────┘
```

---

### 核心类详解

#### 1. VulkanRenderer（主渲染器）
**文件**: `VulkanRenderer.h/cpp`

**职责**: 整个渲染系统的核心协调者，管理渲染循环和资源生命周期。

| 成员/方法 | 功能 |
|-----------|------|
| `run()` | 启动主渲染循环 |
| `mainLoop()` | 主循环：处理输入、更新状态、绘制帧 |
| `drawFrame()` | 获取交换链图像、录制命令、提交到 GPU 队列、呈现 |
| `updateUniformBuffer()` | 更新 MVP 矩阵、相机位置、光源信息 |
| `recordCommandBuffer()` | 录制 Vulkan 绘制命令（绑定管线、缓冲区、调用绘制） |
| `createVertexBuffer()` | 创建顶点缓冲区 |
| `createIndexBuffer()` | 创建索引缓冲区 |
| `createUniformBuffers()` | 创建 Uniform 缓冲区（每帧一个） |
| `createDescriptorPool/Sets()` | 创建描述符池和描述符集 |

**关键数据结构**:
```cpp
struct UniformBufferObject {
    glm::mat4 model;        // 模型矩阵
    glm::mat4 view;         // 视图矩阵
    glm::mat4 proj;         // 投影矩阵
    glm::mat4 normalMatrix; // 法线矩阵
    glm::vec4 viewPos;      // 相机位置
    glm::vec4 lightPos;     // 光源位置
    glm::vec4 lightColor;   // 光源颜色/强度
};
```

---

#### 2. VulkanDevice（设备管理）
**文件**: `VulkanDevice.h/cpp`

**职责**: 管理 Vulkan 实例、物理/逻辑设备、队列和命令池。

| 成员/方法 | 功能 |
|-----------|------|
| `createInstance()` | 创建 Vulkan 实例（启用验证层） |
| `pickPhysicalDevice()` | 选择合适的 GPU（检查交换链支持等） |
| `createLogicalDevice()` | 创建逻辑设备和队列 |
| `createCommandPool()` | 创建命令池 |
| `findMemoryType()` | 查找合适的内存类型 |
| `createBuffer()` | 创建 Vulkan 缓冲区 |
| `createImage()` | 创建 Vulkan 图像 |
| `beginSingleTimeCommands()` | 开始一次性命令（用于数据传输） |
| `endSingleTimeCommands()` | 结束并提交一次性命令 |

**关键数据结构**:
```cpp
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;  // 图形队列族
    std::optional<uint32_t> presentFamily;   // 呈现队列族
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};
```

---

#### 3. VulkanSwapChain（交换链）
**文件**: `VulkanSwapChain.h/cpp`

**职责**: 管理交换链、图像视图、深度缓冲和帧缓冲。

| 成员/方法 | 功能 |
|-----------|------|
| `createSwapChain()` | 创建交换链（选择格式、呈现模式、分辨率） |
| `createImageViews()` | 为交换链图像创建视图 |
| `createRenderPass()` | 创建渲染通道（颜色+深度附件） |
| `createDepthResources()` | 创建深度缓冲区 |
| `createFramebuffers()` | 创建帧缓冲 |
| `recreate()` | 窗口大小改变时重建交换链 |

---

#### 4. VulkanPipeline（图形管线）
**文件**: `VulkanPipeline.h/cpp`

**职责**: 管理着色器、管线状态和描述符布局。

| 成员/方法 | 功能 |
|-----------|------|
| `createGraphicsPipeline()` | 创建完整的图形管线 |
| `createDescriptorSetLayout()` | 创建描述符集布局（UBO 绑定） |
| `createShaderModule()` | 从 SPIR-V 创建着色器模块 |
| `readFile()` | 读取编译后的着色器文件 |

**管线配置**:
- 顶点输入：位置、法线、纹理坐标、切线
- 光栅化：填充模式、背面剔除
- 深度测试：启用深度写入和比较
- 颜色混合：不透明渲染

---

#### 5. VulkanBuffer（缓冲区）
**文件**: `VulkanBuffer.h/cpp`

**职责**: 封装 Vulkan 缓冲区的创建、映射和数据传输。

| 成员/方法 | 功能 |
|-----------|------|
| 构造函数 | 创建指定大小和用途的缓冲区 |
| `map()` | 映射缓冲区内存到 CPU 可访问地址 |
| `unmap()` | 取消映射 |
| `copyFrom()` | 从 CPU 内存复制数据到缓冲区 |

**用途**: 顶点缓冲区、索引缓冲区、Uniform 缓冲区

---

#### 6. VulkanTexture（纹理）
**文件**: `VulkanTexture.h/cpp`

**职责**: 管理纹理图像、视图和采样器。

| 成员/方法 | 功能 |
|-----------|------|
| `loadFromFile()` | 从文件加载纹理 |
| `createFromData()` | 从内存数据创建纹理 |
| `createDefault()` | 创建默认纹理（白色） |
| `createImage()` | 创建 Vulkan 图像 |
| `createImageView()` | 创建图像视图 |
| `createSampler()` | 创建纹理采样器 |
| `transitionImageLayout()` | 转换图像布局 |

---

#### 7. Mesh（网格）
**文件**: `Mesh.h/cpp`

**职责**: 管理几何体数据（顶点和索引），支持多种几何体生成和 OBJ 文件加载。

| 成员/方法 | 功能 |
|-----------|------|
| `createCube()` | 创建立方体网格 |
| `createSphere()` | 创建 UV 球体网格 |
| `createPlane()` | 创建平面网格 |
| `loadFromOBJ()` | 从 OBJ 文件加载模型 |
| `centerAndNormalize()` | 居中并归一化模型到单位大小 |
| `calculateNormals()` | 计算顶点法线（如果 OBJ 没有提供） |
| `calculateTangents()` | 计算切线空间（用于法线贴图） |
| `getVertices()` | 获取顶点数组 |
| `getIndices()` | 获取索引数组 |

**顶点结构**:
```cpp
struct Vertex {
    glm::vec3 pos;       // 位置
    glm::vec3 normal;    // 法线
    glm::vec2 texCoord;  // 纹理坐标
    glm::vec3 tangent;   // 切线（用于法线贴图）
};
```

**OBJ 加载流程**:
1. 使用 tinyobjloader 解析 OBJ 文件
2. 使用 HashMap 进行顶点去重
3. 如果缺少法线，自动计算面法线并累加到顶点
4. 计算切线空间（Tangent Space）用于法线贴图
5. 计算包围盒并可选择居中/归一化模型

---

#### 8. Material（材质）
**文件**: `Material.h/cpp`

**职责**: 管理 PBR 材质属性和纹理。

| 属性 | 说明 |
|------|------|
| `albedo` | 基础颜色 |
| `metallic` | 金属度 (0-1) |
| `roughness` | 粗糙度 (0-1) |
| `ao` | 环境光遮蔽 |
| `emissive` | 自发光颜色 |

**支持的纹理贴图**: Albedo、Normal、Metallic、Roughness、AO

---

#### 9. Camera（相机）
**文件**: `Camera.h/cpp`

**职责**: FPS 风格的相机控制。

| 成员/方法 | 功能 |
|-----------|------|
| `getViewMatrix()` | 获取视图矩阵 |
| `processKeyboard()` | 处理 WASD 移动 |
| `processMouseMovement()` | 处理鼠标旋转 |
| `processMouseScroll()` | 处理滚轮缩放 |

---

#### 10. Scene（场景）
**文件**: `Scene.h/cpp`

**职责**: 场景图管理，包含物体、变换和光源。

**数据结构**:
```cpp
struct Transform {
    glm::vec3 position;
    glm::vec3 rotation;  // 欧拉角
    glm::vec3 scale;
};

struct Light {
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
};
```

---

#### 11. Utils（工具类）
**文件**: `Utils.h/cpp`

**职责**: 通用工具函数。

| 函数 | 功能 |
|------|------|
| `readFile()` | 读取二进制文件 |
| `debugCallback()` | Vulkan 验证层回调 |
| `radians()/degrees()` | 角度转换 |

---

### 着色器架构

#### PBR 着色器 (`shaders/pbr.vert` + `shaders/pbr.frag`)

**顶点着色器输出**:
```glsl
out vec3 fragWorldPos;    // 世界空间位置
out vec3 fragNormal;      // 世界空间法线
out vec2 fragTexCoord;    // 纹理坐标
out vec3 fragTangent;     // 切线
out vec3 fragBitangent;   // 副切线
out vec3 fragViewPos;     // 相机位置
out vec3 fragLightPos;    // 光源位置
```

**片段着色器核心算法**:
```
Cook-Torrance BRDF:
    f = (D * F * G) / (4 * (N·V) * (N·L))

其中:
    D = GGX 法线分布函数 (Normal Distribution Function)
    F = Fresnel-Schlick 菲涅尔项
    G = Smith-GGX 几何遮蔽函数

最终颜色:
    Lo = (kD * albedo/π + specular) * radiance * (N·L)
    color = ambient + Lo
    color = Reinhard Tonemapping(color)
    color = Gamma Correction(color)
```

---

### 数据流

```
CPU 端:
┌────────────┐     ┌──────────────┐     ┌────────────────┐
│  Camera    │ --> │ UBO 更新      │ --> │ Uniform Buffer │
│  变换矩阵  │     │ MVP/光源信息  │     │ (GPU 内存)     │
└────────────┘     └──────────────┘     └────────────────┘

┌────────────┐     ┌──────────────┐     ┌────────────────┐
│  Mesh      │ --> │ 顶点/索引    │ --> │ Vertex/Index   │
│  几何数据  │     │ 数据上传     │     │ Buffer (GPU)   │
└────────────┘     └──────────────┘     └────────────────┘

GPU 端:
┌────────────────┐     ┌────────────────┐     ┌────────────────┐
│ Vertex Shader  │ --> │ Rasterizer     │ --> │ Fragment Shader│
│ 顶点变换       │     │ 光栅化         │     │ PBR 光照计算   │
└────────────────┘     └────────────────┘     └────────────────┘
                                                      │
                                                      ▼
                                              ┌────────────────┐
                                              │ 帧缓冲/屏幕    │
                                              └────────────────┘
```

## 着色器编译

项目包含了预编译的SPIR-V着色器。如果需要修改着色器，使用以下命令重新编译：

```bash
# 编译顶点着色器
glslc shaders/pbr.vert -o shaders/pbr.vert.spv

# 编译片段着色器
glslc shaders/pbr.frag -o shaders/pbr.frag.spv
```

## 故障排除

### 常见问题

1. **找不到Vulkan库**：确保已正确安装Vulkan SDK并设置了环境变量
2. **MoltenVK错误**：确保使用最新版本的Vulkan SDK
3. **编译错误**：检查是否安装了所有依赖库

### 调试

项目在Debug模式下启用了Vulkan验证层，会在控制台输出详细的调试信息。

## 贡献

欢迎提交Issue和Pull Request来改进这个项目！

## 许可

本项目采用MIT许可证。详见LICENSE文件。