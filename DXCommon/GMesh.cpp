#include "pch.h"
#include "GMesh.h"
#include <DirectXMesh.h>
#include "GBuffer.h"
#include "GCommandList.h"
#include "Material.h"


MeshData& GMesh::GetMeshData() const
{
	return data;
}

D3D12_PRIMITIVE_TOPOLOGY GMesh::GetPrimitiveType() const
{
	return primitiveTopology;
}

D3D12_VERTEX_BUFFER_VIEW* GMesh::GetVertexView() 
{
	return &vertexView.value();
}

D3D12_INDEX_BUFFER_VIEW* GMesh::GetIndexView() 
{	
	return &indexView.value();
}

UINT GMesh::GetIndexCount() const
{
	return indexBuffer->GetElementsCount();
}

GMesh::GMesh(MeshData& meshData, std::shared_ptr<GCommandList>& cmdList, D3D12_PRIMITIVE_TOPOLOGY topology): data(meshData), primitiveTopology(topology)
{	
	indexBuffer = std::make_shared<GBuffer>(std::move(GBuffer::CreateBuffer(cmdList, data.GetIndexes().data(), meshData.GetIndexSize(), data.GetIndexes().size(), data.GetName() + L" Indexes")));

	vertexBuffer = std::make_shared<GBuffer>(std::move(GBuffer::CreateBuffer(cmdList, data.GetVertexes().data(), meshData.GetVertexSize(), data.GetVertexes().size(), data.GetName() + L" Vertexes")));

	vertexView = DXLib::Lazy< D3D12_VERTEX_BUFFER_VIEW>([this]
	{
			return	vertexBuffer->VertexBufferView();
	});

	indexView = DXLib::Lazy< D3D12_INDEX_BUFFER_VIEW>([this]
		{
			return	indexBuffer->IndexBufferView();
		});
}


GMesh::GMesh(const GMesh& copy) : data(copy.data), primitiveTopology(copy.primitiveTopology),
                                  vertexBuffer(copy.vertexBuffer), indexBuffer(copy.indexBuffer), vertexView(copy.vertexView), indexView((copy.indexView))
{}
	
std::wstring GMesh::GetName() const
{
	return data.GetName();
}

void GMesh::SetTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
{
	primitiveTopology = topology;
}
