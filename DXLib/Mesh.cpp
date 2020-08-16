#include "Mesh.h"
#include <DirectXMesh.h>
#include "GBuffer.h"
#include "GCommandList.h"

void Mesh::UpdateGraphicConstantData(ObjectConstants data)
{
	constantData.TextureTransform = data.TextureTransform;
	constantData.World = data.World;
	
	objectConstantBuffer->CopyData(0, constantData);
}

void Mesh::Draw(std::shared_ptr<GCommandList> cmdList, UINT constantDataSlot) const
{
	cmdList->SetRootConstantBufferView(constantDataSlot,
		objectConstantBuffer->Resource()->GetGPUVirtualAddress());
	cmdList->SetVBuffer(0, 1, &vertexBuffer->VertexBufferView());
	cmdList->SetIBuffer(&indexBuffer->IndexBufferView());
	cmdList->SetPrimitiveTopology(primitiveTopology);
	cmdList->DrawIndexed(indexBuffer->GetElementsCount(), 1, 0, 0, 0);
}

void Mesh::CalculateTangent(UINT i1, UINT i2, UINT i3, Vertex* vertices)
{
	UINT idx[3];
	idx[0] = i1;
	idx[1] = i2;
	idx[2] = i3;

	DirectX::XMFLOAT3 pos[3];
	pos[0] = vertices[i1].Position;
	pos[1] = vertices[i2].Position;
	pos[2] = vertices[i3].Position;

	DirectX::XMFLOAT3 normals[3];
	normals[0] = vertices[i1].Normal;
	normals[1] = vertices[i2].Normal;
	normals[2] = vertices[i3].Normal;

	DirectX::XMFLOAT2 t[3];
	t[0] = vertices[i1].TexCord;
	t[1] = vertices[i2].TexCord;
	t[2] = vertices[i3].TexCord;

	DirectX::XMFLOAT4 tangent[3];

	ComputeTangentFrame(idx, 1, pos, normals, t, 3, tangent);

	vertices[i1].TangentU = Vector3(tangent[0].x, tangent[0].y, tangent[0].z);
	vertices[i2].TangentU = Vector3(tangent[1].x, tangent[1].y, tangent[1].z);
	vertices[i3].TangentU = Vector3(tangent[2].x, tangent[2].y, tangent[2].z);
}

void Mesh::RecalculateTangent(DWORD* indices, size_t indexesCount, Vertex* vertices)
{
	for (UINT i = 0; i < indexesCount - 3; i += 3)
	{
		CalculateTangent(indices[i], indices[i + 1], indices[i + 2], vertices);
	}	
}

Mesh::Mesh(const std::wstring name): meshName(std::move(name)), primitiveTopology(D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
{
	objectConstantBuffer = std::make_unique<ConstantBuffer<ObjectConstants>>(1);
}

void Mesh::ChangeIndexes(std::shared_ptr<GCommandList> cmdList, DWORD* indices, size_t indexesCount)
{
	if (indexBuffer != nullptr) indexBuffer.reset();

	indexes.resize(indexesCount);

	for (int i = 0; i < indexesCount; ++i)
	{
		indexes[i] = indices[i];
	}
	
	indexBuffer = std::make_unique<GBuffer>(std::move(GBuffer::CreateBuffer(cmdList, indexes.data(), sizeof(DWORD), indexes.size(),  meshName + L" Indexes")));
	
}

void Mesh::ChangeVertices(std::shared_ptr<GCommandList> cmdList, Vertex* vertixes, size_t vertexesCount)
{
	if (vertexBuffer != nullptr) vertexBuffer.reset();

	vertices.resize(vertexesCount);

	for (int i = 0; i < vertexesCount; ++i)
	{
		vertices[i] = vertixes[i];
	}
		
	vertexBuffer = std::make_unique<GBuffer>(std::move(GBuffer::CreateBuffer(cmdList, vertices.data(), sizeof(Vertex), vertices.size(), meshName + L" Vertexes")));
}

void Mesh::SetName(const std::wstring& name)
{
	meshName = name;
}

std::wstring_view Mesh::GetName() const
{
	return meshName;
}

void Mesh::SetTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
{
	primitiveTopology = topology;
}
