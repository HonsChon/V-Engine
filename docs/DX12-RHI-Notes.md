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
IDXGISwapChain3             VkSwapchainKHR              RHISwapChain
ID3D12Resource (Buffer)     VkBuffer + VkDeviceMemory   RHIBuffer
ID3D12Resource (Texture)    VkImage + VkImageView       RHITexture
ID3D12DescriptorHeap        VkDescriptorPool            RHIBindingGroup/Layout
ID3D12PipelineState         VkPipeline                  RHIPipeline
ID3D12RootSignature         VkPipelineLayout            RHIBindingLayout
```

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
