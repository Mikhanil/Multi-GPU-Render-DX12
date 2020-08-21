#pragma once
#include <string>

#include "DirectXBuffers.h"
#include "DXAllocator.h"
#include "GBuffer.h"
#include "ShaderBuffersData.h"

class Material;
class Model;
class GCommandList;

class Mesh
{
	friend Model;
	
	ObjectConstants constantData{};
	std::shared_ptr<ConstantBuffer<ObjectConstants>> objectConstantBuffer = nullptr;
	std::wstring meshName;
	
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	std::vector<Vertex> vertices;
	std::vector<DWORD> indexes;
	
	std::shared_ptr<GBuffer> vertexBuffer = nullptr;
	std::shared_ptr<GBuffer> indexBuffer = nullptr;

	void UpdateGraphicConstantData(ObjectConstants data);

	void Draw(std::shared_ptr<GCommandList> cmdList, UINT constantDataSlot) const;
	
	std::shared_ptr<Material> material = nullptr;

public:

	std::shared_ptr<Material> GetMaterial() const;

	Mesh(const std::wstring name = L"");

	Mesh(const Mesh& copy);

	void SetVertexBuffer(std::shared_ptr<GBuffer>& vbuffer);
	void SetIndexBuffer(std::shared_ptr<GBuffer>& ibuffer);

	void ChangeIndexes(std::shared_ptr<GCommandList> cmdList,const DWORD* indices, size_t indexesCount);
	
	void ChangeVertices(std::shared_ptr<GCommandList> cmdList, const Vertex* vertices, size_t vertexesCount);

	void SetName(const std::wstring& name);

	void SetMaterial(const std::shared_ptr<Material> material);

	std::wstring_view GetName() const;

	void SetTopology(D3D12_PRIMITIVE_TOPOLOGY topology);	
};




