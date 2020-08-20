#pragma once

#include "d3dUtil.h"
#include "DirectXBuffers.h"
#include "GraphicPSO.h"
#include "ShaderBuffersData.h"
#include "GTexture.h"

class GraphicPSO;
class GCommandList;
using namespace Microsoft::WRL;


class Material
{
	static UINT materialIndexGlobal;

	UINT materialIndex = -1;

	std::wstring Name;
	
	PsoType::Type type = PsoType::Opaque;

	MaterialConstants matConstants{};

	UINT NumFramesDirty = globalCountFrameResources;

	std::shared_ptr<GTexture> diffuseMap = nullptr;
	std::shared_ptr<GTexture> normalMap = nullptr;
	

	UINT DiffuseMapIndex = -1;
	UINT NormalMapIndex = -1;

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuTextureHandle;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuTextureHandle;

public:

	MaterialConstants& GetMaterialConstantData();

	UINT GetIndex() const;

	void SetDirty();

	PsoType::Type GetPSO() const;

	void SetNormalMap(std::shared_ptr<GTexture> texture);

	void SetType(PsoType::Type pso);

	void SetDiffuseTexture(std::shared_ptr<GTexture> texture);

	Material(std::wstring name, PsoType::Type pso = PsoType::Opaque);

	void InitMaterial(GMemory& textureHeap);

	void Draw(std::shared_ptr<GCommandList> cmdList) const;

	void Update();

	std::wstring& GetName();

	DirectX::XMFLOAT4 DiffuseAlbedo = DirectX::XMFLOAT4(DirectX::Colors::White);
	DirectX::XMFLOAT3 FresnelR0 = {0.01f, 0.01f, 0.01f};
	float Roughness = .25f;
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};
