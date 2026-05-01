## ADDED Requirements

### Requirement: RHISwapChain provides image acquisition
The RHISwapChain interface SHALL provide `acquireNextImage(signalSemaphore, &imageIndex)` returning a result enum indicating success, out-of-date, or error.

#### Scenario: Successful image acquisition
- **WHEN** caller invokes `acquireNextImage(semaphoreHandle, &index)`
- **THEN** the swap chain returns `RHISwapChainResult::Success` and sets `index` to the next available image

#### Scenario: Swap chain out of date
- **WHEN** the window has been resized and caller invokes `acquireNextImage()`
- **THEN** the swap chain returns `RHISwapChainResult::OutOfDate`

### Requirement: RHISwapChain provides presentation
The RHISwapChain interface SHALL provide `present(waitSemaphore, imageIndex)` to queue the rendered image for display.

#### Scenario: Successful presentation
- **WHEN** caller invokes `present(semaphoreHandle, imageIndex)` after rendering
- **THEN** the swap chain queues the image for presentation and returns `RHISwapChainResult::Success`

#### Scenario: Presentation triggers out-of-date
- **WHEN** the surface becomes invalid during present
- **THEN** the swap chain returns `RHISwapChainResult::OutOfDate` or `RHISwapChainResult::Suboptimal`

### Requirement: RHISwapChain provides recreation
The RHISwapChain interface SHALL provide `recreate(width, height)` to rebuild the swap chain after resize.

#### Scenario: Recreate after window resize
- **WHEN** caller invokes `recreate(newWidth, newHeight)`
- **THEN** the swap chain destroys old resources and creates new ones with the specified dimensions
- **THEN** `getExtent()` returns the new dimensions
- **THEN** `getImageCount()` reflects the new image count

### Requirement: RHISwapChain provides native render pass access
The RHISwapChain interface SHALL provide `getNativeRenderPass()` returning the native render pass handle as `void*`.

#### Scenario: ImGui uses native render pass
- **WHEN** ImGuiLayer needs the swap chain render pass for initialization or resize
- **THEN** it obtains the handle via `swapChain->getNativeRenderPass()` cast to the backend-specific type

### Requirement: RHISwapChain provides native framebuffer access
The RHISwapChain interface SHALL provide `getNativeFramebuffer(imageIndex)` returning the native framebuffer handle for command recording.

#### Scenario: Begin render pass with framebuffer
- **WHEN** Engine records commands for image index N
- **THEN** it obtains the framebuffer via `swapChain->getNativeFramebuffer(N)` and passes it to the render pass begin info
