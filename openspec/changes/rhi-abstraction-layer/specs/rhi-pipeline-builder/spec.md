## ADDED Requirements

### Requirement: Graphics Pipeline Builder 链式 API
RHIDevice SHALL 提供 `createGraphicsPipelineBuilder()` 方法返回 `RHIGraphicsPipelineBuilder` 对象。Builder SHALL 支持链式调用（fluent API）配置以下内容：着色器（Vertex/Fragment）、顶点输入布局、输入装配拓扑、光栅化状态（多边形模式、剔除模式、正面方向）、深度/模板测试、多重采样、颜色混合、动态状态、绑定布局、Push Constant 范围、RenderPass 引用。最终调用 `build()` 返回 `std::unique_ptr<RHIPipeline>`。

#### Scenario: 构建标准 PBR 图形管线
- **WHEN** 通过 Builder 设置 vertex/fragment shader、添加 Position/Normal/TexCoord/Tangent 顶点属性、启用深度测试、设置 Back Cull、关联 2 个绑定布局和 1 个 Push Constant 范围、设置 RenderPass，然后调用 `build()`
- **THEN** SHALL 返回有效的 RHIPipeline，Vulkan 后端内部创建了 VkPipelineLayout + VkGraphicsPipeline

#### Scenario: 构建全屏后处理管线
- **WHEN** 通过 Builder 设置 vertex/fragment shader、无顶点输入、禁用深度测试、禁用 Cull、1 个绑定布局，然后 `build()`
- **THEN** SHALL 返回有效的 RHIPipeline，适用于全屏四边形渲染

#### Scenario: 构建线框模式管线
- **WHEN** 设置 `setPolygonMode(RHIPolygonMode::Line)` 和 `setLineWidth(1.0f)`
- **THEN** SHALL 构建出线框渲染管线

### Requirement: Compute Pipeline Builder
RHIDevice SHALL 提供 `createComputePipelineBuilder()` 方法。Builder SHALL 支持设置计算着色器、绑定布局、Push Constant 范围，然后 `build()` 返回 `std::unique_ptr<RHIPipeline>`。

#### Scenario: 构建 Frustum Culling 计算管线
- **WHEN** 设置 compute shader 路径、添加包含多个 Storage Buffer 的绑定布局、设置 Push Constant 范围，然后 `build()`
- **THEN** SHALL 返回有效的 RHIPipeline，可用于 `cmd->bindComputePipeline()` 和 `cmd->dispatch()`

### Requirement: 统一的 Shader 加载
Builder 的 `setVertexShader()` / `setFragmentShader()` / `setComputeShader()` SHALL 接受 shader 文件路径（SPIR-V .spv 文件），在内部完成文件读取和 ShaderModule 创建。上层代码 SHALL NOT 需要自行实现 `readFile()` 或 `createShaderModule()`。

#### Scenario: 从 .spv 文件加载 shader
- **WHEN** 调用 `builder.setVertexShader("shaders/pbr_vert.spv")`
- **THEN** Builder 内部 SHALL 读取文件、创建 RHIShader 对象，Pass 无需任何文件 I/O 代码

#### Scenario: shader 文件不存在
- **WHEN** 传入不存在的 shader 文件路径
- **THEN** SHALL 抛出异常或返回错误，包含清晰的文件路径信息

### Requirement: 顶点输入布局声明
Builder SHALL 提供 `addVertexBinding(binding, stride, inputRate)` 和 `addVertexAttribute(binding, location, format, offset)` 方法声明顶点输入布局，替代手动填写 `VkVertexInputBindingDescription` 和 `VkVertexInputAttributeDescription`。

#### Scenario: 声明带 Position/Normal/TexCoord/Tangent 的顶点布局
- **WHEN** 调用 `addVertexBinding(0, sizeof(Vertex), PerVertex)` 然后依次 `addVertexAttribute(0, 0, R32G32B32_SFLOAT, 0)` / `addVertexAttribute(0, 1, R32G32B32_SFLOAT, 12)` / `addVertexAttribute(0, 2, R32G32_SFLOAT, 24)` / `addVertexAttribute(0, 3, R32G32B32_SFLOAT, 32)`
- **THEN** SHALL 正确描述 4 个顶点属性的布局

### Requirement: Pipeline 对象接口
`RHIPipeline` SHALL 提供 `getType()` 方法区分 Graphics/Compute 管线类型。Pipeline 对象的生命周期由持有者（Pass）管理，销毁时 SHALL 自动清理底层原生管线资源。

#### Scenario: 销毁 Pipeline
- **WHEN** `RHIPipeline` 的 unique_ptr 被释放
- **THEN** Vulkan 后端 SHALL 自动调用 `vkDestroyPipeline` 和 `vkDestroyPipelineLayout` 清理资源
