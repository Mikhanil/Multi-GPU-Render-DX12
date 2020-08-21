#include "GBuffer.h"
#include "Model.h"

#include "GeometryGenerator.h"
#include "Mesh.h"
#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/mesh.h"
#include "string"

static Assimp::Importer importer;

void Model::Draw(std::shared_ptr<GCommandList> cmdList)
{
	for (auto&& mesh : meshes)
	{
		mesh->Draw(cmdList, StandardShaderSlot::ObjectData);
	}


	int k = 5;
	
}

void Model::UpdateGraphicConstantData(ObjectConstants constantData)
{
	constantData.TextureTransform = constantData.TextureTransform.Transpose();
	constantData.World = (constantData.World * scaleMatrix).Transpose();
	
	for (auto&& mesh : meshes)
	{
		mesh->UpdateGraphicConstantData(constantData);
	}
}


UINT Model::GetMeshesCount() const
{
	return meshes.size();
}

Model::Model(const std::wstring modelName): name(modelName)
{
}

Model::Model(const Model& copy):  name(copy.name)
{
	meshes.resize(copy.meshes.size());

	for (int i = 0; i < copy.meshes.size(); ++i)
	{
		meshes[i] = std::move( std::make_shared<Mesh>(*copy.meshes[i]));
	}	
}


std::shared_ptr<Mesh> Model::GetMesh(const UINT submesh)
{
	return meshes[submesh];
}

void Model::SetMeshMaterial(const UINT submesh, const std::shared_ptr<Material> material)
{
	meshes[submesh]->SetMaterial(material);
}

void Model::AddMesh(const std::shared_ptr<Mesh> mesh)
{
	meshes.push_back(mesh);
}

