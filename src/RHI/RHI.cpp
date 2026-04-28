#include "RHI.h"
#include "Vulkan/VulkanRHIDevice.h"
#include <stdexcept>

namespace RHI {

std::unique_ptr<RHIDevice> CreateDevice(RHIBackend backend, GLFWwindow* window) {
    switch (backend) {
        case RHIBackend::Vulkan:
            return std::make_unique<VulkanRHIDevice>(window);
        case RHIBackend::DX12:
            throw std::runtime_error("[RHI] DX12 backend is not yet implemented.");
        default:
            throw std::runtime_error("[RHI] Unknown backend specified.");
    }
}

} // namespace RHI
