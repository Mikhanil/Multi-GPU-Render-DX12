#pragma once
#include "DXAllocator.h"
#include "assimp/scene.h"
#include "ShaderBuffersData.h"
#include "Mesh.h"

class GCommandList;

class Model
{
	friend class ModelRenderer;
	
	custom_vector<std::shared_ptr<Mesh>> meshes = DXAllocator::CreateVector<std::shared_ptr<Mesh>>();

	

	void Draw(std::shared_ptr<GCommandList> cmdList);

	void UpdateGraphicConstantData(ObjectConstants constantData);


	std::wstring name;

	
	


public:

	Matrix scaleMatrix = Matrix::CreateScale(1);
	
	UINT GetMeshesCount() const;

	std::wstring GetName() const { return name; }
	
	Model(const std::wstring modelName = L"");

	~Model() {};

	std::shared_ptr<Mesh> GetMesh(const UINT submesh)
	{
		return meshes[submesh];
	}
	
	void SetMeshMaterial(const UINT submesh, const std::shared_ptr<Material> material);

	void AddMesh(const std::shared_ptr<Mesh> mesh);
};



