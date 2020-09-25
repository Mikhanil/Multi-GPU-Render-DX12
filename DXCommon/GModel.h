#pragma once
#include "GMesh.h"

class GCommandList;

class GModel
{
	friend class ModelRenderer;	
	std::vector<std::shared_ptr<GMesh>> meshes;
	std::wstring name;

public:

	Matrix scaleMatrix = Matrix::CreateScale(1);
	
	UINT GetMeshesCount() const;

	std::wstring GetName() const;

	GModel(const std::wstring modelName = L"");

	GModel(const GModel& copy);
	
	~GModel() {};

	std::shared_ptr<GMesh> GetMesh(const UINT submesh);

	void AddMesh(const std::shared_ptr<GMesh> mesh);

	std::shared_ptr<GModel> Dublicate(std::shared_ptr<GCommandList> otherDeviceCmdList) const;
};



