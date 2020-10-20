#include "pch.h"
#include "GMesh.h"
#include <DirectXMesh.h>

#include <utility>
#include "GBuffer.h"
#include "GCommandList.h"
#include "NativeModel.h"


std::shared_ptr<NativeMesh> GMesh::GetMeshData() const
{
	return mesh;
}

D3D12_PRIMITIVE_TOPOLOGY GMesh::GetPrimitiveType() const
{
	return mesh->topology;
}

D3D12_VERTEX_BUFFER_VIEW* GMesh::GetVertexView() const
{
	return vertexBuffer->VertexBufferView();
}

D3D12_INDEX_BUFFER_VIEW* GMesh::GetIndexView() const
{	
	return indexBuffer->IndexBufferView();
}

GMesh::GMesh(const std::shared_ptr<NativeMesh>& data, std::shared_ptr<GCommandList>& cmdList): mesh(std::move(data))
{	
	indexBuffer = std::make_shared<GBuffer>(std::move(GBuffer::CreateBuffer(cmdList, mesh->GetIndexes().data(), mesh->GetIndexSize(), mesh->GetIndexes().size(), mesh->GetName() + L" Indexes")));

	vertexBuffer = std::make_shared<GBuffer>(std::move(GBuffer::CreateBuffer(cmdList, mesh->GetVertexes().data(), mesh->GetVertexSize(), mesh->GetVertexes().size(), mesh->GetName() + L" Vertexes")));	
}


void GMesh::Draw(std::shared_ptr<GCommandList> cmdList) const
{
	cmdList->SetVBuffer(0, 1, vertexBuffer->VertexBufferView());
	cmdList->SetIBuffer(indexBuffer->IndexBufferView());
	cmdList->SetPrimitiveTopology(mesh->topology);
	cmdList->DrawIndexed(mesh->GetIndexCount());
}

std::wstring GMesh::GetName() const
{
	return mesh->GetName();
}
