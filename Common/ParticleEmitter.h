#pragma once
#include "GDescriptor.h"
#include "Emitter.h"


class ParticleEmitter :
	public Emitter
{
	friend class CrossAdapterParticleEmitter;

	std::shared_ptr<ConstantUploadBuffer<ObjectConstants>> objectPositionBuffer = nullptr;

	std::shared_ptr<CounteredStructBuffer<ParticleData>> ParticlesPool = nullptr;	
	std::shared_ptr<CounteredStructBuffer<DWORD>> ParticlesAlive = nullptr;	
	std::shared_ptr<CounteredStructBuffer<DWORD>> ParticlesDead = nullptr;	
	std::shared_ptr<CounteredStructBuffer<ParticleData>> InjectedParticles = nullptr;	
		
	
	std::vector<ParticleData> newParticles;
	
	GDescriptor particlesComputeDescriptors;
	GDescriptor particlesRenderDescriptors;
	
	EmitterData emitterData = {};
	ObjectConstants objectWorldData{};
	
	int dirtyCount = globalCountFrameResources;

	bool isWorked = false;
	
protected:
	void Update() override;
	void Draw(std::shared_ptr<GCommandList> cmdList, bool readCounter);
	void Draw(std::shared_ptr<GCommandList> cmdList) override;

	double CalculateGroupCount(UINT particleCount) const;

	void DescriptorInitialize();
	void BufferInitialize();
	
public:

	ParticleEmitter(std::shared_ptr<GDevice> primeDevice, UINT particleCount = 100);
	void Dispatch(std::shared_ptr<GCommandList> cmdList) override;
};
