#pragma once

#include "GDescriptor.h"
#include "FluidEmitter.h"
#include "FluidSimulationData.h"

class FluidParticleEmitter : public FluidEmitter
{
	friend class CrossAdapterFluidParticleEmitter;
public:
	//void ChangeParticleCount(const DWORD newParticleCount);

	FluidParticleEmitter(const std::shared_ptr<GDevice>& primeDevice, DWORD particleCount = 10000);
	void Dispatch(const std::shared_ptr<GCommandList>& cmdList) override;

protected:
	
	
private:
	std::shared_ptr<ConstantUploadBuffer<GBuffer>> someData;
	
	FluidSimulationData simData={};

};
