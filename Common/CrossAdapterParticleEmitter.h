#pragma once
#include "Emitter.h"
#include "GCrossAdapterResource.h"
#include "GDescriptor.h"
class ParticleEmitter;

class CrossAdapterParticleEmitter :    public Emitter
{

	std::shared_ptr<ParticleEmitter> primeParticleEmitter;

	std::shared_ptr<CounteredStructBuffer<ParticleData>> ParticlesPool = nullptr;
	std::shared_ptr<CounteredStructBuffer<UINT>> ParticlesAlive = nullptr;
	std::shared_ptr<CounteredStructBuffer<UINT>> ParticlesDead = nullptr;
	std::shared_ptr<CounteredStructBuffer<ParticleData>> InjectedParticles = nullptr;

	std::shared_ptr<GCrossAdapterResource> CrossAdapterAliveIndexes;
	std::shared_ptr<GCrossAdapterResource> CrossAdapterDeadIndexes;
	std::shared_ptr<GCrossAdapterResource> CrossAdapterParticles;

	GDescriptor updateDescriptors;
	
	static inline std::shared_ptr<ComputePSO> injectPSO;
	static inline std::shared_ptr<ComputePSO> updatePSO;
	static inline std::shared_ptr<GRootSignature> computeRS;
	
	custom_vector<ParticleData> newParticles = MemoryAllocator::CreateVector<ParticleData>();
	
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
	CrossAdapterParticleEmitter(std::shared_ptr<GDevice> primeDevice, std::shared_ptr<GDevice> otherDevice, UINT particleCount);
	void Update() override;;
	void Draw(std::shared_ptr<GCommandList> cmdList) override;
	void Dispatch(std::shared_ptr<GCommandList> cmdList) override;

	void EnableShared();;

	void DisableShared();;
};

