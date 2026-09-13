# DX12 RHI 学习笔记

## 一、DX12 初始化流程 vs Vulkan vs RHI

### 整体对应关系

```
DX12                        Vulkan                      V-Engine RHI
─────────────────────────────────────────────────────────────────────
IDXGIFactory4               VkInstance                  RHIDevice (内部持有)
IDXGIAdapter1               VkPhysicalDevice            RHIDevice (内部持有)
ID3D12Device                VkDevice                    RHIDevice
ID3D12CommandQueue          VkQueue                     RHIDevice (内部持有)
ID3D12Fence + HANDLE Event  VkFence                     RHIDevice (内部持有)
ID3D12CommandAllocator/List VkCommandPool/CommandBuffer RHICommandBuffer
IDXGISwapChain3             VkSwapchainKHR              RHISwapChain
ID3D12Resource (Buffer)     VkBuffer + VkDeviceMemory   RHIBuffer
ID3D12Resource (Texture)    VkImage + VkImageView       RHITexture
ID3D12DescriptorHeap        VkDescriptorPool            RHIBindingGroup/Layout
D3D12_SAMPLER_DESC          VkSampler                   RHISampler
ID3D12PipelineState         VkPipeline                  RHIPipeline
ID3D12RootSignature         VkPipelineLayout            RHIBindingLayout
root 32-bit constants       VkPushConstantRange         addPushConstant
```

> 完整的两后端对象 / 描述符 / 状态 / 同步 / 命令 / Shader / 坐标系对应关系见
> **§十三 "DX12 ↔ Vulkan 对应关系总表(Phase 4 实现为准)"**。

### DX12 Device 创建流程（对应 `DX12RHIDevice` 构造函数）

```
1. Enable Debug Layer (可选)
   ├── D3D12GetDebugInterface() -> ID3D12Debug
   └── debugController->EnableDebugLayer()
   
2. Create DXGI Factory
   └── CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory))
       ├── Debug 模式: flags = DXGI_CREATE_FACTORY_DEBUG
       └── Release 模式: flags = 0

3. 枚举硬件适配器
   └── GetHardwareAdapter(factory, &adapter)
       遍历 factory->EnumAdapters1() 找到支持 D3D12 的 GPU

4. Create D3D12 Device
   └── D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))

5. Create Command Queue
   └── device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue))
       ├── Type = D3D12_COMMAND_LIST_TYPE_DIRECT
       └── Fence 跟 Queue 一起创建: device->CreateFence(0, ..., &fence)

6. Create Fence Event (Win32)
   └── fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr)
```

**Vulkan 对比：**

| 步骤 | DX12 | Vulkan |
|------|------|--------|
| 调试 | `ID3D12Debug::EnableDebugLayer()` | `VK_LAYER_KHRONOS_validation` |
| 工厂/实例 | `CreateDXGIFactory2()` → `IDXGIFactory4` | `vkCreateInstance()` → `VkInstance` |
| 枚举 GPU | `factory->EnumAdapters1()` | `vkEnumeratePhysicalDevices()` |
| 创建设备 | `D3D12CreateDevice()` → `ID3D12Device` | `vkCreateDevice()` → `VkDevice` |
| 队列 | `device->CreateCommandQueue()` | `vkGetDeviceQueue()` |
| Surface | 不需要（HWND 在 SwapChain 创建时传入） | `vkCreateSurfaceKHR()` |


## 二、DX12 SwapChain 创建（对应 `DX12RHISwapChain`）

### 创建流程

```
1. 获取 HWND
   └── glfwGetWin32Window(window) 
       需要 #define GLFW_EXPOSE_NATIVE_WIN32 + #include <GLFW/glfw3native.h>

2. 填写 DXGI_SWAP_CHAIN_DESC1
   ├── Width / Height          ← 从 RHISwapChainDesc 获取
   ├── Format                  ← toDXGIFormat(desc.format)
   ├── SampleDesc.Count = 1    ← 不做 MSAA
   ├── BufferUsage             = DXGI_USAGE_RENDER_TARGET_OUTPUT
   ├── BufferCount             ← 从 RHISwapChainDesc 获取（2=双缓冲，3=三缓冲）
   ├── SwapEffect              = DXGI_SWAP_EFFECT_FLIP_DISCARD（DX12 必须用 FLIP）
   ├── Scaling                 = DXGI_SCALING_STRETCH
   └── AlphaMode               = DXGI_ALPHA_MODE_UNSPECIFIED

3. 创建 SwapChain
   └── factory->CreateSwapChainForHwnd(
           commandQueue,    // 注意：DX12 传 CommandQueue，不是 Device!
           hwnd,
           &desc1,
           nullptr,         // 全屏描述
           nullptr,         // 输出限制
           &swapChain1      // 返回 IDXGISwapChain1
       )

4. 禁用 Alt+Enter
   └── factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER)

5. 升级到 IDXGISwapChain3
   └── swapChain1.As(&swapChain)
       IDXGISwapChain3 提供 GetCurrentBackBufferIndex()

6. 获取当前帧索引
   └── frameIndex = swapChain->GetCurrentBackBufferIndex()
```

**关键区别 vs Vulkan：**

| | DX12 | Vulkan |
|---|---|---|
| 创建时传入 | CommandQueue + HWND | Device + Surface |
| Surface 概念 | 不需要（HWND 直接传入） | 需要先创建 VkSurfaceKHR |
| Present Mode | 通过 SwapEffect 控制（FLIP_DISCARD） | 通过 VkPresentModeKHR（FIFO/MAILBOX） |
| 获取当前帧 | `GetCurrentBackBufferIndex()` | `vkAcquireNextImageKHR()` |
| API 版本 | 推荐 `CreateSwapChainForHwnd`（DXGI 1.2+） | `vkCreateSwapchainKHR` |


