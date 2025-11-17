#include "pch.h"
#include "GPUPrefixSum.h"

using namespace PEPEngine;
using namespace PEPEngine::Graphics;

void GPUPrefixSum::Initialize(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device, uint32_t count)
{
    m_Device = device;

    m_RootSignature.AddConstantParameter(1, 0);
    
    CD3DX12_DESCRIPTOR_RANGE range[2];
    range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
    m_RootSignature.AddDescriptorParameter(&range[0], 1);
    m_RootSignature.AddDescriptorParameter(&range[1], 1);

    m_RootSignature.Initialize(device);

    CompileShaders();

    m_BlockScanPSO.SetRootSignature(m_RootSignature);
    m_BlockScanPSO.SetShader(m_BlockScanShader.get());
    m_BlockScanPSO.Initialize(device);
    
    m_BlockCombinePSO.SetRootSignature(m_RootSignature);
    m_BlockCombinePSO.SetShader(m_BlockCombineShader.get());
    m_BlockCombinePSO.Initialize(device);

    int numGroups = count;
    int offsets = 0;
    while (true)
    {
        m_FreeBuffersOffsets.insert({numGroups, offsets});
        std::wstring bufferName = L"PrefixSumBuffer::" + std::to_wstring(numGroups);
        m_FreeBuffers.insert({numGroups,
            std::make_shared<GBuffer>(
                device,
                sizeof(uint32_t),
                numGroups,
                bufferName.c_str(),
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                )
        });

        if (numGroups == 1)
            break;
        numGroups = ceil(numGroups / 2.f / c_ThreadGroupSize);
        offsets++;

    } 

    computeDescriptors = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_FreeBuffers.size());
    
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    uavDesc.Buffer.FirstElement = 0;
    
    for (auto& [numGroups, buffer] : m_FreeBuffers)
    {
        uavDesc.Buffer.NumElements = buffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = buffer->GetStride();
        buffer->CreateUnorderedAccessView(&uavDesc, &computeDescriptors, m_FreeBuffersOffsets[numGroups]);
    }
    
}

void GPUPrefixSum::Run(
    const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList,
    const std::shared_ptr<PEPEngine::Graphics::GBuffer>& elements,
    bool isInitialRun)
{
    int numGroups = ceil(elements->GetElementCount() / 2.f / c_ThreadGroupSize);

    if (isInitialRun)
    {
        m_InitialElementsBuffer = elements;
    }

    std::shared_ptr<GBuffer>& groupSumBuffer = m_FreeBuffers[numGroups];
    if (!groupSumBuffer)
        throw std::runtime_error("Failed to find appropriate GPU buffer!");

    commandList->TransitionBarrier(elements->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->TransitionBarrier(groupSumBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->FlushResourceBarriers();

    commandList->SetDescriptorsHeap(&computeDescriptors);
    commandList->SetRootSignature(m_RootSignature);

    commandList->SetRoot32BitConstant(RSSlots::ItemCountSlot, elements->GetElementCount(), 0);
    commandList->SetRootDescriptorTable(RSSlots::ElementsBufferSlot, &computeDescriptors, m_FreeBuffersOffsets[elements->GetElementCount()]);
    commandList->SetRootDescriptorTable(RSSlots::GroupSumsBufferSlot, &computeDescriptors,
                                    m_FreeBuffersOffsets.at(numGroups));


    commandList->SetPipelineState(m_BlockScanPSO);
    commandList->Dispatch(numGroups, 1, 1);

    if (numGroups > 1)
    {
        Run(commandList, groupSumBuffer, false);

        commandList->SetRootDescriptorTable(RSSlots::ElementsBufferSlot, &computeDescriptors, 0);
    
        commandList->SetRootDescriptorTable(RSSlots::GroupSumsBufferSlot, &computeDescriptors,
                                        m_FreeBuffersOffsets.at(numGroups));
        
        commandList->SetRoot32BitConstant(0, elements->GetElementCount(), 0);
        
        commandList->Dispatch(numGroups, 1, 1);
    }

    commandList->UAVBarrier(elements->GetD3D12Resource(), true);
    commandList->UAVBarrier(groupSumBuffer->GetD3D12Resource(), true);
}

void GPUPrefixSum::CompileShaders()
{
    m_BlockScanShader = std::move(
        std::make_shared<GShader>(
            L"Shaders\\Helpers\\GPUSort\\PrefixSum.hlsl",
            ComputeShader,
            nullptr,
            "BlockScan",
            "cs_5_1")
    );
    m_BlockScanShader->LoadAndCompile();
    
    m_BlockCombineShader = std::move(
        std::make_shared<GShader>(
            L"Shaders\\Helpers\\GPUSort\\PrefixSum.hlsl",
            ComputeShader,
            nullptr,
            "BlockCombine",
            "cs_5_1")
    );
    m_BlockCombineShader->LoadAndCompile();
}