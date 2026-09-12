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
        signature_->AddConstantParameter(6, 2);
        signature_->AddShaderResourceView(0);
        signature_->AddUnorderedAccessView(0);
        signature_->Initialize(device, false, D3D12_ROOT_SIGNATURE_FLAG_NONE);
        auto createPSO = [&](const char* entry)
        {
            GShader shader(L"Shaders\\GrassSort.hlsl", ComputeShader, nullptr, entry, "cs_5_1");
            shader.LoadAndCompile();
            auto pso = std::make_shared<ComputePSO>();
            pso->SetRootSignature(*signature_);
            pso->SetShader(&shader);
            pso->Initialize(device);
            return pso;
        };
        initializePSO_ = createPSO("CS_Initialize");
        sortPSO_ = createPSO("CS_Bitonic");
    }

    uint32_t capacity = 1;
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
        capacity_ = capacity;
    }

    struct SortConstants
    {
        uint32_t count, capacity, stage, step, sourceStride, expanded;
    } constants{count, capacity, 0, 0,
        static_cast<uint32_t>(verticesPerInstance == 1 ? sizeof(GrassData)
                                                     : sizeof(GrassRenderVertex) * verticesPerInstance),
        verticesPerInstance == 1 ? 0u : 1u};

    commands->TransitionBarrier(source.GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commands->TransitionBarrier(indices_->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commands->FlushResourceBarriers();
    commands->SetPipelineState(*initializePSO_);
    commands->SetComputeRootConstantBufferView(0, objectConstants);
    commands->SetComputeRootConstantBufferView(1, passConstants);
    commands->SetComputeRoot32BitConstants(2, 6, &constants, 0);
    commands->SetComputeRootShaderResourceView(3, source);
    commands->SetComputeRootUnorderedAccessView(4, *indices_);
    const uint32_t groups = (capacity + 255) / 256;
    commands->Dispatch(groups, 1, 1);
    commands->UAVBarrier(indices_->GetD3D12Resource());
    commands->FlushResourceBarriers();

    commands->SetPipelineState(*sortPSO_);
    for (uint32_t stage = 2; stage <= capacity; stage *= 2)
    {
        for (uint32_t step = stage / 2; step > 0; step /= 2)
        {
            constants.stage = stage;
            constants.step = step;
            commands->SetComputeRoot32BitConstants(2, 6, &constants, 0);
            commands->Dispatch(groups, 1, 1);
            commands->UAVBarrier(indices_->GetD3D12Resource());
            commands->FlushResourceBarriers();
        }
        if (stage == capacity)
            break;
    }
    commands->TransitionBarrier(indices_->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commands->FlushResourceBarriers();
}
