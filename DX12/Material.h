#pragma once

#include "d3dUtil.h"
#include "DirectXBuffers.h"
#include "Texture.h"

class PSO;
using namespace Microsoft::WRL;

struct MaterialConstants
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 0.25f;

	// Used in texture mapping.
	DirectX::XMFLOAT4X4 MaterialTransform = MathHelper::Identity4x4();
};

class Material
{
	std::unique_ptr<ConstantBuffer<MaterialConstants>> materialBuffer = nullptr;
	ComPtr<ID3D12DescriptorHeap> materialViewDescriptorHeap;
	
	std::string Name;
	PSO* pso = nullptr;

	MaterialConstants matConstants{};

	UINT NumFramesDirty = globalCountFrameResources;
	UINT cbvSrvUavDescriptorSize = 0;

	std::vector<Texture*> textures{ 1 };	

public:

	PSO* GetPSO();

	void SetDiffuseTexture(Texture* texture);

	Material(std::string name, PSO* pso);	

	void InitMaterial(ID3D12Device* device);
		
	void Draw(ID3D12GraphicsCommandList* cmdList) const;

	void Update();

	std::string& GetName();


	// Index into SRV heap for diffuse texture.
	//int DiffuseSrvHeapIndex = -1;

	// Material constant buffer data used for shading.
	DirectX::XMFLOAT4 DiffuseAlbedo = DirectX::XMFLOAT4(DirectX::Colors::White);
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = .25f;
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

