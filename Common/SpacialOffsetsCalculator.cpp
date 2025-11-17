#include "pch.h"
#include "SpacialOffsetsCalculator.h"

#include "GCommandQueue.h"

void SpacialOffsetsCalculator::Initialize(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device)
{
    m_device = device;
    m_Descriptors = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

    m_RootSignature.AddConstantParameter(1, 0);
    
    CD3DX12_DESCRIPTOR_RANGE range[2];
    range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    m_RootSignature.AddDescriptorParameter(&range[0], 1);
    m_RootSignature.AddDescriptorParameter(&range[1], 1);
    
    m_RootSignature.Initialize(device);

    CompileShaders();

    m_InitPSO.SetRootSignature(m_RootSignature);
    m_InitPSO.SetShader(m_InitShader.get());
    m_InitPSO.Initialize(device);
    m_InitPSO.GetPSO()->SetName(L"SpatialOffsetCalculation::InitPSO");
    
    m_CalcPSO.SetRootSignature(m_RootSignature);
    m_CalcPSO.SetShader(m_CalcShader.get());
    m_CalcPSO.Initialize(device);
    m_CalcPSO.GetPSO()->SetName(L"SpatialOffsetCalculation::CalcPSO");
}

void SpacialOffsetsCalculator::Run(
    const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList,
    const BufferPointer& sortedKeys,
    const BufferPointer& offsets)
{
    assert(sortedKeys->GetElementCount() == offsets->GetElementCount() && "Count mismatch");

    if (!m_bDescriptorsInitialized)
        CreateDescriptors(sortedKeys, offsets);
    
    const size_t GroupCount = ceil(sortedKeys->GetElementCount() / 256.f); 
    
    commandList->TransitionBarrier(sortedKeys->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList->TransitionBarrier(offsets->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->FlushResourceBarriers();

    commandList->SetRootSignature(m_RootSignature);

    commandList->SetDescriptorsHeap(&m_Descriptors);

    commandList->SetRoot32BitConstant(0, sortedKeys->GetElementCount(), 0);
    commandList->SetRootDescriptorTable(1, &m_Descriptors, 0);
    commandList->SetRootDescriptorTable(2, &m_Descriptors, 1);

    {
        commandList->SetPipelineState(m_InitPSO);
        commandList->Dispatch(GroupCount, 1, 1);
    
        commandList->UAVBarrier(offsets->GetD3D12Resource());
        commandList->FlushResourceBarriers();
    }

    {
        commandList->SetPipelineState(m_CalcPSO);
        commandList->Dispatch(GroupCount, 1, 1);
    
        commandList->UAVBarrier(offsets->GetD3D12Resource());
        commandList->FlushResourceBarriers();
    }
}


void SpacialOffsetsCalculator::CompileShaders()
{
    m_InitShader = std::move(
        std::make_shared<PEPEngine::Graphics::GShader>(
            L"Shaders\\Helpers\\SpacialOffsets.hlsl",
            PEPEngine::Graphics::ComputeShader,
            nullptr,
            "InitializeOffsets",
            "cs_5_1"));
    m_InitShader->LoadAndCompile();
    
    m_CalcShader = std::move(
        std::make_shared<PEPEngine::Graphics::GShader>(
            L"Shaders\\Helpers\\SpacialOffsets.hlsl",
            PEPEngine::Graphics::ComputeShader,
            nullptr,
            "CalculateOffsets",
            "cs_5_1"));
    m_CalcShader->LoadAndCompile();
}


void SpacialOffsetsCalculator::CreateDescriptors(const BufferPointer& sortedKeys, const BufferPointer& offsets)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = sortedKeys->GetElementCount();
    srvDesc.Buffer.StructureByteStride = sortedKeys->GetStride();
    sortedKeys->CreateShaderResourceView(&srvDesc, &m_Descriptors, 0);
    
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = offsets->GetElementCount();
    uavDesc.Buffer.StructureByteStride = offsets->GetStride();
    offsets->CreateUnorderedAccessView(&uavDesc, &m_Descriptors, 1);

    m_bDescriptorsInitialized = true;
}

