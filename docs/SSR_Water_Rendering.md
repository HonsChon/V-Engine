# V-Engine SSR 与水面渲染技术文档

## 目录
1. [概述](#概述)
2. [架构设计](#架构设计)
3. [平面反射（当前默认）](#平面反射当前默认)
4. [SSR 算法实现（回退模式）](#ssr-算法实现回退模式)
5. [水面渲染](#水面渲染)
6. [SSR 局限性（重要）](#ssr-局限性重要)
7. [调试记录与经验教训](#调试记录与经验教训)
8. [业界实践与后续方向](#业界实践与后续方向)
9. [常见问题](#常见问题)

---

## 概述

本项目水面反射经历两代方案：

| 方案 | 状态 | 原理 |
|---|---|---|
| **平面反射 (Planar)** | **当前默认** | 镜像相机渲染场景到半分辨率 RT，水面恒等屏幕 UV 采样 |
| **WaterPass 内置 SSR** | 可切换回退 | `water.frag` 逐水面像素屏幕空间步进 |
| **SSRPass 全屏反射** | 遗留 | `ssr.frag` 离屏 pass，输出纹理未被下游消费 |

平面反射（v1.3.0，`ReflectionPass`）从机制上消除了 SSR 的全部固有局限
（被遮挡/屏幕外几何不可反射、轮廓歧义）——镜像相机"亲眼看到"完整场景，
无需步进与判据。SSR 保留用于对比与调试（DebugPanel「Water Reflection」
开关实时切换）。

引擎中两条 SSR 路径：

| 路径 | 位置 | 状态 |
|---|---|---|
| **WaterPass 内置 SSR** | `water.frag` 逐水面像素步进 | 回退模式（`reflectionParams.x=0`） |
| **SSRPass 全屏反射** | `ssr.frag` 离屏 pass | 每帧执行，输出纹理目前未被下游消费（遗留） |

两者共享同一套步进算法实现（`rayMarchScreenSpace` / `binarySearchScreen` / `processHit`）。

---

## 架构设计

### 文件结构
```
src/renderer/passes/
├── ReflectionPass.h/.cpp    # 水面平面反射（镜像相机, 半分辨率 RT）
├── WaterPass.h/.cpp         # 水面渲染（平面反射采样 / SSR 双模式）
├── SSRPass.h/.cpp           # 全屏 SSR pass（离屏 R16G16B16A16, 遗留）
├── GBufferPass.h/.cpp       # G-Buffer 生成（SSR 的输入）
└── ...

shaders/
├── water.vert/.frag         # 水面着色器（binding 5 = 反射 RT）
├── ssr.vert/.frag           # 全屏 SSR 着色器（同款算法）
└── ...
```

### 延迟模式管线位置

```
GBuffer(不透明+Mask) → Blit Albedo→SceneColor → SSAO → SSRPass(全屏, 遗留)
→ ReflectionPass(镜像相机→半分辨率 RT)
→ 合成[LightingPass → TransparentPass → WaterPass(反射RT采样, SrcAlpha混合)]
→ 离屏目标 → FXAAPass → UI → Swapchain
```

### WaterPass 输入绑定（Pure RHI）

```
Binding 0: WaterUBO            # 变换矩阵/水色/波浪/屏幕/SSR参数/反射参数
Binding 1: gPosition           # G-Buffer 世界位置
Binding 2: gNormal             # G-Buffer 法线
Binding 3: gDepth              # G-Buffer 深度（NDC 非线性）
Binding 4: sceneColor          # 场景颜色（GBuffer Albedo blit，未光照）
Binding 5: reflectionRT        # 平面反射 RT（ReflectionPass 输出）
```

注意：水面不写深度（`depthTest(false,false)`），gDepth 在水面像素处保存的是
水面**背后**不透明几何的深度。

---

## 平面反射（当前默认）

### 原理

把相机关于水面平面（y = waterHeight）镜像，用镜像相机把场景（不透明+Mask）
渲染到**半分辨率 RT**，水面片元按屏幕坐标直接采样。

**恒等 UV 性质**是本实现的简化关键：镜像相机与主相机使用**同一投影矩阵**
（仅 view 镜像为 `view * M`）。水面上任意点 P 满足 `M·P = P`（平面不动点），
因此其 clip 空间 xy 在主画面与反射 RT 中**完全相同**——水面片元
`texture(reflectionRT, gl_FragCoord.xy / screenSize)` 即得正确倒影，
零投影计算、透视天然正确。

### 四个数学要素（`SceneRenderer::updateUniforms`）

```
1. 镜像矩阵        M = T(0,2h,0)·S(1,-1,1)        （p.y' = 2h - p.y）
   镜像 view       V' = view * M
   镜像相机位置    eye' = (ex, 2h - ey, ez)

2. 斜近裁剪投影    （SceneRenderer::makeObliqueProjection，[0,1] 约定版）
   把投影矩阵近裁剪面替换为水面平面，剔除水下几何（纯 CPU 矩阵技巧，
   零着色器改动，双后端天然一致）
   - 世界空间水面平面 C = (0,1,0,-h)（正侧=水上为可见侧）
   - 相机空间平面     C' = (V'⁻¹)ᵀ · C
   - 新第三行 r = k·C（水面精确映射到 z_clip=0），k = 1/dot(C,Q)，
     Q = P⁻¹·(±sgn,±sgn,1,1) 取 dot>0 的远角（自动兼容 Vulkan Y 翻转）
   - k 钳制上限 1.5：相机接近水面出画的仰角时 D→0、k→∞，倾斜远平面
     会把反射内容整个裁掉（表现为转动相机时倒影突然消失/闪烁）；
     水面入画时内容比理论极限 ≈1，kmax=1.5（约束 0.67）全覆盖且
     不影响水面精确剔除
   - 反射专用基础投影 far=1000（主画面 far=100 不变）：缩小远平面
     倾斜的"咬合区"（∝ h/far）
   - 约定：相机须在平面负侧（即相机在水上）；C=近平面时精确还原原矩阵

3. 绕序          镜像 det = -1 → 屏幕空间绕序翻转
   反射管线 setFrontFace(Clockwise) + setCullMode(Back)
   （主画面用 CCW 渲染外表面 → 绕序被镜像翻转后，Clockwise 才能保留
   贴水外表面；用 CCW 会剔除外表面、渲染出内表面，pbr.frag 的
   gl_FrontFacing 法线翻转会掩盖错误使内表面"看似合理"）
   ⚠ 调试史教训：绕序符号穿过多层约定（网格绕序 × GLM × Vulkan Y 翻转
   × 镜像 det），纯推导两次都差一个符号（曾误诊为根因来回翻转
     CCW/CW）——**以 RenderDoc 实测为准**。最终确认：当时"倒影呈球内部"
     的真正根因是深度约定错配（要素②），绕序自始配置正确方向为 Clockwise
   ⚠ Y 翻转（applyApiYFlip）与斜裁剪叠加顺序：先 flip 后 oblique
     （推导只依赖实际矩阵的第四行，对翻转后的矩阵同样成立）

4. 有效域          相机须在水面之上（camPos.y > h + ε）；水下时本帧
   只清 RT（天空色），水面回退天空色（斜近裁剪要求相机在负侧）
```

### ReflectionPass 结构（Pure RHI，双后端）

```
- RT: 半分辨率 R8G8B8A8_UNORM（与主画面同格式，pbr.frag 已输出
  tonemap+gamma 的 LDR）+ D32 深度，自建 RenderPass（color 终态
  ShaderReadOnly，双后端自动迁移）+ Framebuffer + Linear/Clamp 采样器
- 管线: 复用 pbr_vert/frag.spv（着色器对相机的全部认知来自 UBO 的
  view/proj/viewPos，无屏幕空间假设）→ 零新场景着色器
  UBO/Set1 布局与 ForwardPass 完全一致 → 借用 getMaterialLayout()，
  RenderSystem 每帧分配的材质描述符直接兼容
- 绘制: 遍历 renderables（跳过 transparent），Set0→Set1→push→draw
- resize: RT/FB 重建（SceneRenderer::onResize 统一接线）
```

### 斜近裁剪原理直读：为什么水面不必与近平面平行

> 常见疑问：水面是斜的、近平面平行于屏幕，二者不平行，怎么能"替换"？
> 关键认知：**近平面从来不是矩阵里一个"必须平行于屏幕的东西"，它只是
> 第三行算术的副产品。我们不是把水面"转成"近平面，而是把 z 的计算式
> 整个换掉了。**

**① 硬件裁剪器只会比大小。** 光栅化前的裁剪测试只有一条：

```
0 ≤ z_clip ≤ w_clip（[0,1] API；x/y 同理）
```

硬件不知道也不关心任何"平面"——它只对矩阵乘出的数字做比较。"近平面"
只是标准投影矩阵第三行**恰好**把 `z_clip=0` 的解集写成了一块平行于屏幕
的平面而已——这是那种打分函数的巧合性质，不是要求。

**② 投影矩阵的第三行 = 一个"打分函数"。** 标准矩阵给每个顶点打的分是
"沿视线方向的距离"（所以 0 分线天然平行屏幕）；我们把它换成
"高出水面多少"（`k·(C·p)`）——0 分线就变成了**水面本身**，朝哪都无所谓。
判卷规则（分数 ≥ 0 及格）原封不动，"在水下"与"分数 < 0"变成同一件事。

**③ 具体改动：只替换第三行的 4 个元素，其余 12 个不动。**

```
              col0      col1    col2        col3
 row0 (x)   │  f/asp │   0    │    0     │    0     │ ← 不动（画面 x）
 row1 (y)   │   0    │  −f    │    0     │    0     │ ← 不动（画面 y，Vulkan 翻转）
 row2 (z)   │   0    │   0    │   m22    │   m32    │ ← ★ 替换为 k·C ★
 row3 (w)   │   0    │   0    │   −1     │    0     │ ← 不动（透视除法）

 row2_旧 = ( 0,      0,      m22,        m32      )  → z_clip = 视线深度函数
 row2_新 = ( k·C.x,  k·C.y,  k·C.z,      k·C.w    )  → z_clip = k·(C·p)
```

代码（`SceneRenderer::makeObliqueProjection`）：

```cpp
float k = glm::min(1.0f / D, 1.5f);
glm::vec4 r = clipPlane * k;   // 新第三行 = k·C
m[0][2] = r.x;  m[1][2] = r.y;  m[2][2] = r.z;  m[3][2] = r.w;
```

**④ 数值实例。** 镜像相机在水下 2 单位、水面 y=0 → 相机空间水面
`C·p = y_cam − 2`，取 k=1 → `row2_新 = (0, 1, 0, −2)`：

| 顶点 | 相机空间 | 新 z_clip = y−2 | z_ndc | 结果 |
|---|---|---|---|---|
| 球底（水上 0.5） | (0, 2.5, −5) | +0.5 | 0.10 | ✓ 保留 |
| 球顶（水上 2.5） | (0, 4.5, −5) | +2.5 | 0.50 | ✓ 保留 |
| 水下石头 | (0, 1.0, −5) | **−1.0** | −0.20 | ✗ 被裁 |
| 恰在水面 | (0, 2.0, −5) | 0 | 0 | 切割线 |

**⑤ 为什么画面其他部分分毫不变？** x、y 行和 w 行都没动 → 屏幕投影位置
（x/w, y/w）完全一致。只有 z 的**含义**变了（距离→高度），而 z 只用于
裁剪（正是目的）和深度测试（帧内仍单调，排序正确）。净效果精确表现为：
"整个画面照旧，水下物体消失"。

**⑥ 跨水面的三角形为什么被精确切开？** 判卷是逐顶点的：一脚水上
（+0.5）两脚水下（−1）的三角形，硬件在 z=0 处切开——而 z=0 恰是水面，
切口就落在三角形穿过水面的那条线上，切点 UV/法线自动插值 → 逐三角形
精确剔除。

**⑦ k 的角色与约束。** 任意 k>0 下"z=0 ⇔ 水面"恒成立（水下剔除永远
精确），k 只缩放"高度分数"并决定倾斜远平面位置——k=1/D 由远角钉扎给出
（尽量保留原视锥），再钳制 ≤1.5 防掠射角 k→∞ 吞掉倒影（见数学要素②）。
附带约束：相机须在平面负侧（镜像相机在水下），保证可见几何 w_clip>0
不退化——这也是"主相机潜入水下就跳过反射"的原因。

### water.frag 采样（binding 5）

```glsl
if (ubo.reflectionParams.x > 0.5) {          // 平面反射
    vec2 distortion = perturbedNormal.xz * ubo.reflectionParams.y * 0.5;
    vec2 reflUV = clamp(screenCoord + distortion, 0.001, 0.999);
    reflectionColor = texture(reflectionRT, reflUV).rgb;
    reflectionStrength = 1.0;                 // 精确命中，无衰减
} else { ...SSR 步进（回退）... }
```

波纹扰动通过法线 xz 分量偏移采样坐标（模拟微波水面的歪曲）；
Fresnel/高光/深度遮挡软化与 SSR 模式共享。SSR 路径的"反射增强"
（pow 0.7、×1.5）是补偿其边缘衰减的艺术调整，平面反射路径跳过。

### 平面反射的收益

| SSR 固有局限 | 平面反射 |
|---|---|
| ⚠ 无法反射被遮挡几何 | ✅ 镜像相机视角独立，遮挡关系正确 |
| ⚠ 无法反射屏幕外几何 | ✅ 视锥内全部可见 |
| ⚠ 轮廓歧义（守卫的两难） | ✅ 无步进无判据，无歧义 |
| 逐像素 jitter 点斑 | ✅ 无 jitter |
| 半透明物体不参与 | ✅ 同样不参与（当前限制：透明不入反射，可后续加） |
| 反射亮度用未光照 Albedo | ✅ pbr.frag 完整光照+tonemap，与主画面一致 |

代价：场景几何多画一遍（半分辨率 ≈ 1/4 像素成本，仅不透明+Mask）。

### 预期行为（易误判为 bug 的三点）

1. **反射 RT 里物体可能只有一半** —— 镜像相机与主相机共用 45° 视锥，物体在
   RT 中被视锥边缘裁断是**预期**的：被裁部分对应的反射光线只落在主画面
   底边之外的虚拟水面像素上，恒等 UV 性质保证水面采样区间与 RT 内容严格
   一一对应，缺失部分永远不会被采样（RenderDoc 验证 RT 时勿误判）。
2. **低机位平视看不到倒影，需俯视** —— 倒影俯角 ≈ atan((眼高+物高)/距离)，
   物体直接视角俯角 ≈ atan((眼高−物高)/距离)，物体悬于视线高度时倒影被推
   到约 2× 俯角处，超出 45° FOV 底边（人眼 135° 视野内余光可见，相机不行）。
   旧 SSR"平视也有倒影"是屏幕空间过度延伸伪影。进入水场景（按 5）时引擎
   会把相机设为展示位姿 (0,3,6, 俯角-25°)，物体与倒影同框。
3. **倒影与物体之间有天空色空档** —— 悬浮物体（不贴水）的倒影天然与物体
   分离，中间水面反射的是天空；只有贴水物体/远距物体的倒影才与物体相接。

---

## SSR 算法实现（回退模式）

### 基础概念：deltaDepth 与厚度窗口（thickness）

**SSR 在比较"谁和谁"？** SSR 没有 3D 场景数据，只有一张**"深度照片"**
（G-Buffer 深度缓冲，相机视角：每个屏幕像素记录沿相机视线的**最近表面**
距离）。而反射射线是从水面出发的另一条线。每一步比较的是两个
**不相干的东西**——它们只是恰好投影在同一个屏幕像素上：

```
delta = （射线自己走到哪了）−（相机在这个像素拍到的最近表面在哪）
```

- `delta < 0`：射线在该表面的**前方**（还没撞上）
- `delta > 0`：射线在该表面的**后方**
- `delta ∈ (0, thickness)`：刚穿到表面后面一点点 → **命中窗口**

**为什么命中不能只看 `delta > 0`？** delta 为正有两种完全不同的含义：

```
情况 A（真命中）：射线从表面前方穿到了后方
   → delta 刚从负转正，值很小（一步增量以内，如 +0.02）

情况 B（假命中）：射线从未到过该表面前面，一直从它背后几米处飞过
   （例：像素的深度照片记录着 10m 处的墙，而射线此刻在 15m 处——
    两者只是屏幕位置重合，射线根本没碰墙）
   → delta 是大正数（如 +5）
```

**thickness 就是区分 A/B 的分界线**：把每个表面当作"有 thickness 厚的
一块板"——射线扎进板夹层内（超出表面 < thickness）算真命中；从板的
背面之外飞过（超出 ≥ thickness）算掠过。真实穿越是渐进的（刚穿过时只
超出厘米级），从后面飞过的间距是米级，因此 5cm 的窗口即可区分。

**为什么用线性深度比较？** NDC 深度是非线性的（远处被压缩），同样 0.01
的 NDC 差在近处是几厘米、远处是几十米。比较前先 `linearizeDepth()`，
thickness 的单位就是直观的世界单位（5cm），调参不随距离失真。

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

穿透发生的瞬间（一步跨过整个窗口，窗口内的值从未被采样到）：

```
深度（离相机的距离）
  ↑
  │   上一步 prev：delta = −0.01        这一步 current：delta = +0.12
  │   （射线在墙前 0.01）               （射线在墙后 0.12）
  │        │ ←—— 一步深度增量 0.13 ——→  │
  │  ════════════════════════════════════  ← 墙面深度
  │              (0 …… 0.05)
  │                ↑ 命中窗口 ↑
```

命中判定因此失效 → 未命中回退。穿越检测利用"上一步还在表面前方
（delta≤0）"这一事实推断**这一步之内必然穿过了表面**，转交二分搜索
（见下节）找回精确交面。

### 为什么需要"深度连续性守卫"（轮廓歧义）

无守卫时，**从物体后方掠过**的射线与**真实穿越**在单步深度比较上完全同构：

```
真实穿越:   prev(表面A前方) → current(表面A后方)     delta 翻正 ✓ 应命中
后方掠过:   prev(背景前方)   → current(物体A后方)     delta 同样翻正 ✗ 误命中
```

误命中会把物体颜色采样到大片水面 → **倒影拉长成条纹**。守卫用"相邻采样点
的场景深度是否连续"区分两者（真实穿越发生在连续表面上，相邻像素深度差
< 0.1；轮廓断层为米级跳变）。

### 二分搜索细化（binarySearchScreen）

步进发现 prev 在表面前方（delta<0）、current 在后方（delta>0）后，交点必
在两点之间但位置未知。二分法把这段区间不断对半切，将交点"夹"出来
（`water.frag` `binarySearchScreen`，两处命中分支共用）：

```glsl
vec2 binarySearchScreen(vec2 startUV, vec2 endUV,
                        float startNDCDepth, float endNDCDepth, float thickness) {
    for (int i = 0; i < 8; i++) {
        // ① 取中点：UV 和射线的 NDC 深度一起线性插值
        //    （相邻两步仅 ~1px，屏幕路径近似直线，中点 ≈ 射线真实经过的位置）
        midUV       = (startUV + endUV) * 0.5;
        midNDCDepth = (startNDCDepth + endNDCDepth) * 0.5;
        // （出屏保护：midUV 超出 [0,1] 即 break——屏幕外无深度可比）

        float delta = linearizeDepth(midNDCDepth)
                    - linearizeDepth(texture(gDepth, midUV).r);

        if (delta > 0.0 && delta < thickness) return midUV; // ② 落进窗口 → 就是它
        else if (delta > 0.0) { end   = mid; }   // ③ 中点已在表面后 → 交点在前半段，砍后半
        else                  { start = mid; }   // ④ 中点还在表面前 → 交点在后半段，砍前半
    }
    return midUV;   // 8 轮后区间缩至 1/256 步长
}
```

走一遍例子（命中窗口 (0, 0.05)）：

```
start ●──────────────────────────────────● end
(delta=−0.01，墙前)                    (delta=+0.30，墙后)

第1轮：中点 delta=+0.15 → 穿太深（>0.05）→ 砍后半（end=mid）   区间剩 1/2
第2轮：中点 delta=−0.05 → 还没穿（<0）  → 砍前半（start=mid）  区间剩 1/4
第3轮：中点 delta=+0.02 → 落进窗口 → 返回 ✓
```

要点：

- **不变量**：start 端永远在表面前（delta<0）、end 端永远在表面后
  （delta>0），每次切半都保持该性质 → 真交点始终被夹在区间内，不会丢
- **8 轮 = 区间缩至 1/256**：步长约 1px 时最终精度 ~1/256 像素（亚像素
  级）；即使②的提前返回始终未触发，末轮返回的 mid 也足够接近交点
- **比较用线性深度**：与主步进同一约定（thickness 是世界单位）
- 正常命中分支（精化已落在窗口内的命中点）与穿越检测分支（找回被一步
  跨过的交面）传参均为 `(prevScreen, currentScreen)`

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

### 平面反射（Planar Reflection）——平坦水面的标准解法 ✅ 已实现（v1.3.0）

把相机关于水面镜像，用镜像相机把场景渲染到一张（通常半分辨率）反射 RT，
水面片元投影采样。**精确**：遮挡/屏幕外/薄物体全部正确，无步进无判据。

实现要点（均已落地）：镜像 view 矩阵（`view * reflectMatrix(y=waterHeight)`）、
绕序处理（保持 `frontFace=CounterClockwise`——RenderDoc 实测 Clockwise 会渲染
出远侧内表面，见[绕序经验教训](#平面反射当前默认)）、
斜近裁剪面（Lengyel 投影修改，剔除水下几何）、恒等 UV 采样 + 波纹扰动
（见[平面反射章节](#平面反射当前默认)）。

### SSR + cubemap/反射探针 + TAA（通用组合）

SSR 有效区域用 SSR，失效处回退 cubemap（天空/环境），TAA 累积掩盖抖动。
适合通用湿润表面/粗糙反射；**解决不了"该是墙却是天空"的内容错误**。

### 其他

- Hi-Z 层次深度追踪（性能与精度更好，仍是屏幕空间局限）
- 时域复用 + 降噪（SSAO/SSR 通用）

---

## 常见问题

### Q1: 倒影出现点状蓝色回退
平面反射为默认模式，不应出现回退（若出现，检查相机是否在水下——此时 RT
仅清屏）。若切回 SSR 模式：先确认是否为**被遮挡几何**区域（固有局限，
见局限性章节）；若是开阔区域，检查厚度窗口是否小于该距离的每步线性深度
增量（穿越检测应已兜底）。

### Q2: 倒影拉长成条纹
SSR 模式的轮廓断层误命中——确认连续性守卫（`CROSSING_DEPTH_TOLERANCE`）
未被移除，厚度未调得过大（>0.15 会引入擦背掠过误命中）。平面反射无此问题。

### Q3: 反射过暗/过亮
SSR 模式的 sceneColor 是 **Albedo blit（未光照）**——反射亮度不随场景光照
变化。平面反射模式用 pbr.frag 完整光照渲染，与主画面一致。

### Q4: 性能
平面反射：场景几何多画一遍（半分辨率 ≈ 1/4 像素成本，仅不透明+Mask），
无步进无抖动，成本稳定可预测。SSR 模式：WaterPass 步数上限 2048
（实际≈屏幕像素距离）；降低 `setSSRMaxSteps` 或水面网格分辨率。

### Q5: 如何切换 平面反射 / SSR
DebugPanel →「Water Reflection」→ `Planar Reflection` 复选框（实时切换）+
`Distortion` 波纹扰动滑条。对应 `RenderSettings::waterPlanarReflection` /
`waterReflectionDistortion`。

---

*文档版本：3.2*
*最后更新：2026/09/19（v3.2 增补「斜近裁剪原理直读」章节——近平面=第三行副产品/打分函数直觉/矩阵改动对照/数值实例/精确切割；绕序条目修正为最终配置 Clockwise（此前误诊期记录已过时）；v3.1 绕序经验教训、预期行为三则、展示位姿；v3.0 平面反射实现；v2.x 见历史）*
