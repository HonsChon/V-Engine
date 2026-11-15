# GPU 硬件光追（DXR）开发记录

> 实现：RT-GBuffer + ReSTIR DI + 1spp 路径追踪间接光 + SVGF 降噪  
> 范围：DX12 / DXR Tier 1.1+（Vulkan 后端暂未实现 `VK_KHR_ray_tracing_pipeline`）  
> 入口：`F6` 切换 `RenderMode::GPURayTrace`；Tier 不足时自动回退 Normal

## 1. 架构总览

```
src/RHI/RHIAccelerationStructure.h   BLAS/TLAS 抽象（尺寸查询 + GPU 地址）
src/RHI/RHIPipeline.h                RHIRayTracingPipelineBuilder（library/raygen/miss/hitgroup）
src/RHI/RHICommandBuffer.h           buildBLAS/buildTLAS/traceRays/bindRayTracingPipeline
src/RHI/DX12/DX12RHIAccelerationStructure.cpp   AS 资源 + prebuild info + 构建描述
src/RHI/DX12/DX12RHIPipeline.cpp                RTPSO（DXIL_LIBRARY/HIT_GROUP/根签名 subobjects）
src/renderer/raytrace/GPURayTracer.*            场景采集/AS 管理/管线/每帧调度
shaders/hlsl/raytrace/                          原生 HLSL（lib_6_3/6_5 + cs_6_5）
```

### 每帧管线（分辨率吃 `rtResolutionScale`）

| # | Pass | 方式 | 输出 |
|---|------|------|------|
| 1 | RT-GBuffer | DispatchRays | worldPos(w=深度)/normal+rough/albedo+metal/emissive+ao |
| 2 | ReSTIR 初始候选 | compute | 蓄水池（sample/wSum/M/W），目标函数 `rhoLight` |
| 3 | ReSTIR 空间复用 | compute | K 邻域 + 无偏 shift-corrected 合并（RayQuery 阴影可选） |
| 4 | ReSTIR 时间复用 | compute | `prevViewProj` 重投影 + 深度/法线验证 + GRIS 合并 |
| 5 | 着色 | DispatchRays | 直接光 `W·g·V`（firefly clamp 8.0）+ 递归 PT（NEE 2 采样、RR） |
| 6 | SVGF 方差 | compute | 时间 EMA + 首帧 3×3 空间初始化 |
| 7 | SVGF à-trous ×3 | compute | 法线 `pow(n,32)` + 深度高斯 + 方差自适应颜色权重 |
| 8 | Resolve | compute | 曝光 → ACES → gamma → RGBA8（FXAA passthrough 消费） |

### 绑定约定（与 DX12 根签名规则一致）

- `set0` b0：帧常量（**root constants**，b0 space3——见 §3 教训 1）
- `set1`：t0 TLAS / t1-t2 顶点索引（raw SRV）/ t3 材质 / t4 实例 /
  t5 灯光（structured，80B stride）/ t6 贴图数组（bindless，1024 槽）/ s7 采样器
- `set2`：各 pass 输入 t0..，输出 **u10 起**（避免同号 SRV/UAV 在绑定组
  查找中撞车）
- 几何：全场景合并为一张大 VB/IB，BLAS 引用局部切片（世界变换在 TLAS）；
  与 CPU 版 `buildFromScene`（meshPath 去重、透明跳过、后备光源）逐条对应

## 2. 调试工具

| 工具 | 用法 |
|------|------|
| `GRT_PHASES` 位掩码 | `0x01` AS / `0x02` GBuffer / `0x04` initial / `0x08` spatial / `0x10` temporal / `0x20` shade / `0x40` svgf / `0x80` resolve，默认 0xFF |
| `DX12_GBV=1` | 开启 GPU-Based Validation（慢，查资源误用） |
| 窗口截图 | 用 `PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)` 按窗口抓取——**不要截全屏**（终端/其他窗口会遮挡，曾导致整轮"画面黑"误诊） |

## 3. 三个关键教训

### 3.1 C++ float ↔ HLSL uint 位模式错位（曾致 GPU TDR 挂起）

