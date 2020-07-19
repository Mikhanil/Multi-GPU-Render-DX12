#pragma once
#include "Renderer.h"
#include "GraphicBuffer.h"
#include "STLCustomAllocator.h"
#include "assimp/scene.h"


class ModelMesh
{
public:
	ModelMesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, std::string name, custom_vector<Vertex>& vertices,
	          custom_vector<DWORD>& indices, D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	void Update(Transform* transform);

	void Draw(ID3D12GraphicsCommandList* cmdList) const;
	void static CalculateTangent(UINT i1, UINT i2, UINT i3, custom_vector<Vertex>& vertex);

	Material* material;

private:
	ObjectConstants bufferConstant{};
	std::unique_ptr<ConstantBuffer<ObjectConstants>> objectConstantBuffer = nullptr;

	std::string name;


	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType;
	std::unique_ptr<VertexBuffer> vertexBuffer = nullptr;
	std::unique_ptr<IndexBuffer> indexBuffer = nullptr;
};

class ModelRenderer : public Renderer
{
	std::vector<ModelMesh> meshes;

	void ProcessNode(aiNode* node, const aiScene* scene, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

	static ModelMesh ProcessMesh(aiMesh* mesh, const aiScene* scene, ID3D12Device* device,
	                             ID3D12GraphicsCommandList* cmdList);

	void Draw(ID3D12GraphicsCommandList* cmdList) override;

	void Update() override;

public:

	bool AddModel(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::string& filePath);

	UINT GetMeshesCount()
	{
		return meshes.size();
	}

	void SetMeshMaterial(UINT index, Material* material)
	{
		meshes[index].material = material;
	}
};
