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

# ========== SSAO 着色器 ==========
echo ""
echo "编译 SSAO 着色器..."

SSAO_SHADERS=(
    "ssao/ssao_deinterleave.comp"
    "ssao/ssao.vert"
    "ssao/ssao.frag"
    "ssao/ssao_reinterleave.comp"
    "ssao/ssao_blur.vert"
    "ssao/ssao_blur.frag"
)

mkdir -p shaders/ssao

for SHADER in "${SSAO_SHADERS[@]}"; do
    NAME=$(basename "$SHADER")
    # Replace dots with underscores for output name: ssao.vert -> ssao_vert.spv
    OUT_NAME="${NAME//./_}.spv"
    echo "编译 $SHADER..."
    glslc "shaders/$SHADER" -o "shaders/ssao/$OUT_NAME"
    if [ $? -eq 0 ]; then
        echo "✓ $SHADER 编译成功 -> ssao/$OUT_NAME"
    else
        echo "✗ $SHADER 编译失败"
        exit 1
    fi
done

echo ""
echo "所有着色器编译完成！"
