#pragma once

#include "d3dUtil.h"
#include "DirectXBuffers.h"
#include "Shader.h"
#include "Texture.h"

using namespace Microsoft::WRL;

enum MaterialType
{
	SkyBox,
	Opaque,
	Wireframe,
	AlphaDrop,
	Transparent
};

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
	ComPtr<ID3D12PipelineState> pso;
	ComPtr<ID3D12PipelineState> debugPso;
	ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	std::string Name;

	std::vector<Shader*> shaders;

	MaterialConstants matConstants{};

	UINT NumFramesDirty = globalCountFrameResources;
	UINT cbvSrvUavDescriptorSize = 0;

	std::vector<Texture*> textures{ 1 };
	DXGI_FORMAT backBufferFormat;
	DXGI_FORMAT depthStencilFormat;

	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
	bool ism4xmsaa;
	UINT m4xMsaaQuality = 0;

	static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

	D3D12_INPUT_LAYOUT_DESC GetInputLayoutInfo();

	void CreateRootSignature(ID3D12Device* device);

	void CreatePso(ID3D12Device* device);

	MaterialType type;

public:

	MaterialType GetType();

	void SetDiffuseTexture(Texture* texture);


	Material(ID3D12Device* device, std::string name, MaterialType type, DXGI_FORMAT backBufferFormat,
	         DXGI_FORMAT depthStencilFormat,
	         const std::vector<D3D12_INPUT_ELEMENT_DESC> layout, bool isM4xMsaa, UINT m4xMsaaQuality);

	void AddShader(Shader* shader);

	void InitMaterial(ID3D12Device* device);

	ID3D12PipelineState* GetPSO();

	ID3D12RootSignature* GetRootSignature();

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

