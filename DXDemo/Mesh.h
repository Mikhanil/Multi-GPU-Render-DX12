#pragma once
#include <string>

#include "DirectXBuffers.h"
#include "DXAllocator.h"
#include "GBuffer.h"
#include "ShaderBuffersData.h"

class Model;
class GCommandList;

class Mesh
{
	friend Model;
	
	ObjectConstants constantData{};
	std::shared_ptr<ConstantBuffer<ObjectConstants>> objectConstantBuffer = nullptr;
	std::wstring meshName;
	
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	custom_vector<Vertex> vertices = DXAllocator::CreateVector<Vertex>();
	custom_vector<DWORD> indexes = DXAllocator::CreateVector<DWORD>();
	
	std::shared_ptr<GBuffer> vertexBuffer = nullptr;
	std::shared_ptr<GBuffer> indexBuffer = nullptr;

	void UpdateGraphicConstantData(ObjectConstants data);

	void Draw(std::shared_ptr<GCommandList> cmdList, UINT constantDataSlot) const;
	
	static void CalculateTangent(UINT i1, UINT i2, UINT i3, Vertex* vertices);

	

public:

	static bool isDebug;
	
	bool isTest = false;
	UINT baseVertex = 0;
	UINT indexCount = 0;
	UINT indexStart = 0;
	
	static void RecalculateTangent(DWORD* indices, size_t indexesCount, Vertex* vertices);

	Mesh(const std::wstring name = L"");

	Mesh(const Mesh& copy);

	void SetVertexBuffer(std::shared_ptr<GBuffer>& vbuffer);
	void SetIndexBuffer(std::shared_ptr<GBuffer>& ibuffer);

	void ChangeIndexes(std::shared_ptr<GCommandList> cmdList,const DWORD* indices, size_t indexesCount);
	
	void ChangeVertices(std::shared_ptr<GCommandList> cmdList, const Vertex* vertices, size_t vertexesCount);

	void SetName(const std::wstring& name);

	void SetMaterialIndex(UINT index);

	std::wstring_view GetName() const;

	void SetTopology(D3D12_PRIMITIVE_TOPOLOGY topology);	
};




