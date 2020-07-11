#pragma once
#include "Component.h"
#include "Material.h"


class Renderer : public Component
{
protected:
	ObjectConstants bufferConstant{};
	std::unique_ptr<ConstantBuffer<ObjectConstants>> objectConstantBuffer = nullptr;
	void SendDataToConstantBuffer();

public:

	Renderer();


	Material* material = nullptr;
	MeshGeometry* mesh = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;


	void Update() override;
	void Draw(ID3D12GraphicsCommandList* cmdList) override;
};
