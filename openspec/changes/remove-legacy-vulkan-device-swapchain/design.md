## Context

当前引擎有两套设备/交换链层：旧的 `VulkanDevice`/`VulkanSwapChain` 和新的 `VulkanRHIDevice`/`VulkanRHISwapChain`。Engine 直接持有旧类，而 Render Pass 层同时引用两者。`VulkanRHIDevice` 已具备独立运行能力（standalone 模式，自行创建 instance/device/surface），但 `RHIDevice`/`RHISwapChain` 抽象层的接口还不够完整（缺少帧同步、格式查询、debug label 等功能）。

后续需要接入 DX12 后端，所有 Pass 必须只依赖 RHI 抽象接口。

## Goals / Non-Goals

**Goals:**
- Engine 仅通过 `RHIDevice*` + `RHISwapChain*` 管理 GPU 资源和帧循环
- 所有 Pass 构造/使用时只能看到 `RHIDevice*`，不能引用任何 `VulkanRHI*` 具体类
- `RHIDevice` 补全旧 `VulkanDevice` 提供的所有必要功能
- `RHISwapChain` 补全帧生命周期管理
- 彻底删除 `VulkanDevice.h/.cpp` 和 `VulkanSwapChain.h/.cpp`

**Non-Goals:**
- 不在本次引入 DX12 后端实现
- 不重构 ImGuiLayer 内部实现（仅修改其构造参数来源）
- 不改变渲染管线逻辑，只改设备/交换链的获取方式

## Decisions

### D1: RHIDevice 接口扩展策略

**决策**：在 `RHIDevice` 基类新增如下虚方法，由 `VulkanRHIDevice` 实现：

| 方法 | 用途 | 来源 |
|------|------|------|
| `findSupportedFormat(formats, tiling, features)` | 格式查询 | VulkanDevice |
| `findDepthFormat()` | 深度格式 | VulkanDevice |
| `createRawBuffer(size, usage, memProps, &buf, &mem)` | 低级 buffer 创建 | VulkanDevice |
| `createRawImage(w,h,fmt,tiling,usage,memProps,&img,&mem)` | 低级 image 创建 | VulkanDevice |
| `copyBuffer(src, dst, size)` | buffer copy | VulkanDevice |
| `beginSingleTimeCommands()` / `endSingleTimeCommands(cmd)` | 单次 cmd | VulkanDevice |
| `beginDebugLabel(cmd,name,color)` / `endDebugLabel(cmd)` / `insertDebugLabel(cmd,name,color)` | RenderDoc | VulkanDevice |
| `createSemaphore()` / `createFence(signaled)` / `destroySemaphore(h)` / `destroyFence(h)` | Sync 对象 | Engine 直接 Vk 调用 |
| `waitForFence(h)` / `resetFence(h)` | Fence 操作 | Engine |
| `allocateCommandBuffers(count)` | Command buffer 分配 | Engine |
| `submitGraphicsQueue(...)` | Queue submit | Engine |
| `getNativeDevice()` → `void*` | 给 ImGui 等低级代码 | 新增 |
| `getNativeInstance()` → `void*` | 给 ImGui | 新增 |
| `getNativePhysicalDevice()` → `void*` | 给 ImGui | 新增 |
| `getGraphicsQueueFamilyIndex()` → `uint32_t` | 给 ImGui | 新增 |
| `getNativeGraphicsQueue()` → `void*` | 给 ImGui | 新增 |

**替代方案**：只把 native handle 暴露出去，让 Engine 继续做原始 Vulkan 调用。
**拒绝原因**：违反 RHI 封装原则，DX12 后端无法工作。

### D2: RHISwapChain 帧生命周期接口

**决策**：在 `RHISwapChain` 基类新增：

```cpp
enum class RHISwapChainResult { Success, Suboptimal, OutOfDate, Error };

virtual RHISwapChainResult acquireNextImage(void* signalSemaphore, uint32_t* outImageIndex) = 0;
virtual RHISwapChainResult present(void* waitSemaphore, uint32_t imageIndex) = 0;
virtual void recreate(uint32_t width, uint32_t height) = 0;
virtual void* getNativeRenderPass() const = 0;
virtual void* getNativeFramebuffer(uint32_t imageIndex) const = 0;
```

Semaphore/fence 用 `void*` 传递 native handle，因为这些 sync 对象目前还是 per-backend 的 raw handle。

### D3: Engine 持有方式

**决策**：
```cpp
// Engine.h
std::unique_ptr<RHIDevice>    m_rhiDevice;      // VulkanRHIDevice(standalone)
std::unique_ptr<RHISwapChain> m_rhiSwapChain;   // VulkanRHISwapChain
```

Engine 通过 `VulkanRHIDevice::createStandalone(window)` 创建设备，再通过 device 创建 swap chain。所有子系统只收到 `RHIDevice*` / `RHISwapChain*`。

### D4: Pass 层的严格约束

**决策**：所有 `RenderPassBase`、`ComputePassBase` 及其子类（ForwardPass、GBufferPass、NaniteDebugPass 等）的成员字段和构造参数 **只能使用 `RHIDevice*`**，禁止 `#include` 任何 `Vulkan*.h`。需要 native handle 时通过 `RHIDevice::getNativeDevice()` 等获取。

### D5: SwapChain 由 RHIDevice 创建

**决策**：在 `RHIDevice` 新增工厂方法：
```cpp
virtual std::unique_ptr<RHISwapChain> createSwapChain(uint32_t width, uint32_t height) = 0;
```

## Risks / Trade-offs

- **[risk] void* 类型安全**：sync 对象和 native handle 通过 `void*` 传递，失去编译期类型检查 → 通过明确文档和内联注释缓解；后续可封装为 `RHISemaphore`/`RHIFence` 类型
- **[risk] 大范围修改导致编译错误**：约 15+ 文件同步修改 → 分步实施，先补接口、再切 Engine、最后清理旧类
- **[trade-off] 低级创建 API 暴露在 RHIDevice**：`createRawBuffer` 等方法打破了"纯高级 RHI"风格 → 可接受，因为 Engine/ImGui 层确实需要，且命名以 `Raw` 前缀区分
- **[trade-off] ImGui 仍需 native handle**：短期内 ImGui 集成需要直接的 VkInstance/VkDevice → 通过 `getNative*()` 方法提供，不影响 Pass 层纯净性
