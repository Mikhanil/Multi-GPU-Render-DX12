#include "pch.h"
#include "CrossAdapterParticleEmitter.h"


#include "MathHelper.h"
#include "ParticleEmitter.h"

CrossAdapterParticleEmitter::CrossAdapterParticleEmitter(std::shared_ptr<GDevice> primeDevice, std::shared_ptr<GDevice> otherDevice, UINT particleCount): secondDevice(otherDevice)
{
	this->device = primeDevice;
	
	primeParticleEmitter = std::make_shared<ParticleEmitter>(primeDevice, particleCount);

	ParticlesPool = std::make_shared<CounteredStructBuffer<ParticleData>>(otherDevice, primeParticleEmitter->emitterData.ParticlesTotalCount, L"Second Particles Pool Buffer");
	InjectedParticles = std::make_shared<CounteredStructBuffer<ParticleData>>(otherDevice, primeParticleEmitter->emitterData.ParticleInjectCount, L"Second Injected Particle Buffer");
	ParticlesAlive = std::make_shared<CounteredStructBuffer<UINT>>(otherDevice, primeParticleEmitter->emitterData.ParticlesTotalCount, L"Second Particles Alive Index Buffer");
	ParticlesDead = std::make_shared<CounteredStructBuffer<UINT>>(otherDevice, primeParticleEmitter->emitterData.ParticlesTotalCount, L"Second Particles Dead Index Buffer");

	auto desc = ParticlesAlive->GetD3D12ResourceDesc();
	CrossAdapterAliveIndexes = std::make_shared<GCrossAdapterResource>(desc, primeDevice, otherDevice, L"Cross Adapter Particle Alive Index Buffer");
	CrossAdapterDeadIndexes = std::make_shared<GCrossAdapterResource>(desc, primeDevice, otherDevice, L"Cross Adapter Particle Dead Index Buffer");

	desc = ParticlesPool->GetD3D12ResourceDesc();
	CrossAdapterParticles = std::make_shared<GCrossAdapterResource>(desc, primeDevice, otherDevice, L"Cross Adapter Particle Buffer");
	
	
	newParticles.resize(primeParticleEmitter->emitterData.ParticleInjectCount);


	CD3DX12_DESCRIPTOR_RANGE rng[4];
	rng[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
	rng[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
	rng[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
	rng[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3);

	computeRS = std::make_shared<GRootSignature>();
	computeRS->AddConstantParameter(sizeof(EmitterData) / sizeof(float), 0); // EmitterData		
	computeRS->AddDescriptorParameter(&rng[0], 1);
	computeRS->AddDescriptorParameter(&rng[1], 1);
	computeRS->AddDescriptorParameter(&rng[2], 1);
	computeRS->AddDescriptorParameter(&rng[3], 1);
	computeRS->Initialize(otherDevice);
	
	injectPSO = std::make_shared<ComputePSO>();
	injectPSO->SetShader(injectedShader.get());
	injectPSO->SetRootSignature(*computeRS.get());
	injectPSO->Initialize(secondDevice);


	updatePSO = std::make_shared<ComputePSO>();
	updatePSO->SetShader(simulatedShader.get());
	updatePSO->SetRootSignature(*computeRS.get());
	updatePSO->Initialize(secondDevice);
	
	updateDescriptors = secondDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = ParticlesPool->GetElementCount();
	uavDesc.Buffer.StructureByteStride = ParticlesPool->GetStride();
	uavDesc.Buffer.CounterOffsetInBytes = ParticlesPool->GetBufferSize() - sizeof(UINT);

	ParticlesPool->CreateUnorderedAccessView(&uavDesc, &updateDescriptors, 0, ParticlesPool->GetD3D12Resource());

	uavDesc.Buffer.NumElements = ParticlesDead->GetElementCount();
	uavDesc.Buffer.StructureByteStride = ParticlesDead->GetStride();
	uavDesc.Buffer.CounterOffsetInBytes = ParticlesDead->GetBufferSize() - sizeof(UINT);
	ParticlesDead->CreateUnorderedAccessView(&uavDesc, &updateDescriptors, 1, ParticlesDead->GetD3D12Resource());
	ParticlesAlive->CreateUnorderedAccessView(&uavDesc, &updateDescriptors, 2, ParticlesAlive->GetD3D12Resource());

	uavDesc.Buffer.NumElements = InjectedParticles->GetElementCount();
	uavDesc.Buffer.StructureByteStride = InjectedParticles->GetStride();
	uavDesc.Buffer.CounterOffsetInBytes = InjectedParticles->GetBufferSize() - sizeof(UINT);
	InjectedParticles->CreateUnorderedAccessView(&uavDesc, &updateDescriptors, 3, InjectedParticles->GetD3D12Resource());
	
}

void CrossAdapterParticleEmitter::Update()
{
	primeParticleEmitter->gameObject = this->gameObject;	
	primeParticleEmitter->Update();	
}

void CrossAdapterParticleEmitter::Draw(std::shared_ptr<GCommandList> cmdList)
{
	if(UseSharedCompute)
	{
		ParticlesAlive->ReadCounter(&primeParticleEmitter->emitterData.ParticlesAliveCount);
		
		cmdList->CopyResource(primeParticleEmitter->ParticlesPool->GetD3D12Resource(), CrossAdapterParticles->GetPrimeResource().GetD3D12Resource());
		cmdList->CopyResource(primeParticleEmitter->ParticlesAlive->GetD3D12Resource(), CrossAdapterAliveIndexes->GetPrimeResource().GetD3D12Resource());				
	}
	
	primeParticleEmitter->Draw(cmdList, !UseSharedCompute);
}

void CrossAdapterParticleEmitter::Dispatch(std::shared_ptr<GCommandList> cmdList)
{
	if(DirtyActivated == Enable)
	{
		if (primeParticleEmitter->isWorked)
		{
			cmdList->CopyResource(ParticlesPool->GetD3D12Resource(), CrossAdapterParticles->GetSharedResource().GetD3D12Resource());
			cmdList->CopyResource(ParticlesAlive->GetD3D12Resource(), CrossAdapterAliveIndexes->GetSharedResource().GetD3D12Resource());
			cmdList->CopyResource(ParticlesDead->GetD3D12Resource(), CrossAdapterDeadIndexes->GetSharedResource().GetD3D12Resource());
		}
		
		DirtyActivated = None;
	}

	if(DirtyActivated == Disable)
	{
		cmdList->CopyResource( primeParticleEmitter->ParticlesPool->GetD3D12Resource(), CrossAdapterParticles->GetPrimeResource().GetD3D12Resource());
		cmdList->CopyResource( primeParticleEmitter->ParticlesAlive->GetD3D12Resource(), CrossAdapterAliveIndexes->GetPrimeResource().GetD3D12Resource());
		cmdList->CopyResource(primeParticleEmitter->ParticlesDead->GetD3D12Resource(), CrossAdapterDeadIndexes->GetPrimeResource().GetD3D12Resource());

		DirtyActivated = None;
	}
	
	
	if(UseSharedCompute)
	{
		cmdList->TransitionBarrier(ParticlesPool->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmdList->TransitionBarrier(ParticlesAlive->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmdList->TransitionBarrier(ParticlesDead->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmdList->FlushResourceBarriers();

		cmdList->SetRootSignature(computeRS.get());
		cmdList->SetDescriptorsHeap(&updateDescriptors);

		cmdList->SetRootDescriptorTable(ParticleComputeSlot::ParticlesPool, &updateDescriptors,
			ParticleComputeSlot::ParticlesPool - 1);
		cmdList->SetRootDescriptorTable(ParticleComputeSlot::ParticleDead, &updateDescriptors,
			ParticleComputeSlot::ParticleDead - 1);
		cmdList->SetRootDescriptorTable(ParticleComputeSlot::ParticleAlive, &updateDescriptors,
			ParticleComputeSlot::ParticleAlive - 1);

		if (primeParticleEmitter->emitterData.ParticlesTotalCount - primeParticleEmitter->emitterData.ParticlesAliveCount >= primeParticleEmitter->emitterData.ParticleInjectCount)
		{
			cmdList->TransitionBarrier(InjectedParticles->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);		
			cmdList->FlushResourceBarriers();

			ParticleData tempParticle;
			for (int i = 0; i < primeParticleEmitter->emitterData.ParticleInjectCount; ++i)
			{
				tempParticle.Position = Vector3(MathHelper::RandF(-1, 1), MathHelper::RandF(-1, 1), MathHelper::RandF(-1, 1));
				tempParticle.Velocity = Vector3(MathHelper::RandF(-5, 5), MathHelper::RandF(100, 200), MathHelper::RandF(-5, 5));
				tempParticle.TotalLifeTime = MathHelper::RandF(10, 15);
				tempParticle.LiveTime = tempParticle.TotalLifeTime;
				tempParticle.TextureIndex = 0;

				newParticles[i] = tempParticle;
			}

			InjectedParticles->LoadData(newParticles.data(), cmdList);

			cmdList->SetPipelineState(*injectPSO.get());

			cmdList->SetRootDescriptorTable(ParticleComputeSlot::ParticleInjection, &updateDescriptors, ParticleComputeSlot::ParticleInjection - 1);

			cmdList->SetRoot32BitConstants(ParticleComputeSlot::EmitterData, sizeof(EmitterData) / sizeof(float), &primeParticleEmitter->emitterData, 0);

			cmdList->Dispatch(primeParticleEmitter->emitterData.InjectedGroupCount, primeParticleEmitter->emitterData.InjectedGroupCount, 1);

			cmdList->UAVBarrier(InjectedParticles->GetD3D12Resource());
			cmdList->FlushResourceBarriers();
		}

		if (primeParticleEmitter->emitterData.ParticlesAliveCount > 0)
		{
			primeParticleEmitter->emitterData.SimulatedGroupCount = primeParticleEmitter->CalculateGroupCount(primeParticleEmitter->emitterData.ParticlesAliveCount);

			cmdList->SetRoot32BitConstants(ParticleComputeSlot::EmitterData, sizeof(EmitterData) / sizeof(float), &primeParticleEmitter->emitterData, 0);

			cmdList->SetPipelineState(*updatePSO.get());

			cmdList->Dispatch(primeParticleEmitter->emitterData.SimulatedGroupCount, primeParticleEmitter->emitterData.SimulatedGroupCount, 1);
		}

		ParticlesAlive->CopyCounterForRead(cmdList);
		
		cmdList->CopyResource(CrossAdapterParticles->GetSharedResource().GetD3D12Resource(), ParticlesPool->GetD3D12Resource());
		cmdList->CopyResource(CrossAdapterAliveIndexes->GetSharedResource().GetD3D12Resource(), ParticlesAlive->GetD3D12Resource());
		cmdList->CopyResource(CrossAdapterDeadIndexes->GetSharedResource().GetD3D12Resource(), ParticlesDead->GetD3D12Resource());
		
	}
	else
	{
		primeParticleEmitter->Dispatch(cmdList);

		cmdList->CopyResource(CrossAdapterParticles->GetPrimeResource().GetD3D12Resource(), primeParticleEmitter->ParticlesPool->GetD3D12Resource());
		cmdList->CopyResource(CrossAdapterAliveIndexes->GetPrimeResource().GetD3D12Resource(), primeParticleEmitter->ParticlesAlive->GetD3D12Resource());
		cmdList->CopyResource(CrossAdapterDeadIndexes->GetPrimeResource().GetD3D12Resource(), primeParticleEmitter->ParticlesDead->GetD3D12Resource());
	}
}

void CrossAdapterParticleEmitter::EnableShared()
{
	UseSharedCompute = true;

	DirtyActivated = Enable;
}

void CrossAdapterParticleEmitter::DisableShared()
{
	UseSharedCompute = false;

	DirtyActivated = Disable;
}
