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

	std::shared_ptr<UnorderedCounteredStructBuffer<ParticleData>> ParticlesPool = nullptr;
	
	std::shared_ptr<UnorderedCounteredStructBuffer<UINT>> ParticlesAlive = nullptr;
	
	std::shared_ptr<UnorderedCounteredStructBuffer<UINT>> ParticlesDead = nullptr;
	
	std::shared_ptr<UnorderedCounteredStructBuffer<ParticleData>> InjectedParticles = nullptr;
	
	
	
	UINT RenderParticleCount = 0;
	
	custom_vector<ParticleData> newParticles = MemoryAllocator::CreateVector<ParticleData>();
	
	GDescriptor particlesSrvUavDescriptors;
	
	EmitterData emitterData = {};
	ObjectConstants objectWorldData{};

	
	int dirtyCount = globalCountFrameResources;
	std::shared_ptr<GDevice> device;

public:

	double CalculateGroupCount(UINT particleCount) const;
	void DescriptorInitialize();
	void BufferInitialize();
	void PSOInitialize() const;
	ParticleEmitter(std::shared_ptr<GDevice> device, UINT particleCount = 100);

	void Update() override;;
	void Draw(std::shared_ptr<GCommandList> cmdList) override;;
	void Dispatch(std::shared_ptr<GCommandList> cmdList);
};
