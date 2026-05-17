#include "DX12RHIDevice.h"
#include "DX12RHIBuffer.h"

#include <iostream>
#include <stdexcept>

DX12RHIDevice::DX12RHIDevice(GLFWwindow *window)
    : window(window)
{
    createDevice();
    createCommandQueue();
    createFenceEvent();

    std::cout << "[DX12RHIDevice] Initialized successfully.\n";
}

DX12RHIDevice::~DX12RHIDevice()
{
    // 1. Wait for all GPU work to complete (equivalent to vkDeviceWaitIdle)
    waitForGPU();

    // 2. Close the fence event handle
    if (fenceEvent) {
        CloseHandle(fenceEvent);
        fenceEvent = nullptr;
    }

    std::cout << "[DX12RHIDevice] Destroyed.\n";
}

std::shared_ptr<RHIBuffer> DX12RHIDevice::createBuffer(const RHIBufferDesc &desc)
{
    return std::make_shared<DX12RHIBuffer(this, desc);
}

void DX12RHIDevice::createDevice()
{
    if (enableValidationLayers_ && !checkValidationLayerSupport()){
        throw std::runtime_error("Validation layers requested but not available!");
    }

    // open debug layer
    if (enableValidationLayers_){ 
        enableDebugLayer();
    }

    // create factory
    uint factoryFlags = 0;
    
#ifdef NDEBUG
    factoryFlags = 0;
#else
    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)))){
        throw std::runtime_error("failed to create DXGI factory!");
    }

    GetHardwareAdapter(factory.Get(), &adapter);
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))))
    {
        throw std::runtime_error("failed to create D3D12 device!");
    }
}

void DX12RHIDevice::createCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue))))
    {
        throw std::runtime_error("failed to create command queue!");
    }

    // Fence is created together with the queue (following nvrhi pattern)
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        throw std::runtime_error("failed to create fence!");
    }
    fenceValue = 0;
}

void DX12RHIDevice::createFenceEvent()
{
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent){
        throw std::runtime_error("failed to create fence event!");
    }
}

bool DX12RHIDevice::checkValidationLayerSupport()
{
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        return true;
    }
    else
    {
        return false;
    }
    
}

bool DX12RHIDevice::enableDebugLayer()
{
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(& debugController)))){
        debugController->EnableDebugLayer();
        return true;
    }
    else
    {
        return false;
    }
}

void DX12RHIDevice::waitForGPU()
{
    if (!commandQueue || !fence) return;

    // signal the fence with the next value
    const uint64_t waitValue = ++fenceValue;
    commandQueue->Signal(fence.Get(), waitValue);

    // if the GPU has not reached this fence value yet, wait for it
    if (fence->GetCompletedValue() < waitValue)
    {
        fence->SetEventOnCompletion(waitValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }

}
