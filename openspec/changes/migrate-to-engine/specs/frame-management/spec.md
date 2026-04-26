## ADDED Requirements

### Requirement: 帧同步对象管理
Engine SHALL 创建和管理 MAX_FRAMES_IN_FLIGHT (=2) 套同步对象：imageAvailableSemaphore、renderFinishedSemaphore、inFlightFence，以及 per-swapchain-image 的 imagesInFlight 数组。

#### Scenario: 同步对象创建
- **WHEN** Engine 初始化 FrameResources
- **THEN** 创建 2 套 semaphore + fence，imagesInFlight 大小等于 swapchain image count

### Requirement: drawFrame 帧提交流程
Engine SHALL 实现完整的帧提交流程：等待 fence → acquire image → 更新 uniform → reset fence → 录制命令 → queue submit → present。当 swapchain 过期时自动重建。

#### Scenario: 正常帧提交
- **WHEN** drawFrame() 被调用
- **THEN** 等待当前帧 fence，获取下一张 image，录制命令缓冲，提交到 graphics queue，present 到 present queue

#### Scenario: Swapchain 过期
- **WHEN** vkAcquireNextImageKHR 或 vkQueuePresentKHR 返回 OUT_OF_DATE
- **THEN** 调用 recreateSwapChain()，当前帧跳过

### Requirement: Command Buffer 管理
Engine SHALL 分配 MAX_FRAMES_IN_FLIGHT 个主命令缓冲区，每帧重置并重新录制。

#### Scenario: 命令缓冲录制
- **WHEN** 帧开始录制
- **THEN** reset 当前帧的 command buffer，调用 SceneRenderer::recordCommands() 录制渲染命令
