#include "pch.h"
#include "GrassSorter.h"
#include "GrassData.h"
#include "GCommandList.h"
#include <limits>
#include <stdexcept>

using namespace PEPEngine::Graphics;

static_assert(offsetof(GrassRenderVertex, ExtraPad0) == 36,
              "GrassSort.hlsl reads the instance vertex count at byte 36");

void GrassSorter::Sort(const std::shared_ptr<GCommandList>& commands,
                       const std::shared_ptr<GDevice>& device, const GBuffer& source,
                       const GBuffer& objectConstants, const GBuffer& passConstants,
                       uint32_t count, uint32_t verticesPerInstance)
{
    if (!signature_)
    {
        signature_ = std::make_shared<GRootSignature>();
        signature_->AddConstantBufferParameter(0);
        signature_->AddConstantBufferParameter(1);
        signature_->AddConstantParameter(4, 2);
        signature_->AddShaderResourceView(0);
        signature_->AddUnorderedAccessView(0);
        signature_->AddUnorderedAccessView(1);
        signature_->Initialize(device, false, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        // Matches the embedded BitonicSort_RootSig in the unmodified Microsoft shaders.
        CD3DX12_DESCRIPTOR_RANGE counterRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        CD3DX12_DESCRIPTOR_RANGE sortRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        bitonicSignature_ = std::make_shared<GRootSignature>();
        bitonicSignature_->AddConstantParameter(2, 0);
        bitonicSignature_->AddDescriptorParameter(&counterRange, 1);
        bitonicSignature_->AddDescriptorParameter(&sortRange, 1);
        bitonicSignature_->AddConstantParameter(2, 1);
        bitonicSignature_->Initialize(device, false, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        auto createPSO = [&](const wchar_t* file, const char* entry, const GRootSignature& signature)
        {
            GShader shader(file, ComputeShader, nullptr, entry, "cs_5_1");
            shader.LoadAndCompile();
            auto pso = std::make_shared<ComputePSO>(signature);
            pso->SetShader(&shader);
            pso->Initialize(device);
            return pso;
        };
        initializePSO_ = createPSO(L"Shaders\\GrassSort.hlsl", "CS_Initialize", *signature_);
        preSortPSO_ = createPSO(L"Shaders\\MiniEngine\\Bitonic64PreSortCS.hlsl", "main", *bitonicSignature_);
        outerSortPSO_ = createPSO(L"Shaders\\MiniEngine\\Bitonic64OuterSortCS.hlsl", "main", *bitonicSignature_);
        innerSortPSO_ = createPSO(L"Shaders\\MiniEngine\\Bitonic64InnerSortCS.hlsl", "main", *bitonicSignature_);
    }

    // Full 2048-item blocks also initialize every padding index read by the 64-bit kernels.
    uint32_t capacity = 2048;
    while (capacity < count)
    {
        if (capacity > std::numeric_limits<uint32_t>::max() / 2)
            throw std::overflow_error("Grass sort capacity overflow");
        capacity *= 2;
    }
    if (capacity != capacity_)
    {
        indices_ = std::make_shared<GBuffer>(device, sizeof(uint32_t) * 2, capacity,
            L"Grass sorted instance indices", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        counter_ = std::make_shared<GBuffer>(device, sizeof(uint32_t), 1,
            L"Grass sort item count", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        descriptors_ = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
        D3D12_SHADER_RESOURCE_VIEW_DESC counterSrv{};
        counterSrv.Format = DXGI_FORMAT_R32_TYPELESS;
        counterSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        counterSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        counterSrv.Buffer.NumElements = 1;
        counterSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        counter_->CreateShaderResourceView(&counterSrv, &descriptors_, 0);
        D3D12_UNORDERED_ACCESS_VIEW_DESC sortUav{};
        sortUav.Format = DXGI_FORMAT_R32_TYPELESS;
        sortUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        sortUav.Buffer.NumElements = capacity * 2;
        sortUav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        indices_->CreateUnorderedAccessView(&sortUav, &descriptors_, 1);
        capacity_ = capacity;
    }

    const uint32_t constants[] = {count, capacity,
        static_cast<uint32_t>(verticesPerInstance == 1 ? sizeof(GrassData)
                                                     : sizeof(GrassRenderVertex) * verticesPerInstance),
        verticesPerInstance == 1 ? 0u : 1u};
    commands->TransitionBarrier(source.GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commands->TransitionBarrier(indices_->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commands->TransitionBarrier(counter_->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commands->FlushResourceBarriers();
    commands->SetPipelineState(*initializePSO_);
    commands->SetComputeRootConstantBufferView(0, objectConstants);
    commands->SetComputeRootConstantBufferView(1, passConstants);
    commands->SetComputeRoot32BitConstants(2, 4, constants, 0);
    commands->SetComputeRootShaderResourceView(3, source);
    commands->SetComputeRootUnorderedAccessView(4, *indices_);
    commands->SetComputeRootUnorderedAccessView(5, *counter_);
    commands->Dispatch((capacity + 255) / 256, 1, 1);
    commands->UAVBarrier(indices_->GetD3D12Resource());
    commands->TransitionBarrier(counter_->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commands->FlushResourceBarriers();

    // MiniEngine BitonicSort.cpp dispatch sequence (see Shaders/MiniEngine/README.md).
    // GrassCount is CPU-known, so dispatch full initialized blocks directly; no indirect/readback path.
    commands->SetPipelineState(*preSortPSO_);
    commands->SetDescriptorsHeap(&descriptors_);
    commands->SetComputeRootDescriptorTable(1, &descriptors_, 0);
    commands->SetComputeRootDescriptorTable(2, &descriptors_, 1);
    const uint32_t ordering[] = {0, 0xffffffffu}; // counter offset, ascending NullItem
    commands->SetComputeRoot32BitConstants(3, 2, ordering, 0);
    const uint32_t stages[] = {2048, 1024};
    commands->SetComputeRoot32BitConstants(0, 2, stages, 0);
    const uint32_t groups = capacity / 2048;
    commands->Dispatch(groups, 1, 1);
    commands->UAVBarrier(indices_->GetD3D12Resource());
    commands->FlushResourceBarriers();

    for (uint32_t k = 4096; k <= capacity; k *= 2)
    {
        commands->SetPipelineState(*outerSortPSO_);
        for (uint32_t j = k / 2; j >= 2048; j /= 2)
        {
            const uint32_t merge[] = {k, j};
            commands->SetComputeRoot32BitConstants(0, 2, merge, 0);
            commands->Dispatch(groups, 1, 1);
            commands->UAVBarrier(indices_->GetD3D12Resource());
            commands->FlushResourceBarriers();
        }
        commands->SetPipelineState(*innerSortPSO_);
        commands->SetComputeRoot32BitConstant(0, k, 0);
        commands->Dispatch(groups, 1, 1);
        commands->UAVBarrier(indices_->GetD3D12Resource());
        commands->FlushResourceBarriers();
        if (k == capacity)
            break;
    }
    commands->TransitionBarrier(indices_->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commands->FlushResourceBarriers();
}
