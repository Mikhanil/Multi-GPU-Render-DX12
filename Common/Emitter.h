#pragma once
#include "Renderer.h"

class Emitter :
    public Renderer
{

protected:
	std::shared_ptr<GRootSignature> renderSignature;
	std::shared_ptr<GRootSignature> computeSignature;
	std::shared_ptr<GraphicPSO> renderPSO;
	std::shared_ptr<ComputePSO> injectedPSO;
	std::shared_ptr<ComputePSO> simulatedPSO;
	std::shared_ptr<GShader> injectedShader;
	std::shared_ptr<GShader> simulatedShader;

	static ParticleData GenerateParticle();
	void CompileComputeShaders();

	void PSOInitialize();

	std::vector<std::shared_ptr<GTexture>> Atlas;

	std::shared_ptr<GDevice> device;
	
public:

	virtual void Dispatch(std::shared_ptr<GCommandList> cmdList) = 0;
};

