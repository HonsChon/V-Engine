# DX12 后端补齐计划

> 状态: 草案(2026/09/07 制定)
> Phase 0/1/2 已完成(2026/09/09,见文末"执行记录");本文"现状摘要"一节为 Phase 0 前快照,已过时,请以代码为准。
> 背景: 基于对 `src/RHI/DX12` 与 `src/RHI/Vulkan` 全量对比分析,以及上层 RHI 消费方式调研。Vulkan 是完整可运行后端;DX12 目前是"可编译的部分脚手架",且从未在真实 MSVC/Windows 上编译过。

## 目标与约束(已确认的方向)

- **阶段化产物**:
  1. DX12 RHI 接口编译级完备 + Windows 冒烟 demo
  2. 上层全面去 Vulkan 化,Engine 全走 RHI
  3. DX12 跑通默认 Forward + ImGui
  4. deferred 全功能对齐
- **Shader 策略**: GLSL 单一源 + 构建期 spirv-cross→HLSL + dxc→DXIL(不动现有 GLSL 资产)
- **验证**: 有 Windows 机器/虚拟机;每个阶段验收都必须在 Windows 上真实编译/运行;macOS 上 Vulkan 行为不得回归

## 现状摘要(DX12 vs Vulkan 主要差距)

### 不可实例化 / 未接线

- `RHI::CreateDevice` 对 DX12 直接 throw(`src/RHI/RHI.cpp:11-12`)
- 引擎不走工厂:`src/Application/Engine.cpp:75` 硬编码 `make_unique<VulkanRHIDevice>`,全仓库无人调用 `RHI::CreateDevice`
- DX12 仅 `if(WIN32)` 下编译(CMakeLists.txt:232-265),macOS 从不构建
- `RHIDevice` 43 个纯虚,DX12 只实现 6 个(`DX12RHIDevice.h:33-41`);`RHISwapChain` 10 个纯虚实现 0 个 → 两者均为抽象类,不可实例化

### 每帧运行必需、但 DX12 不存在

- 交换链帧生命周期:无 acquire/present/recreate、无 back-buffer RTV heap、无 `getRHIRenderPass/getRHIFramebuffer`(引擎每个 pass 都要用)
- 帧同步:Vulkan 侧 2-frame-in-flight;DX12 只有一把私有 fence 且仅用于析构 `waitForGPU()`
- 命令提交:无任何 `ExecuteCommandLists` 调用点

### CommandBuffer 语义降级(`DX12RHICommandBuffer.cpp`)

| 方法 | 问题 |
|---|---|
| `endRenderPass` | 空实现 —— D3D12 无 render pass 对象,**语义上合法**,需注释固化 |
| `beginRenderPass` | 忽略 renderPass 参数与 loadOp;有 clear value 就无脑清所有 RTV(N² 清空 bug);深度不清 stencil;`rtvHandles[8]` 上限 |
| `drawIndexedIndirect` / `dispatchIndirect` | 静默 no-op(无 command signature);引擎零调用,应改为显式报错而非静默 |
| `pipelineBarrier` | 忽略参数只发空 UAV barrier —— 对引擎实际用法(Compute→Graphics ShaderWrite→ShaderRead)语义正确,需映射校验 |
| `blitImage` | 不做缩放/filter |
| `fillBuffer` | 忽略 offset/size,全量 clear |
| `pushConstants` | 写死 root parameter index 0、忽略 stages |
| `setBindingGroup` | 假设 root param index == set index(与 root signature 展平模型不匹配) |
| `bindVertexBuffer` | `StrideInBytes=0`(D3D12 stride 必须来自 VBV) |
| topology | 绑定写死 `D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST` |

### 资源类大半是空壳

- 描述符系统整体缺失:无 shader-visible heap、无 sampler heap、无任何 `CreateShaderResourceView/CreateConstantBufferView`;`DX12RHIBindingGroup::updateBuffer/updateTexture` 空实现;Vulkan 有自动增长 pool 机制
- `DX12RHISampler` 纯空壳(无 desc、无 sampler 描述符、cpp 为空)
- `DX12RHITexture` 不创建资源,只包外部传入的 `ID3D12Resource`;`uploadPixels/createLayerView` 未覆写
- `DX12RHIBuffer::uploadData` 对 GPUOnly(DEFAULT heap)直接 Map 非法,无 staging 路径
- `DX12RHIShader` 只支持 `D3DReadFileToBlob` 读预编译 DXIL,与仓库 GLSL→SPIR-V 内容管线断链

