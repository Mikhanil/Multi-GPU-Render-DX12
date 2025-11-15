#include "pch.h"
#include "GPUCountingSort.h"

#include "ComputePSO.h"
#include "GBuffer.h"
#include "GRootSignature.h"


void GPUCountingSort::Initialize(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device)
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

    for (uint8_t kernel = EKernels::ClearCounts; kernel < EKernels::NumKernels; kernel++)
    {
        m_PSOs[kernel] = std::make_shared<PEPEngine::Graphics::ComputePSO>();
        m_PSOs[kernel]->SetRootSignature(m_SortSignature);
        m_PSOs[kernel]->SetShader(m_Shaders[kernel].get());
        m_PSOs[kernel]->Initialize(device);
    }

    m_ComputeDescriptors = m_Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 5);
    
    scan.Initialize(device);
}

void GPUCountingSort::Run(
    const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList,
    const std::shared_ptr<PEPEngine::Graphics::GBuffer>& itemsBuffer,
    const std::shared_ptr<PEPEngine::Graphics::GBuffer>& keysBuffer,
    UINT maxValue)
{
    UINT count = itemsBuffer->GetElementCount();
    const size_t numGroupsX = ceil(static_cast<float>(count) / static_cast<float>(256));

    // these return false if the buffer already exist, i don't care about it really
    TryCreateBuffer(m_SortedItemsBuffer, sizeof(UINT), count);
    TryCreateBuffer(m_SortedKeysBuffer, sizeof(UINT), count);
    TryCreateBuffer(m_CountsBuffer, sizeof(UINT), count);
    if (!m_bDescriptorsInitialized)
        InitializeDescriptors(itemsBuffer, keysBuffer);

    commandList->TransitionBarrier(itemsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->TransitionBarrier(keysBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->TransitionBarrier(m_SortedItemsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->TransitionBarrier(m_SortedKeysBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->TransitionBarrier(m_CountsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->FlushResourceBarriers();

    commandList->SetRootSignature(m_SortSignature);

    commandList->SetDescriptorsHeap(&m_ComputeDescriptors);

    commandList->SetRoot32BitConstant(ESortSlots::NumInputsSlot, count, 0);
    commandList->SetRootDescriptorTable(ESortSlots::InputItemsSlot,     &m_ComputeDescriptors, EBufferOffsets::ItemsBuffer);
    commandList->SetRootDescriptorTable(ESortSlots::InputKeysSlot,      &m_ComputeDescriptors, EBufferOffsets::KeysBuffer);
    commandList->SetRootDescriptorTable(ESortSlots::SortedItemsSlot,    &m_ComputeDescriptors, EBufferOffsets::SortedItemsBuffer);
    commandList->SetRootDescriptorTable(ESortSlots::SortedItemsSlot,    &m_ComputeDescriptors, EBufferOffsets::SortedKeysBuffer);
    commandList->SetRootDescriptorTable(ESortSlots::CountsSlot,         &m_ComputeDescriptors, EBufferOffsets::CountsBuffer);

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

    commandList->SetRootSignature(m_SortSignature);

    commandList->SetDescriptorsHeap(&m_ComputeDescriptors);
    
    commandList->SetRoot32BitConstant(ESortSlots::NumInputsSlot, count, 0);
    commandList->SetRootDescriptorTable(ESortSlots::InputItemsSlot,     &m_ComputeDescriptors, EBufferOffsets::ItemsBuffer);
    commandList->SetRootDescriptorTable(ESortSlots::InputKeysSlot,      &m_ComputeDescriptors, EBufferOffsets::KeysBuffer);
    commandList->SetRootDescriptorTable(ESortSlots::SortedItemsSlot,    &m_ComputeDescriptors, EBufferOffsets::SortedItemsBuffer);
    commandList->SetRootDescriptorTable(ESortSlots::SortedItemsSlot,    &m_ComputeDescriptors, EBufferOffsets::SortedKeysBuffer);
    commandList->SetRootDescriptorTable(ESortSlots::CountsSlot,         &m_ComputeDescriptors, EBufferOffsets::CountsBuffer);

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
    m_Shaders[EKernels::ClearCounts] = std::move(
        std::make_shared<PEPEngine::Graphics::GShader>(
            L"Shaders\\Helpers\\GPUSort\\CountingSort.hlsl",
            PEPEngine::Graphics::ComputeShader,
            nullptr,
            "ClearCounts",
            "cs_5_1"));
    m_Shaders[EKernels::ClearCounts]->LoadAndCompile();

    m_Shaders[EKernels::CalculateCounts] = std::move(
        std::make_shared<PEPEngine::Graphics::GShader>(
            L"Shaders\\Helpers\\GPUSort\\CountingSort.hlsl",
            PEPEngine::Graphics::ComputeShader,
            nullptr,
            "CalculateCounts",
            "cs_5_1"));
    m_Shaders[EKernels::CalculateCounts]->LoadAndCompile();

    m_Shaders[EKernels::ScatterOutput] = std::move(
        std::make_shared<PEPEngine::Graphics::GShader>(
            L"Shaders\\Helpers\\GPUSort\\CountingSort.hlsl",
            PEPEngine::Graphics::ComputeShader,
            nullptr,
            "ScatterOutput",
            "cs_5_1"));
    m_Shaders[EKernels::ScatterOutput]->LoadAndCompile();

    m_Shaders[EKernels::CopyBack] = std::move(
        std::make_shared<PEPEngine::Graphics::GShader>(
            L"Shaders\\Helpers\\GPUSort\\CountingSort.hlsl",
            PEPEngine::Graphics::ComputeShader,
            nullptr,
            "CopyBack",
            "cs_5_1"));
    m_Shaders[EKernels::CopyBack]->LoadAndCompile();
}

bool GPUCountingSort::TryCreateBuffer(std::shared_ptr<PEPEngine::Graphics::GBuffer>& buffer, UINT stride, UINT count)
{
    bool createNewBuffer = buffer == nullptr
                        || !buffer->IsValid()
                        || buffer->GetStride() != stride
                        || buffer->GetElementCount() != count;
    if (createNewBuffer)
    {
        buffer->Reset();
        buffer = std::make_shared<PEPEngine::Graphics::GBuffer>(m_Device, stride, count);

        m_bDescriptorsInitialized = false;
        return true;
    }
    return false;
}

void GPUCountingSort::InitializeDescriptors(const BufferPtr& itemsBuffer, const BufferPtr& keysBuffer)
{
    if (m_bDescriptorsInitialized)
        return;

    /*
     * I could have probably put these in some kind of an array, but i didn't think it would be necessary
     */
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

