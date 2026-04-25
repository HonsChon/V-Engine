## ADDED Requirements

### Requirement: Deinterleaved Texturing 去交错
系统 SHALL 在 SSAO 计算前，使用 compute shader 将全分辨率的 GBuffer Position 和 Normal 纹理按 4×4 模式拆分为 16 张子纹理。子纹理 SHALL 存储在 `arrayLayers = 16` 的 Texture2DArray 中，每张子纹理的分辨率为 `ceil(width/4) × ceil(height/4)`。拆分模式为：layer `i` (i=0..15) 对应原始纹理中坐标 `(x*4 + i%4, y*4 + i/4)` 的像素。

#### Scenario: 去交错执行
- **WHEN** GBuffer Pass 已完成且 SSAO 功能开启
- **THEN** 系统执行 compute shader 将 Position 和 Normal 纹理拆分为 16 层的 texture array

#### Scenario: 子纹理内容正确性
- **WHEN** 去交错完成后
- **THEN** texture array 的 layer `i` 中坐标 `(x, y)` 的值 SHALL 等于原始纹理中坐标 `(x*4 + i%4, y*4 + i/4)` 的值

### Requirement: SSAO 采样计算
系统 SHALL 对去交错后的 16 张子纹理分别执行基于法线导向半球的 SSAO 采样计算。每张子纹理使用相同的 64 个采样核心，但使用各自固定的随机旋转角度（对应 4×4 噪声模式中的一个元素）。计算结果输出到 R8_UNORM 格式的 AO texture array（16 层）中。AO 值范围为 [0, 1]，其中 0 表示完全遮蔽，1 表示无遮蔽。

#### Scenario: 分层 SSAO 计算
- **WHEN** 去交错完成后
- **THEN** SSAOPass 对 16 层子纹理分别执行 SSAO 采样，每层使用固定旋转角度和相同采样核心，输出遮蔽因子到 AO texture array 对应层

#### Scenario: 角落和缝隙区域产生更强遮蔽
- **WHEN** 像素位于几何体的角落、缝隙或物体接触面附近
- **THEN** 该像素的 AO 值 SHALL 低于开阔区域的 AO 值，反映出环境遮蔽效果

#### Scenario: 开阔平面区域无遮蔽
- **WHEN** 像素位于开阔的平面区域，周围无遮挡几何体
- **THEN** 该像素的 AO 值 SHALL 接近 1.0（无遮蔽）

#### Scenario: 纹理缓存优势
- **WHEN** SSAO 对单张子纹理计算时
- **THEN** 该子纹理内所有像素 SHALL 使用相同的采样偏移模式（不需要动态噪声纹理采样），以最大化纹理缓存命中率

### Requirement: Reinterleaved Texturing 重组
系统 SHALL 在 SSAO 计算完成后，使用 compute shader 将 16 层 AO texture array 重组为一张全分辨率的 AO 纹理（R8_UNORM）。重组模式为去交错的逆操作：全分辨率纹理坐标 `(px, py)` 的值取自 AO array 的 layer `(py%4)*4 + (px%4)` 中坐标 `(px/4, py/4)` 的值。

#### Scenario: 重组执行
- **WHEN** 16 层 SSAO 计算全部完成
- **THEN** 系统执行 compute shader 将 AO texture array 重组为全分辨率 AO 纹理

#### Scenario: 重组结果完整性
- **WHEN** 重组完成后
- **THEN** 全分辨率 AO 纹理 SHALL 包含每个像素的遮蔽值，无遗漏

### Requirement: 采样核心生成
系统 SHALL 在初始化时在 CPU 端生成 64 个半球内的随机采样点。采样点 MUST 位于以法线方向为 z 轴的半球内，且使用加速插值（`lerp(0.1, 1.0, scale * scale)`）使采样点密度靠近原点更高。采样核心数据 SHALL 通过 Uniform Buffer 传递给 SSAO 着色器。

#### Scenario: 采样核心初始化
- **WHEN** SSAOPass 初始化时
- **THEN** 系统生成 64 个归一化且按距离加权的半球内采样向量，存入 UBO

#### Scenario: 采样分布特性
- **WHEN** 采样核心生成完成
- **THEN** 采样点 SHALL 在半球内分布，且靠近原点的采样密度高于远处

### Requirement: 每层旋转角度
系统 SHALL 在初始化时为 16 张子纹理各生成一个固定的随机旋转角度，用于在 SSAO 计算中旋转采样核心以打破层间规律性。旋转角度 SHALL 通过 push constant 或 UBO 传递给 SSAO 着色器，无需动态噪声纹理采样。