### Pipeline 硬编码

- vertex attribute 语义写死 `"TEXCOORD"`(`DX12RHIPipeline.cpp:307`)
- blend 只写 RT[0]、`IndependentBlendEnable=FALSE`;RTV format 缺失时 padding 硬编码 `R8G8B8A8_UNORM`
- `FrontAndBack` 剔面→NONE;Point fill→wireframe;lineWidth/dynamicState 存而不用

### 上层 Vulkan 泄漏(挡 DX12 的路)

- `Engine.cpp:405-428`:主循环直接调裸 `vkResetCommandBuffer/vkBeginCommandBuffer/vkEndCommandBuffer`,waitStages 直接传 VK 数值
- `SceneRenderer.h:21` 用 raw `VkCommandBuffer` 过渡桥接,每段 `wrapCommandBuffer` 重复包装
- `ImGuiLayer` 强转 native handles 并链接 `imgui_impl_vulkan`(CMake 无 dx12 imgui backend)
- `Window.h` / `Mesh.h` / `core/Utils.h` 携带 Vulkan 类型

### 相对好的部分

- 格式转换表 19/19 `RHIFormat` 双向对齐(`DX12TypeConversions.h` 无缺项)
- `DX12RHIFramebuffer` 的 RTV/DSV CPU heap 是真实现
- PSO / root signature 构建骨架是真实的(仅索引映射有 bug)

### 引擎实际 RHI 消费画像(决定验收标准)

- **默认场景只跑 ForwardPass + ImGui**;GBuffer/SSAO/SSR/Water/GPU culling 均为按键切换
- 引擎实际**不用** `drawIndexedIndirect`/`dispatchIndirect`(GPU culling 结果读回 CPU 后走 `drawIndexed`)
- pushConstants:每管线 1 个 range,最大 144B;binding layout 最多 2 个/管线、6 entries/个
- binding group 初始化时分配一次,仅 SSR/SSAO 每帧 `updateTexture`
- 渲染 pass 全部 loadOp=CLEAR;采样器仅 linear + ClampToEdge/Repeat,无 aniso/compare/border
- topology 全 TriangleList;CullMode 只有 Back/None;FrontFace 全 CounterClockwise;无 MSAA
- 顶点约定:interleaved,stride 44B,location 0-3 = pos/normal/uv/tangent
- swapchain 默认 `R8G8B8A8_UNORM`,bufferCount=2,带 VSync + 内部 D32 depth

---

## Phase 0 — Windows 编译基线(预估:小)

DX12 代码从未在真实 MSVC 上编译过,先建立可编译基线。

- 修 DX12 各 .cpp 编译错误、include、`DX12RHISwapChain.cpp` 的 `GLFW_EXPOSE_NATIVE_WIN32` 路径
- CMake:`if(WIN32)` 分支完整收集 DX12 源 + DirectX-Headers 子模块,开启编译告警
- 新建 **WIN32-only 冒烟 target**(如 `rhi_dx12_smoke`),链接 d3d12/dxgi/dxguid
- **验收**:Windows 上 `cmake -B build-win && cmake --build` 全绿;冒烟 target 可运行并弹出空窗口(仅 GLFW + DX12 device + swapchain 创建,不画内容)

## Phase 1 — RHI 接口完备化 + DX12 冒烟 demo(预估:大,核心工程)

目标:`DX12RHIDevice` 43 个纯虚、`DX12RHISwapChain` 10 个纯虚**全部实现、类可实例化**,用"清屏+画三角形"demo 验证全链路。

### 1a. Device 工厂补齐(`DX12RHIDevice.h/.cpp`)

- `createTexture` / `createSampler` / `createBindingGroup` / `allocateBindingGroup` / `createRenderPass` / `createSwapChain` / `wrapExternal*` / `waitIdle` / `findMemoryType` 系 / `beginSingleTimeCommands` / debug label(PIX `BeginEvent`)/ 6 个 native 访问器(DX12 版)

### 1b. 同步对象 + 提交语义(先定稿设计,再实现)