## 三、DX12 析构 / GPU 同步（对应 `DX12RHIDevice` 析构函数）

### 等待 GPU 完成（等价于 vkDeviceWaitIdle）

DX12 **没有** `DeviceWaitIdle` 一步到位的函数，需要手动组合 Fence + Win32 Event：

```cpp
void DX12RHIDevice::waitForGPU() {
    // 1. 往队列发一个 Signal
    const uint64_t waitValue = ++fenceValue;
    commandQueue->Signal(fence.Get(), waitValue);

    // 2. 如果 GPU 还没到达，CPU 阻塞等待
    if (fence->GetCompletedValue() < waitValue) {
        fence->SetEventOnCompletion(waitValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}
```

**时序图：**
```
CPU: Signal(fence, 5) → GetCompletedValue()=3 < 5 → SetEventOnCompletion(5) → [阻塞...]  → 被唤醒
                                                                                     ↑
GPU: [工作中...]  →  [完成]  →  fence 到达 5  →  触发 fenceEvent ─────────────────────┘
```

### 析构顺序

```cpp
DX12RHIDevice::~DX12RHIDevice() {
    waitForGPU();                    // 1. 等 GPU 完成所有工作
    CloseHandle(fenceEvent);         // 2. 关闭 Win32 Event 句柄
    // 3. ComPtr 自动释放：fence → commandQueue → device → adapter → factory
}
```

**Vulkan 对比：**

| DX12 | Vulkan |
|------|--------|
| `waitForGPU()` (手动 Fence) | `vkDeviceWaitIdle(device)` (一行搞定) |
| `CloseHandle(fenceEvent)` | 无（Vulkan Fence 不需要额外事件对象） |
| ComPtr 自动 Release | 手动 `vkDestroyDevice` / `vkDestroyInstance` 等 |


## 四、COM 智能指针（ComPtr）vs Vulkan 手动管理

DX12 使用 COM 对象，通过 `ComPtr` 自动管理生命周期：

```cpp
ComPtr<ID3D12Device> device;        // 类似 shared_ptr，引用计数自动 AddRef/Release
device.Get()                        // 获取裸指针
device.GetAddressOf()  或  &device  // 获取指针的指针（用于创建函数的输出参数）
swapChain1.As(&swapChain3)          // QueryInterface 升级接口版本
```

**Vulkan 没有这个机制**，所有对象必须手动 `vkDestroy*`。


## 五、类型转换层

每个后端都有一个 `TypeConversions.h`，负责 RHI 抽象类型 ↔ 原生类型的映射：

| 文件 | 作用 |
|------|------|
| `VulkanTypeConversions.h` | `RHIFormat` → `VkFormat`、`RHIBufferUsage` → `VkBufferUsageFlags` 等 |
| `DX12TypeConversions.h` | `RHIFormat` → `DXGI_FORMAT`、`RHIMemoryUsage` → `D3D12_HEAP_TYPE` 等 |

**DX12 特有的转换：**

| RHI | DX12 | 说明 |
|-----|------|------|
| `RHIMemoryUsage::GPUOnly` | `D3D12_HEAP_TYPE_DEFAULT` | GPU 独占内存 |
| `RHIMemoryUsage::CPUToGPU` | `D3D12_HEAP_TYPE_UPLOAD` | CPU 写 → GPU 读 |
| `RHIMemoryUsage::GPUToCPU` | `D3D12_HEAP_TYPE_READBACK` | GPU 写 → CPU 读 |
| `RHIBufferUsage::Storage` | `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS` | UAV 访问 |
| 深度格式 | Typeless 格式 | DX12 深度纹理做 SRV 时需要 Typeless |


## 六、RHISwapChainDesc

```cpp
struct RHISwapChainDesc {
    uint32_t       width       = 0;
    uint32_t       height      = 0;
    RHIFormat      format      = RHIFormat::R8G8B8A8_UNORM;
    uint32_t       bufferCount = 2;         // 双缓冲 / 三缓冲
    RHIPresentMode presentMode = RHIPresentMode::VSync;
};
```

| 字段 | DX12 对应 | Vulkan 对应 |
|------|-----------|-------------|
| `format` | `DXGI_FORMAT` | `VkFormat` + `VkColorSpaceKHR` |
| `bufferCount` | `DXGI_SWAP_CHAIN_DESC1::BufferCount` | `VkSwapchainCreateInfoKHR::minImageCount` |
| `presentMode::VSync` | `DXGI_SWAP_EFFECT_FLIP_DISCARD` + normal present | `VK_PRESENT_MODE_FIFO_KHR` |
| `presentMode::Immediate` | Allow tearing flag | `VK_PRESENT_MODE_MAILBOX_KHR` / `IMMEDIATE` |


## 七、RHIBufferDesc

```cpp
struct RHIBufferDesc {
    uint64_t       size         = 0;
    RHIBufferUsage usage        = RHIBufferUsage::None;
    RHIMemoryUsage memoryUsage  = RHIMemoryUsage::GPUOnly;
    uint32_t       structStride = 0;            // 结构化缓冲区步长
    RHIFormat      format       = RHIFormat::Undefined;  // 类型化缓冲区格式
    RHIImageLayout initialState = RHIImageLayout::Undefined;
    std::string    debugName;
};
```

