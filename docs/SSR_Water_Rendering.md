# V-Engine SSR 与水面渲染技术文档

## 目录
1. [概述](#概述)
2. [架构设计](#架构设计)
3. [SSR 算法实现（当前版本）](#ssr-算法实现当前版本)
4. [水面渲染](#水面渲染)
5. [SSR 局限性（重要）](#ssr-局限性重要)
6. [调试记录与经验教训](#调试记录与经验教训)
7. [业界实践与后续方向](#业界实践与后续方向)
8. [常见问题](#常见问题)

---

## 概述

本项目实现了基于**屏幕空间反射（Screen Space Reflection, SSR）**的水面渲染。SSR 只利用当前帧的深度/颜色缓冲（屏幕上已有的信息）做反射射线求交，因此**只能反射屏幕内且未被遮挡的几何**——这一根本约束带来一系列固有局限（见[SSR 局限性](#ssr-局限性重要)），其中部分伪影无法通过算法调参消除。

引擎中存在两条独立的 SSR 路径：

| 路径 | 位置 | 状态 |
|---|---|---|
| **WaterPass 内置 SSR** | `water.frag` 逐水面像素步进 | 实际生效，本文重点 |
| **SSRPass 全屏反射** | `ssr.frag` 离屏 pass | 每帧执行，输出纹理目前未被下游消费（遗留） |

两者共享同一套步进算法实现（`rayMarchScreenSpace` / `binarySearchScreen` / `processHit`）。

---

## 架构设计

### 文件结构
```
src/renderer/passes/
├── WaterPass.h/.cpp        # 水面渲染（内置逐像素 SSR）
├── SSRPass.h/.cpp          # 全屏 SSR pass（离屏 R16G16B16A16）
├── GBufferPass.h/.cpp      # G-Buffer 生成（SSR 的输入）
└── ...

shaders/
├── water.vert/.frag        # 水面着色器（SSR 步进在 frag 内）
├── ssr.vert/.frag          # 全屏 SSR 着色器（同款算法）
└── ...
```

### 延迟模式管线位置

```
GBuffer(不透明+Mask) → Blit Albedo→SceneColor → SSAO → SSRPass(全屏)
→ 合成[LightingPass → TransparentPass → WaterPass(内置SSR, SrcAlpha混合)]
→ 离屏目标 → FXAAPass → UI → Swapchain
```

### WaterPass 输入绑定（Pure RHI）

```
Binding 0: WaterUBO            # 变换矩阵/水色/波浪/屏幕/SSR参数
Binding 1: gPosition           # G-Buffer 世界位置
Binding 2: gNormal             # G-Buffer 法线
Binding 3: gDepth              # G-Buffer 深度（NDC 非线性）
Binding 4: sceneColor          # 场景颜色（GBuffer Albedo blit，未光照）
```

注意：水面不写深度（`depthTest(false,false)`），gDepth 在水面像素处保存的是
水面**背后**不透明几何的深度。

---

## SSR 算法实现（当前版本）

### 步进框架

射线在**屏幕空间 (UV + NDC depth)** 均匀步进（保持透视正确），深度比较时
转换为**线性深度**（使用世界单位的厚度阈值）：

```glsl
vec4 rayMarchScreenSpace(vec3 rayOrigin, vec3 rayDir) {
    vec3 startScreen = worldToScreen(rayOrigin);
    vec3 endScreen   = worldToScreen(rayOrigin + rayDir * maxDistance);
    // （近/远平面裁剪 endScreen ...）

    vec3 screenDelta = endScreen - startScreen;
    float numSteps = min(maxSteps, length(screenDelta.xy) * screenWidth);
    numSteps = max(numSteps, 32.0);
    float jitter = hash(gl_FragCoord.xy);          // 打破规律条纹
    vec3 stepScreen = screenDelta / numSteps;
    vec3 currentScreen = startScreen + stepScreen * jitter;
    vec3 prevScreen = startScreen;

    // 上一步深度差与场景深度（连续性守卫基准）
    float prevSampledLinearDepth = linearizeDepth(texture(gDepth, startScreen.xy).r);
    float prevDelta = linearizeDepth(startScreen.z) - prevSampledLinearDepth;

    for (int i = 0; i < int(numSteps); i++) {
        // 边界检查（出屏即停）
        float sampledNDCDepth = texture(gDepth, currentScreen.xy).r;
        float deltaDepth = linearizeDepth(currentScreen.z) - linearizeDepth(sampledNDCDepth);

        // ① 正常命中：步进落在厚度窗口内
        if (deltaDepth > 0.0 && deltaDepth < thickness) { ...二分细化+命中... }
        // ② 穿越检测：一步从表面前方跳到窗口之外（tunneling）
        //    + 深度连续性守卫：相邻采样点场景深度连续（排除轮廓断层）
        else if (deltaDepth >= thickness && prevDelta <= 0.0 &&
                 abs(sampledLinearDepth - prevSampledLinearDepth) < 0.5) { ...二分细化+命中... }

        prevDelta = deltaDepth;
        prevSampledLinearDepth = sampledLinearDepth;
        prevScreen = currentScreen;
        currentScreen += stepScreen;
    }
    return vec4(0.0);   // 未命中 → 天空色回退
}
```

### 为什么需要"穿越检测"（tunneling）

屏幕空间约 1px/步的均匀步进，其**线性深度增量随距离急剧增大**（线性化导数
dL/dd ∝ 1/(f − d(f−n))²）：

| 射线打到距离 | 每步线性深度增量 | 厚度窗口(0.05) |
|---|---|---|
| ~5 单位 | ~0.01 | 覆盖 ✓ |
| ~15 单位 | ~0.04 | 边缘 |
| ~25 单位 | ~0.13 | **穿透** |

增量超过厚度窗口时，射线一步从表面前方跳到窗口之外，命中判定失效 →
未命中回退。穿越检测在 prev→current 之间二分细化找回交面。

### 为什么需要"深度连续性守卫"（轮廓歧义）

无守卫时，**从物体后方掠过**的射线与**真实穿越**在单步深度比较上完全同构：

```
真实穿越:   prev(表面A前方) → current(表面A后方)     delta 翻正 ✓ 应命中
后方掠过:   prev(背景前方)   → current(物体A后方)     delta 同样翻正 ✗ 误命中
```

误命中会把物体颜色采样到大片水面 → **倒影拉长成条纹**。守卫用"相邻采样点
的场景深度是否连续"区分两者（真实穿越发生在连续表面上，相邻像素深度差
< 0.1；轮廓断层为米级跳变）。

### 命中处理（processHit）

```glsl
vec4 processHit(vec2 hitUV, vec3 rayDir, float stepIndex, float numSteps) {
    vec3 hitNormal = texture(gNormal, hitUV).rgb;
    if (dot(hitNormal, rayDir) > 0.0) return vec4(0,0,0,-1);  // 背面剔除
    float edgeFade = pow(clamp(1 - max(|u-.5|,|v-.5|)*2, 0, 1), 2);  // 屏幕边缘淡出
    float distanceFade = 1 - stepIndex / numSteps;
    return vec4(texture(sceneColor, hitUV).rgb, edgeFade * distanceFade);
}
```

### 当前参数

| 参数 | WaterPass（水面） | SSRPass（全屏） | 说明 |
|---|---|---|---|
| maxDistance | 30.0 | 50.0 | 世界单位最大步进距离 |
| maxSteps | 2048 | 64 | 上限（实际≈屏幕像素距离） |
| thickness | 0.05 | 0.05 | 线性深度空间厚度窗口（世界单位） |

> 厚度不宜小于厘米级以下：穿透由穿越检测兜底后，更小的窗口只会徒增
> 对二分细化的依赖；更大的窗口（>0.15）会引入"擦着背面掠过"的误命中。

---

## 水面渲染

水面片元着色流程（`water.frag` main）：

1. 波浪法线扰动（sin/cos 叠加，waveStrength=0.02）
2. `reflectDir = reflect(-viewDir, perturbedNormal)`
3. **内置 SSR 步进**（上文算法）→ 反射颜色 + 衰减
4. 未命中 → 天空色回退（`mix(vec3(0.5,0.7,1.0), vec3(0.2,0.4,0.8), ...)`）
5. Fresnel（F0=0.1，艺术化增强）混合反射与水体色
6. 高光（Blinn-Phong 256 次幂）
7. 深度遮挡软化（世界高度 + 深度比较的 edgeSoftness，处理水面与物体交界）

折射采样当前被禁用（`underwaterColor = waterBaseColor`，调试图留）。

---

## SSR 局限性（重要）

以下局限源自 SSR 的根本约束——**只有当前帧屏幕上的深度/颜色可用**。
其中 ⚠ 标记的是无法通过调参/启发式消除的固有局限：

### ⚠ 无法反射被遮挡的几何

深度/颜色缓冲只保存每个像素**最前表面**。反射射线打到被前景物体遮挡的
区域（如球体背后的墙面）时，深度缓冲里是球——射线"看不到"墙。

**表现**：球体倒影上方的墙倒影区域出现点状蓝色回退（jitter 使相位随机
→ 点斑）。这是本引擎实际踩到的核心问题：多次算法迭代后确认该区域
的反射本质上不可得，只能回退。

**业界对策**：cubemap/反射探针兜底、或平面反射（见后续方向）。

### ⚠ 无法反射屏幕外几何

射线步进出屏幕即停止 → 屏幕边缘的反射缺失。当前用 edgeFade 淡出掩盖。

### ⚠ 轮廓歧义（守卫的两难）

"真实穿越"与"从物体后方掠过轮廓"在单深度样本下同构，只能靠启发式区分。
当前守卫（采样深度连续性）的取舍：

- 接受：连续表面上的真实穿越 ✓
- 拒绝：轮廓断层处的后方掠过 ✓（否则倒影拉长）
- **误拒**：紧贴前景物体轮廓出口的真实穿越（prev 采样落在前景上）→
  该处倒影缺失一小片

三种情况在局部深度信息下不可兼得，是屏幕空间方法的原理性歧义。
曾尝试的替代判据均失败（见[调试记录](#调试记录与经验教训)）。

### 逐像素 jitter → 点状噪声

步进相位随机化用于打破条纹，但使命中/未命中在边缘判定区逐像素翻转，
产生点斑。**业界用 TAA/时域滤波累积掩盖**，本引擎暂无 TAA。

### 半透明物体不参与反射

GBuffer 只含不透明+Mask 几何；Blend 半透明物体（玻璃球等）不在反射中。

### 其他

- 单次反弹（反射中无二次反射）
- 无粗糙度模糊（镜面反射，无 SSR blur）
- sceneColor 是 Albedo blit（未光照）→ 反射亮度与最终画面不完全一致

---

## 调试记录与经验教训

2026/09 水面倒影伪影的四轮迭代（commit 已合并为 `e0a5151`），
记录于此避免重复踩坑：

| 轮次 | 改动 | 结果 | 根因 |
|---|---|---|---|
| 0 | 初始（厚度 0.03/0.01） | 远处倒影点状蓝色回退 | **穿透**：远处每步线性深度增量(0.04~0.13) > 厚度窗口 |
| 1 | 穿越检测（无守卫） | 点斑消失，但**倒影拉长成条纹** | 轮廓断层与真实穿越同构 → 从物体后方掠过被误命中，物体颜色涂到大片水面 |
| 2 | + 采样深度连续性守卫 + 厚度 0.05 | 拉长消失，**球上方点斑回归** | 守卫误拒紧贴轮廓的真实穿越（prev 采样落在前景物体上） |
| 3 | 射线历史判据（prevScreen.z < 当前表面深度） | **整片点斑，更糟** | 判据跨区域跃迁无意义：水面射线天然"在背景之前"，进入任何物体区域都判"穿越" |
| 4 | 步长自适应窗口（无状态） | 球上方点斑依旧 | 该区域反射**被球遮挡**（固有局限），任何判据都救不了 |
| 5 | 回退到轮次 2（当前版本） | 拉长已修、点斑为固有局限 | — |

**核心教训**：
1. 先区分**算法 bug**（穿透、轮廓误命中）与**方法固有局限**（遮挡反射不可得），
   后者继续调参是死路
2. 步对启发式（比较 prev/current 两步的深度关系）在轮廓歧义下不可能完备，
   每种判据都在"误命中"与"误拒绝"之间换边
3. 点状噪声 = jitter 暴露的边缘判定翻转，需要 TAA 类时域手段掩盖
4. **平面反射**从机制上消除全部此类问题（见下节）

---

## 业界实践与后续方向

### 平面反射（Planar Reflection）——平坦水面的标准解法

把相机关于水面镜像，用镜像相机把场景渲染到一张（通常半分辨率）反射 RT，
水面片元投影采样。**精确**：遮挡/屏幕外/薄物体全部正确，无步进无判据。

实现要点：镜像 view 矩阵（`view * reflectMatrix(y=waterHeight)`）、
翻转绕序（cull Front）、斜近裁剪面（剔除水下几何）、投影纹理采样 + 波纹扰动。
本引擎已规划此方案（ReflectionPass + 半分辨率 RT + water.frag 投影采样）。

### SSR + cubemap/反射探针 + TAA（通用组合）

SSR 有效区域用 SSR，失效处回退 cubemap（天空/环境），TAA 累积掩盖抖动。
适合通用湿润表面/粗糙反射；**解决不了"该是墙却是天空"的内容错误**。

### 其他

- Hi-Z 层次深度追踪（性能与精度更好，仍是屏幕空间局限）
- 时域复用 + 降噪（SSAO/SSR 通用）

---

## 常见问题

### Q1: 倒影出现点状蓝色回退
先确认是否为**被遮挡几何**区域（固有局限，见局限性章节）；若是开阔区域，
检查厚度窗口是否小于该距离的每步线性深度增量（穿越检测应已兜底）。

### Q2: 倒影拉长成条纹
轮廓断层误命中——确认连续性守卫（`CROSSING_DEPTH_TOLERANCE`）未被移除，
厚度未调得过大（>0.15 会引入擦背掠过误命中）。

### Q3: 反射过暗/过亮
sceneColor 是 **Albedo blit（未光照）**——反射亮度不随场景光照变化；
如需一致需改为采样光照后颜色（需要改合成顺序）。

### Q4: 性能
WaterPass 步数上限 2048（实际≈屏幕像素距离）；降低 `setSSRMaxSteps`
或水面网格分辨率。半分辨率 RT 是平面反射方案的标准省法。

---

*文档版本：2.0*
*最后更新：2026/09/14（SSR 穿越检测+连续性守卫定稿；记录固有局限与四轮调试教训）*
