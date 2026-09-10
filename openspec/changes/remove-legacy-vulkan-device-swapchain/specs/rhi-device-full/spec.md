## ADDED Requirements

### Requirement: RHIDevice provides format query
The RHIDevice interface SHALL provide `findSupportedFormat()` and `findDepthFormat()` methods to query the best-supported format from a candidate list, replacing direct Vulkan calls in VulkanDevice.

#### Scenario: Query depth format
- **WHEN** caller invokes `findDepthFormat()` on RHIDevice
- **THEN** the device returns the best available depth format (D32_SFLOAT > D32_SFLOAT_S8_UINT > D24_UNORM_S8_UINT)

#### Scenario: Query supported format with tiling and features
- **WHEN** caller invokes `findSupportedFormat(candidates, tiling, features)`
- **THEN** the device returns the first candidate format that supports the requested tiling and feature flags

### Requirement: RHIDevice provides raw buffer and image creation
The RHIDevice interface SHALL provide `createRawBuffer()` and `createRawImage()` methods that create native GPU resources and return handles via output parameters (void* for portability).

#### Scenario: Create a raw buffer
- **WHEN** caller invokes `createRawBuffer(size, usage, memoryProperties, &bufferHandle, &memoryHandle)`
- **THEN** the device allocates a GPU buffer and bound memory, returning native handles

#### Scenario: Create a raw image
- **WHEN** caller invokes `createRawImage(width, height, format, tiling, usage, memoryProperties, &imageHandle, &memoryHandle)`
- **THEN** the device allocates a GPU image and bound memory, returning native handles

### Requirement: RHIDevice provides buffer copy
The RHIDevice interface SHALL provide `copyBuffer(src, dst, size)` to copy data between buffers using a single-time command.

#### Scenario: Copy staging buffer to device-local buffer
- **WHEN** caller invokes `copyBuffer(srcHandle, dstHandle, byteSize)`
- **THEN** the device submits a transfer command to copy the specified byte range

### Requirement: RHIDevice provides single-time command helpers
The RHIDevice interface SHALL provide `beginSingleTimeCommands()` and `endSingleTimeCommands(cmd)` for one-shot GPU operations.

#### Scenario: Execute a single-time command
- **WHEN** caller invokes `beginSingleTimeCommands()`
- **THEN** the device returns a native command buffer handle (void*) ready for recording
- **WHEN** caller invokes `endSingleTimeCommands(cmd)`
- **THEN** the device submits and waits for the command, then frees the command buffer

### Requirement: RHIDevice provides debug label API
The RHIDevice interface SHALL provide `beginDebugLabel()`, `endDebugLabel()`, and `insertDebugLabel()` for RenderDoc-style GPU debugging markers.

#### Scenario: Annotate a render pass in RenderDoc
- **WHEN** caller invokes `beginDebugLabel(cmd, "Shadow Pass", r, g, b, a)` before recording and `endDebugLabel(cmd)` after
- **THEN** RenderDoc displays the commands within a labeled, colored group

### Requirement: RHIDevice provides sync object management
The RHIDevice interface SHALL provide methods to create, destroy, wait, and reset semaphores and fences.

#### Scenario: Create and use frame sync objects
- **WHEN** caller invokes `createSemaphore()` and `createFence(signaled=true)`
- **THEN** the device returns native handles (void*) for the created sync objects
- **WHEN** caller invokes `waitForFence(fenceHandle)` then `resetFence(fenceHandle)`
- **THEN** the device blocks until the fence is signaled, then resets it

#### Scenario: Destroy sync objects
- **WHEN** caller invokes `destroySemaphore(handle)` or `destroyFence(handle)`
- **THEN** the device releases the native sync resource

### Requirement: RHIDevice provides command buffer allocation
The RHIDevice interface SHALL provide `allocateCommandBuffers(count)` returning a vector of native command buffer handles.

#### Scenario: Allocate frame command buffers
- **WHEN** caller invokes `allocateCommandBuffers(2)`
- **THEN** the device returns 2 native command buffer handles from its command pool

### Requirement: RHIDevice provides graphics queue submission
The RHIDevice interface SHALL provide `submitGraphicsQueue()` accepting wait/signal semaphores, command buffers, and a fence.

#### Scenario: Submit draw commands
- **WHEN** caller invokes `submitGraphicsQueue(waitSemaphores, waitStages, cmdBuffers, signalSemaphores, fence)`
- **THEN** the device submits the command buffers to the graphics queue with proper synchronization

### Requirement: RHIDevice provides native handle access
The RHIDevice interface SHALL expose `getNativeDevice()`, `getNativeInstance()`, `getNativePhysicalDevice()`, `getGraphicsQueueFamilyIndex()`, and `getNativeGraphicsQueue()` for ImGui and other low-level integrations.

#### Scenario: Initialize ImGui with native handles
- **WHEN** caller requests native handles via `getNativeDevice()` etc.
- **THEN** the device returns correct `void*` casted native API handles

### Requirement: RHIDevice provides SwapChain factory
The RHIDevice interface SHALL provide `createSwapChain(width, height)` returning a unique_ptr<RHISwapChain>.

#### Scenario: Create swap chain from device
- **WHEN** caller invokes `createSwapChain(1920, 1080)`
- **THEN** the device creates and returns a fully initialized RHISwapChain

### Requirement: All Passes SHALL only depend on RHIDevice
All RenderPassBase, ComputePassBase, and their subclasses SHALL only hold `RHIDevice*` pointers. They MUST NOT include or reference any `VulkanRHI*` headers or concrete types.

#### Scenario: ForwardPass construction
- **WHEN** ForwardPass is constructed with `RHIDevice*`
- **THEN** it stores only the abstract pointer and accesses GPU resources exclusively through RHIDevice methods

#### Scenario: Compile-time enforcement
- **WHEN** a Pass includes a VulkanRHI-specific header
- **THEN** the build SHALL fail (enforced by code review / include guard convention)