| 字段 | 用途 |
|------|------|
| `structStride` | 非 0 时为 StructuredBuffer，DX12 创建 SRV/UAV 时需要 |
| `format` | TypedBuffer 的格式（如 Index Buffer 做 SRV 用 R32_UINT） |
| `initialState` | DX12 资源初始状态（Vulkan 通过 Image Layout 管理） |
| `debugName` | RenderDoc / PIX / Vulkan Validation 中显示的名称 |


## 八、参考架构（nvrhi）

nvrhi 的 DX12 后端设计要点：

- **Fence 绑在 Queue 上**，不是 Device 上
- **Device 持有一个全局 `HANDLE fenceEvent`**，所有 Queue 共享
- **析构：`waitForIdle()` + `CloseHandle(event)` + ComPtr 自动释放**
- **Buffer 使用引用计数**（`RefCountPtr`），CommandList 内部 hold 住资源引用防止提交期间释放
- **资源状态自动追踪**（`BufferStateExtension`）
- **描述符视图管理**（Buffer 自己创建 CBV/SRV/UAV 到 Descriptor Heap）


## 九、IDXGIFactory vs IDXGIAdapter

| 组件 | 职责 | 类比 |
|------|------|------|
| `IDXGIFactory` | 枚举和创建 `IDXGIAdapter`，创建 SwapChain | Vulkan 的 `VkInstance` |
| `IDXGIAdapter` | 代表一块物理 GPU，查询设备信息（显存、功能级别），创建 `ID3D12Device` | Vulkan 的 `VkPhysicalDevice` |

**典型流程：**

```
CreateDXGIFactory → IDXGIFactory
    └── factory->EnumAdapters1(idx, &adapter) → 遍历所有 GPU
        └── adapter->GetDesc1(&desc) → 查询名称、显存、架构
        └── D3D12CreateDevice(adapter, ...) → 找到支持 D3D12 的首个物理设备
```

**筛选逻辑**（`GetHardwareAdapter`）：
1. 跳过软件适配器（`DXGI_ADAPTER_FLAG_SOFTWARE`）
2. 尝试 `D3D12CreateDevice`，失败则继续枚举下一个
3. 选中第一个支持 D3D12 Feature Level 12.0 的物理设备


## 十、VertexBinding / VertexAttribute（顶点输入布局）

### 概念

两个结构体共同定义 D3D12 Input Layout：

```cpp
struct VertexBinding {
    uint32_t binding;                    // Input Slot 索引（0 ~ N-1）
    uint32_t stride;                     // 每个顶点/实例的字节步长
    RHIVertexInputRate inputRate;        // PerVertex 或 PerInstance
};

struct VertexAttribute {
    uint32_t binding;                    // 关联的 Input Slot
    uint32_t location;                   // Shader 语义索引（SemanticIndex）
    RHIFormat format;                    // 属性格式
    uint32_t offset;                     // 在顶点中的字节偏移
};
```

### 为什么是 vector？

GPU 允许从**多个 Vertex Buffer Slot** 同时读取数据：

| Slot | 数据 | 速率 |
|------|------|------|
| 0 | 位置、法线、UV | 逐顶点 |
| 1 | 实例颜色、实例矩阵 | 逐实例 |

每个 slot 就是一个 `VertexBinding`。attribute 可以指向不同的 slot，实现**逐顶点 + 逐实例**混合输入。

### 示例

```cpp
// Slot 0: 逐顶点数据 (stride=32)
addVertexBinding(0, 32, RHIVertexInputRate::Vertex);
addVertexAttribute(0, 0, RHIFormat::R32G32B32_FLOAT, 0);   // pos,    offset=0
addVertexAttribute(0, 1, RHIFormat::R32G32B32_FLOAT, 12);  // normal, offset=12
addVertexAttribute(0, 2, RHIFormat::R32G32_FLOAT,   24);   // uv,     offset=24

// Slot 1: 逐实例数据 (stride=16)
addVertexBinding(1, 16, RHIVertexInputRate::Instance);
addVertexAttribute(1, 3, RHIFormat::R32G32B32A32_FLOAT, 0); // instanceColor
```

生成 D3D12 Input Layout：

```
Slot 0 (32 bytes, PerVertex)     Slot 1 (16 bytes, PerInstance)
  [0~11]  pos      → TEXCOORD0    [0~15] instanceColor → TEXCOORD3
  [12~23] normal   → TEXCOORD1
  [24~31] uv       → TEXCOORD2
```

### 传入的数据不限于顶点

Vertex Buffer + Input Layout 本质是**二进制数据搬运工**——告诉 GPU stride、format、offset，它就从 Buffer 里取出数据塞给 VS 输入语义。可传入：

| 数据 | 示例格式 | 用途 |
|------|---------|------|
| 位置 | `R32G32B32_FLOAT` | 常规顶点 |
| 法线 | `R32G32B32_FLOAT` | 光照 |
| UV | `R32G32_FLOAT` | 纹理采样 |
| 顶点颜色 | `R32G32B32A32_FLOAT` | 逐顶点着色 |
| 骨骼索引/权重 | `R32G32B32A32_UINT` + `R32G32B32A32_FLOAT` | 蒙皮动画 |
| 实例矩阵 | 4 × `R32G32B32A32_FLOAT` | Instance 渲染 |

### 对应关系

