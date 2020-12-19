#pragma once
#include "GCrossAdapterResource.h"
#include "GDescriptor.h"
#include "GTexture.h"


using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;
using namespace DirectX::SimpleMath;

class CloudGenerator
{
	struct alignas(sizeof(Vector4)) CloudGeneratorData
	{				
		float CloudCover = 0.2;
		float CloudAlpha = 8.0;
		float SkyTint = 0.5;
		float TotalTime = 0.0f;

		float CloudScale = 1.1;
		float CloudSpeed = 0.03;
		float CloudDark = 0.5;
		float CloudLight = 0.3;

		Vector4 SkyColor1 = Vector4(0.2, 0.4, 0.6, 1);
		Vector4 SkyColor2 = Vector4(0.4, 0.7, 1.0, 1);
		Vector4 CloudColor = Vector4(1.1, 1.1, 0.9, 1);

		Vector2 RenderTargetSize = Vector2(4096, 4096);
		Vector2 padding;
	};

	CloudGeneratorData generatorParameters = {};
	GShader cloudNoiseShader;

	ComputePSO primeGeneratedPSO;
	GRootSignature primeCloudGeneratedSignature;

	ComputePSO secondGeneratedPSO;
	GRootSignature secondCloudGeneratedSignature;
	
	GDescriptor primeUavDescriptor;
	GDescriptor secondUavDescriptor;
	GDescriptor srvDescriptor;

	GTexture primeCloudTex;
	GTexture secondCloudTex;
	GCrossAdapterResource crossAdapterCloudTex;
	
	std::shared_ptr<GDevice> primeDevice;
	std::shared_ptr<GDevice> secondDevice;
	
	const DXGI_FORMAT rtvFormat = GetSRGBFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
	D3D12_VIEWPORT viewport{};
	D3D12_RECT scissorRect{};
	UINT width;
	UINT height;

	UINT groupCountWidth{};
	UINT groupCountHeight{};

	
	void Initialize();
	
public:
	CloudGenerator(const std::shared_ptr<GDevice> primeDevice, const std::shared_ptr<GDevice> secondDevice, UINT width, UINT height);

	GDescriptor* GetCloudSRV();

	GTexture& GetCloudTexture();

	void ChangeTextureSize(int width, int height);
	
	void PrimeCompute(const std::shared_ptr<GCommandList>& cmdList, float deltaTime);

	void PrimeCopy(const std::shared_ptr<GCommandList>& primeCmdList) const;
	void SecondCompute(const std::shared_ptr<GCommandList>& cmdList, float deltaTime);
};



