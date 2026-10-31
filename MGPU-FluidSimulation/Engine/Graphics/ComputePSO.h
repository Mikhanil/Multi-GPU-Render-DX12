#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "GRootSignature.h"
#include "DirectXBuffers.h"
#include "GShader.h"
#include "PSO.h"
#include "SimpleMath.h"

namespace PEPEngine::Graphics
{
    struct GenerateMipsCB
    {
        DirectX::SimpleMath::Vector2 TexelSize; // 1.0 / OutMip1.Dimensions
    };


    class ComputePSO final : public PSO
    {
    public:
        ComputePSO();
        ComputePSO(const GRootSignature& RS);
        void SetShader(const GShader* shader);

        void Initialize(const std::shared_ptr<GDevice>& device) override;
        PSOType GetType() override { return PSOType::Compute; }
        void SetRootSignature(const GRootSignature& rs) override;

    private:
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    };
}
