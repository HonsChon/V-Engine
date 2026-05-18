#include "DX12RHIBuffer.h"

DX12RHIBuffer::DX12RHIBuffer(DirectXRHIDevice *device, RHIBuffre &desc)
{
    
}

void *DX12RHIBuffer::map()
{
    if (mapped_)
    {
        return;
    }
}