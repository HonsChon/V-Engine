## 1. RHI 类型系统与基础设施

- [x] 1.1 创建 `src/RHI/RHITypes.h`：定义所有 RHI 枚举（RHIFormat、RHIBufferUsage、RHITextureUsage、RHIShaderStage、RHICompareOp、RHICullMode、RHILoadOp、RHIStoreOp、RHIIndexType、RHIPolygonMode、RHIVertexInputRate、RHIPipelineType、RHIDescriptorType、RHIMemoryUsage、RHIImageLayout、RHIFilter、RHIAddressMode、RHIBackend 等），以及 RHIClearValue、RHIPushConstantRange 等通用结构体
- [x] 1.2 为 RHIBufferUsage、RHITextureUsage、RHIShaderStage 等标志位枚举实现按位操作符重载（`|`、`&`、`~`）
- [x] 1.3 创建 `src/RHI/Vulkan/` 目录，创建 `VulkanTypeConversions.h/cpp`：实现 `toVkFormat()`、`toVkShaderStage()`、`toVkBufferUsage()` 等所有 RHI 枚举到 Vulkan 原生类型的转换函数

## 2. RHI 核心抽象接口

- [x] 2.1 创建 `src/RHI/RHIBuffer.h`：定义 RHIBufferDesc 和 RHIBuffer 纯虚基类（map/unmap/uploadData/getSize 接口）
- [x] 2.2 创建 `src/RHI/RHITexture.h`：定义 RHITextureDesc 和 RHITexture 纯虚基类（getWidth/getHeight/getFormat 接口）
- [x] 2.3 创建 `src/RHI/RHISampler.h`：定义 RHISamplerDesc 和 RHISampler 纯虚基类
- [x] 2.4 创建 `src/RHI/RHIShader.h`：定义 RHIShader 纯虚基类（getStage 接口）
- [x] 2.5 创建 `src/RHI/RHIDescriptor.h`：定义 RHIBindingLayoutDesc、RHIBindingLayout、RHIBindingGroupDesc、RHIBindingGroup 纯虚基类
- [x] 2.6 创建 `src/RHI/RHIPipeline.h`：定义 RHIPipeline 纯虚基类、RHIGraphicsPipelineBuilder 和 RHIComputePipelineBuilder 纯虚基类（链式 API 接口）
- [x] 2.7 创建 `src/RHI/RHIRenderPass.h`：定义 RHIRenderPassDesc、RHIRenderPass、RHIFramebufferDesc、RHIFramebuffer 纯虚基类
- [x] 2.8 创建 `src/RHI/RHICommandBuffer.h`：定义 RHICommandBuffer 纯虚基类（beginRenderPass/endRenderPass、bind、draw、dispatch、barrier 等全部录制命令）
- [x] 2.9 创建 `src/RHI/RHISwapChain.h`：定义 RHISwapChain 纯虚基类（acquireNextImage/getFormat/getExtent/getFramebuffer 接口）
- [x] 2.10 创建 `src/RHI/RHIDevice.h`：定义 RHIDevice 纯虚基类（所有资源创建工厂方法：createBuffer、createTexture、createSampler、createShader、createBindingLayout、createBindingGroup、createGraphicsPipelineBuilder、createComputePipelineBuilder、createRenderPass、createFramebuffer 等）
- [x] 2.11 创建 `src/RHI/RHI.h`：聚合头文件 + `RHI::CreateDevice(RHIBackend, GLFWwindow*)` 工厂函数声明

## 3. Vulkan 后端 — 资源层实现

- [x] 3.1 创建 `src/RHI/Vulkan/VulkanRHIDevice.h/cpp`：实现 RHIDevice，封装现有 VulkanDevice 的 instance/device/queue/commandPool 创建逻辑，实现所有工厂方法
- [x] 3.2 创建 `src/RHI/Vulkan/VulkanRHIBuffer.h/cpp`：实现 RHIBuffer，封装 VkBuffer + VkDeviceMemory 的创建、映射、上传（含 staging buffer）逻辑
- [x] 3.3 创建 `src/RHI/Vulkan/VulkanRHITexture.h/cpp`：实现 RHITexture，封装 VkImage + VkDeviceMemory + VkImageView 的创建，支持 render target 和文件加载两种模式
- [x] 3.4 创建 `src/RHI/Vulkan/VulkanRHISampler.h/cpp`：实现 RHISampler，封装 VkSampler 创建
- [x] 3.5 创建 `src/RHI/Vulkan/VulkanRHIShader.h/cpp`：实现 RHIShader，封装 SPIR-V 文件读取 + VkShaderModule 创建

