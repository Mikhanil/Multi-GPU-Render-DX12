#include "pch.h"
#include "ParticleEmitter.h"
#include "GameObject.h"
#include "MathHelper.h"
#include "Transform.h"

ParticleEmitter::ParticleEmitter(std::shared_ptr<GDevice> device, UINT particleCount)
{
	emitterData.ParticlesCount = particleCount;
	emitterData.Color = DirectX::Colors::Blue;
	emitterData.Size = 0.5f;
		

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

		CD3DX12_DESCRIPTOR_RANGE range[1];
		range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		
		renderSignature = std::make_shared<GRootSignature>();
		renderSignature->AddConstantBufferParameter(0); // Object position
		renderSignature->AddConstantBufferParameter(1); // World Data
		renderSignature->AddConstantBufferParameter(0, 1); //EmitterData
		renderSignature->AddUnorderedAccessView(0); //Particles
		renderSignature->Initialize(device);
				
		D3D12_GRAPHICS_PIPELINE_STATE_DESC renderPSODesc;

		ZeroMemory(&renderPSODesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
		renderPSODesc.InputLayout = {nullptr, 0};
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
			D3D12_RENDER_TARGET_BLEND_DESC desc = {};
			desc.BlendEnable = true;
			desc.LogicOpEnable = false;
			desc.SrcBlend = D3D12_BLEND_ONE;
			desc.DestBlend = D3D12_BLEND_ONE;
			desc.BlendOp = D3D12_BLEND_OP_ADD;
			desc.SrcBlendAlpha = D3D12_BLEND_ONE;
			desc.DestBlendAlpha = D3D12_BLEND_ZERO;
			desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			desc.LogicOp = D3D12_LOGIC_OP_NOOP;
			desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			renderPSO->SetRenderTargetBlendState(0, desc);
		}
		renderPSO->Initialize(device);
	}

	if (dispathPSO == nullptr)
	{
		auto computeShader = std::move(
			std::make_shared<GShader>(L"Shaders\\ComputeParticle.hlsl", ComputeShader, nullptr, "CS", "cs_5_1"));
		computeShader->LoadAndCompile();

		CD3DX12_DESCRIPTOR_RANGE range[2];
		range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);

		
		computeSignature = std::make_shared<GRootSignature>();
		computeSignature->AddConstantBufferParameter(0); // EmitterData
		//computeSignature->AddUnorderedAccessView(0); //Particles
		//computeSignature->AddUnorderedAccessView(1); // Particles Index
		computeSignature->AddDescriptorParameter(&range[0], 1);
		computeSignature->AddDescriptorParameter(&range[1], 1);
		computeSignature->Initialize(device);
		
		dispathPSO = std::make_shared<ComputePSO>();
		dispathPSO->SetRootSignature(*computeSignature.get());
		dispathPSO->SetShader(computeShader.get());
		dispathPSO->Initialize(device);		
	}


	objectPositionBuffer = std::make_shared<ConstantUploadBuffer<ObjectConstants>>(device, 1, L"Emitter Position");
	emitterBuffer = std::make_shared<ConstantUploadBuffer<EmitterData>>(device, 1, L"Emitter Data");	
	
	std::vector<UINT> indexes;
	std::vector<ParticleData> cpuParticle;


	ParticleData tempParticle;
	tempParticle.Acceleration = 1;
	for (int i = 0; i < emitterData.ParticlesCount; ++i)
	{
		indexes.push_back(i);
		
		tempParticle.Position = Vector3(MathHelper::RandF(-30.0f, 30.0f), MathHelper::RandF(-30.0f, 30.0f),
			MathHelper::RandF(-30.0f, 30.0f));
		cpuParticle.push_back(tempParticle);
	}
	
	auto queue = device->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
	auto cmdList = queue->GetCommandList();

	particles = std::make_shared<GBuffer>(cmdList, L"Particles Buffer", CD3DX12_RESOURCE_DESC::Buffer(cpuParticle.size() * sizeof(ParticleData), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), sizeof(ParticleData), cpuParticle.size(), cpuParticle.data());
	
	particlesIndex = std::make_shared<GBuffer>(cmdList, L"Particles Indexes Buffer", CD3DX12_RESOURCE_DESC::Buffer(indexes.size() * sizeof(UINT), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), sizeof(UINT), indexes.size(), indexes.data());


	
	cmdList->TransitionBarrier(particlesIndex->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->TransitionBarrier(particles->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->FlushResourceBarriers();
	queue->ExecuteCommandList(cmdList);
	queue->Flush();
	
	

	ParticleUAV = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	ParticleIndexUAV = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	
	
	D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	desc.Buffer.NumElements = particleCount;
	desc.Buffer.StructureByteStride = sizeof(UINT);
	desc.Buffer.FirstElement = 0;
	desc.Buffer.CounterOffsetInBytes = 0;
	
	particlesIndex->CreateUnorderedAccessView(&desc, &ParticleIndexUAV);

	desc.Buffer.NumElements = particleCount;
	desc.Buffer.StructureByteStride = sizeof(ParticleData);
	
	particles->CreateUnorderedAccessView(&desc, &ParticleUAV);
}

void ParticleEmitter::Update()
{
	const auto transform = gameObject->GetTransform();

	if (transform->IsDirty())
	{
		objectWorldData.TextureTransform = transform->TextureTransform.Transpose();
		objectWorldData.World = (transform->GetWorldMatrix()).Transpose();
		objectPositionBuffer->CopyData(0, objectWorldData);
	}

	if (dirtyCount >= globalCountFrameResources)
	{
		emitterBuffer->CopyData(0, emitterData);
		dirtyCount--;
	}
}

void ParticleEmitter::Draw(std::shared_ptr<GCommandList> cmdList)
{
	cmdList->TransitionBarrier(particles->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmdList->FlushResourceBarriers();
	
	cmdList->SetRootSignature(renderSignature.get());
	cmdList->SetDescriptorsHeap(&ParticleUAV);
	cmdList->SetRootConstantBufferView(ParticleRenderSlot::ObjectData, *objectPositionBuffer.get());
	cmdList->SetRootConstantBufferView(ParticleRenderSlot::EmitterData, *emitterBuffer.get());
	cmdList->SetRootUnorderedAccessView(ParticleRenderSlot::Particles, *particles.get());
	cmdList->SetPipelineState(*renderPSO.get());
	cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
	cmdList->Draw(emitterData.ParticlesCount);

	cmdList->TransitionBarrier(particles->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->FlushResourceBarriers();
}

void ParticleEmitter::Dispatch(std::shared_ptr<GCommandList> cmdList)
{
	cmdList->TransitionBarrier(particles->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmdList->TransitionBarrier(particlesIndex->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmdList->FlushResourceBarriers();
	
	cmdList->SetDescriptorsHeap(&ParticleIndexUAV);
	cmdList->SetPipelineState(*dispathPSO.get());
	cmdList->SetRootSignature(computeSignature.get());
	cmdList->SetRootConstantBufferView(0, *emitterBuffer.get());
	cmdList->SetRootDescriptorTable(1, &ParticleUAV);
	cmdList->SetRootDescriptorTable(2, &ParticleIndexUAV);
	cmdList->Dispatch(emitterData.ParticlesCount / 32, 1, 1);

	cmdList->TransitionBarrier(particles->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->TransitionBarrier(particlesIndex->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->FlushResourceBarriers();
}
