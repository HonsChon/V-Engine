## ADDED Requirements

### Requirement: 统一的 Buffer 创建接口
RHIDevice SHALL 提供 `createBuffer(RHIBufferDesc)` 方法创建 `RHIBuffer`。`RHIBufferDesc` SHALL 包含：size（字节大小）、usage（RHIBufferUsage 标志组合）、memoryUsage（GPU Only / CPU to GPU / GPU to CPU 等内存访问模式）。

#### Scenario: 创建 GPU 端 Vertex Buffer
- **WHEN** 创建 RHIBufferDesc 设置 size=vertices_size、usage=Vertex|TransferDst、memoryUsage=GPUOnly
- **THEN** SHALL 创建 RHIBuffer，Vulkan 后端分配 DEVICE_LOCAL 内存

#### Scenario: 创建可持久映射的 Uniform Buffer
- **WHEN** 创建 RHIBufferDesc 设置 size=sizeof(UBO)、usage=Uniform、memoryUsage=CPUToGPU
- **THEN** SHALL 创建 RHIBuffer，Vulkan 后端分配 HOST_VISIBLE | HOST_COHERENT 内存

#### Scenario: 创建 Storage Buffer
- **WHEN** 创建 RHIBufferDesc 设置 usage=Storage|TransferDst
- **THEN** SHALL 创建 RHIBuffer，可用于计算着色器

### Requirement: Buffer 数据上传
RHIBuffer SHALL 提供 `map()` / `unmap()` 方法用于 CPU 可见内存的映射访问。SHALL 额外提供 `uploadData(data, size)` 便捷方法，对于 CPU 可见 Buffer 直接 memcpy，对于 GPU Only Buffer 内部使用 staging buffer 完成上传。

#### Scenario: 持久映射 Uniform Buffer 更新
- **WHEN** 对 CPUToGPU 内存模式的 Buffer 调用 `map()`，获取指针后 memcpy 数据，然后 `unmap()`
- **THEN** 数据 SHALL 对 GPU 可见（HOST_COHERENT 模式无需显式 flush）

#### Scenario: 通过 staging 上传 Vertex 数据
- **WHEN** 对 GPUOnly 内存模式的 Buffer 调用 `uploadData(vertices.data(), vertices.size() * sizeof(Vertex))`
- **THEN** RHI 后端 SHALL 内部创建 staging buffer、复制数据、提交传输命令、等待完成

### Requirement: 统一的 Texture/Image 创建接口
RHIDevice SHALL 提供 `createTexture(RHITextureDesc)` 方法创建 `RHITexture`。`RHITextureDesc` SHALL 包含：width、height、format、usage（RHITextureUsage 标志组合）、mipLevels、arrayLayers、samples。

#### Scenario: 创建 Render Target 纹理
- **WHEN** 创建 RHITextureDesc 设置 width=1920、height=1080、format=R16G16B16A16_SFLOAT、usage=ColorAttachment|Sampled
- **THEN** SHALL 创建 RHITexture，Vulkan 后端创建 VkImage + VkDeviceMemory + VkImageView

#### Scenario: 创建深度缓冲
- **WHEN** 创建 RHITextureDesc 设置 format=D32_SFLOAT、usage=DepthStencilAttachment|Sampled
- **THEN** SHALL 创建 RHITexture，ImageView 使用 DEPTH aspect

#### Scenario: 从文件加载纹理
- **WHEN** 调用 `RHITexture::loadFromFile(device, "texture.png")` 或类似静态工厂方法
- **THEN** SHALL 加载图片数据、创建 RHITexture、通过 staging buffer 上传、创建 ImageView 和默认 Sampler

### Requirement: Sampler 创建
RHIDevice SHALL 提供 `createSampler(RHISamplerDesc)` 方法。`RHISamplerDesc` SHALL 包含：minFilter、magFilter、addressModeU/V/W、anisotropy、compareOp 等。

#### Scenario: 创建线性过滤 Sampler
- **WHEN** 创建 RHISamplerDesc 设置 Linear 过滤、Repeat 寻址模式、16x 各向异性
- **THEN** SHALL 创建 RHISampler，Vulkan 后端对应 VkSampler

#### Scenario: 创建 Nearest 过滤的 Sampler
- **WHEN** 创建 RHISamplerDesc 设置 Nearest 过滤、ClampToEdge
- **THEN** SHALL 创建适用于深度/整数格式纹理采样的 RHISampler

### Requirement: Image Layout Transition
RHICommandBuffer SHALL 提供 `transitionImageLayout(texture, oldLayout, newLayout)` 或等效机制，用于在渲染通道之间转换纹理的 layout。

#### Scenario: Shader Read 到 Color Attachment 转换
- **WHEN** 调用 `cmd->transitionImageLayout(texture, ShaderReadOnly, ColorAttachment)`
- **THEN** Vulkan 后端 SHALL 插入正确的 pipeline barrier 和 image memory barrier

### Requirement: 资源生命周期自动管理
RHIBuffer、RHITexture、RHISampler 对象销毁时 SHALL 自动调用析构函数清理底层原生资源（VkBuffer + VkDeviceMemory、VkImage + VkDeviceMemory + VkImageView、VkSampler）。

#### Scenario: Buffer 销毁
- **WHEN** RHIBuffer 的 unique_ptr 被释放
- **THEN** Vulkan 后端 SHALL 自动调用 vkDestroyBuffer + vkFreeMemory

#### Scenario: Texture 销毁
- **WHEN** RHITexture 的 unique_ptr 被释放
- **THEN** Vulkan 后端 SHALL 自动调用 vkDestroyImageView + vkDestroyImage + vkFreeMemory
