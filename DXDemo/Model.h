#pragma once
#include "DXAllocator.h"
#include "assimp/scene.h"
#include "ShaderBuffersData.h"
#include "Mesh.h"

class GCommandList;

class Model
{
	friend class ModelRenderer;
	
	custom_vector<Mesh> meshes = DXAllocator::CreateVector<Mesh>();

	static void ProcessNode(std::shared_ptr<Model> model, aiNode* node, const aiScene* scene, std::shared_ptr<GCommandList>);

	static Mesh CreateSubMesh(aiMesh* mesh, std::shared_ptr<Model> model,
		std::shared_ptr<GCommandList> cmdList);

	void Draw(std::shared_ptr<GCommandList> cmdList);

	void UpdateGraphicConstantData(ObjectConstants constantData);

	static std::shared_ptr<Model> LoadFromFile(const std::string& filePath, std::shared_ptr<GCommandList> cmdList);

	std::wstring name;

	UINT GetMeshesCount() const;

	void SetMeshMaterial(const UINT submesh, const UINT materiaIindex);


public:
	
	Model(const std::wstring modelName = L"");

	~Model() {};
	
	
};



