#include "Mesh.h"
#include <DirectXMesh.h>
#include "GBuffer.h"
#include "GCommandList.h"
#include "Material.h"
void Mesh::UpdateGraphicConstantData(ObjectConstants data)
{

	constantData.MaterialIndex = material->GetIndex();
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

	cmdList->DrawIndexed( indexBuffer->GetElementsCount());	
	
}

std::shared_ptr<Material> Mesh::GetMaterial() const
{
	return material;
}

Mesh::Mesh(const std::wstring name): meshName(std::move(name)), primitiveTopology(D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
{
	objectConstantBuffer = std::make_unique<ConstantBuffer<ObjectConstants>>(1);
}

Mesh::Mesh(const Mesh& copy) : constantData(copy.constantData), objectConstantBuffer(copy.objectConstantBuffer), meshName(copy.meshName), primitiveTopology(copy.primitiveTopology),
vertices(copy.vertices), indexes(copy.indexes), vertexBuffer(copy.vertexBuffer), indexBuffer(copy.indexBuffer), material( copy.material)
{

}

void Mesh::SetVertexBuffer(std::shared_ptr<GBuffer>& vbuffer)
{
	if (vertexBuffer) vertexBuffer.reset();

	vertexBuffer = vbuffer;
}

void Mesh::SetIndexBuffer(std::shared_ptr<GBuffer>& ibuffer)
{
	if (indexBuffer) indexBuffer.reset();

	indexBuffer = ibuffer;
}


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

void Mesh::SetMaterial(const std::shared_ptr<Material> mat)
{
	material = mat;
}

std::wstring_view Mesh::GetName() const
{
	return meshName;
}

void Mesh::SetTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
{
	primitiveTopology = topology;
}
