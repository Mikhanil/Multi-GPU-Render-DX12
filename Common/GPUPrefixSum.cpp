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
    m_BlockScanPSO.GetPSO()->SetName(L"PrefixSum::BlockScan");
    
    m_BlockCombinePSO.SetRootSignature(m_RootSignature);
    m_BlockCombinePSO.SetShader(m_BlockCombineShader.get());
    m_BlockCombinePSO.Initialize(device);
    m_BlockCombinePSO.GetPSO()->SetName(L"PrefixSum::BlockCombine");

    m_FreeBuffersOffsets.insert({count, 0});

    uint32_t numGroups = GetNumGroups(count);
    uint32_t offsets = 1;
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
        
        numGroups = GetNumGroups(numGroups);
        offsets++;
    } 

    computeDescriptors = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_FreeBuffers.size() + 1);
    
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
    if (!m_InitialElementsBuffer)
    {
        m_InitialElementsBuffer = elements;
        
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = elements->GetElementCount();
        uavDesc.Buffer.StructureByteStride = elements->GetStride();

        m_InitialElementsBuffer->CreateUnorderedAccessView(&uavDesc, &computeDescriptors, 0);
        m_FreeBuffers.insert({elements->GetElementCount(), elements});
    }

    uint32_t itemCount = elements->GetElementCount();
    size_t numGroups = GetNumGroups(itemCount);

    auto itElemBuf = m_FreeBuffers.find(itemCount);
    assert(itElemBuf != m_FreeBuffers.end());
    assert(itElemBuf->second.get() == elements.get()); // or at least compatible

    auto itGroupBuf = m_FreeBuffers.find(numGroups);
    assert(itGroupBuf != m_FreeBuffers.end());

    auto itElemOff = m_FreeBuffersOffsets.find(itemCount);
    auto itGroupOff = m_FreeBuffersOffsets.find(numGroups);
    assert(itElemOff != m_FreeBuffersOffsets.end());
    assert(itGroupOff != m_FreeBuffersOffsets.end());

    std::shared_ptr<GBuffer>& groupSumBuffer = m_FreeBuffers[numGroups];
    if (!groupSumBuffer)
        throw std::runtime_error("Failed to find appropriate GPU buffer!");

    commandList->TransitionBarrier(elements->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->TransitionBarrier(groupSumBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->FlushResourceBarriers();

    commandList->SetDescriptorsHeap(&computeDescriptors);
    commandList->SetRootSignature(m_RootSignature);

    commandList->SetRootDescriptorTable(
        RSSlots::ElementsBufferSlot,
        &computeDescriptors,
        m_FreeBuffersOffsets[itemCount]);
    
    commandList->SetRootDescriptorTable(
        RSSlots::GroupSumsBufferSlot,
        &computeDescriptors,
        m_FreeBuffersOffsets[numGroups]);

    commandList->SetRoot32BitConstant(
        RSSlots::ItemCountSlot,
        itemCount,
        0);

    commandList->SetPipelineState(m_BlockScanPSO);
    commandList->Dispatch(numGroups, 1, 1);

    commandList->UAVBarrier(groupSumBuffer->GetD3D12Resource(), true);
    
    if (numGroups > 1)
    {
        Run(commandList, groupSumBuffer, false);

        commandList->SetRootDescriptorTable(
            RSSlots::ElementsBufferSlot,
            &computeDescriptors,
            m_FreeBuffersOffsets[itemCount]);
    
        commandList->SetRootDescriptorTable(
            RSSlots::GroupSumsBufferSlot,
            &computeDescriptors,
            m_FreeBuffersOffsets[numGroups]);
        
        commandList->SetRoot32BitConstant(
            0,
            itemCount,
            0);

        commandList->SetPipelineState(m_BlockCombinePSO);
        commandList->Dispatch(numGroups, 1, 1);
    }

    commandList->UAVBarrier(elements->GetD3D12Resource(), true);
}

void GPUPrefixSum::CompileShaders()
{
    static std::string threadCountStr = std::to_string(FLUID_SIM_GROUP_COUNT);
    D3D_SHADER_MACRO macros[] = {"GROUP_SIZE", threadCountStr.c_str(), NULL, NULL};
    
    m_BlockScanShader = std::move(
        std::make_shared<GShader>(
            L"Shaders\\Helpers\\GPUSort\\PrefixSum.hlsl",
            ComputeShader,
            macros,
            "BlockScan",
            "cs_5_1")
    );
    m_BlockScanShader->LoadAndCompile();
    
    m_BlockCombineShader = std::move(
        std::make_shared<GShader>(
            L"Shaders\\Helpers\\GPUSort\\PrefixSum.hlsl",
            ComputeShader,
            macros,
            "BlockCombine",
            "cs_5_1")
    );
    m_BlockCombineShader->LoadAndCompile();
}

uint32_t GPUPrefixSum::GetNumGroups(uint32_t count) const
{
    size_t itemsPerGroup = ThreadGroupCount * 2;
    return (count + itemsPerGroup - 1) / itemsPerGroup;
}