| DX12 | Vulkan |
|------|--------|
| `VertexBinding`（Input Slot） | `VkVertexInputBindingDescription` |
| `VertexAttribute`（Input Element） | `VkVertexInputAttributeDescription` |
| `IASetVertexBuffers(binding, ...)` | `vkCmdBindVertexBuffers(binding, ...)` |
| `IASetPrimitiveTopology()` | `vkCmdSetPrimitiveTopologyEXT()`（或 PSO 中固定） |
| SemanticName = "TEXCOORD" + SemanticIndex | `location = entry.location`（Vulkan 直接用 location） |


## 十一、Root Signature 构建（PipelineLayout 的 DX12 等价物）

### 概念映射

| DX12 | Vulkan | 说明 |
|------|--------|------|
| `D3D12_ROOT_PARAMETER`（descriptor table） | `VkDescriptorSetLayout` | 描述 descriptor 的绑定方式 |
| `D3D12_ROOT_PARAMETER`（32-bit constants） | `VkPushConstantRange` | 根常量 |
| `D3D12_ROOT_SIGNATURE` | `VkPipelineLayout` | 最终产物，绑定到 CommandList |
| 构建时机 | 构建时机 | 都在 Pipeline Builder 的 build() 中创建 |

### 构建流程（两趟法）

```cpp
// 第一趟：统计所有 binding layout 的 entry 总数
size_t totalEntries = 0;
for (const auto* layout : bindingLayouts_)
    totalEntries += static_cast<const DX12RHIBindingLayout*>(layout)->getDesc().entries.size();

// 预分配（指针永不失效）
std::vector<D3D12_DESCRIPTOR_RANGE> ranges(totalEntries);
std::vector<D3D12_ROOT_PARAMETER> rootParams(totalEntries + pushConstantRanges_.size());

// 第二趟：填充
//   1) descriptor table 参数（每个 entry 一个 range + 一个 root parameter）
//   2) 32-bit constants 参数（push constants 放在 descriptor tables 后面）
//   3) 序列化 + 创建 RootSignature
```

### 为什么需要两趟 + 预分配

**错误模式**（之前代码的问题）：

```cpp
for (auto& entry : ...) {
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;  // 每个 entry 一个临时 vector
    ranges.push_back(range);                       // 其实只需 1 个元素
    allRanges.push_back(std::move(ranges));         // vector 被 move 到 allRanges
    param.pDescriptorRanges = allRanges.back().data();  // 存指针
    rootParams.push_back(param);
}
```

两个问题：
1. **`vector<D3D12_DESCRIPTOR_RANGE>` 多余**——每个 entry 总是只需要一个 range，临时 vector 毫无必要
2. **`allRanges` reallocation → 指针悬空**——后续 `push_back` 触发 `allRanges` reallocation 时，之前存的 `pDescriptorRanges` 指向被销毁的旧内存

**正确做法**：预分配后直接用 `&ranges[idx]`，指针永远稳定。

### Root Parameter 布局

```
Root Signature Layout:
┌──────────────────────────────────────────────┐
│ Root Parameter 0: DescriptorTable (Set 0)    │  ← bindingLayouts_[0]
│ Root Parameter 1: DescriptorTable (Set 1)    │  ← bindingLayouts_[1]
│ ...                                          │
│ Root Parameter N: 32BitConstants             │  ← pushConstants (所有 staging)
└──────────────────────────────────────────────┘
```

### ShaderVisibility

| RHI Stage 组合 | DX12 `D3D12_SHADER_VISIBILITY` | Vulkan 对应 |
|----------------|--------------------------------|-------------|
| Vertex 仅 | `VERTEX` | `VK_SHADER_STAGE_VERTEX_BIT` |
| Fragment 仅 | `PIXEL` | `VK_SHADER_STAGE_FRAGMENT_BIT` |
| 同时含 Vertex + Fragment | `ALL` | `VK_SHADER_STAGE_VERTEX | FRAGMENT` |
| Compute | `ALL` | `VK_SHADER_STAGE_COMPUTE_BIT` |

### Descriptor Range Type 映射

| RHI `RHIDescriptorType` | DX12 `D3D12_DESCRIPTOR_RANGE_TYPE` | Vulkan `VkDescriptorType` |
|------------------------|-------------------------------------|--------------------------|
| `UniformBuffer` / `UniformBufferDynamic` | `CBV` | `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` / `UNIFORM_BUFFER_DYNAMIC` |
| `StorageBuffer` / `StorageBufferDynamic` | `UAV` | `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` / `STORAGE_BUFFER_DYNAMIC` |
| `SampledImage` / `CombinedImageSampler` / `InputAttachment` | `SRV` | `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` / `COMBINED_IMAGE_SAMPLER` / `INPUT_ATTACHMENT` |
| `StorageImage` | `UAV` | `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE` |
| `Sampler` | `SAMPLER` | `VK_DESCRIPTOR_TYPE_SAMPLER` |

### 附录：register / space 映射约定（实现为准）

> 此附录是代码注释（`DX12RHIPipeline.h`、`scripts/dx12/inject_hlsl.ps1`）引用的
> "docs/DX12-RHI-Notes.md §11 appendix"，在此正式落档。Phase 1 实现比上文
> "每个 entry 一个 root parameter" 的旧草图更进了一步，以 **layout 分组** 构建：

#### 已落地的最终结构（与上文代码草图不同，以此为准）

1. **每个 binding layout 最多产生两个 root parameter 表**：
   - 资源表：该 layout 内所有 CBV/SRV/UAV entry（合并到一个 `D3D12_DESCRIPTOR_RANGE` 数组）
   - 采样器表：该 layout 内所有 `Sampler` entry（D3D12 禁止把 SAMPLER range 混进资源表）
   - 同一 layout 内同 stage 的 entry 按 binding 升序合并到同一 range 数组。
