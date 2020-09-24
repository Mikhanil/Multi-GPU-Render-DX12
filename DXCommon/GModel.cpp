#include "pch.h"
#include "GModel.h"
#include "GeometryGenerator.h"
#include "GMesh.h"
#include "string"


UINT GModel::GetMeshesCount() const
{
	return meshes.size();
}

std::wstring GModel::GetName() const
{
	return name;
}

GModel::GModel(const std::wstring modelName): name(modelName)
{
}

GModel::GModel(const GModel& copy):  name(copy.name)
{
	meshes.resize(copy.meshes.size());

	for (int i = 0; i < copy.meshes.size(); ++i)
	{
		meshes[i] = std::move( std::make_shared<GMesh>(*copy.meshes[i]));
	}	
}


std::shared_ptr<GMesh> GModel::GetMesh(const UINT submesh)
{
	return meshes[submesh];
}

void GModel::AddMesh(const std::shared_ptr<GMesh> mesh)
{
	meshes.push_back(mesh);
}

void GModel::DublicateModelData(std::shared_ptr<GDevice> device)
{
	for (auto && mesh : meshes)
	{
		mesh->DublicateGraphicData(device);
	}
}

