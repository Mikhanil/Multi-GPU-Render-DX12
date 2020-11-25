#include "pch.h"
#include "ParticleEmitter.h"
#include "GameObject.h"
#include "MathHelper.h"
#include "Transform.h"

double ParticleEmitter::CalculateGroupCount(UINT particleCount) const
{
	auto numGroups = (particleCount % 1024 != 0) ? ((particleCount / 1024) + 1) : (particleCount / 1024);
	auto secondRoot = std::pow((double)numGroups, (double)(1.0 / 2.0));
	secondRoot = std::ceil(secondRoot);
	return secondRoot;
}

void ParticleEmitter::DescriptorInitialize()
{
	particlesSrvUavDescriptors = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 5);


	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = ParticlesPool->GetElementCount();
	uavDesc.Buffer.StructureByteStride = ParticlesPool->GetStride();
	uavDesc.Buffer.CounterOffsetInBytes = ParticlesPool->GetBufferSize() - sizeof(UINT);

	ParticlesPool->CreateUnorderedAccessView(&uavDesc, &particlesSrvUavDescriptors, 0, ParticlesPool->GetD3D12Resource());

	uavDesc.Buffer.NumElements = ParticlesDead->GetElementCount();
	uavDesc.Buffer.StructureByteStride = ParticlesDead->GetStride();
	uavDesc.Buffer.CounterOffsetInBytes = ParticlesDead->GetBufferSize() - sizeof(UINT);
	ParticlesDead->CreateUnorderedAccessView(&uavDesc, &particlesSrvUavDescriptors, 1, ParticlesDead->GetD3D12Resource());
	ParticlesAlive->CreateUnorderedAccessView(&uavDesc, &particlesSrvUavDescriptors, 2, ParticlesAlive->GetD3D12Resource());

	uavDesc.Buffer.NumElements = InjectedParticles->GetElementCount();
	uavDesc.Buffer.StructureByteStride = InjectedParticles->GetStride();
	uavDesc.Buffer.CounterOffsetInBytes = InjectedParticles->GetBufferSize() - sizeof(UINT);
	InjectedParticles->CreateUnorderedAccessView(&uavDesc, &particlesSrvUavDescriptors, 3, InjectedParticles->GetD3D12Resource());	

	
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = emitterData.ParticlesTotalCount;
	srvDesc.Buffer.StructureByteStride = ParticlesPool->GetStride();

	ParticlesPool->CreateShaderResourceView(&srvDesc, &particlesSrvUavDescriptors, 4);
}