2. **root parameter 顺序**：所有 layout 的表按 layout 顺序排列，32-bit constants
   （push constants）参数放在最后。
3. **ShaderVisibility**：取该表内 entry 的 stage 并集 —— 仅 Vertex → `VERTEX`；
   仅 Fragment → `PIXEL`；Vertex+Fragment / Compute → `ALL`。
4. **register / space 约定**（spirv-cross 产物与 C++ root signature 必须一致）：
   - binding layout `i` → **register space `i`**
   - layout 内 entry `binding b` → **register `b`**（`ShaderRegister = b, RegisterSpace = i`）
   - push constants → 单个 32-bit constants 参数，`ShaderRegister 0`，
     **register space = bindingLayouts_.size()**（`scripts/dx12/inject_hlsl.ps1`
     给 spirv-cross 生成的 push-constant `cbuffer` 追加 `register(b0, space=N)`，N 即该值）
5. **descriptor 类型映射**：`UniformBuffer/Dynamic→CBV`、`StorageBuffer/Dynamic + StorageImage→UAV`、
   `SampledImage/CombinedImageSampler/InputAttachment→SRV`、`Sampler→SAMPLER`（见上表）。
6. **采样器**：root signature 不声明 static sampler（`NumStaticSamplers = 0`），
   采样器走独立 shader-visible SAMPLER 描述符堆 + 每 layout 的采样器表。
7. **CommandBuffer 侧消费**：`DX12RHICommandBuffer::setBindingGroup(set, group)` 用
   pipeline 缓存的 `getTableRootParam(set, isSamplerTable)` 找到对应 root 参数；
   `pushConstants` 用 `getPushConstantRootParam()`，不再假设 index 0。

#### 版本对齐清单（改动 root signature 时必须同步）

| 位置 | 内容 |
|------|------|
| `DX12RHIPipeline::build()` | 表分组 + visibility + 参数顺序 |
| `DX12RHIDescriptor.cpp` | binding group 描述符按"资源组 + 采样器组"顺序写入 ring |
| `scripts/dx12/inject_hlsl.ps1` | push-constant `space = layout 数` |
| CMake `CompileDX12Shaders` | 每 shader 传入该 shader 对应管线的 layout 数 |
| smoke demo 断言 | DXIL 的 root 绑定与 C++ root signature 一致 |

## 十二、描述符环形堆(Descriptor Ring)与持久/临时分离

### 为什么需要自管理描述符分配

- D3D12 的 SRV/CBV/UAV 必须住在 descriptor heap 里;一次绘制只能绑定**一个**
  shader-visible CBV/SRV/UAV heap(和一个 sampler heap)。
- root signature 的 descriptor table 存的是 heap 内的 GPU handle,table 指向的描述符内容
  必须在 GPU 执行期间保持有效。
- Vulkan 有 `VkDescriptorPool` + `vkAllocateDescriptorSets`("每帧分配 set"),D3D12 没有
  对应抽象,只能自己在 heap 里做子分配。RHI 要在 DX12 上模拟 `RHIBindingGroup` 语义,
  就需要一个 heap 内的分配器。

### 两类分配

| 类型 | 例子 | 生命周期 |
|---|---|---|
| **持久** | 各 pass 的 binding group(GBuffer/Lighting/Water/SSAO 的 UBO+纹理绑定) | 初始化时分配一次;之后只做 in-place 重写(`updateTexture` 换纹理,槽位不变) |
| **临时** | `blitImage` 的 SRV+sampler+RTV、`clearColorImage` 的 RTV、`fillBuffer` 的 UAV | 每帧录制时申请,用完即弃 |

难点:CPU 在**录制时**写描述符,GPU 在**提交后**才读。2 帧在飞时,下一帧复用同一槽位
会覆盖 GPU 还在用的描述符。

### 环形堆结构(`DX12RHIDevice::RingHeap`)

- **bump 分配**:cursor 只前进,O(1)、无空闲链表、无碎片。
- **4 个 segment**(= 2 帧在飞 + 余量):每段记录最后一次用到它的 submit 的 fence 值;
  复用某段前 `WaitForSingleObject` 等该 fence(`ringWaitForSegment`),
  由 `finalizeDescriptorBatch()` 在 submit 时盖章。
- 回绕时 `ringFlushAll` 排空全部段,再复用。
- 环:resource(CBV/SRV/UAV,shader-visible,4×2048)、sampler(shader-visible,4×128)、
  RTV/DSV/CPU-UAV(CPU-only,仅临时)。

### 持久/临时分离(2026/09/10 修复)

原实现把 binding group 的持久描述符和每帧临时描述符放在同一个 cursor 上,回绕到 0 后会
逐帧覆盖持久描述符(延迟渲染每帧 blit 消耗 1 个 SRV,约 8000 帧后覆盖 GBuffer 的
UBO/SRV → 画面突然全黑)。

修复:`RingHeap.persistentEnd` 水位线。

- `allocateResourceDescriptors/SamplerDescriptors(..., persistent=true)` 由
  `DX12RHIBindingGroup` 构造使用,分配后抬高 `persistentEnd = max(persistentEnd, end)`;
- 临时分配回绕时 `cursor = persistentEnd`(而非 0),并检查剩余空间;
- 临时分配仍按 segment fence 门控,行为不变。

