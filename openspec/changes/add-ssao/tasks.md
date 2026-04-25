## 1. SSAO Shader 编写

- [ ] 1.1 创建 `shaders/ssao/ssao_deinterleave.comp` — Deinterleave compute shader，将全分辨率 GBuffer Position/Normal 按 4×4 模式拆分写入 16 层 texture array
- [ ] 1.2 创建 `shaders/ssao/ssao.vert` — 全屏三角形顶点着色器，输出纹理坐标
- [ ] 1.3 创建 `shaders/ssao/ssao.frag` — SSAO 采样计算片段着色器（读取 deinterleaved Position/Normal 子纹理，使用 64 个采样核心 + push constant 传入的层旋转角度，计算遮蔽因子）
- [ ] 1.4 创建 `shaders/ssao/ssao_reinterleave.comp` — Reinterleave compute shader，将 16 层 AO texture array 重组为全分辨率 AO 纹理
- [ ] 1.5 创建 `shaders/ssao/ssao_blur.vert` — 模糊通道顶点着色器（复用全屏三角形）
- [ ] 1.6 创建 `shaders/ssao/ssao_blur.frag` — 4×4 box blur 片段着色器
- [ ] 1.7 编译所有 SSAO shader 为 SPIR-V（更新 `compile_shaders.sh`）

## 2. SSAOPass 资源创建

- [ ] 2.1 创建 `src/renderer/passes/ssao/SSAOPass.h` — SSAOPass 类声明，包含 4 个子阶段（Deinterleave → SSAO → Reinterleave → Blur）的资源管理
- [ ] 2.2 创建 `src/renderer/passes/ssao/SSAOPass.cpp` — 实现构造函数、析构函数和 cleanup
- [ ] 2.3 实现 deinterleaved texture array 创建 — Position array（R16G16B16A16_SFLOAT, 16层, width/4 × height/4）和 Normal array（同规格）
- [ ] 2.4 实现 AO texture array 创建（R8_UNORM, 16层, width/4 × height/4）— 存储 16 层 SSAO 输出
- [ ] 2.5 实现全分辨率 AO 纹理创建（R8_UNORM, width × height）— Reinterleave 输出
- [ ] 2.6 实现模糊后 AO 纹理创建（R8_UNORM, width × height）— 最终输出
- [ ] 2.7 实现 SSAO render pass 创建（用于 SSAO 计算，color attachment 为 AO array 的单层）
- [ ] 2.8 实现 blur render pass 和 framebuffer 创建
- [ ] 2.9 实现 16 个 framebuffer 创建（每个 framebuffer 绑定 AO texture array 的一个 layer view）

## 3. SSAOPass 采样核心与旋转角度

- [ ] 3.1 实现采样核心生成（64 个半球内随机点，加速插值分布）
- [ ] 3.2 实现 16 层旋转角度生成（每层一个 [0, 2π) 随机角度）
- [ ] 3.3 实现 Uniform Buffer 创建和更新（采样核心数组 + SSAO 参数：radius, bias, power）

## 4. SSAOPass Pipeline 和描述符集

- [ ] 4.1 实现 Deinterleave compute pipeline 创建（描述符集：输入 GBuffer Position/Normal + 输出 texture array）
- [ ] 4.2 实现 Deinterleave 描述符集布局和描述符集创建
- [ ] 4.3 实现 SSAO 计算 graphics pipeline 创建（加载 ssao.vert/ssao.frag）
- [ ] 4.4 实现 SSAO 描述符集布局和描述符集创建（UBO + deinterleaved Position/Normal array 输入）
- [ ] 4.5 实现 Reinterleave compute pipeline 创建（描述符集：输入 AO array + 输出全分辨率 AO 纹理）
- [ ] 4.6 实现 Reinterleave 描述符集布局和描述符集创建
- [ ] 4.7 实现 Blur graphics pipeline 创建（加载 ssao_blur.vert/ssao_blur.frag）
- [ ] 4.8 实现 Blur 描述符集布局和描述符集创建（全分辨率 AO 纹理输入）
- [ ] 4.9 实现全屏三角形顶点缓冲创建（SSAO 计算和 Blur 共用）

