#include "ComputePSO.h"
#include "GDevice.h"
#include "GShader.h"

namespace PEPEngine::Graphics
{
    void ComputePSO::Initialize(const std::shared_ptr<GDevice>& device)
    {
        const auto result = device->GetDXDevice()->CreateComputePipelineState(&psoDesc,
                                                                              IID_PPV_ARGS(&nativePSO));
        ThrowIfFailed(result);
    }

    void ComputePSO::SetRootSignature(const GRootSignature& rs)
    {
        PSO::SetRootSignature(rs);
        psoDesc.pRootSignature = rs.GetNativeSignature().Get();
    }

    ComputePSO::ComputePSO() : PSO()
    {
    }

    ComputePSO::ComputePSO(const GRootSignature& RS) : PSO(RS)
    {
        ZeroMemory(&psoDesc, sizeof(D3D12_COMPUTE_PIPELINE_STATE_DESC));
        psoDesc.pRootSignature = rs.GetNativeSignature().Get();
    }


    void ComputePSO::SetShader(const GShader* shader)
    {
        if (shader->GetType() == ComputeShader)
        {
            psoDesc.CS = shader->GetShaderResource();
            return;
        }

        assert("Bad Shader");
    }
}
