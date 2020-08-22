#include "GBuffer.h"
#include "Model.h"

#include "GeometryGenerator.h"
#include "Mesh.h"
#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/mesh.h"
#include "string"


UINT Model::GetMeshesCount() const
{
	return meshes.size();
}

std::wstring Model::GetName() const
{
	return name;
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

void Model::AddMesh(const std::shared_ptr<Mesh> mesh)
{
	meshes.push_back(mesh);
}