## 4. Vulkan 后端 — Descriptor 层实现

- [x] 4.1 创建 `src/RHI/Vulkan/VulkanRHIDescriptor.h/cpp`：实现 RHIBindingLayout（封装 VkDescriptorSetLayout）和 RHIBindingGroup（封装 VkDescriptorSet + 资源绑定更新）
- [x] 4.2 在 VulkanRHIDevice 中实现全局 Descriptor Pool 自动管理器：自动创建/扩容 VkDescriptorPool，对上层透明

## 5. Vulkan 后端 — Pipeline 层实现

- [x] 5.1 创建 `src/RHI/Vulkan/VulkanRHIPipeline.h/cpp`：实现 RHIPipeline（封装 VkPipeline + VkPipelineLayout）
- [x] 5.2 实现 VulkanGraphicsPipelineBuilder：链式 API，将 Builder 配置转换为 VkGraphicsPipelineCreateInfo 并创建管线，内含 shader 文件加载
- [x] 5.3 实现 VulkanComputePipelineBuilder：链式 API，将 Builder 配置转换为 VkComputePipelineCreateInfo 并创建管线

## 6. Vulkan 后端 — RenderPass 与 CommandBuffer 实现

- [x] 6.1 创建 `src/RHI/Vulkan/VulkanRHIRenderPass.h/cpp`：实现 RHIRenderPass（封装 VkRenderPass + 自动 subpass 依赖管理）和 RHIFramebuffer（封装 VkFramebuffer）
- [x] 6.2 创建 `src/RHI/Vulkan/VulkanRHICommandBuffer.h/cpp`：实现 RHICommandBuffer 所有命令，将 RHI 调用转换为 vkCmd* 原生调用
- [x] 6.3 创建 `src/RHI/Vulkan/VulkanRHISwapChain.h/cpp`：实现 RHISwapChain，封装现有 VulkanSwapChain 逻辑

## 7. RHI 工厂与构建系统

- [x] 7.1 实现 `src/RHI/RHI.cpp`：`RHI::CreateDevice()` 工厂函数，根据 RHIBackend 参数选择创建 VulkanRHIDevice（DX12 暂抛异常）
- [x] 7.2 更新 `CMakeLists.txt`：添加 `src/RHI/Vulkan/` 目录下所有新文件到编译目标，确保编译通过
- [x] 7.3 确保所有 RHI 抽象头文件（`src/RHI/*.h`）不包含任何 Vulkan/DX12 头文件

## 8. Phase 1 验证 — 迁移 ForwardPass

- [x] 8.1 重构 `RenderPassBase.h`：将 `VkCommandBuffer` 参数类型改为 `RHICommandBuffer*`（或提供兼容层）
- [x] 8.2 重构 `ForwardPass.h/cpp`：移除所有 `vkCreate*` / `vkDestroy*` / `vkCmd*` 调用，改用 RHI 接口（RHIBindingLayout、RHIBindingGroup、Pipeline Builder、RHIBuffer 等）
- [x] 8.3 编译并运行，验证 ForwardPass 渲染结果与重构前一致

## 9. Phase 2 — 迁移 GBufferPass + LightingPass

- [x] 9.1 重构 `GBufferPass.h/cpp`：使用 RHI 接口替代原生 Vulkan 调用（createAttachments → createTexture、createRenderPass → RHI createRenderPass、createPipeline → Pipeline Builder）
- [x] 9.2 重构 `LightingPass.h/cpp`：使用 RHI 接口替代原生 Vulkan 调用
- [x] 9.3 编译运行，验证 Deferred Shading 管线（GBuffer → Lighting）渲染结果正确

## 10. Phase 3 — 迁移其余 Pass