Debug 构建下回绕会打一行 `[DX12RHIDevice] descriptor ring wrap (heap=... persistentEnd=... total=...)`,
便于确认水位线生效(日志中 `persistentEnd` 应等于持久描述符数量,例如 sampler 环 78)。

### 替代方案(未采用)

- **持久/临时分两个 shader-visible heap**:一次绘制不会同时用两类描述符,技术上可行,
  但命令层要管理 heap 切换,改动面大。
- **每个 binding group 独占一小段堆(block allocator)**:避免共享 cursor,但浪费槽位、需要块管理。
- **每帧新建 heap / CopyDescriptors**:分配或拷贝开销大,无收益。
- **ImGui 的做法**:自带固定 bump heap(只加不减),因为 ImGui 的描述符全是持久的——
  说明"持久用 bump、临时用环形"本来就是合理分工,问题只在混用。

## 十三、DX12 ↔ Vulkan 对应关系总表(Phase 4 实现为准)

> 本文前面各节按主题展开;本节把两个后端**实际实现**的对应关系汇总成一张速查表,
> 供双后端同步修改时对照。Vulkan 侧代码在 `src/RHI/Vulkan/`,DX12 侧在 `src/RHI/DX12/`,
> 公共接口在 `src/RHI/*.h`。

### 13.1 对象级对应

| 概念 | Vulkan | DX12(本仓库) | RHI 抽象 |
|---|---|---|---|
| 实例 / 工厂 | `VkInstance` | `IDXGIFactory4` | `RHIDevice`(内部持有) |
| 物理设备 | `VkPhysicalDevice` | `IDXGIAdapter1` | `RHIDevice`(内部持有) |
| 逻辑设备 | `VkDevice` | `ID3D12Device` | `RHIDevice` |
| 队列 | `VkQueue` | `ID3D12CommandQueue`(单个 DIRECT) | `RHIDevice`(内部持有) |
| 命令池 / 命令缓冲 | `VkCommandPool` + `VkCommandBuffer` | `ID3D12CommandAllocator` + `ID3D12GraphicsCommandList` | `RHICommandBuffer`(包装外部句柄) |
| 队列提交 | `vkQueueSubmit` | `ExecuteCommandLists` | `submitGraphicsQueue` |
| Fence(CPU 等 GPU) | `VkFence` | `ID3D12Fence` + Win32 Event | `createFence/waitForFence/resetFence`(`DX12FenceSync`) |
| 信号量 | `VkSemaphore` | 无原生对象,用 `ID3D12Fence` 单调值模拟 | `createSemaphore` |
| 交换链 | `VkSwapchainKHR` | `IDXGISwapChain3`(FLIP_DISCARD) | `RHISwapChain` |
| Buffer | `VkBuffer` + `VkDeviceMemory` | `ID3D12Resource`(DIMENSION_BUFFER) | `RHIBuffer` |
| 纹理 / ImageView | `VkImage` + `VkImageView` | `ID3D12Resource`(TEXTURE2D) + RTV/DSV/SRV/UAV 描述符 | `RHITexture` |
| 采样器 | `VkSampler` | `D3D12_SAMPLER_DESC`(写入 sampler heap) | `RHISampler` |
| 描述符集布局 | `VkDescriptorSetLayout` | root signature 的 descriptor table / range | `RHIBindingLayout` |
| 描述符集 | `VkDescriptorSet`(pool 分配) | heap 内 ring 分配的一段连续描述符 | `RHIBindingGroup` |
| 描述符池 | `VkDescriptorPool`(自动增长) | 4 段环形堆 + CPU-only 环(见 §十二) | `RHIDevice` 内部 |
| Push constant | `VkPushConstantRange` + `vkCmdPushConstants` | root 32-bit constants(`SetGraphics/ComputeRoot32BitConstants`) | `addPushConstant` / `pushConstants` |
| 管线布局 | `VkPipelineLayout` | `ID3D12RootSignature` | 多个 `RHIBindingLayout` + PC range |
| 管线 | `VkPipeline` | `ID3D12PipelineState` | `RHIPipeline` |
| RenderPass / Framebuffer | `VkRenderPass` + `VkFramebuffer` | **无对应对象**:`begin/endRenderPass` 手工做状态迁移 + `OMSetRenderTargets` | `RHIRenderPass` / `RHIFramebuffer` |
| 动态状态 | `vkCmdSetViewport/Scissor/...` | `RSSetViewports/RSSetScissorRects` | `setViewport/setScissor` |
| 屏障 | `vkCmdPipelineBarrier` | `ResourceBarrier`(TRANSITION / UAV) | `pipelineBarrier/bufferBarrier/transitionImageLayout` |
| Blit | `vkCmdBlitImage` | 内部全屏三角形 blit pipeline(按 RTV 格式缓存) | `blitImage` |
| Clear | `vkCmdClearColorImage` | 内部常量色全屏三角形 clear pipeline(按 RTV 格式缓存) | `clearColorImage` |
| Fill | `vkCmdFillBuffer` | `ClearUnorderedAccessViewUint`(CPU-only UAV 描述符 + shader-visible 环副本) | `fillBuffer` |
| 调试标注 | `vkCmdBeginDebugUtilsLabelEXT` | PIX `BeginEvent`(动态加载 WinPixEventRuntime,缺失降级为 no-op) | `beginDebugLabel` 等 |

### 13.2 描述符与绑定模型(详见 §十一 附录)

