#include "pch.h"
#include "GPUPrefixSum.h"

using namespace PEPEngine;
using namespace PEPEngine::Graphics;

void GPUPrefixSum::Initialize(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device)
{
    m_Device = device;

    // uint item count at register(b0)
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
}

void GPUPrefixSum::Run(
    const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList,
    const std::shared_ptr<PEPEngine::Graphics::GBuffer>& elements,
    bool isInitialRun)
{
    int numGroups = ceil(elements->GetElementCount() / 2.f / c_ThreadGroupSize);

    if (isInitialRun)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = elements->GetElementCount();
        uavDesc.Buffer.StructureByteStride = elements->GetStride();
        uavDesc.Buffer.CounterOffsetInBytes = 0;

        elements->CreateUnorderedAccessView(&uavDesc, &computeDescriptors, 0);
        m_InitialElementsBuffer = elements;
    }

    std::shared_ptr<GBuffer> groupSumBuffer;
    if (m_FreeBuffers.find(numGroups) == m_FreeBuffers.end())
    {
        groupSumBuffer = std::make_shared<GBuffer>(m_Device, sizeof(uint32_t), numGroups);

        // need to add one to facilitate the descriptor for the buffer that is the input buffer. 
        m_FreeBuffersOffsets.insert({numGroups, m_FreeBuffers.size() + 1});
        m_FreeBuffers.insert({ numGroups, groupSumBuffer });

        // alright, so here I just don't care that it's weird to recreate the descriptor heap
        // after each iteration, this code should not run often, it should run only once
        // (or maybe like 5 times) on 400k particles in original simulation this portion ran only
        // 4 times

        // need to add one to facilitate the descriptor for the buffer that is the input buffer. 
        computeDescriptors = m_Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_FreeBuffers.size() + 1);
        
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = m_InitialElementsBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = m_InitialElementsBuffer->GetStride();
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        m_InitialElementsBuffer->CreateUnorderedAccessView(&uavDesc, &computeDescriptors, 0);

        size_t i = 1;
        for (auto& [_numGroups, buffer] : m_FreeBuffers)
        {
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = buffer->GetElementCount();
            uavDesc.Buffer.StructureByteStride = buffer->GetStride();
            uavDesc.Buffer.CounterOffsetInBytes = 0;

            buffer->CreateUnorderedAccessView(&uavDesc, &computeDescriptors, i);
            i++;
        }
    }
    else
    {
        groupSumBuffer = m_FreeBuffers[numGroups];
    }

    commandList->TransitionBarrier(elements->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->TransitionBarrier(groupSumBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->FlushResourceBarriers();

    commandList->SetDescriptorsHeap(&computeDescriptors);
    commandList->SetPipelineState(m_BlockScanPSO);

    // this thing is weird but here i set this to 0 if the 
    commandList->SetRootDescriptorTable(RSSlots::ElementsBufferSlot, &computeDescriptors, 0);
    
    commandList->SetRootDescriptorTable(RSSlots::GroupSumsBufferSlot, &computeDescriptors,
                                    m_FreeBuffersOffsets.at(numGroups));

    commandList->SetRoot32BitConstant(0, elements->GetElementCount(), 0);

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