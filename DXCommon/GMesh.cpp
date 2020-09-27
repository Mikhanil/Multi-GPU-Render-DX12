#include "pch.h"
#include "GMesh.h"
#include <DirectXMesh.h>
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

D3D12_VERTEX_BUFFER_VIEW* GMesh::GetVertexView() 
{
	return &vertexView.value();
}

D3D12_INDEX_BUFFER_VIEW* GMesh::GetIndexView() 
{	
	return &indexView.value();
}

GMesh::GMesh(std::shared_ptr<NativeMesh> meshData, std::shared_ptr<GCommandList>& cmdList): mesh(meshData)
{	
	indexBuffer = std::make_shared<GBuffer>(std::move(GBuffer::CreateBuffer(cmdList, mesh->GetIndexes().data(), mesh->GetIndexSize(), mesh->GetIndexes().size(), mesh->GetName() + L" Indexes")));

	vertexBuffer = std::make_shared<GBuffer>(std::move(GBuffer::CreateBuffer(cmdList, mesh->GetVertexes().data(), mesh->GetVertexSize(), mesh->GetVertexes().size(), mesh->GetName() + L" Vertexes")));

	vertexView = DXLib::Lazy< D3D12_VERTEX_BUFFER_VIEW>([this]
	{
			return	vertexBuffer->VertexBufferView();
	});

	indexView = DXLib::Lazy< D3D12_INDEX_BUFFER_VIEW>([this]
		{
			return	indexBuffer->IndexBufferView();
		});
}


GMesh::GMesh(const GMesh& copy) : mesh(copy.mesh),
                                  vertexBuffer(copy.vertexBuffer), indexBuffer(copy.indexBuffer), vertexView(copy.vertexView), indexView((copy.indexView))
{}

void GMesh::Draw(std::shared_ptr<GCommandList> cmdList)
{
	cmdList->SetVBuffer(0, 1, GetVertexView());
	cmdList->SetIBuffer(GetIndexView());
	cmdList->SetPrimitiveTopology(mesh->topology);
	cmdList->DrawIndexed(mesh->GetIndexCount());
}

std::wstring GMesh::GetName() const
{
	return mesh->GetName();
}
