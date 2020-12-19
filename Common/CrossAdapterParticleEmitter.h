#pragma once
#include "Emitter.h"
#include "GCrossAdapterResource.h"
#include "GDescriptor.h"
class ParticleEmitter;

class CrossAdapterParticleEmitter :    public Emitter
{

	std::shared_ptr<ParticleEmitter> primeParticleEmitter;

	std::shared_ptr<GBuffer> ParticlesPool = nullptr;
	std::shared_ptr<CounteredStructBuffer<DWORD>> ParticlesAlive = nullptr;
	std::shared_ptr<CounteredStructBuffer<DWORD>> ParticlesDead = nullptr;
	std::shared_ptr<GBuffer> InjectedParticles = nullptr;

	std::shared_ptr<GCrossAdapterResource> CrossAdapterAliveIndexes;
	std::shared_ptr<GCrossAdapterResource> CrossAdapterDeadIndexes;
	std::shared_ptr<GCrossAdapterResource> CrossAdapterParticles;

	GDescriptor updateDescriptors;
	
	std::shared_ptr<ComputePSO> injectPSO;
	std::shared_ptr<ComputePSO> updatePSO;
	std::shared_ptr<GRootSignature> computeRS;
	
	std::vector<ParticleData> newParticles;
	
	bool UseSharedCompute = false;

	
	
	std::shared_ptr<GDevice> secondDevice;

	enum Status : short
	{
		None = -1,
		Enable = 0,
		Disable = 1
		
	};

	Status DirtyActivated = None;
	
public:
	void InitPSO(std::shared_ptr<GDevice> otherDevice);
	void CreateBuffers();
	CrossAdapterParticleEmitter(std::shared_ptr<GDevice> primeDevice, std::shared_ptr<GDevice> otherDevice, DWORD particleCount);
	void Update() override;;
	void Draw(std::shared_ptr<GCommandList> cmdList) override;
	void Dispatch(std::shared_ptr<GCommandList> cmdList) override;

	void ChangeParticleCount(UINT count);

	void EnableShared();;

	void DisableShared();;
};

