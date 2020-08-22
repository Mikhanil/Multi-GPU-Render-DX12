#pragma once
#include "DXAllocator.h"
#include "assimp/scene.h"
#include "ShaderBuffersData.h"
#include "Mesh.h"

class GCommandList;

class Model
{
	friend class ModelRenderer;	
	std::vector<std::shared_ptr<Mesh>> meshes;
	std::wstring name;

public:

	Matrix scaleMatrix = Matrix::CreateScale(1);
	
	UINT GetMeshesCount() const;

	std::wstring GetName() const;

	Model(const std::wstring modelName = L"");

	Model(const Model& copy);
	
	~Model() {};

	std::shared_ptr<Mesh> GetMesh(const UINT submesh);

	void AddMesh(const std::shared_ptr<Mesh> mesh);
};