## 5. SSAOPass 执行逻辑

- [ ] 5.1 实现 `execute()` 方法 — 编排 4 个子阶段的执行顺序和 image barrier
- [ ] 5.2 实现 Deinterleave 阶段 — dispatch compute shader，添加 barrier 等待写入完成
- [ ] 5.3 实现 SSAO 计算阶段 — 循环 16 层，每层绑定对应的 framebuffer 和 push constant（layer index + 旋转角度），绘制全屏三角形
- [ ] 5.4 实现 Reinterleave 阶段 — dispatch compute shader，添加 barrier 等待写入完成
- [ ] 5.5 实现 Blur 阶段 — 绑定 blur framebuffer 和全分辨率 AO 纹理，绘制全屏三角形
- [ ] 5.6 实现各阶段之间的 VkImageMemoryBarrier（确保 compute→graphics、graphics→compute 的正确同步）
- [ ] 5.7 实现 `resize()` 方法 — 销毁并重建所有分辨率相关资源（texture array、framebuffer、描述符集）

## 6. LightingPass 修改

- [ ] 6.1 修改 `LightingPass.h` — 添加 `setSSAOTexture()` 方法和 SSAO 相关成员变量
- [ ] 6.2 修改 `LightingPass.cpp` — 更新描述符集布局，增加 binding 4 用于 SSAO 纹理
- [ ] 6.3 修改 `LightingPass.cpp` — 更新描述符集写入，绑定模糊后的 SSAO 纹理到 binding 4
- [ ] 6.4 修改 `shaders/deferred_lighting.frag` — 添加 SSAO 采样器（binding 4），将 AO 值乘以环境光：`ambient *= ssaoValue`

## 7. SceneRenderer 集成

- [ ] 7.1 修改 `SceneRenderer.h` — 添加 `SSAOPass` 成员和 `executeSSAOPass()` 方法声明
- [ ] 7.2 修改 `SceneRenderer.cpp` — 在 `createPasses()` 中创建 SSAOPass 实例
- [ ] 7.3 修改 `SceneRenderer.cpp` — 实现 `executeSSAOPass()`，设置 GBuffer 输入并调用 SSAOPass::execute()
- [ ] 7.4 修改 `SceneRenderer.cpp` — 在 `render()` 中将 SSAO 插入 GBuffer 之后、Lighting 之前
- [ ] 7.5 修改 `SceneRenderer.cpp` — 在 SSAO 执行后将模糊后 AO 纹理传递给 LightingPass
- [ ] 7.6 修改 `SceneRenderer.cpp` — 在 `onResize()` 中调用 SSAOPass 的 resize
- [ ] 7.7 修改 `SceneRenderer.cpp` — 在 `destroyPasses()` 中清理 SSAOPass

## 8. RenderSettings 参数扩展

- [ ] 8.1 修改 `SceneRenderer.h` 中的 `RenderSettings` — 添加 `enableSSAO`、`ssaoRadius`、`ssaoPower`、`ssaoBias` 字段及默认值
- [ ] 8.2 在 `executeSSAOPass()` 中读取 RenderSettings 参数传递给 SSAOPass 的 UBO

## 9. 构建系统更新

- [ ] 9.1 修改 `CMakeLists.txt` — 将 `src/renderer/passes/ssao/SSAOPass.cpp` 添加到编译源文件列表
- [ ] 9.2 更新 `compile_shaders.sh` — 添加 `shaders/ssao/` 目录下所有 shader 的编译命令（ssao_deinterleave.comp、ssao.vert/frag、ssao_reinterleave.comp、ssao_blur.vert/frag）
