#pragma once

#include "d3dUtil.h"
#include "DirectXBuffers.h"
#include "ShaderBuffersData.h"
#include "Texture.h"

class PSO;
using namespace Microsoft::WRL;


class Material
{
	static UINT materialIndexGlobal;

	UINT materialIndex = -1;
		
	std::string Name;
	PSO* pso = nullptr;

	MaterialConstants matConstants{};

	UINT NumFramesDirty = globalCountFrameResources;
	UINT cbvSrvUavDescriptorSize = 0;

	std::vector<Texture*> textures{ 1 };

	UINT DiffuseMapIndex = -1;

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuTextureHandle;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuTextureHandle;

public:

	MaterialConstants& GetMaterialConstantData();

	UINT GetIndex() const
	{
		return materialIndex;
	}
	
	void SetDirty()
	{
		NumFramesDirty = globalCountFrameResources;
	}
	
	PSO* GetPSO() const;

	void SetDiffuseTexture(Texture* texture);

	Material(std::string name, PSO* pso);	

	void InitMaterial(ID3D12Device* device, ID3D12DescriptorHeap* textureHeap);
		
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

