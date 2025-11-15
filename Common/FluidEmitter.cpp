#include "pch.h"
#include "FluidEmitter.h"

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

        CD3DX12_DESCRIPTOR_RANGE range[2];
        range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

        renderSignature.AddConstantParameter(sizeof(FluidParticleDrawData) / sizeof(float), 0);
        renderSignature.AddDescriptorParameter(&range[0], 1); //Particles positions 
        renderSignature.AddDescriptorParameter(&range[1], 1); //Particles velocities 
        renderSignature.Initialize(m_device);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC renderPSODesc;

        ZeroMemory(&renderPSODesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
        renderPSODesc.InputLayout = {nullptr, 0};
        renderPSODesc.pRootSignature = renderSignature.GetNativeSignature().Get();
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
        renderPSO->Initialize(m_device);
    }

    if (computePSOs.empty())
    {
        computeSignature.AddConstantBufferParameter(0);
        computeSignature.AddConstantParameter(4, 1);
        
        CD3DX12_DESCRIPTOR_RANGE ranges[10];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
        ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3);
        ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 4);
        ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 5);
        ranges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 6);
        ranges[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 7);
        ranges[8].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 8);
        
        ranges[9].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        
        computeSignature.AddDescriptorParameter(&ranges[0], 1);
        computeSignature.AddDescriptorParameter(&ranges[1], 1);
        computeSignature.AddDescriptorParameter(&ranges[2], 1);
        computeSignature.AddDescriptorParameter(&ranges[3], 1);
        computeSignature.AddDescriptorParameter(&ranges[4], 1);
        computeSignature.AddDescriptorParameter(&ranges[5], 1);
        computeSignature.AddDescriptorParameter(&ranges[6], 1);
        computeSignature.AddDescriptorParameter(&ranges[7], 1);
        computeSignature.AddDescriptorParameter(&ranges[8], 1);
        computeSignature.AddDescriptorParameter(&ranges[9], 1);

        computeSignature.Initialize(m_device);

        CompileComputeShaders();

        for (uint8_t kernel = EKernels::ExternalForces; kernel < EKernels::Count; kernel++)
        {
            computePSOs[kernel] = std::make_shared<ComputePSO>();
            computePSOs[kernel]->SetRootSignature(computeSignature);
            computePSOs[kernel]->SetShader(computeKernels[kernel].get());
            computePSOs[kernel]->Initialize(m_device);
        }
    }
}
