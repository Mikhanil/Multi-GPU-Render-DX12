#include "pch.h"
#include "GMesh.h"
#include <DirectXMesh.h>
#include "GBuffer.h"
#include "GCommandList.h"
#include "Material.h"
#include "GDevice.h"
#include "GCommandQueue.h"
#include "GCommandList.h"

D3D12_PRIMITIVE_TOPOLOGY GMesh::GetPrimitiveType() const
{
	return primitiveTopology;
}

D3D12_VERTEX_BUFFER_VIEW* GMesh::GetVertexView(std::shared_ptr<GDevice> device) const
{
	const auto it = vertexView.find(device);

	if (it != vertexView.end()) return it->second.get();

	vertexView[device] = std::make_shared<D3D12_VERTEX_BUFFER_VIEW>((vertexBuffers[device]->VertexBufferView()));
	
	
	return vertexView[device].get();
}

D3D12_INDEX_BUFFER_VIEW* GMesh::GetIndexView(std::shared_ptr<GDevice> device) const
{

	const auto it = indexView.find(device);

	if (it != indexView.end()) return it->second.get();

	indexView[device] = std::make_shared<D3D12_INDEX_BUFFER_VIEW>(indexBuffers[device]->IndexBufferView());


	return indexView[device].get();		
}

UINT GMesh::GetIndexCount() const
{
	return indexes.size();
}

GMesh::GMesh(const std::wstring name): meshName(std::move(name)), primitiveTopology(D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
{
	
}

GMesh::GMesh(const GMesh& copy) : meshName(copy.meshName), primitiveTopology(copy.primitiveTopology),
vertices(copy.vertices), indexes(copy.indexes), vertexBuffers(copy.vertexBuffers), indexBuffers(copy.indexBuffers), vertexView(copy.vertexView), indexView((copy.indexView))
{}
	

void GMesh::ChangeIndexes(std::shared_ptr<GCommandList> cmdList, const DWORD* indices, size_t indexesCount)
{
	const auto it = indexBuffers.find(cmdList->GetDevice());

	if(it == indexBuffers.end())
	{
		indexes.resize(indexesCount);

		for (int i = 0; i < indexesCount; ++i)
		{
			indexes[i] = indices[i];
		}

		const auto indexBuffer = std::make_shared<GBuffer>(std::move(
			GBuffer::CreateBuffer(cmdList, indexes.data(), sizeof(DWORD), indexes.size(), meshName + L" Indexes")));

		indexBuffers[cmdList->GetDevice()] = std::move(indexBuffer);
	}
	else
	{
		indexBuffers[cmdList->GetDevice()] = std::move( std::make_shared<GBuffer>(std::move(
			GBuffer::CreateBuffer(cmdList, indexes.data(), sizeof(DWORD), indexes.size(), meshName + L" Indexes"))));
		
	}
	
}

void GMesh::ChangeVertices(std::shared_ptr<GCommandList> cmdList, const Vertex* vertixes, size_t vertexesCount)
{
	const auto it = vertexBuffers.find(cmdList->GetDevice());

	if (it == vertexBuffers.end())
	{
		vertices.resize(vertexesCount);

		for (int i = 0; i < vertexesCount; ++i)
		{
			vertices[i] = vertixes[i];
		}

		const auto vertexBuffer = std::make_shared<GBuffer>(std::move(GBuffer::CreateBuffer(cmdList, vertices.data(), sizeof(Vertex), vertices.size(), meshName + L" Vertexes")));

		vertexBuffers[cmdList->GetDevice()] = std::move(vertexBuffer);
	}
	else
	{
		vertexBuffers[cmdList->GetDevice()] = std::move(std::make_shared<GBuffer>(std::move(GBuffer::CreateBuffer(cmdList, vertices.data(), sizeof(Vertex), vertices.size(), meshName + L" Vertexes"))));

	}	
}

void GMesh::SetName(const std::wstring& name)
{
	meshName = name;
}

std::wstring GMesh::GetName() const
{
	return meshName;
}

void GMesh::SetTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
{
	primitiveTopology = topology;
}

void GMesh::DublicateGraphicData(std::shared_ptr<GDevice> device)
{
	auto queue = device->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
	auto cmdList = queue->GetCommandList();

	ChangeIndexes(cmdList, indexes.data(), indexes.size());
	ChangeVertices(cmdList, vertices.data(), vertices.size());

	queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
}
