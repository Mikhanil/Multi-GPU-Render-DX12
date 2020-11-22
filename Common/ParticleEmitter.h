#pragma once
#include "GDescriptor.h"
#include "GMeshBuffer.h"
#include "Renderer.h"


class ParticleEmitter :
	public Renderer
{
	static inline  std::shared_ptr<GRootSignature> renderSignature;
	static inline  std::shared_ptr<GRootSignature> computeSignature;
	static inline std::shared_ptr<GraphicPSO> renderPSO;
	static inline std::shared_ptr<ComputePSO> injectedPSO;
	static inline std::shared_ptr<ComputePSO> simulatedPSO;

	std::shared_ptr<ConstantUploadBuffer<ObjectConstants>> objectPositionBuffer = nullptr;
	std::shared_ptr<ConstantUploadBuffer<EmitterData>> emitterBuffer = nullptr;

	std::shared_ptr<GBuffer> ParticlesPool = nullptr;
	
	std::shared_ptr<GBuffer> ParticlesAlive = nullptr;
	std::shared_ptr<GBuffer> ParticlesAliveCount = nullptr;
	
	std::shared_ptr<GBuffer> ParticlesDead = nullptr;
	std::shared_ptr<GBuffer> ParticlesDeadCount = nullptr;
	
	std::shared_ptr<GBuffer> InjectedParticles = nullptr;
	
	std::shared_ptr<GBuffer> RenderParticles = nullptr;
	std::shared_ptr<GBuffer> ParticlesRenderCount = nullptr;
	
	std::shared_ptr<ReadBackBuffer<UINT>> ReadBackCount = nullptr;
	
	UINT AliveParticleCount = 0;
	
	custom_vector<ParticleData> newParticles = MemoryAllocator::CreateVector<ParticleData>();
	
	GDescriptor particlesSrvUavDescriptors;
	
	EmitterData emitterData = {};
	ObjectConstants objectWorldData{};

	
	int dirtyCount = globalCountFrameResources;
	
public:

	ParticleEmitter(std::shared_ptr<GDevice> device, UINT particleCount = 100);

	void Update() override;;
	void Draw(std::shared_ptr<GCommandList> cmdList) override;;
	void Dispatch(std::shared_ptr<GCommandList> cmdList);
};
