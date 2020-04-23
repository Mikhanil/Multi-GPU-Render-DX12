#pragma once
#include "Renderer.h"
#include "GraphicBuffer.h"
#include "assimp/Importer.hpp"
#include "assimp/mesh.h"
#include "assimp/scene.h"


class ModelMesh
{
public:
	ModelMesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, std::vector<Vertex>& vertices, std::vector<DWORD>& indices, D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
	{
		PrimitiveType = topology;
		vertexBuffer = std::make_unique<VertexBuffer>(device, cmdList, vertices.data(), vertices.size());
		indexBuffer = std::make_unique<IndexBuffer>(device, cmdList, DXGI_FORMAT_R32_UINT, indices.data(), indices.size());
	}

	

	void Draw(ID3D12GraphicsCommandList* cmdList)
	{
		cmdList->IASetVertexBuffers(0, 1, &vertexBuffer->VertexBufferView());
		cmdList->IASetIndexBuffer(&indexBuffer->IndexBufferView());
		cmdList->IASetPrimitiveTopology(PrimitiveType);
		cmdList->DrawIndexedInstanced(indexBuffer->GetElementsCount(), 1, 0, 0, 0);
	}
private:
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType;
	std::unique_ptr<VertexBuffer> vertexBuffer = nullptr;
	std::unique_ptr < IndexBuffer> indexBuffer = nullptr;
};

class ModelRenderer : public Renderer
{
	std::vector<ModelMesh> meshes;

	void ProcessNode(aiNode* node, const aiScene* scene, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

	static ModelMesh ProcessMesh(aiMesh* mesh, const aiScene* scene, ID3D12Device* device,
	                             ID3D12GraphicsCommandList* cmdList);

	void Draw(ID3D12GraphicsCommandList* cmdList) override;
	
public:

	bool AddModel(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::string& filePath);
};

