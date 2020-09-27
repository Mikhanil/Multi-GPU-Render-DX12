#pragma once
#include <d3d12.h>
#include <iterator>
#include <string>
#include "MemoryAllocator.h"
#include "ShaderBuffersData.h"

class NativeMesh
{
	std::wstring meshName;
	custom_vector<Vertex> vertices = MemoryAllocator::CreateVector<Vertex>();
	custom_vector<DWORD> indexes = MemoryAllocator::CreateVector<DWORD>();	

public:
	NativeMesh(const Vertex* vertices, size_t vertexesCount, const DWORD* indices, size_t indexesCount, D3D12_PRIMITIVE_TOPOLOGY topology = D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED,  std::wstring name = L"") : meshName(
		std::move(name)), topology(topology)
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

	UINT GetIndexCount() const
	{
		return indexes.size();
	}
	
	std::wstring& GetName()
	{
		return meshName;
	}

	D3D12_PRIMITIVE_TOPOLOGY topology;
	

	static UINT GetVertexSize()
	{
		return sizeof(Vertex);
	}

	static UINT GetIndexSize()
	{
		return sizeof(DWORD);
	}
};


class NativeModel
{
	custom_vector<std::shared_ptr<NativeMesh>> meshes = MemoryAllocator::CreateVector<std::shared_ptr<NativeMesh>>();
	
	std::wstring name;
	
public:

	NativeModel(std::wstring name): name(name) {  }

	void AddMesh(std::shared_ptr<NativeMesh> mesh)
	{
		meshes.push_back(mesh);
	}
	
	UINT GetMeshesCount() const { return  meshes.size(); };

	std::wstring GetName() const { return  name; };

	std::shared_ptr<NativeMesh> GetMesh(UINT index)
	{
		assert(index <= meshes.size());

		return meshes[index];
	}

	
};

