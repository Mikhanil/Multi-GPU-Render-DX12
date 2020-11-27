#include "pch.h"
#include "Emitter.h"

#include "MathHelper.h"

ParticleData Emitter::GenerateParticle()
{
	ParticleData tempParticle;
	tempParticle.Position = Vector3(MathHelper::RandF(-1, 1), MathHelper::RandF(-1, 1), MathHelper::RandF(-1, 1));
	tempParticle.Velocity = Vector3(MathHelper::RandF(-5, 5), MathHelper::RandF(30, 50),
		MathHelper::RandF(-5, 5));
	tempParticle.TotalLifeTime = MathHelper::RandF(2.0, 3.0);
	tempParticle.LiveTime = tempParticle.TotalLifeTime;
	tempParticle.TextureIndex = 0;
	return tempParticle;
}

void Emitter::CompileComputeShaders()
{
	D3D_SHADER_MACRO defines[] =
	{
		"INJECTION", "1",
		nullptr, nullptr
	};

	injectedShader = std::move(
		std::make_shared<GShader>(L"Shaders\\ComputeParticle.hlsl", ComputeShader, defines, "CS", "cs_5_1"));
	injectedShader->LoadAndCompile();

	defines[0] = { "SIMULATION",  "1" };

	simulatedShader = std::move(
		std::make_shared<GShader>(L"Shaders\\ComputeParticle.hlsl", ComputeShader, defines, "CS", "cs_5_1"));
	simulatedShader->LoadAndCompile();
}

void Emitter::PSOInitialize()
{
	if (renderPSO == nullptr)
	{
		auto vertexShader = std::move(
			std::make_shared<GShader>(L"Shaders\\ParticleDraw.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
		vertexShader->LoadAndCompile();

		auto pixelShader = std::move(
			std::make_shared<GShader>(L"Shaders\\ParticleDraw.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));
		pixelShader->LoadAndCompile();

		auto geometryShader = std::move(
			std::make_shared<GShader>(L"Shaders\\ParticleDraw.hlsl", PixelShader, nullptr, "GS", "gs_5_1"));
		geometryShader->LoadAndCompile();

		CD3DX12_DESCRIPTOR_RANGE range[3];
		range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 1);
		range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 1);
		range[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, Atlas.size(), 2, 1);

		renderSignature = std::make_shared<GRootSignature>();
		renderSignature->AddConstantBufferParameter(0); // Object position		
		renderSignature->AddConstantBufferParameter(1); // World Data
		renderSignature->AddConstantParameter(sizeof(EmitterData) / sizeof(float), 0, 1); //EmitterData
		renderSignature->AddDescriptorParameter(&range[0], 1); //Particles
		renderSignature->AddDescriptorParameter(&range[1], 1); //Particles Render Index
		renderSignature->AddDescriptorParameter(&range[2], 1); //Particles Render Index
		renderSignature->Initialize(device);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC renderPSODesc;

		ZeroMemory(&renderPSODesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
		renderPSODesc.InputLayout = { nullptr, 0 };
		renderPSODesc.pRootSignature = renderSignature->GetRootSignature().Get();
		renderPSODesc.VS = vertexShader->GetShaderResource();
		renderPSODesc.PS = pixelShader->GetShaderResource();
		renderPSODesc.GS = geometryShader->GetShaderResource();
		renderPSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		renderPSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		renderPSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		renderPSODesc.SampleMask = UINT_MAX;
		renderPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
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

	if (computeSignature == nullptr)
	{
		CD3DX12_DESCRIPTOR_RANGE range[4];
		range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
		range[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
		range[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3);

		computeSignature = std::make_shared<GRootSignature>();
		computeSignature->AddConstantParameter(sizeof(EmitterData) / sizeof(float), 0); // EmitterData		
		computeSignature->AddDescriptorParameter(&range[0], 1);
		computeSignature->AddDescriptorParameter(&range[1], 1);
		computeSignature->AddDescriptorParameter(&range[2], 1);
		computeSignature->AddDescriptorParameter(&range[3], 1);
		computeSignature->Initialize(device);

		CompileComputeShaders();


		injectedPSO = std::make_shared<ComputePSO>();
		injectedPSO->SetRootSignature(*computeSignature.get());
		injectedPSO->SetShader(injectedShader.get());
		injectedPSO->Initialize(device);



		simulatedPSO = std::make_shared<ComputePSO>();
		simulatedPSO->SetRootSignature(*computeSignature.get());
		simulatedPSO->SetShader(simulatedShader.get());
		simulatedPSO->Initialize(device);
	}
}