`FrameUBO` 中 `restirCandidates / maxBounces / lightCount / frameIndex` 在
HLSL 是 `uint`，C++ 却以 `float` 写入——`32.0f` 的位模式 `0x42000000` 被
当作 uint 读出 = **1107296256**，ReSTIR 候选循环跑十亿次 → 设备挂起
（`DXGI_ERROR_DEVICE_HUNG`）。

**规则：cbuffer 字段类型必须位级一致。** 诊断路径：`clamp()`/值域防护版
不挂、`max()` 版挂 → 说明"值坏了"；再用「硬编码常量能落地 / CBV 读值
不可信」交叉验证。GBV 和 D3D12 验证层都抓不到这类"数值错误"。

### 3.2 descriptor-table CBV 读值不可靠 → 帧常量改 root constants

调试中发现通过描述符表绑定的帧 CBV 在部分驱动组合下读到未定义数据
（矩阵字段偶发可用、高位寄存器直接垃圾）。将整个 `RTFrame`（224B =
56 DWORD，含各 pass 的 step/reset/exposure）改为 **root constants**
（`addPushConstant(..., sizeof(FrameUBO))`，root sig 上限 64 DWORD 内），
此后所有 pass 数据 100% 稳定。descriptor table 只保留 SRV/UAV。

### 3.3 `updateTexture` 对多条目组不生效 → 静态变体组

ping-pong 历史（temporal/variance/à-trous）原先每帧
`group->updateTexture(0, ...)` 改写描述符，**在 6 条目以上的组上改写
不落地**（resolve 的单条目组正常）。现象：à-trous 稳定读到黑（初始绑定
的 colorFiltered 从未被写），单帧红色/直通标记可落地但真实输入恒黑。

**解决：所有 ping-pong 绑定改为「静态变体组」**——按 `histIdx` 0/1 预建
两套组（temporal、variance、à-trous step1），à-trous step2/3 与 resolve
（denoiser 开/关）也是静态组；运行时零描述符改写。

### 3.4 描述符 ring 容量

Sponza（103 实例 / 69 贴图）下现有材质组把 SAMPLER ring（512 槽）
persistent 段耗尽（`persistentEnd=511`），RT 场景组再要 1 槽即抛
`sampler descriptor ring exhausted`。已扩容：资源 ring 8192→16384、
采样器 ring 512→2048。

## 4. 与 CPU 参照实现的差异

| 项 | CPU (`src/raytrace/`) | GPU |
|----|----------------------|-----|
| 主命中 | phase A 内联 | 独立 RT-GBuffer pass（DispatchRays） |
| 阴影 | BVH occluded | RayQuery inline（Tier 1.1） |
| GI | `tracePathFrom` 循环 | closest-hit 递归（payload 携带 rng/深度/吞吐） |
| 方差 | 跨帧样本统计 `E[l²]-E[l]²` | 时间 EMA + 首帧 3×3 空间初始化 |
| 方差 3×3 预平滑 | 有 | 无（单 pass 无法读改写邻域；EMA 已平滑） |
| 纹理 | CpuTexture 双线性 + LOD0 | TextureManager + bindless + `SampleLevel(uv, 0)` |

## 5. 已知限制

- 仅 DX12；Vulkan 后端存根（需 `VK_KHR_acceleration_structure` +
  `VK_KHR_ray_tracing_pipeline` 两套基础设施）
- 无 alpha-test/透明物体参与光追（与 CPU 版一致：透明跳过、无 anyhit）
- 贴图固定 LOD 0（RT 内无导数，远处纹理有走样）
- TLAS 在场景 hash 变化时全量重建（无 refit/增量更新）
- 无 GPU 时间戳统计（DebugPanel 显示 CPU 录制耗时）

## 6. 验证清单

```
# 默认场景：F6 → 应看到与 CPU ReSTIR 版一致的球/平面场景（含阴影）
VulkanPBR.exe --autotest F6 --interval 2 --seconds 10

# Sponza：26 万三角形 / 103 BLAS / 69 bindless 贴图
VulkanPBR.exe --scene assets/scenes/sponza.vscene --autotest F6 --interval 2 --seconds 20

# 阶段二分（排查挂起/黑屏）
GRT_PHASES=0x83 VulkanPBR.exe --autotest F6 --interval 2 --seconds 8
```