- DX12 无 binary semaphore:DXGI FLIP + 单 DIRECT 队列下,`submitGraphicsQueue` 的 GPU-GPU wait 可省略(队列内天然有序);`renderFinished`/per-image fence 用 **ID3D12Fence 单调递增 value** 实现,`resetFence` = value++
- `waitForFence` = CPU 等待(fence + Win32 event);`createFence(true)` 预信号语义保留
- **验收**:2-frame 主循环(acquire → wait per-image fence → record → ExecuteCommandLists + Signal → Present)在 demo 跑通,debug layer 无 error

### 1c. 交换链完整实现(`DX12RHISwapChain.h/.cpp`)

- 每个 back-buffer 建 RTV(CPU heap)、内部 D32 depth 资源 + DSV、per-image fence 映射、`acquireNextImage`(FLIP 下返回当前 index,但"该帧 fence 未到则等")、`present`、`recreate`(resize/OutOfDate 路径)、`getRHIRenderPass/getRHIFramebuffer`、native 访问器
- **验收**:demo 中 resize 窗口不崩、无泄漏

### 1d. 资源真实化

- `DX12RHITexture`:真正 `CreateCommittedResource`,按 `RHITextureUsage` 推导 `D3D12_RESOURCE_FLAGS`(SRV/RTV/DSV/UAV),支持 `R8/RGBA8/RGBA16F/D32_FLOAT`、16 层 array + per-layer 子 RTV;实现 `uploadPixels`(staging→`CopyTextureRegion`)、`createLayerView`
- `DX12RHIBuffer::uploadData`:GPUOnly 走 staging + `CopyBufferRegion`(不再对 DEFAULT heap 直接 Map)
- `DX12RHISampler`:存 desc,建真实 sampler 描述符(复用 `toD3D12Filter`,补混合 filter 映射)
- **验收**:冒烟 demo 用 RHI 接口创建纹理/采样器/UBO 并上传,读回 GPUToCPU buffer 验证内容

### 1e. 描述符系统(缺失最重的一块)

- Device 内建**环形 shader-visible CBV/SRV/UAV heap**(按 2 帧 + 每帧少量重写分配,贴合 SSR/SSAO 每帧 `updateTexture`)+ 独立 sampler heap(或 static sampler,二选一,定稿时拍板)
- `RHIBindingLayout` → 存储 layout entries;`RHIBindingGroup` = 从 ring 分配描述符 + CPU 写入 CBV/SRV/UAV
- `updateBuffer/updateTexture` 真实现(SRV 需要 `structStride`,typed/structured buffer 建 SRV)
- 修复 `setBindingGroup` 与 root signature 的索引映射(见 1f)

### 1f. Pipeline/CommandBuffer 语义修复(骨架已有,逐条校准)

- root signature 重建:**每个 layout 一张 table(含该 layout 所有同 stage entry 的 range)**,不再按 entry 展平;pipeline 缓存 `layoutIndex → rootParamIndex` 映射;`pushConstants` 写正确 root 常量参数索引;`setBindingGroup` 按映射循环绑 table;补齐 `SetDescriptorHeaps`
- 修正:`bindVertexBuffer` `StrideInBytes`;topology 不写死;blend 支持多 RT(`IndependentBlendEnable`);RTV format 缺失时报错而非 padding;vertex 语义由 RHI 显式约定表替代硬编码 `"TEXCOORD"`
- 语义校准(把"stub 其实是合法语义"的代码注释+测试固化):
  - `endRenderPass` 空实现 = D3D12 本就无 render pass,保留 + 注释
  - `beginRenderPass` 读 `DX12RHIRenderPass` 的 loadOp/storeOp:仅 CLEAR attachment + 传入对应 clear value 时才清(修 N² bug);深度带 stencil flag;`rtvHandles[8]` 上限加断言
  - `pipelineBarrier`:引擎实际只传 Compute→Graphics ShaderWrite→ShaderRead,`UAV barrier(pResource=null)` 语义正确,保留并对 stage/access 做映射校验
  - `fillBuffer` 带 offset/size(零填充 staging + `CopyBufferRegion`);`blitImage` 支持缩放;`bufferBarrier` 对齐 state transition + 校验
