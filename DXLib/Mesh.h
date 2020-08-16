#pragma once
#include <string>
#include "DirectXBuffers.h"
#include "ShaderBuffersData.h"
#include "DXAllocator.h"

class GBuffer;
class GCommandList;
class Model;

class Mesh
{
	friend Model;
	ObjectConstants constantData{};
	std::unique_ptr<ConstantBuffer<ObjectConstants>> objectConstantBuffer = nullptr;
	std::wstring meshName;
	
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology;

	custom_vector<Vertex> vertices = DXAllocator::CreateVector<Vertex>();
	custom_vector<DWORD> indexes = DXAllocator::CreateVector<DWORD>();
	
	std::unique_ptr<GBuffer> vertexBuffer = nullptr;
	std::unique_ptr<GBuffer> indexBuffer = nullptr;

	void UpdateGraphicConstantData(ObjectConstants data);

	void Draw(std::shared_ptr<GCommandList> cmdList, UINT constantDataSlot) const;
	
	static void CalculateTangent(UINT i1, UINT i2, UINT i3, Vertex* vertices);

	

public:

	static void RecalculateTangent(DWORD* indices, size_t indexesCount, Vertex* vertices);

	explicit Mesh(const std::wstring name = L"");

	void ChangeIndexes(std::shared_ptr<GCommandList> cmdList, DWORD* indices, size_t indexesCount);
	void ChangeVertices(std::shared_ptr<GCommandList> cmdList, Vertex* vertices, size_t vertexesCount);

	void SetName(const std::wstring& name);

	void SetMaterialIndex(UINT index)
	{
		constantData.MaterialIndex = index;
	}
	
	std::wstring_view GetName() const;

	void SetTopology(D3D12_PRIMITIVE_TOPOLOGY topology);	
};




