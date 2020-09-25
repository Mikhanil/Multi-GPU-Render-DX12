#pragma once
#include <string>
#include <utility>

#include "GBuffer.h"
#include "ShaderBuffersData.h"
#include "Lazy.h"


class MeshData
{
	std::wstring meshName;
	custom_vector<Vertex> vertices = MemoryAllocator::CreateVector<Vertex>();
	custom_vector<DWORD> indexes = MemoryAllocator::CreateVector<DWORD>();

public:
	MeshData(const Vertex* vertices, size_t vertexesCount, const DWORD* indices, size_t indexesCount, std::wstring name = L""): meshName(
		std::move(name))
	{
		std::copy(vertices, vertices + vertexesCount, std::back_inserter(this->vertices));
		std::copy(indices, indices + indexesCount, std::back_inserter(this->indexes));		
	}

	custom_vector<Vertex>& GetVertexes()
	{
		return vertices;
	}
	
	custom_vector<DWORD>& GetIndexes()
	{
		return indexes;
	}

	std::wstring& GetName()
	{
		return meshName;
	}

	static UINT GetVertexSize()
	{
		return sizeof(Vertex);
	}

	static UINT GetIndexSize()
	{
		return sizeof(DWORD);
	}
};



class GModel;
class GCommandList;

class GMesh
{
	friend GModel;	

	MeshData& data;
	
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology;	
	
	std::shared_ptr<GBuffer> vertexBuffer = nullptr;
	std::shared_ptr<GBuffer> indexBuffer = nullptr;

	DXLib::Lazy< D3D12_VERTEX_BUFFER_VIEW> vertexView;
	DXLib::Lazy< D3D12_INDEX_BUFFER_VIEW> indexView;

	
public:

	MeshData& GetMeshData() const;
	D3D12_PRIMITIVE_TOPOLOGY GetPrimitiveType() const;;
	D3D12_VERTEX_BUFFER_VIEW* GetVertexView() ;
	D3D12_INDEX_BUFFER_VIEW* GetIndexView() ;

	UINT GetIndexCount() const;

	GMesh(MeshData& meshData, std::shared_ptr<GCommandList>& cmdList, D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED);

	GMesh(const GMesh& copy);

	std::wstring GetName() const;

	void SetTopology(D3D12_PRIMITIVE_TOPOLOGY topology);	
};