- indirect(`drawIndexedIndirect`/`dispatchIndirect`):引擎零调用,本轮不做 ExecuteIndirect;把静默 no-op 改为**显式报错/TODO**,避免误用
- **验收**:demo 三角形经完整 PSO + root signature + 描述符链路渲染;D3D12 debug layer 无 error;Vulkan 侧零改动

## Phase 2 — 上层抽象化,Engine 全走 RHI(预估:中,部分可与 Phase 1 并行)

DX12 能接上引擎的前提,同时净化 Vulkan 泄漏。

- **RHI 接口收敛**(动接口必须双后端同步改):
  - `submitGraphicsQueue` 去掉泄漏的 `uint32_t waitStages`(后端内部选默认 wait stage)
  - `RHICommandBuffer` 增加 `reset/begin/end` 会话语义;`Engine.cpp:405-423` 裸 `vkBegin/EndCommandBuffer` 改走抽象
  - `SceneRenderer` 去掉过渡 `VkCommandBuffer`(`SceneRenderer.h:21`),直接持有 `RHICommandBuffer*`,删除每段 `wrapCommandBuffer` 重复包装
- **原生桥接**:RHI 统一 native 访问(`getNative*` 返回 `void*`),DX12 侧补 ID3D12Device/CommandQueue/RTV 等——仅供 UI/工具层使用
- **ImGui 后端化**:`ImGuiLayer` 按 `RHIBackend` 选择 imgui_impl_vulkan(保留)或 imgui_impl_dx12(CMake WIN32 编译 `src/third_party/imgui/backends/imgui_impl_dx12.cpp`);DX12 分支传给 imgui:device、queue、back-buffer RTV、渲染期描述符 heap
- **其余去 Vulkan**:`Window.h` 删 VkSurface 死代码;`Mesh.h` 的 VK_FORMAT 顶点描述迁移到 RHI 约定;`core/Utils.h` 的 Vk debug 工具并入后端或删除
- **验收**:macOS Vulkan 引擎画面/交互零回归(每个接口改动后立即跑);Windows 上 Vulkan 分支可编译(可选)

## Phase 3 — DX12 跑通默认 Forward + ImGui(预估:中)

- 引擎设备创建改走 `RHI::CreateDevice`(或后端宏);冒烟 demo 提升为真实 Engine 入口
- 打通完整帧:swapchain pass(颜色+depth,CLEAR)→ ForwardPass(2 组 layout、128B push、44B 顶点布局、material set)→ ImGui 同 pass
- **DX12 shader 内容管线落地**(GLSL 源策略):
  - 新增 `compile_shaders_dx12.sh`/CMake target:`*.vert/frag/comp → glslc(.spv) → spirv-cross --hlsl → 注入 register/space 映射(set→space、binding→register) → dxc -Fo *.dxil`
  - `DX12RHIShader` 支持 load `.dxil`;`DX12RHIPipeline` 按 root signature 约定消费
  - 产出校验:每轮编译后比对 DXIL 的 root 绑定与 C++ root signature 一致(冒烟 target 加断言)
- 修 Phase 1/2 暴露的运行时问题(状态转换顺序、SRV 可见性、descriptor ring 回绕)
- **验收**:Windows 上默认 Forward 场景 + ImGui 画面与 Vulkan 一致(几何、纹理、UI),resize/VSync/2-frame 稳定,debug layer 干净

## Phase 4 — deferred 全功能对齐(预估:中~大,可选但推荐)

- GBuffer(3×RTV + D32 DSV)、SSAO(16 层 array 的 per-layer RTV/UAV、compute deinterleave/reinterleave)、SSR、Water(alpha blend)、blit 路径、每帧 `updateTexture` 重写
- GPUToCPU 双缓冲 readback(fence 后 `Map`);cluster culling 的 compute barrier 链
- 各 pass 的 `.dxil` 全部产出并纳入构建
- **验收**:与 Vulkan 的 7/8/9 键功能画面一致;DX12 在 Windows 上成为可选后端

## Phase 5 — 长期 backlog(不计入本轮)

- `ExecuteIndirect`(待 GPU-driven/间接绘制真正启用时再做,含 command signature 生成)
- MSAA、动态 state、stencil、line/point 填充、UInt16 index、mip 生成等(Vulkan 端也未完整使用的特性)

## 关键风险

