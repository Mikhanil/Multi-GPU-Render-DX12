#include "pch.h"
#include "FluidEmitter.h"

ParticleData FluidEmitter::GenerateParticle()
{
    return ParticleData{};
}

void FluidEmitter::CompileComputeShaders()
{
    computeKernels[EKernels::ExternalForces] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, nullptr, "ExternalForces", "cs_5_1"));
    computeKernels[EKernels::ExternalForces]->LoadAndCompile();
    
    computeKernels[EKernels::UpdateSpatialHash] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, nullptr, "UpdateSpatialHash", "cs_5_1"));
    computeKernels[EKernels::UpdateSpatialHash]->LoadAndCompile();
    
    computeKernels[EKernels::Reorder] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, nullptr, "Reorder", "cs_5_1"));
    computeKernels[EKernels::Reorder]->LoadAndCompile();
    
    computeKernels[EKernels::ReorderCopyBack] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, nullptr, "ReorderCopyBack", "cs_5_1"));
    computeKernels[EKernels::ReorderCopyBack]->LoadAndCompile();
    
    computeKernels[EKernels::CalculateDensities] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, nullptr, "CalculateDensities", "cs_5_1"));
    computeKernels[EKernels::CalculateDensities]->LoadAndCompile();
    
    computeKernels[EKernels::CalculatePressureForce] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, nullptr, "CalculatePressureForce", "cs_5_1"));
    computeKernels[EKernels::CalculatePressureForce]->LoadAndCompile();
    
    computeKernels[EKernels::CalculateViscosity] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, nullptr, "CalculateViscosity", "cs_5_1"));
    computeKernels[EKernels::CalculateViscosity]->LoadAndCompile();
    
    computeKernels[EKernels::UpdatePositions] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, nullptr, "UpdatePositions", "cs_5_1"));
    computeKernels[EKernels::UpdatePositions]->LoadAndCompile();
    
    computeKernels[EKernels::UpdateDensityTexture] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, nullptr, "UpdateDensityTexture", "cs_5_1"));
    computeKernels[EKernels::UpdateDensityTexture]->LoadAndCompile();
}

void FluidEmitter::PSOInitialize()
{
    if (renderPSO == nullptr)
    {
        auto vertexShader = std::move(
            std::make_shared<GShader>(L"Shaders\\FluidParticleDraw.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
        vertexShader->LoadAndCompile();

        auto pixelShader = std::move(
            std::make_shared<GShader>(L"Shaders\\FluidParticleDraw.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));
        pixelShader->LoadAndCompile();

        /*
        CD3DX12_DESCRIPTOR_RANGE range[3];
        range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 1);
        range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 1);
        range[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, Atlas.size(), 2, 1);
        */

        CD3DX12_DESCRIPTOR_RANGE range[1];
        range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);

        renderSignature = std::make_shared<GRootSignature>();
        renderSignature->AddConstantParameter(sizeof(FluidParticleDrawData) / sizeof(float), 0);
        renderSignature->AddDescriptorParameter(range, 1); //Particles' positions and velocities 
        renderSignature->Initialize(device);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC renderPSODesc;

        ZeroMemory(&renderPSODesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
        renderPSODesc.InputLayout = {nullptr, 0};
        renderPSODesc.pRootSignature = renderSignature->GetNativeSignature().Get();
        renderPSODesc.VS = vertexShader->GetShaderResource();
        renderPSODesc.PS = pixelShader->GetShaderResource();
        renderPSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        renderPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        renderPSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        renderPSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        renderPSODesc.SampleMask = UINT_MAX;
        renderPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        renderPSODesc.NumRenderTargets = 1;
        renderPSODesc.RTVFormats[0] = GetSRGBFormat(BackBufferFormat);
        renderPSODesc.SampleDesc.Count = 1;
        renderPSODesc.SampleDesc.Quality = 0;
        renderPSODesc.DSVFormat = DepthStencilFormat;

        renderPSO = std::make_shared<GraphicPSO>(RenderMode::Particle);
        renderPSO->SetPsoDesc(renderPSODesc);
        {
            D3D12_RENDER_TARGET_BLEND_DESC blendDesc = {};
            blendDesc.BlendEnable = true;
            blendDesc.LogicOpEnable = false;
            blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blendDesc.SrcBlendAlpha = D3D12_BLEND_ZERO;
            blendDesc.DestBlendAlpha = D3D12_BLEND_ONE;
            blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
            blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
            blendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            renderPSO->SetRenderTargetBlendState(0, blendDesc);
        }
        renderPSO->Initialize(device);
    }

    if (computePSOs.empty())
    {

        computeSignature = std::make_shared<GRootSignature>();
        computeSignature->AddConstantBufferParameter(0);
        
        CD3DX12_DESCRIPTOR_RANGE ranges[3];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 6, 0);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 7);
        
        computeSignature->AddDescriptorParameter(ranges, 3);

        computeSignature->Initialize(device);

        CompileComputeShaders();

        for (int8_t kernel = EKernels::ExternalForces; kernel < EKernels::Count; kernel++)
        {
            computePSOs[static_cast<EKernels>(kernel)] = std::make_shared<ComputePSO>();
            computePSOs[static_cast<EKernels>(kernel)]->SetRootSignature(*computeSignature.get());
            computePSOs[static_cast<EKernels>(kernel)]->SetShader(computeKernels[static_cast<EKernels>(kernel)].get());
            computePSOs[static_cast<EKernels>(kernel)]->Initialize(device);
        }
    }
}
