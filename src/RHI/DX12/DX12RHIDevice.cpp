#include "DX12RHIDevice.h"

DX12RHIDevice::DX12RHIDevice(GLFWwindow *window)
{

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

    




}

bool DX12RHIDevice::checkValidationLayerSupport()
{
    ComPtr<ID3D12Debug> debugController;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
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
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(& debugController)))){
        debugController->EnableDebugLayer();
        return true;
    }
    else
    {
        return false;
    }
}
