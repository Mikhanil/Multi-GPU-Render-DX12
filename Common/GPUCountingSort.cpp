#include "pch.h"
#include "GPUCountingSort.h"

#include "ComputePSO.h"
#include "GBuffer.h"
#include "GRootSignature.h"


void GPUCountingSort::Initialize(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device, size_t count)
{
    m_Device = device;
   
    
    // Parameter for numInputs (register b0)
    m_SortSignature.AddConstantParameter(1, 0);

    CD3DX12_DESCRIPTOR_RANGE range[5];
    range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
    range[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
    range[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3);
    range[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 4);

    m_SortSignature.AddDescriptorParameter(&range[0], 1);
    m_SortSignature.AddDescriptorParameter(&range[1], 1);
    m_SortSignature.AddDescriptorParameter(&range[2], 1);
    m_SortSignature.AddDescriptorParameter(&range[3], 1);
    m_SortSignature.AddDescriptorParameter(&range[4], 1);

    m_SortSignature.Initialize(device);

    CompileShaders();

    for (auto kernel : EKernels::All)
    {
        m_PSOs[kernel] = std::make_shared<PEPEngine::Graphics::ComputePSO>();
        m_PSOs[kernel]->SetRootSignature(m_SortSignature);
        m_PSOs[kernel]->SetShader(m_Shaders[kernel].get());
        m_PSOs[kernel]->Initialize(device);
        m_PSOs[kernel]->GetPSO()->SetName(EKernels::ToWide(kernel).c_str());
    }

    m_ComputeDescriptors = m_Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 5);
    
    TryCreateBuffer(m_SortedItemsBuffer, sizeof(UINT), count, L"CountingSort::SortedItemsBuffer");
    TryCreateBuffer(m_SortedKeysBuffer, sizeof(UINT), count, L"CountingSort::SortedKeysBuffer");
    TryCreateBuffer(m_CountsBuffer, sizeof(UINT), count, L"CountingSort::CountsBuffer");
    
    scan.Initialize(device, count);
}

