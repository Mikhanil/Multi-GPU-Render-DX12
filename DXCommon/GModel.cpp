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

std::shared_ptr<GModel> GModel::Dublicate(std::shared_ptr<GCommandList> otherDeviceCmdList) const
{
	auto dublicateModel = std::make_shared<GModel>(name);

	for (auto && originMesh : meshes)
	{
		auto dublicateMesh = std::make_shared<GMesh>(originMesh->GetMeshData(), otherDeviceCmdList, originMesh->GetPrimitiveType());
		dublicateModel->AddMesh(std::move(dublicateMesh));
	}

	return dublicateModel;
}