1. **DX12 源码从未编译过**——Phase 0 会暴露大量编译期问题,已前置处理
2. **描述符/root signature 模型是重灾区**:Vulkan set/binding/stage → D3D12 table/visibility/register 的映射需先在 Phase 1f 定稿(参考 `docs/DX12-RHI-Notes.md` 第十一节既有方案)
3. **DXGI FLIP 与 Vulkan semaphore 模型不对等**:Phase 1b 的 fence-value 映射出错会引发难查的帧间问题,单独成块并加 demo 压测
4. **spirv-cross HLSL 的 register/space/root-constant 保真度**决定 Phase 3 顺畅度,建议先打通一个 shader 全链路再批量
5. 每改一个 RHI 接口签名,必须双后端 + macOS/Windows 双平台同步验证

## 执行顺序

```
Phase 0 → Phase 1(1a → 1c → 1d → 1e → 1f,Windows 上持续冒烟)→ Phase 2 → Phase 3 → Phase 4
```

Phase 2 的 RHI 接口收敛项(submit 签名、command buffer 会话语义)尽量前移与 Phase 1 并行,避免 DX12 demo 代码随后返工。

---

## 执行记录

### Phase 0 & Phase 1 — 已完成(2026/09/09, commit c51fbb5 及之前)
DX12 后端全接口实现 + `rhi_dx12_smoke` 冒烟 demo(纹理三角形、2-frame、resize、debug layer 自检)。

### Phase 2 — 已完成(2026/09/09, commit af1358e / 3c638ef / b63c237)
- `submitGraphicsQueue` 删除泄漏的 `uint32_t waitStages`;Vulkan 内部默认 `COLOR_ATTACHMENT_OUTPUT` wait stage(`RHIDevice.h`)
- `RHICommandBuffer` 增加 `begin()/end()/getNativeHandle()`(Vulkan: vkReset/vkBegin/vkEnd;DX12: allocator+list Reset / Close);`begin()` 清空 wrapper 缓存状态
- `Engine.cpp` 每帧持有一个持久 RHICommandBuffer wrapper,`drawFrame()` 内裸 `vkReset/Begin/EndCommandBuffer` 全部移除
- `SceneRenderer` 删除过渡 `VkCommandBuffer` 与 12 处每段 `wrapCommandBuffer`,record 系列签名全部 `RHICommandBuffer*`
- ImGui 后端化:`RHIDevice::getBackend()`;`ImGuiLayer` pimpl 化并按后端分发(Vulkan 路径保留;**DX12 分支编译就绪**,运行时验证在 Phase 3)。DX12 分支自建 shader-visible SRV heap + bump 分配回调;CMake WIN32 编译 `imgui_impl_dx12.cpp`
- 死代码清理:`Window::createSurface`/`getRequiredInstanceExtensions`、`Core/Utils.*`(全文件零调用)删除;`Mesh.h` 的 Vk vertex-input helper 替换为 RHI 约定单一事实来源(`Vertex::getStride()/getRHIAttributes()`),Forward/GBuffer/NaniteDebug/Water 四个 pass 统一消费
- 构建卫生:MSVC 增加 `/utf-8`(源文件为 UTF-8,CP936 误读会产生假续行符破坏代码结构 —— ImGuiLayer 重构时实际踩到)
- 验收(Windows):VulkanPBR 构建并运行无回归;`rhi_dx12_smoke` 改用新会话 API + submit 签名,exit 0、validation errors 0

**与计划偏差**
- ImGui DX12 分支不再需要 swapchain 暴露 per-image RTV handle:`imgui_impl_dx12` 主视口 `RenderDrawData` 不设置 RTV,引擎在 RHI `beginRenderPass`(已绑定 back-buffer RTV)内渲染 UI —— 与本仓库现有"render pass 内嵌 UI"模式一致;仅需 device/queue/RTV format 的 native 访问(已有)
- Phase 2 验收原本含"Windows 上 Vulkan 分支可编译(可选)" —— 实际在 Windows 上完整构建并运行了 Vulkan 引擎

### 待办(进入 Phase 3)
- `Engine.cpp:75` 仍硬编码 `VulkanRHIDevice`;`RHI::CreateDevice` 的 DX12 分支仍 throw(Phase 3 首项)
- ImGui DX12 分支、`.dxil` shader 内容管线为全引擎 shader 集落地后,运行期验证
