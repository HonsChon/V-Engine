# Vulkan PBR 渲染项目

这是一个基于Vulkan API的物理渲染（PBR）项目，实现了Cook-Torrance BRDF着色模型。

## 项目特性

- **Vulkan API**: 使用现代的Vulkan图形API进行高性能渲染
- **Cook-Torrance BRDF**: 实现了工业标准的物理渲染模型
- **材质系统**: 支持albedo、normal、metallic、roughness和AO贴图
- **相机控制**: 支持鼠标和键盘控制的第一人称相机
- **场景管理**: 简单的场景图系统，支持多个物体和变换

## 系统要求

- macOS 10.15+ (Catalina或更高版本)
- Xcode Command Line Tools
- CMake 3.16+
- Vulkan SDK
- MoltenVK (macOS上的Vulkan实现)

## 依赖库

- **Vulkan**: 图形API
- **GLFW**: 窗口管理和输入处理
- **GLM**: 数学库

## 安装依赖

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

## 在Visual Studio Code中运行

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
- **鼠标移动**: 相机旋转
- **鼠标滚轮**: 调整视野

## 项目结构

```
├── CMakeLists.txt          # 构建配置
├── README.md              # 项目说明
├── .vscode/              # VS Code配置
│   ├── launch.json
│   └── tasks.json
├── src/                  # 源代码
│   ├── main.cpp
│   ├── VulkanRenderer.*   # 主渲染器
│   ├── VulkanDevice.*     # Vulkan设备管理
│   ├── VulkanSwapChain.*  # 交换链管理
│   ├── VulkanBuffer.*     # 缓冲区管理
│   ├── VulkanTexture.*    # 纹理管理
│   ├── VulkanPipeline.*   # 渲染管线
│   ├── Mesh.*            # 网格数据
│   ├── Material.*        # 材质系统
│   ├── Camera.*          # 相机控制
│   ├── Scene.*           # 场景管理
│   └── Utils.*           # 工具函数
├── shaders/              # 着色器
│   ├── pbr.vert         # 顶点着色器
│   └── pbr.frag         # 片段着色器
└── assets/               # 资源文件
    └── (贴图和模型文件)
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