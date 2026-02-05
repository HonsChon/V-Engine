#!/bin/bash

# 着色器编译脚本
# 确保已安装Vulkan SDK并设置了环境变量

echo "编译着色器..."

# 检查glslc是否可用
if ! command -v glslc &> /dev/null; then
    echo "错误: 找不到glslc编译器。请确保已安装Vulkan SDK并设置了PATH环境变量。"
    exit 1
fi

# 编译顶点着色器
echo "编译 pbr.vert..."
glslc shaders/pbr.vert -o shaders/pbr.vert.spv
if [ $? -eq 0 ]; then
    echo "✓ pbr.vert 编译成功"
else
    echo "✗ pbr.vert 编译失败"
    exit 1
fi

# 编译片段着色器
echo "编译 pbr.frag..."
glslc shaders/pbr.frag -o shaders/pbr.frag.spv
if [ $? -eq 0 ]; then
    echo "✓ pbr.frag 编译成功"
else
    echo "✗ pbr.frag 编译失败"
    exit 1
fi

echo "所有着色器编译完成！"