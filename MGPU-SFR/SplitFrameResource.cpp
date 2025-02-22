#include "SplitFrameResource.h"

#include "GCrossAdapterResource.h"
#include "GDevice.h"
#include "GDescriptor.h"

SplitFrameResource::SplitFrameResource(std::shared_ptr<GDevice>* devices, const UINT deviceCount, UINT passCount,
                                       UINT materialCount)
{
    for (UINT i = 0; i < deviceCount; ++i)
    {
        PassConstantUploadBuffers.push_back(
            std::make_shared<ConstantUploadBuffer<PassConstants>>(devices[i], passCount,
                                                                  devices[i]->GetName() + L" Forward Path Data"));
        SsaoConstantUploadBuffers.push_back(
            std::make_shared<ConstantUploadBuffer<SsaoConstants>>(devices[i], 1,
                                                                  devices[i]->GetName() + L" SSAO Path Data"));
        MaterialBuffers.push_back(
            std::make_shared<StructuredUploadBuffer<MaterialConstants>>(devices[i], materialCount,
                                                                        devices[i]->GetName() + L" Materials Data"));
        RenderTargetViewMemory.push_back(devices[i]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
    }
}

SplitFrameResource::~SplitFrameResource()
{
    PassConstantUploadBuffers.clear();
    SsaoConstantUploadBuffers.clear();
    MaterialBuffers.clear();
    CrossAdapterBackBuffer.reset();
    PrimeDeviceBackBuffer.Reset();
    RenderTargetViewMemory.clear();
}
