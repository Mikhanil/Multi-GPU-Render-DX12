#pragma once
#include <string>

#include "DirectXBuffers.h"
#include "DXAllocator.h"
#include "GBuffer.h"
#include "ShaderBuffersData.h"
#include "Lazy.h"


class Material;
class Model;
class GCommandList;

class Mesh
{
	friend Model;
	
	std::wstring meshName;
	
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	std::vector<Vertex> vertices;
	std::vector<DWORD> indexes;
	
	std::shared_ptr<GBuffer> vertexBuffer = nullptr;
	std::shared_ptr<GBuffer> indexBuffer = nullptr;


	mutable std::shared_ptr<D3D12_VERTEX_BUFFER_VIEW> vertexView = nullptr;
	mutable std::shared_ptr<D3D12_INDEX_BUFFER_VIEW> indexView = nullptr;


	
public:
	D3D12_PRIMITIVE_TOPOLOGY GetPrimitiveType() const;;
	D3D12_VERTEX_BUFFER_VIEW* GetVertexView() const;
	D3D12_INDEX_BUFFER_VIEW* GetIndexView() const;

	UINT GetIndexCount() const;

	Mesh(const std::wstring name = L"");

	Mesh(const Mesh& copy);

	void ChangeIndexes(std::shared_ptr<GCommandList> cmdList,const DWORD* indices, size_t indexesCount);
	
	void ChangeVertices(std::shared_ptr<GCommandList> cmdList, const Vertex* vertices, size_t vertexesCount);

	void SetName(const std::wstring& name);

	void SetMaterial(const std::shared_ptr<Material> material);

	std::wstring GetName() const;

	void SetTopology(D3D12_PRIMITIVE_TOPOLOGY topology);	
};