void ParticleEmitter::BufferInitialize()
{
	objectPositionBuffer = std::make_shared<ConstantUploadBuffer<ObjectConstants>>(device, 1, L"Emitter Position");
	ParticlesPool = std::make_shared<UnorderedCounteredStructBuffer<ParticleData>>(device, emitterData.ParticlesTotalCount, L"Particles Pool Buffer");
	InjectedParticles = std::make_shared<UnorderedCounteredStructBuffer<ParticleData>>(device, emitterData.ParticleInjectCount, L"Injected Particle Buffer");
	ParticlesAlive = std::make_shared<UnorderedCounteredStructBuffer<UINT>>(device, emitterData.ParticlesTotalCount, L"Particles Alive Index Buffer");
	ParticlesDead = std::make_shared<UnorderedCounteredStructBuffer<UINT>>(device, emitterData.ParticlesTotalCount, L"Particles Dead Index Buffer");	
	
	{
		std::vector<UINT> deadIndex;

		for (int i = 0; i < emitterData.ParticlesTotalCount; ++i)
		{
			deadIndex.push_back(i);
		}
		
		auto queue = device->GetCommandQueue();
		auto cmdList = queue->GetCommandList();

		ParticlesDead->LoadData(deadIndex.data(), cmdList);	
		
		ParticlesDead->SetCounterValue(cmdList, emitterData.ParticlesTotalCount);		

		ParticlesDead->CopyCounterForRead(cmdList);

		
		ParticlesAlive->SetCounterValue(cmdList, 0);		
		ParticlesAlive->CopyCounterForRead(cmdList);

		cmdList->TransitionBarrier(ParticlesDead->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
		cmdList->TransitionBarrier(ParticlesAlive->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
		cmdList->FlushResourceBarriers();
		
		queue->ExecuteCommandList(cmdList);
		queue->Flush();
	}
}

void ParticleEmitter::PSOInitialize() const
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

		CD3DX12_DESCRIPTOR_RANGE range[2];
		range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 1);
		range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 1);
		
		renderSignature = std::make_shared<GRootSignature>();
		renderSignature->AddConstantBufferParameter(0); // Object position		
		renderSignature->AddConstantBufferParameter(1); // World Data
		renderSignature->AddConstantParameter(sizeof(EmitterData) / sizeof(float), 0, 1); //EmitterData
		renderSignature->AddDescriptorParameter(&range[0], 1); //Particles
		renderSignature->AddDescriptorParameter(&range[1], 1); //Particles Render Index
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
		
		D3D_SHADER_MACRO defines[] =
		{
			"INJECTION", "1",
			nullptr, nullptr
		};
		
		auto injectedShader = std::move(
			std::make_shared<GShader>(L"Shaders\\ComputeParticle.hlsl", ComputeShader, defines, "CS", "cs_5_1"));
		injectedShader->LoadAndCompile();

		defines[0] = { "SIMULATION",  "1"};
		
		auto simulatedShader = std::move(
			std::make_shared<GShader>(L"Shaders\\ComputeParticle.hlsl", ComputeShader, defines, "CS", "cs_5_1"));
		simulatedShader->LoadAndCompile();
		
		
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

ParticleEmitter::ParticleEmitter(std::shared_ptr<GDevice> device, UINT particleCount): device((device))
{
	emitterData.ParticlesTotalCount = particleCount;
	emitterData.Color = DirectX::Colors::Blue;
	emitterData.Size = Vector2::One * 0.5f;
	emitterData.DeltaTime = 1.0f / 60.0f;
	emitterData.Force = Vector3(0, 9.8f, 0) * -1;
	emitterData.Acceleration = 1.0f;
	emitterData.SimulatedGroupCount = CalculateGroupCount(particleCount);

	emitterData.ParticleInjectCount = particleCount / 8;
	newParticles.resize(emitterData.ParticleInjectCount);

	emitterData.InjectedGroupCount = CalculateGroupCount(emitterData.ParticleInjectCount);

	PSOInitialize();

	BufferInitialize();

	DescriptorInitialize();
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
}

