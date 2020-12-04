#pragma once
#include "GDescriptor.h"
#include "GTexture.h"


using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

class CloudGenerator
{
	GShader cloudNoiseShader;
	ComputePSO generatedPSO;
	GRootSignature cloudGeneratedSignature;
	GTexture generatedCloudTex;
	
	GDescriptor uavDescriptor;
	GDescriptor srvDescriptor;
	
	std::shared_ptr<GDevice> device;
	
	const DXGI_FORMAT rtvFormat = GetSRGBFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
	D3D12_VIEWPORT viewport{};
	D3D12_RECT scissorRect{};
	UINT width;
	UINT height;

	UINT groupCountWidth;
	UINT groupCountHeight;
	
	void Initialize();
	
public:
	CloudGenerator(const std::shared_ptr<GDevice> device, UINT width, UINT height);

	GDescriptor* GetCloudSRV();

	GTexture& GetCloudTexture();

	void Compute(const std::shared_ptr<GCommandList> cmdList);
};