| 概念 | Vulkan | DX12 |
|---|---|---|
| 绑定编号 | `layout(set = i, binding = b)` | register space `i`,register `b`(`ShaderRegister=b, RegisterSpace=i`) |
| layout → 根参数 | 每个 set 一个 descriptor set | 每个 layout 最多两张表:**资源表**(CBV/SRV/UAV 合并)+ **采样器表**(D3D12 禁止 SAMPLER 混入资源表) |
| 参数顺序 | set 顺序 | layout 表顺序 + push constants 放最后 |
| 可见性 | stageFlags | 表内 entry stage 并集:仅 VS→`VERTEX`,仅 FS→`PIXEL`,VS+FS / Compute→`ALL` |
| CombinedImageSampler | 一个 `COMBINED_IMAGE_SAMPLER` | 拆成 SRV(`t#`)+ SAMPLER(`s#`)两组描述符 |
| Push constant | `VkPushConstantRange`(可多段) | **单个** 32-bit constants 参数,`register(b0, space = layout 数)`,按 16B register 粒度(8B 块会被读成未初始化,故 SSAO compute PC 补到 16B) |
| 描述符类型映射 | `UNIFORM_BUFFER` / `STORAGE_BUFFER` / `SAMPLED_IMAGE` / `STORAGE_IMAGE` / `SAMPLER` | CBV / UAV / SRV / UAV / SAMPLER |
| StorageBuffer 视图 | `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` | spirv-cross 把 SSBO 降级为 `RWByteAddressBuffer` → UAV 必须用 **RAW 视图**(`R32_TYPELESS` + `D3D12_BUFFER_UAV_FLAG_RAW`);`structStride>0` 时才是 structured UAV |
| 描述符更新时机 | 可随时 `vkUpdateDescriptorSets`(需避免 in-flight 冲突) | CPU 写堆;持久描述符由水位线保护,临时描述符按 segment fence 轮转(§十二) |
| 堆绑定 | 无此概念 | 一次绘制只能绑定**一个** shader-visible CBV/SRV/UAV heap + 一个 sampler heap;`bindPipelineInternal` 每次重绑 |

### 13.3 资源状态 / 布局对应

| `RHIImageLayout` / Vulkan 布局 | DX12 `D3D12_RESOURCE_STATES` |
|---|---|
| `Undefined` / `General` | `COMMON`(`General` 且纹理带 `Storage` 用途 → `UNORDERED_ACCESS`) |
| `ShaderReadOnly` | `PIXEL_SHADER_RESOURCE \| NON_PIXEL_SHADER_RESOURCE` |
| `ColorAttachment` | `RENDER_TARGET` |
| `DepthStencilAttachment` | `DEPTH_WRITE` |
| `DepthStencilReadOnly` | `DEPTH_READ` |
| `TransferSrc` | `COPY_SOURCE` |
| `TransferDst` | `COPY_DEST` |
| `PresentSrc` | `PRESENT`(与 `COMMON` 同为 0,barrier 层不区分) |

- Vulkan 的布局迁移可由 render pass 的 `initialLayout/finalLayout` 隐式完成;DX12 全部显式:
  `beginRenderPass` 前 `ensureState(RENDER_TARGET/DEPTH_WRITE)`,`endRenderPass` 按 attachment 的
  `finalLayout` 再迁移一次(见 `DX12RHICommandBuffer.cpp`)。
- 状态追踪:Vulkan 由驱动 + 引擎显式迁移;DX12 在 `DX12RHITexture`/`DX12RHIBuffer` 上维护
  `currentState`(layer view 与父纹理**共享**同一个 tracker)。
- `bufferBarrier` 的 access → state 映射:`ShaderWrite→UNORDERED_ACCESS`、
  `ShaderRead→UAV(Storage)/shader-read`、`TransferWrite→COPY_DEST`、
  `TransferRead→COPY_SOURCE`、`Vertex/Uniform→VERTEX_AND_CONSTANT_BUFFER`、`Index→INDEX_BUFFER`。
- **READBACK heap 只能处于 `COMMON`/`COPY_DEST`**(GPU 不能读),`bufferBarrier` 会钳制,
  否则引擎里"把 readback buffer 迁到 shader-read"(Vulkan 无害)会在 D3D 报错。

### 13.4 同步与帧循环对应

| 概念 | Vulkan | DX12 |
|---|---|---|
| 帧在飞 | `MAX_FRAMES_IN_FLIGHT=2`:`inFlightFences[2]` | 同左,`createFence(true)` 预信号语义一致 |
| 图像可用 | `imageAvailableSemaphores[2]` + `vkAcquireNextImageKHR` | 无对应等待(单队列 + FLIP):`GetCurrentBackBufferIndex()`,per-image fence 等待上一帧 present 完成 |
| 渲染完成 | `renderFinishedSemaphores[2]` + present wait | `present()` 后在队列上 Signal per-image fence(单调值) |
| GPU-GPU 等待 | 二进制信号量 | **省略**:单 DIRECT 队列内天然有序;信号量仅作记账(递增 fence 值) |
| Fence 复用 | `vkResetFences` 重置为未触发 | 不可回退:单调递增 value;`resetFence` = `value++`,`waitForFence` 等该值 |
| 交换链重建 | `vkCreateSwapchainKHR` + 旧的销毁;OutOfDate 每帧可重试 | `ResizeBuffers`,失败回退整体重建;失败可重入重试(`Engine::recreateSwapChain` 捕获后置位重试) |
| 呈现结果 | `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` | `Present` 返回 `0x887A0005`(OUT_OF_DATE 与 DEVICE_REMOVED 共用此值)/ `0x087A0001`(OCCLUDED 视为成功) |

