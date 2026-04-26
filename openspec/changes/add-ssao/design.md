## Context

V-Engine 是一个基于 Vulkan 的实时渲染引擎，已实现延迟渲染管线。当前渲染流程为：
**GBuffer Pass → Lighting Pass → SSR Pass → Forward Pass → Water Pass → Debug Pass**

GBuffer 已输出：
- Position (R16G16B16A16_SFLOAT) — 世界空间位置
- Normal (R16G16B16A16_SFLOAT) — 世界空间法线
- Albedo (R8G8B8A8_UNORM) — 反照率 + 金属度
- Depth (D32_SFLOAT) — 深度缓冲

当前 LightingPass 使用 `ambient = ambientColor * ambientIntensity * albedo` 作为环境光项，没有任何空间遮蔽。所有渲染通道继承 `RenderPassBase`，通过 `SceneRenderer` 统一调度。引擎使用 double buffering（MAX_FRAMES_IN_FLIGHT = 2）。

## Goals / Non-Goals

**Goals:**
- 实现基于 John Chapman 风格的 SSAO 算法（法线导向半球采样 + 随机旋转核心）
- 采用 Deinterleaved Texturing 优化纹理缓存命中率，提升 SSAO 计算性能
- SSAO 计算后进行模糊以消除噪点
- 将 SSAO 遮蔽因子集成到 LightingPass 的环境光计算中
- 提供运行时参数调节（开关、采样半径、采样数、强度、偏移量）
- 支持窗口 resize 时自动重建 SSAO 资源

**Non-Goals:**
- 不实现 HBAO+、GTAO 等更高级的 AO 算法（可作为后续升级）
- 不做 temporal filtering（时间域滤波，可后续添加）
- 不修改 Forward Pass 的 AO 处理（仅影响延迟管线）

## Decisions

### Decision 1: SSAO 算法选择 — 法线导向半球采样 (Normal-Oriented Hemisphere)

**选择**：采用经典的法线导向半球随机核心采样法（基于 John Chapman / LearnOpenGL 的方案）。

**替代方案**：
- Crytek 原始 SSAO：在完整球体中采样，会产生"内部"遮蔽伪影
- HBAO：基于水平视角的遮蔽，质量更高但实现更复杂
- GTAO：积分 AO，最接近 ground truth，但计算量大

**理由**：法线导向半球采样在质量和复杂度之间取得良好平衡，且教学资料丰富，适合引擎首个 AO 实现。后续可替换为 GTAO。

### Decision 2: Deinterleaved Texturing 纹理缓存优化

**选择**：采用 NVIDIA HBAO+ 中提出的 Deinterleaved Texturing 技术。整个 SSAO 流程分为 4 个阶段：
1. **Deinterleave**：使用 compute shader 将全分辨率的 GBuffer Position 和 Normal 按 4×4 模式拆分为 16 张子纹理（每张为 `width/4 × height/4`），存储在 texture array 的 16 个 layer 中
2. **SSAO 计算**：对 16 张子纹理分别执行 SSAO 采样，每张子纹理内相邻像素在原始图像中间隔 4 个像素，因此采样核心完全一致，纹理缓存命中率极高。使用 instanced draw 或 layer rendering，一次 draw call 渲染 16 个 layer
3. **Reinterleave**：使用 compute shader 将 16 张 AO 子结果重组为全分辨率的 AO 纹理
4. **Blur**：对全分辨率 AO 纹理执行模糊

**替代方案**：
- 直接全分辨率采样：实现简单，但纹理 cache miss 严重，采样模式随机导致性能差
- 半分辨率 SSAO：降低分辨率提升性能，但会丢失细节

**理由**：Deinterleaved Texturing 的核心优势是每张子纹理内所有像素使用完全相同的采样偏移（因为噪声纹理 4×4 正好对齐），GPU 纹理单元能充分预取缓存行，带宽利用率大幅提升。在 1080p 下实测可比传统方案快 2-3 倍。

### Decision 3: Deinterleave/Reinterleave 使用 Compute Shader，SSAO 计算使用 Fragment Shader

**选择**：
- Deinterleave 和 Reinterleave 使用 **compute shader**：这两个阶段是简单的纹理数据搬运，compute shader 更灵活且无需 render pass/framebuffer 开销
- SSAO 核心计算和 Blur 使用 **fragment shader + 全屏三角形**：与引擎现有 Pass 架构一致

**替代方案**：
- 全部使用 fragment shader：deinterleave 需要 MRT 或多次绘制，不如 compute 直接
- 全部使用 compute shader：SSAO 核心计算和 blur 用 compute 也可以，但偏离引擎现有模式

