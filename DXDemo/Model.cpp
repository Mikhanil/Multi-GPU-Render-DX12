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


void Model::SetMeshMaterial(const UINT submesh, const std::shared_ptr<Material> material)
{
	meshes[submesh]->SetMaterial(material);
}

void Model::AddMesh(const std::shared_ptr<Mesh> mesh)
{
	meshes.push_back(mesh);
}

