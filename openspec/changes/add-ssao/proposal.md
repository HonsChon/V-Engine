## Why

当前引擎的光照计算仅使用固定常量环境光（`ambient = color * intensity * albedo`），缺乏空间遮蔽信息，导致场景中的角落、缝隙、物体接触面等区域没有正确的明暗变化，画面整体显得"平"而缺乏深度感。SSAO（Screen Space Ambient Occlusion）是现代实时渲染引擎中最基础且性价比最高的全局光照近似技术，能够显著提升视觉真实感。引擎已具备完整的延迟渲染管线（GBuffer 包含 Position、Normal、Depth），具备实现 SSAO 的一切前提条件。

## What Changes

- **新增 SSAOPass 渲染通道**：基于 GBuffer 的深度和法线数据，采用 Deinterleaved Texturing 优化（将全分辨率纹理拆分为 4×4=16 张子纹理，分别计算 SSAO，再重组），使用半球采样 + 随机核心的方式计算每像素的环境遮蔽因子
- **新增 Deinterleave/Reinterleave Pass**：将 GBuffer 深度和法线纹理按 4×4 模式拆分为 16 张子纹理，SSAO 计算后再将 16 张 AO 子结果重组为全分辨率纹理
- **新增 SSAO Blur Pass**：对重组后的 SSAO 输出进行模糊（blur），消除采样噪点
- **新增 SSAO 相关 Shader**：`ssao_deinterleave.comp`（去交错计算着色器）、`ssao.vert`/`ssao.frag`（SSAO 计算着色器）、`ssao_reinterleave.comp`（重组计算着色器）、`ssao_blur.vert`/`ssao_blur.frag`（模糊着色器）
- **修改 LightingPass**：在延迟光照阶段采样 SSAO 纹理，将遮蔽因子应用到环境光分量上
- **修改 SceneRenderer**：在渲染管线中集成 SSAO Pass，调度其在 GBuffer 之后、Lighting 之前执行
- **新增 SSAO 参数控制**：在 RenderSettings 中添加 SSAO 开关和质量参数，支持通过 UI 面板调节

## Capabilities

### New Capabilities
- `ssao`: 屏幕空间环境遮蔽计算，包含 SSAO 采样、噪声纹理生成、模糊降噪、以及与光照管线的集成

### Modified Capabilities
<!-- 暂无已有 spec，这是项目的第一个 change -->

## Impact

- **新增文件**：
  - `src/renderer/passes/ssao/SSAOPass.h/.cpp` — SSAO 全流程渲染通道（Deinterleave → SSAO → Reinterleave → Blur）
  - `shaders/ssao/ssao_deinterleave.comp` — 去交错 compute shader
  - `shaders/ssao/ssao.vert`, `shaders/ssao/ssao.frag` — SSAO 采样计算着色器
  - `shaders/ssao/ssao_reinterleave.comp` — 重组 compute shader
  - `shaders/ssao/ssao_blur.vert`, `shaders/ssao/ssao_blur.frag` — 模糊着色器
- **修改文件**：
  - `src/renderer/SceneRenderer.h/.cpp` — 添加 SSAO Pass 实例和执行调度
  - `src/renderer/passes/RenderContext.h` — 可能需要添加 SSAO 纹理引用到上下文中
  - `src/renderer/passes/LightingPass.h/.cpp` — 增加 SSAO 纹理输入，修改光照计算
  - `shaders/deferred_lighting.frag` — 采样 SSAO 纹理并应用遮蔽
- **依赖**：无新增外部依赖，仅使用 Vulkan API 和 GLM
- **性能影响**：增加 Deinterleave + 16 次 1/16 分辨率 SSAO 计算 + Reinterleave + 全屏模糊，由于 Deinterleaved Texturing 的缓存优化，总体 GPU 时间预计 0.3-1.0ms（优于传统全分辨率方案）
