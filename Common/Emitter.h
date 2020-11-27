#pragma once
#include "Renderer.h"

class Emitter :
    public Renderer
{

protected:
	static inline  std::shared_ptr<GRootSignature> renderSignature;
	static inline  std::shared_ptr<GRootSignature> computeSignature;
	static inline std::shared_ptr<GraphicPSO> renderPSO;
	static inline std::shared_ptr<ComputePSO> injectedPSO;
	static inline std::shared_ptr<ComputePSO> simulatedPSO;
	static inline std::shared_ptr<GShader> injectedShader;
	static inline std::shared_ptr<GShader> simulatedShader;


	void PSOInitialize() const;

	std::vector<std::shared_ptr<GTexture>> Atlas;

	std::shared_ptr<GDevice> device;
	
public:

	virtual void Dispatch(std::shared_ptr<GCommandList> cmdList) = 0;
};