#### Scenario: 旋转角度初始化
- **WHEN** SSAOPass 初始化时
- **THEN** 系统为 16 层各生成一个 [0, 2π) 范围内的随机旋转角度

#### Scenario: 旋转角度应用
- **WHEN** SSAO 着色器为某一层执行采样时
- **THEN** 采样核心 SHALL 使用该层对应的固定旋转角度进行旋转

### Requirement: SSAO 模糊降噪
系统 SHALL 对重组后的全分辨率 AO 纹理执行模糊处理以消除采样噪点和 reinterleave 接缝。模糊 MUST 使用 4×4 的简单均值模糊（box blur），在单独的渲染通道中执行，输出到独立的模糊后 AO 纹理。

#### Scenario: 模糊处理执行
- **WHEN** Reinterleave 完成后
- **THEN** 系统对全分辨率 AO 纹理执行 4×4 box blur，输出到模糊后的 AO 纹理

#### Scenario: 模糊结果质量
- **WHEN** 模糊处理完成
- **THEN** 输出纹理 SHALL 不存在明显的采样噪点或 reinterleave 接缝，同时保持合理的 AO 细节

### Requirement: LightingPass 集成
LightingPass SHALL 在延迟光照计算中采样 SSAO 模糊后的纹理，将 AO 值乘以环境光分量。当 SSAO 未启用或纹理不可用时，SHALL 使用默认值 1.0（无遮蔽）。

#### Scenario: 环境光应用 AO
- **WHEN** LightingPass 执行光照计算，且 SSAO 纹理可用
- **THEN** 环境光项 SHALL 乘以对应像素的 SSAO 值：`ambient *= ssaoValue`

#### Scenario: SSAO 未启用时的回退
- **WHEN** SSAO 功能关闭或 SSAO 纹理不可用
- **THEN** LightingPass SHALL 使用 AO = 1.0，光照结果与 SSAO 添加之前一致

### Requirement: 渲染管线集成
SceneRenderer SHALL 将 SSAOPass 集成到渲染管线中，执行顺序为 GBuffer Pass 之后、Lighting Pass 之前。SSAOPass MUST 在 GBuffer 完成后才能执行。

#### Scenario: 正常渲染流程
- **WHEN** 执行一帧渲染，SSAO 开启
- **THEN** 渲染顺序 SHALL 为：GBuffer → Deinterleave → SSAO(×16层) → Reinterleave → Blur → Lighting → 后续 Pass

#### Scenario: SSAO 关闭时跳过
- **WHEN** RenderSettings 中 SSAO 关闭
- **THEN** SceneRenderer SHALL 跳过 SSAOPass 的执行，直接进入 Lighting Pass

### Requirement: SSAO 参数控制
系统 SHALL 提供以下可运行时调节的 SSAO 参数：
- **开关** (`enableSSAO`): bool，默认开启
- **采样半径** (`ssaoRadius`): float，默认 0.5，控制采样范围
- **强度** (`ssaoPower`): float，默认 1.0，控制遮蔽强度
- **偏移量** (`ssaoBias`): float，默认 0.025，防止自遮蔽
这些参数 SHALL 存储在 RenderSettings 结构中，并通过 UBO 传递给着色器。

#### Scenario: 默认参数
- **WHEN** 引擎首次启动，未修改任何 SSAO 参数
- **THEN** SSAO SHALL 使用默认参数运行：radius=0.5, power=1.0, bias=0.025

#### Scenario: 运行时参数修改
- **WHEN** 用户通过 UI 修改 SSAO 参数（如增大半径）
- **THEN** SSAO 效果 SHALL 在下一帧立即反映参数变化

### Requirement: 窗口 Resize 支持
SSAOPass SHALL 支持窗口大小变化。当 resize 发生时，MUST 重建所有分辨率相关资源：deinterleaved texture array（Position、Normal、AO 输出）、全分辨率 AO 纹理、模糊后 AO 纹理以及对应的 framebuffer。子纹理尺寸 SHALL 为 `ceil(newWidth/4) × ceil(newHeight/4)`。

#### Scenario: 窗口缩放
- **WHEN** 窗口大小从 1920×1080 变为 1280×720
- **THEN** SSAOPass SHALL 销毁旧资源，以新分辨率重建：子纹理为 320×180（16 层），全分辨率 AO 为 1280×720，以及所有对应的 framebuffer 和描述符集