- [x] 10.1 重构 `SSAOPass.h/cpp`：构造函数接受 RHIDevice*，内部暂保留原生 Vulkan（4阶段极复杂，需 Image2DArray 支持，留作独立 change 完全迁移）
- [x] 10.2 重构 `SSRPass.h/cpp`：使用 RHI 接口
- [x] 10.3 重构 `WaterPass.h/cpp`：使用 RHI 接口
- [x] 10.4 重构 `NaniteDebugPass.h/cpp`：使用 RHI 接口
- [x] 10.5 重构 `ComputePassBase.h/cpp`：基类改用 RHIDevice，移除 descriptorPool 管理（由 RHI 统一管理）
- [x] 10.6 重构 `FrustumCullingPass.h/cpp`：构造函数接受 RHIDevice*，内部暂保留旧 ComputePipeline（渐进迁移）
- [x] 10.7 重构 `ClusterCullingPass.h/cpp`：构造函数接受 RHIDevice*，内部暂保留旧 ComputePipeline（渐进迁移）
- [ ] 10.8 编译运行，全面回归测试所有渲染效果

## 10b. Phase 3b — Pure RHI 深度迁移（移除所有 Vulkan 后端头文件）

- [x] 10b.1 `NaniteDebugPass.h/cpp`：移除所有 VulkanRHI*.h 头文件和 static_cast，vkCmd 全部转为 RHICommandBuffer 方法
- [x] 10b.2 `ComputePassBase.h/cpp`：移除 insertMemoryBarrier，insertBufferBarrier 改用 RHICommandBuffer::bufferBarrier
- [x] 10b.3 `FrustumCullingPass.h/cpp`：完全纯 RHI 重写，使用 RHIBuffer/RHIBindingGroup/RHI Compute Pipeline Builder，readback 改用 RHIBuffer::map/unmap
- [x] 10b.4 `ClusterCullingPass.h/cpp`：完全纯 RHI 重写，包括双缓冲 readback 系统和外部 buffer 依赖
- [x] 10b.5 `GPUDrivenRenderer.h/cpp`：纯 RHI 封装，构造函数接受 RHIDevice*，返回 RHIBuffer* 而非 VkBuffer
- [x] 10b.6 `NaniteManager.cpp`：RHI/Native 适配器模式，m_clusterDataBufferRHI 转为 RHIBuffer，命令录制通过 wrapCommandBuffer 桥接
- [x] 10b.7 `ForwardPass.h/cpp`：二次重写，彻底移除所有 static_cast 和 VulkanRHI*.h 头文件，材质更新改用 RHITexture*/RHISampler*
- [x] 10b.8 `GBufferPass.h`：纯 RHI 头文件重写，所有 accessor 返回 RHITexture*/RHIRenderPass*，渲染命令接受 RHICommandBuffer*，材质 API 改用 RHITexture*/RHISampler*
- [x] 10b.9 `GBufferPass.cpp`：移除 Vulkan 后端 includes，材质更新改用 BindingGroup::updateTexture，渲染命令全部改用 RHICommandBuffer 方法
- [x] 10b.10 `SSAOPass.h/cpp`：完全纯 RHI 重写（最大的剩余 Pass）

## 11. 上层集成与清理

- [x] 11.1 重构 `SceneRenderer.h/cpp`：移除剩余 vkCmd 调用，全部使用 RHICommandBuffer 录制（GBuffer + Forward 部分完成，SSAO/Blit 等保留 Vulkan 兼容）
- [x] 11.2 重构 `Engine.h/cpp`：使用 `RHI::CreateDevice()` 创建设备，替代直接 new VulkanDevice
- [x] 11.3 更新 `ImGuiLayer`：通过原生 Handle 后门获取 VkRenderPass / VkCommandBuffer 等，确保 ImGui 正常工作
- [x] 11.4 移除或归档旧的 `src/RHI/VulkanPipeline.h/cpp` 和 `src/RHI/ComputePipeline.h/cpp`（已被新 Pipeline Builder 替代）
- [ ] 11.5 最终编译、运行、全面验证所有功能（前向渲染、延迟渲染、SSAO、SSR、水面、Nanite 调试、GPU 剔除）