void GPUCountingSort::Run(
    const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList,
    const std::shared_ptr<PEPEngine::Graphics::GBuffer>& itemsBuffer,
    const std::shared_ptr<PEPEngine::Graphics::GBuffer>& keysBuffer,
    UINT maxValue)
{
    UINT count = itemsBuffer->GetElementCount();
    const size_t numGroupsX = ceil(static_cast<float>(count) / static_cast<float>(FLUID_SIM_GROUP_COUNT));

    if (!m_bDescriptorsInitialized)
        InitializeDescriptors(itemsBuffer, keysBuffer);

    commandList->TransitionBarrier(itemsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->TransitionBarrier(keysBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->TransitionBarrier(m_SortedItemsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->TransitionBarrier(m_SortedKeysBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->TransitionBarrier(m_CountsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->FlushResourceBarriers();

    commandList->SetComputeRootSignature(m_SortSignature);

    commandList->SetDescriptorsHeap(&m_ComputeDescriptors);

    commandList->SetComputeRoot32BitConstant(ESortSlots::NumInputsSlot, count, 0);
    commandList->SetComputeRootDescriptorTable(ESortSlots::InputItemsSlot,     &m_ComputeDescriptors, EBufferOffsets::ItemsBuffer);
    commandList->SetComputeRootDescriptorTable(ESortSlots::InputKeysSlot,      &m_ComputeDescriptors, EBufferOffsets::KeysBuffer);
    commandList->SetComputeRootDescriptorTable(ESortSlots::SortedItemsSlot,    &m_ComputeDescriptors, EBufferOffsets::SortedItemsBuffer);
    commandList->SetComputeRootDescriptorTable(ESortSlots::SortedKeysSlot,     &m_ComputeDescriptors, EBufferOffsets::SortedKeysBuffer);
    commandList->SetComputeRootDescriptorTable(ESortSlots::CountsSlot,         &m_ComputeDescriptors, EBufferOffsets::CountsBuffer);

    {
        commandList->SetPipelineState(*m_PSOs[EKernels::ClearCounts]);
        commandList->Dispatch(numGroupsX, 1, 1);

        commandList->UAVBarrier(m_CountsBuffer->GetD3D12Resource());
        commandList->UAVBarrier(itemsBuffer->GetD3D12Resource());
        commandList->FlushResourceBarriers();
    }

    {
        commandList->SetPipelineState(*m_PSOs[EKernels::CalculateCounts]);
        commandList->Dispatch(numGroupsX, 1, 1);
    
        commandList->UAVBarrier(m_CountsBuffer->GetD3D12Resource());
        commandList->FlushResourceBarriers();
    }

    {
        scan.Run(commandList, m_CountsBuffer, true);
    }

    commandList->SetComputeRootSignature(m_SortSignature);

    commandList->SetDescriptorsHeap(&m_ComputeDescriptors);
    
    commandList->SetComputeRoot32BitConstant(ESortSlots::NumInputsSlot, count, 0);
    commandList->SetComputeRootDescriptorTable(ESortSlots::InputItemsSlot,     &m_ComputeDescriptors, EBufferOffsets::ItemsBuffer);
    commandList->SetComputeRootDescriptorTable(ESortSlots::InputKeysSlot,      &m_ComputeDescriptors, EBufferOffsets::KeysBuffer);
    commandList->SetComputeRootDescriptorTable(ESortSlots::SortedItemsSlot,    &m_ComputeDescriptors, EBufferOffsets::SortedItemsBuffer);
    commandList->SetComputeRootDescriptorTable(ESortSlots::SortedKeysSlot,    &m_ComputeDescriptors, EBufferOffsets::SortedKeysBuffer);
    commandList->SetComputeRootDescriptorTable(ESortSlots::CountsSlot,         &m_ComputeDescriptors, EBufferOffsets::CountsBuffer);

    commandList->SetPipelineState(*m_PSOs[EKernels::ScatterOutput]);
    commandList->Dispatch(numGroupsX, 1, 1);

    commandList->UAVBarrier(m_CountsBuffer->GetD3D12Resource());
    commandList->UAVBarrier(m_SortedItemsBuffer->GetD3D12Resource());
    commandList->UAVBarrier(m_SortedKeysBuffer->GetD3D12Resource());
    commandList->FlushResourceBarriers();
    
    commandList->SetPipelineState(*m_PSOs[EKernels::CopyBack]);
    
    commandList->Dispatch(numGroupsX, 1, 1);
    commandList->UAVBarrier(itemsBuffer->GetD3D12Resource());
    commandList->UAVBarrier(keysBuffer->GetD3D12Resource());
    commandList->FlushResourceBarriers();
}

void GPUCountingSort::CompileShaders()
{
    static std::string threadCountStr = std::to_string(FLUID_SIM_GROUP_COUNT);
    D3D_SHADER_MACRO macros[] = {"GROUP_SIZE", threadCountStr.c_str(), NULL, NULL};
    
    m_Shaders[EKernels::ClearCounts] = std::move(
        std::make_shared<PEPEngine::Graphics::GShader>(
            L"Shaders\\Helpers\\GPUSort\\CountingSort.hlsl",
            PEPEngine::Graphics::ComputeShader,
            macros,
            "ClearCounts",
            "cs_5_1"));
    m_Shaders[EKernels::ClearCounts]->LoadAndCompile();

    m_Shaders[EKernels::CalculateCounts] = std::move(
        std::make_shared<PEPEngine::Graphics::GShader>(
            L"Shaders\\Helpers\\GPUSort\\CountingSort.hlsl",
            PEPEngine::Graphics::ComputeShader,
            macros,
            "CalculateCounts",
            "cs_5_1"));
    m_Shaders[EKernels::CalculateCounts]->LoadAndCompile();

    m_Shaders[EKernels::ScatterOutput] = std::move(
        std::make_shared<PEPEngine::Graphics::GShader>(
            L"Shaders\\Helpers\\GPUSort\\CountingSort.hlsl",
            PEPEngine::Graphics::ComputeShader,
            macros,
            "ScatterOutput",
            "cs_5_1"));
    m_Shaders[EKernels::ScatterOutput]->LoadAndCompile();

    m_Shaders[EKernels::CopyBack] = std::move(
        std::make_shared<PEPEngine::Graphics::GShader>(
            L"Shaders\\Helpers\\GPUSort\\CountingSort.hlsl",
            PEPEngine::Graphics::ComputeShader,
            macros,
            "CopyBack",
            "cs_5_1"));
    m_Shaders[EKernels::CopyBack]->LoadAndCompile();
}

bool GPUCountingSort::TryCreateBuffer(std::shared_ptr<PEPEngine::Graphics::GBuffer>& buffer, UINT stride, UINT count, const std::wstring& name)
{
    bool createNewBuffer = buffer == nullptr
                        || !buffer->IsValid()
                        || buffer->GetStride() != stride
                        || buffer->GetElementCount() != count;
    if (createNewBuffer)
    {
        if (buffer && buffer->IsValid())
            buffer->Reset();
        buffer = std::make_shared<PEPEngine::Graphics::GBuffer>(m_Device, stride, count, name.c_str(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        m_bDescriptorsInitialized = false;
        return true;
    }
    return false;
}

void GPUCountingSort::InitializeDescriptors(const BufferPtr& itemsBuffer, const BufferPtr& keysBuffer)
{
    if (m_bDescriptorsInitialized)
        return;
    
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    uavDesc.Buffer.FirstElement = 0;
    
    uavDesc.Buffer.NumElements = itemsBuffer->GetElementCount();
    uavDesc.Buffer.StructureByteStride = itemsBuffer->GetStride();
    itemsBuffer->CreateUnorderedAccessView(&uavDesc, &m_ComputeDescriptors, EBufferOffsets::ItemsBuffer);
    
    uavDesc.Buffer.NumElements = keysBuffer->GetElementCount();
    uavDesc.Buffer.StructureByteStride = keysBuffer->GetStride();
    keysBuffer->CreateUnorderedAccessView(&uavDesc, &m_ComputeDescriptors, EBufferOffsets::KeysBuffer);
    
    uavDesc.Buffer.NumElements = m_SortedItemsBuffer->GetElementCount();
    uavDesc.Buffer.StructureByteStride = m_SortedItemsBuffer->GetStride();
    m_SortedItemsBuffer->CreateUnorderedAccessView(&uavDesc, &m_ComputeDescriptors, EBufferOffsets::SortedItemsBuffer);
    
    uavDesc.Buffer.NumElements = m_SortedKeysBuffer->GetElementCount();
    uavDesc.Buffer.StructureByteStride = m_SortedKeysBuffer->GetStride();
    m_SortedKeysBuffer->CreateUnorderedAccessView(&uavDesc, &m_ComputeDescriptors, EBufferOffsets::SortedKeysBuffer);
    
    uavDesc.Buffer.NumElements = m_CountsBuffer->GetElementCount();
    uavDesc.Buffer.StructureByteStride = m_CountsBuffer->GetStride();
    m_CountsBuffer->CreateUnorderedAccessView(&uavDesc, &m_ComputeDescriptors, EBufferOffsets::CountsBuffer);

    m_bDescriptorsInitialized = true;
}

