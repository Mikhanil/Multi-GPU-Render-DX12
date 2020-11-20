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
	static inline std::shared_ptr<ComputePSO> dispathPSO;

	std::shared_ptr<ConstantUploadBuffer<ObjectConstants>> objectPositionBuffer = nullptr;
	std::shared_ptr<ConstantUploadBuffer<EmitterData>> emitterBuffer = nullptr;

	std::shared_ptr<GBuffer> particles = nullptr;
	std::shared_ptr<GBuffer> particlesIndex = nullptr;

	EmitterData emitterData = {};
	ObjectConstants objectWorldData{};

	GDescriptor ParticleUAV;
	GDescriptor ParticleIndexUAV;
	
	int dirtyCount = globalCountFrameResources;

public:

	ParticleEmitter(std::shared_ptr<GDevice> device, UINT particleCount = 100);

	void Update() override;;
	void Draw(std::shared_ptr<GCommandList> cmdList) override;;
	void Dispatch(std::shared_ptr<GCommandList> cmdList);
};