void ParticleEmitter::Draw(std::shared_ptr<GCommandList> cmdList)
{
	ParticlesDead->ReadCounter(&RenderParticleCount);
	ParticlesAlive->ReadCounter(&RenderParticleCount);
	
	cmdList->TransitionBarrier(ParticlesPool->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cmdList->TransitionBarrier(ParticlesAlive->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmdList->FlushResourceBarriers();
		
	cmdList->SetRootSignature(renderSignature.get());
	cmdList->SetDescriptorsHeap(&particlesSrvUavDescriptors);
	
	cmdList->SetRootConstantBufferView(ParticleRenderSlot::ObjectData, *objectPositionBuffer.get());
		
	cmdList->SetRoot32BitConstants(ParticleRenderSlot::EmitterData, sizeof(EmitterData) / sizeof(float), &emitterData, 0);

	
	cmdList->SetRootDescriptorTable(ParticleRenderSlot::ParticlesPool, &particlesSrvUavDescriptors, ParticleComputeSlot::Count - 1);
	
	cmdList->SetRootDescriptorTable(ParticleRenderSlot::ParticlesAliveIndex, &particlesSrvUavDescriptors, 2);
	
	cmdList->SetPipelineState(*renderPSO.get());
	cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
	cmdList->SetIBuffer();
	cmdList->SetVBuffer();
	cmdList->Draw(RenderParticleCount);

	cmdList->TransitionBarrier(ParticlesPool->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->TransitionBarrier(ParticlesAlive->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->FlushResourceBarriers();
}

void ParticleEmitter::Dispatch(std::shared_ptr<GCommandList> cmdList) 
{	
	cmdList->TransitionBarrier(ParticlesPool->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);	
	cmdList->FlushResourceBarriers();
	
	cmdList->SetRootSignature(computeSignature.get());
	cmdList->SetDescriptorsHeap(&particlesSrvUavDescriptors);	
	
	cmdList->SetRootDescriptorTable(ParticleComputeSlot::ParticlesPool, &particlesSrvUavDescriptors,
	                                ParticleComputeSlot::ParticlesPool - 1);
	cmdList->SetRootDescriptorTable(ParticleComputeSlot::ParticleDead, &particlesSrvUavDescriptors,
	                                ParticleComputeSlot::ParticleDead - 1);
	cmdList->SetRootDescriptorTable(ParticleComputeSlot::ParticleAlive, &particlesSrvUavDescriptors,
	                                ParticleComputeSlot::ParticleAlive - 1);
	
	if(emitterData.ParticlesTotalCount - RenderParticleCount >= emitterData.ParticleInjectCount)	
	{
		cmdList->TransitionBarrier(InjectedParticles->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmdList->TransitionBarrier(ParticlesAlive->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmdList->TransitionBarrier(ParticlesDead->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmdList->FlushResourceBarriers();
		
		ParticleData tempParticle;
		for (int i = 0; i < emitterData.ParticleInjectCount; ++i)
		{
			tempParticle.Position = Vector3(MathHelper::RandF(-1, 1), MathHelper::RandF(-1, 1), MathHelper::RandF(-1, 1));
			tempParticle.Velocity = Vector3(MathHelper::RandF(-5, 5), MathHelper::RandF(100, 200), MathHelper::RandF(-5, 5));
			tempParticle.LiveTime = MathHelper::RandF(10, 15);

			newParticles[i] = tempParticle;			
		}
					
		InjectedParticles->LoadData(newParticles.data(), cmdList);		
		
		cmdList->SetPipelineState(*injectedPSO.get());
		
		cmdList->SetRootDescriptorTable(ParticleComputeSlot::ParticleInjection, &particlesSrvUavDescriptors, ParticleComputeSlot::ParticleInjection - 1);

		cmdList->SetRoot32BitConstants(ParticleComputeSlot::EmitterData, sizeof(EmitterData) / sizeof(float), &emitterData, 0);
		
		cmdList->Dispatch(emitterData.InjectedGroupCount, emitterData.InjectedGroupCount, 1);			
	
		cmdList->UAVBarrier(InjectedParticles->GetD3D12Resource());
		cmdList->FlushResourceBarriers();
	}

	if (RenderParticleCount > 0)
	{

		emitterData.ParticlesAliveCount = RenderParticleCount;
		emitterData.SimulatedGroupCount = CalculateGroupCount(RenderParticleCount);

		cmdList->SetRoot32BitConstants(ParticleComputeSlot::EmitterData, sizeof(EmitterData) / sizeof(float), &emitterData, 0);

		cmdList->SetPipelineState(*simulatedPSO.get());

		cmdList->Dispatch(emitterData.SimulatedGroupCount, emitterData.SimulatedGroupCount, 1);		
	}


	ParticlesAlive->CopyCounterForRead(cmdList);
	ParticlesDead->CopyCounterForRead(cmdList);

	cmdList->TransitionBarrier(ParticlesPool->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->TransitionBarrier(ParticlesAlive->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->TransitionBarrier(ParticlesDead->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->FlushResourceBarriers();
}
