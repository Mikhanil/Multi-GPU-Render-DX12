#pragma once
#include "ModelRenderer.h"

class GCommandList;
class GMemory;

class SkyBox : public ModelRenderer
{
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuTextureHandle{};
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuTextureHandle{};
	
public:
	SkyBox(const std::shared_ptr<GDevice>& device, const std::shared_ptr<GModel>& model, GTexture& skyMapTexture,
	       GMemory* srvMemory, UINT offset = 0);

protected:
	void Draw(std::shared_ptr<GCommandList> cmdList) override;
	void Update() override;;;
};
