#!/bin/bash

# VulkanPBR 项目快速启动脚本

echo "🚀 VulkanPBR 项目启动脚本"
echo "========================"

# 检查是否在正确的目录
if [ ! -f "CMakeLists.txt" ]; then
    echo "❌ 错误: 请在项目根目录下运行此脚本"
    exit 1
fi

# 检查依赖
echo "🔍 检查依赖..."

# 检查CMake
if ! command -v cmake &> /dev/null; then
    echo "❌ 错误: 未找到CMake，请使用 'brew install cmake' 安装"
    exit 1
fi
echo "✅ CMake: $(cmake --version | head -n1)"

# 检查Vulkan SDK
if [ -z "$VULKAN_SDK" ] && [ -z "$VK_ICD_FILENAMES" ]; then
    echo "⚠️  警告: Vulkan SDK 环境变量未设置"
    echo "请确保已安装Vulkan SDK并设置了环境变量"
fi

# 检查pkg-config和库
if command -v pkg-config &> /dev/null; then
    if pkg-config --exists glfw3; then
        echo "✅ GLFW: $(pkg-config --modversion glfw3)"
    else
        echo "❌ 错误: 未找到GLFW，请使用 'brew install glfw' 安装"
        exit 1
    fi
else
    echo "⚠️  无法检查GLFW版本（pkg-config未安装）"
fi

# 编译着色器
echo ""
echo "🎨 编译着色器..."
if [ -x "compile_shaders.sh" ]; then
    ./compile_shaders.sh
    if [ $? -ne 0 ]; then
        echo "❌ 着色器编译失败"
        exit 1
    fi
else
    echo "⚠️  着色器编译脚本不可执行，正在修复权限..."
    chmod +x compile_shaders.sh
    ./compile_shaders.sh
fi

# 创建构建目录
echo ""
echo "🏗️  配置项目..."
mkdir -p build
cd build

# 配置CMake
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if [ $? -ne 0 ]; then
    echo "❌ CMake配置失败"
    exit 1
fi

# 构建项目
echo ""
echo "🔨 构建项目..."
cmake --build . --config Debug --parallel
if [ $? -ne 0 ]; then
    echo "❌ 构建失败"
    exit 1
fi

echo ""
echo "🎉 构建成功！"
echo ""
echo "运行选项："
echo "1. 直接运行: ./VulkanPBR"
echo "2. 在VS Code中运行: 按F5开始调试"
echo "3. 从终端运行: cd build && ./VulkanPBR"
echo ""

# 询问是否立即运行
read -p "是否立即运行程序？(y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "🎮 启动VulkanPBR..."
    ./VulkanPBR
fi