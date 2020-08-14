#pragma once

#include "d3dUtil.h"
#include "DirectXBuffers.h"
#include "ShaderBuffersData.h"
#include "Texture.h"

class GraphicPSO;
class GCommandList;
using namespace Microsoft::WRL;


class Material
{
	static UINT materialIndexGlobal;

	UINT materialIndex = -1;

	std::string Name;
	GraphicPSO* pso = nullptr;

	MaterialConstants matConstants{};

	UINT NumFramesDirty = globalCountFrameResources;

	custom_vector<Texture*> textures = DXAllocator::CreateVector<Texture*>();

	Texture* normalMap = nullptr;

	UINT DiffuseMapIndex = -1;
	UINT NormalMapIndex = -1;

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuTextureHandle;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuTextureHandle;

public:

	MaterialConstants& GetMaterialConstantData();

	UINT GetIndex() const;

	void SetDirty();

	GraphicPSO* GetPSO() const;

	void SetNormalMap(Texture* texture);

	void SetPSO(GraphicPSO* pso);

	void SetDiffuseTexture(Texture* texture);

	Material(std::string name, GraphicPSO* pso);

	void InitMaterial(GMemory& textureHeap);

	void Draw(std::shared_ptr<GCommandList> cmdList) const;

	void Update();

	std::string& GetName();

	DirectX::XMFLOAT4 DiffuseAlbedo = DirectX::XMFLOAT4(DirectX::Colors::White);
	DirectX::XMFLOAT3 FresnelR0 = {0.01f, 0.01f, 0.01f};
	float Roughness = .25f;
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};
