#include "Mesh.h"
#include <DirectXMesh.h>
#include "GBuffer.h"
#include "GCommandList.h"
#include "Material.h"


D3D12_PRIMITIVE_TOPOLOGY Mesh::GetPrimitiveType() const
{
	return primitiveTopology;
}

D3D12_VERTEX_BUFFER_VIEW* Mesh::GetVertexView() const
{
	if(vertexView == nullptr)
	{
		vertexView = std::make_shared<D3D12_VERTEX_BUFFER_VIEW>((vertexBuffer->VertexBufferView()));
	}
	
	return vertexView.get();
}

D3D12_INDEX_BUFFER_VIEW* Mesh::GetIndexView() const
{
	if (indexView == nullptr)
	{
		indexView = std::make_shared<D3D12_INDEX_BUFFER_VIEW>(indexBuffer->IndexBufferView());
	}

	return indexView.get();
}

UINT Mesh::GetIndexCount() const
{
	return indexBuffer->GetElementsCount();
}

Mesh::Mesh(const std::wstring name): meshName(std::move(name)), primitiveTopology(D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
{
	
}

Mesh::Mesh(const Mesh& copy) : meshName(copy.meshName), primitiveTopology(copy.primitiveTopology),
vertices(copy.vertices), indexes(copy.indexes), vertexBuffer(copy.vertexBuffer), indexBuffer(copy.indexBuffer), vertexView(copy.vertexView), indexView((copy.indexView))
{}
	

void Mesh::ChangeIndexes(std::shared_ptr<GCommandList> cmdList, const DWORD* indices, size_t indexesCount)
{
	if (indexBuffer != nullptr) indexBuffer.reset();

	indexes.resize(indexesCount);

	for (int i = 0; i < indexesCount; ++i)
	{
		indexes[i] = indices[i];
	}
	
	indexBuffer = std::make_shared<GBuffer>(std::move(GBuffer::CreateBuffer(cmdList, indexes.data(), sizeof(DWORD), indexes.size(),  meshName + L" Indexes")));
	
}

void Mesh::ChangeVertices(std::shared_ptr<GCommandList> cmdList, const Vertex* vertixes, size_t vertexesCount)
{
	if (vertexBuffer != nullptr) vertexBuffer.reset();

	vertices.resize(vertexesCount);

	for (int i = 0; i < vertexesCount; ++i)
	{
		vertices[i] = vertixes[i];
	}
		
	vertexBuffer = std::make_shared<GBuffer>(std::move(GBuffer::CreateBuffer(cmdList, vertices.data(), sizeof(Vertex), vertices.size(), meshName + L" Vertexes")));
}

void Mesh::SetName(const std::wstring& name)
{
	meshName = name;
}

std::wstring Mesh::GetName() const
{
	return meshName;
}

void Mesh::SetTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
{
	primitiveTopology = topology;
}
