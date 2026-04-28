## ADDED Requirements

### Requirement: 声明式绑定布局创建
系统 SHALL 提供 `RHIBindingLayoutDesc` 描述结构，通过 `addBinding(bindingIndex, descriptorType, shaderStage, count)` 声明式地定义一个绑定布局中的所有绑定项。RHIDevice SHALL 通过 `createBindingLayout(desc)` 创建 `RHIBindingLayout` 对象。

#### Scenario: 创建包含 UBO 和贴图的绑定布局
- **WHEN** 构建一个 RHIBindingLayoutDesc，添加 binding 0 为 UniformBuffer（VertexFragment 阶段）、binding 1 为 CombinedImageSampler（Fragment 阶段），然后调用 `device->createBindingLayout(desc)`
- **THEN** SHALL 成功创建 RHIBindingLayout，Vulkan 后端内部对应一个 VkDescriptorSetLayout

#### Scenario: 创建仅含 Storage Buffer 的计算绑定布局
- **WHEN** 构建一个 RHIBindingLayoutDesc，添加多个 StorageBuffer binding（Compute 阶段），然后创建
- **THEN** SHALL 成功创建适用于计算管线的 RHIBindingLayout

### Requirement: 绑定组创建与资源绑定
系统 SHALL 提供 `RHIBindingGroupDesc`，通过 `setBuffer(binding, buffer, offset, range)` 和 `setTexture(binding, texture, sampler)` 绑定实际资源。RHIDevice SHALL 通过 `createBindingGroup(layout, desc)` 创建 `RHIBindingGroup` 对象。

#### Scenario: 绑定 UBO 和纹理到绑定组
- **WHEN** 创建 RHIBindingGroupDesc，设置 binding 0 为一个 RHIBuffer（Uniform），binding 1 为一个 RHITexture + RHISampler，然后调用 `device->createBindingGroup(layout, desc)`
- **THEN** SHALL 成功创建 RHIBindingGroup，Vulkan 后端内部对应一个已更新的 VkDescriptorSet

#### Scenario: 更新绑定组的资源
- **WHEN** 需要替换绑定组中某个 binding 指向的纹理
- **THEN** SHALL 支持通过 `updateBindingGroup(group, desc)` 或重新创建绑定组来实现资源替换

### Requirement: 全局 Descriptor Pool 自动管理
Vulkan 后端 SHALL 内部维护 Descriptor Pool 的自动分配和扩容。上层代码（Pass）在创建 BindingGroup 时 SHALL NOT 需要手动创建或管理 VkDescriptorPool。

#### Scenario: Pool 容量不足时自动扩容
- **WHEN** 连续创建大量 BindingGroup 导致当前 Pool 容量不足
- **THEN** Vulkan 后端 SHALL 自动创建新的 VkDescriptorPool 并从中分配，对上层透明

#### Scenario: 释放绑定组
- **WHEN** RHIBindingGroup 对象被销毁
- **THEN** 底层 Descriptor 资源 SHALL 被正确回收（释放回 Pool 或标记为可复用）

### Requirement: 在 CommandBuffer 中设置绑定组
`RHICommandBuffer::setBindingGroup(uint32_t setIndex, RHIBindingGroup* group)` SHALL 将指定的绑定组绑定到管线的指定 set 位置。

#### Scenario: 绑定多个 set
- **WHEN** 依次调用 `cmd->setBindingGroup(0, globalGroup)` 和 `cmd->setBindingGroup(1, materialGroup)` 
- **THEN** 后端 SHALL 将 set 0 绑定全局资源、set 1 绑定材质资源，对应 Vulkan 的 `vkCmdBindDescriptorSets` 或 DX12 的 `SetGraphicsRootDescriptorTable`

### Requirement: 绑定布局与 DX12 Root Signature 兼容
RHIBindingLayout 的设计 SHALL 兼容 DX12 的 Root Signature / Root Parameter 模型。每个 RHIBindingLayout 对应一个 "set"，多个 layout 组合后可映射为完整的 Root Signature。

#### Scenario: 多 set 组合
- **WHEN** Pipeline 使用 2 个 RHIBindingLayout（set 0: global, set 1: per-material）
- **THEN** 在 Vulkan 后端映射为 VkPipelineLayout 的多个 DescriptorSetLayout；在未来 DX12 后端可映射为 Root Signature 的多个 Root Parameter