### 13.5 命令录制对应

| 概念 | Vulkan | DX12 |
|---|---|---|
| 会话开始 / 结束 | `vkResetCommandBuffer` + `vkBeginCommandBuffer` / `vkEndCommandBuffer` | allocator + list `Reset` / `Close` |
| RenderPass 开始 | `vkCmdBeginRenderPass`(loadOp/clear 由 render pass 驱动) | 手工 `ensureState` + `OMSetRenderTargets` + 按 loadOp `ClearRenderTargetView/ClearDepthStencilView` |
| RenderPass 结束 | `vkCmdEndRenderPass`(隐式 finalLayout) | 空实现(D3D12 无此对象,语义合法)+ 按 `finalLayout` 显式迁移 |
| 顶点缓冲绑定 | `vkCmdBindVertexBuffers`(stride 来自绑定) | `IASetVertexBuffers`(stride 来自 PSO 的 input layout,`bindVertexBuffer` 查 pipeline 缓存的 stride) |
| 描述符绑定 | `vkCmdBindDescriptorSets` | `SetGraphics/ComputeRootDescriptorTable`(pipeline 缓存 `layout → rootParam` 映射) |
| Push constant | `vkCmdPushConstants` | `SetGraphics/ComputeRoot32BitConstants`(单 root 参数,offset 必须 0/4 对齐) |
| 间接绘制 | `vkCmdDrawIndexedIndirect` | **未实现**:显式抛错(需要 command signature,Phase 5 backlog);引擎当前零调用 |

### 13.6 Shader 内容管线对应

| 阶段 | Vulkan | DX12 |
|---|---|---|
| 源 | GLSL(`shaders/**.vert/frag/comp`) | 同一份 GLSL(单一源) |
| 编译 | `glslc → .spv`(输出 `bin/shaders/`) | `glslc → .spv → spirv-cross --hlsl --shader-model 60 [--flip-vert-y] → inject_hlsl.ps1(PC register)→ dxc → .dxil`(输出 `bin/shaders_dx12/`) |
| 全屏 pass | Vulkan NDC 约定顶点着色器,无需处理 | `--flip-vert-y`(仅 `deferred_lighting/ssr/ssao/ssao_blur/blit` 顶点) |
| 加载路径 | 直接 `shaders/X.spv` | `DX12RHIShader` 把 `shaders/X.spv` 转译为 `shaders_dx12/X.dxil`(优先),避免把 SPIR-V 当 DXIL 读 |
| 绑定校验 | Validation Layers(运行时) | D3D12 debug layer + **Debug-only DXIL↔root signature 反射断言**(dxcompiler.dll 动态加载;缺失时降级提示) |
| 采样器 | 随 descriptor set 绑定 | 独立 sampler heap;非 compare 采样器 `ComparisonFunc=NEVER` 避免告警 |

### 13.7 坐标系与约定

| 项 | Vulkan | DX12 |
|---|---|---|
| NDC Y 方向 | 向下 | 向上 |
| 投影矩阵 | `applyApiYFlip()` 翻转 `proj[1][1]`(`SceneRenderer.cpp`) | 不翻转(GLM 原生 Y-up 即 D3D 约定) |
| 全屏 pass 顶点 | `gl_Position = uv*2-1` 直接可用 | 需 `--flip-vert-y`(见 §13.6) |
| 屏幕 UV / 深度重建 | `ndc.xy*0.5+0.5` 直接可用 | `worldToScreen()` 里按 `projection[1][1]` 符号翻转 Y(water/ssr;`ssao.frag` 用 `abs()` 规避) |
| 纹理 V 坐标 / 正面剔除 | 与 D3D 约定一致 | 无需调整 |
| Depth range | GLM perspective NDC z ∈ [-1,1],viewport 映射到 [0,1] | 同上(两 API 默认 depth range 一致) |

### 13.8 已知不对称 / 不可直接映射

- **无 render pass 对象**:`endRenderPass` 是空实现 + `finalLayout` 迁移;`getNativeRenderPass()` 返回
  nullptr(ImGui 等按 RHI 抽象走)。
- **无二进制信号量**:单队列下 GPU-GPU 等待省略,`createSemaphore` 用 fence 值模拟。
- **只能绑定一个 shader-visible resource heap + 一个 sampler heap**:所有描述符(持久+临时)
  必须住在同一个堆内,靠 ring 子分配(§十二)。
- **READBACK heap 限制**:不能带 `ALLOW_UNORDERED_ACCESS`(`Storage|GPUToCPU` 组合非法),
  也不能被 GPU 读;`map()` 仅在 CPU 侧。
- **root constants 16B register 粒度**:push constant 小于 16B 时 HLSL cbuffer 会读满一个 register,
  需要补齐(SSAO compute 案例)。
- **spirv-cross SSBO → `RWByteAddressBuffer`**:UAV 必须 RAW 视图;`readonly` 会变成 SRV,与
  RHI 的 UAV 模型冲突(故 compute SSBO 不带 `readonly`)。
- **设备移除**:DXGI `0x887A0005` 同时表示 OUT_OF_DATE 与 DEVICE_REMOVED;后者需要重建
  `ID3D12Device` 与全部资源才能恢复(本轮只做了重试保护,不崩溃但不自动恢复)。
- **present/acquire 模型**:FLIP 模式 `GetCurrentBackBufferIndex` + per-image fence,与
  `vkAcquireNextImageKHR` 的信号量模型不对等(见 §13.4)。

