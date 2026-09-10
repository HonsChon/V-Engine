#include "RHI.h"
#include "Vulkan/VulkanRHIDevice.h"

#if defined(_WIN32)
#include "DX12/DX12RHIDevice.h"
#endif

#include <stdexcept>

namespace RHI {

std::unique_ptr<RHIDevice> CreateDevice(RHIBackend backend, GLFWwindow* window) {
    switch (backend) {
        case RHIBackend::Vulkan:
            return std::make_unique<VulkanRHIDevice>(window);
#if defined(_WIN32)
        case RHIBackend::DX12:
            return std::make_unique<DX12RHIDevice>(window);
#endif
        default:
            throw std::runtime_error("[RHI] Unsupported backend requested.");
    }
}

} // namespace RHI