**理由**：混合方案各取所长。Compute shader 适合简单的数据搬运/重排列，fragment shader 适合有依赖纹理采样的光照计算。

### Decision 4: 将所有 SSAO 阶段整合到同一个 SSAOPass 类中

**选择**：`SSAOPass` 类内部管理 4 个子阶段（Deinterleave → SSAO 计算 → Reinterleave → Blur），对外提供统一接口。

**替代方案**：
- 拆分为 4 个独立 Pass 类

**理由**：这 4 个阶段紧密耦合且资源共享（deinterleaved 纹理数组在 SSAO 和 reinterleave 间传递），合并管理避免资源生命周期复杂化。SSAOPass 对外只暴露最终模糊后的 AO 纹理。

### Decision 5: 子纹理存储方式 — Texture2DArray

**选择**：使用 `VkImage` 的 `arrayLayers = 16` 创建 texture array 来存储 16 张去交错后的子纹理。SSAO 计算通过 `gl_Layer`（geometry shader 或 multiview）或 16 次 draw 指定目标 layer。

**替代方案**：
- 16 个独立 VkImage：管理复杂，描述符集膨胀
- 一张大图按区域分割：UV 计算容易出错

**理由**：Texture array 在 Vulkan 中是原生支持的，16 个 layer 共享同一个描述符 binding，简洁高效。SSAO fragment shader 通过 push constant 传入当前 layer index 和对应的采样偏移。

### Decision 6: 采样核心和噪声纹理的生成方式

**选择**：
- **采样核心**：CPU 端生成 64 个半球内随机采样点，使用加速插值使采样点集中在原点附近，存入 UBO
- **噪声纹理**：在 Deinterleaved Texturing 模式下，4×4 噪声纹理不再用于旋转（因为每张子纹理对应固定的偏移），而是直接在 CPU 端为 16 张子纹理各分配一个固定的随机旋转角度，通过 push constant 或 UBO 传入

**理由**：Deinterleaved 模式下每张子纹理中的像素在原始图中间隔 4 像素，天然对应 4×4 噪声纹理中的一个固定元素。因此不需要在 shader 中动态采样噪声纹理，直接用常量旋转角度即可，进一步节省带宽。

### Decision 7: AO 纹理格式

**选择**：
- 去交错后的子纹理：使用 `VK_FORMAT_R8_UNORM`（AO 输出 array，16 层）
- 重组后的全分辨率 AO 纹理：使用 `VK_FORMAT_R8_UNORM`
- 模糊后的最终 AO 纹理：使用 `VK_FORMAT_R8_UNORM`

**替代方案**：
- R16_SFLOAT：精度更高但内存翻倍
- R32_SFLOAT：过度精度

**理由**：AO 值范围 [0,1]，8 位精度在模糊后完全足够，节省带宽和显存。Deinterleaved 子纹理也只需 8 位。

### Decision 8: 集成到 LightingPass 的方式

**选择**：在 LightingPass 的描述符集中新增一个 binding（binding 4）来接收最终模糊后的 SSAO 纹理。在 `deferred_lighting.frag` 中采样并乘到环境光项上：`ambient *= ssaoValue`。

**理由**：最小化对现有 LightingPass 的侵入。只增加一个纹理 binding 和一行乘法。LightingPass 不感知 deinterleaved texturing 的内部实现。

## Risks / Trade-offs

- **[屏幕空间限制]** SSAO 只能基于屏幕可见信息计算遮蔽，物体边缘和屏幕外区域会产生伪影 → 通过模糊和合理的采样半径缓解；后续可添加 temporal filtering
- **[Deinterleave 额外显存]** 16 层 texture array（Position + Normal + AO 输出）增加显存占用 → 子纹理为 1/16 分辨率，总量约等于 1 张全分辨率纹理，开销可控
- **[Deinterleave 边界处理]** 子纹理边缘的采样可能越界 → 使用 clamp-to-edge 采样模式，SSAO shader 中检查采样坐标有效性
- **[Reinterleave 接缝]** 重组时相邻子纹理的 AO 值可能不连续 → 后续 blur pass 能有效消除接缝
- **[实现复杂度增加]** 4 个子阶段比 2 个子阶段复杂 → 全部封装在 SSAOPass 内部，对外接口不变
- **[GBuffer 依赖]** SSAO 强依赖 GBuffer 的 Position 和 Normal 纹理 → 当 GBuffer Pass 未启用时自动跳过 SSAO
- **[深度重建 vs Position 纹理]** 当前直接使用 Position 纹理（deinterleave 后的），未来可优化为从深度重建位置以节省带宽 → 列为后续优化项
