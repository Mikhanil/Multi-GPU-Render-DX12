#pragma once
#include "d3d12.h"
#include <SimpleMath.h>

#include "GCommandList.h"
#include "MemoryAllocator.h"


using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;

class NativeModel;
class Material;
class GMesh;

class GModel
{
    std::shared_ptr<NativeModel> model;

    std::vector<std::shared_ptr<GMesh>> gmeshes{};

    custom_vector<std::shared_ptr<Material>> meshesMaterials = MemoryAllocator::CreateVector<std::shared_ptr<
        Material>>();

public:
    DirectX::SimpleMath::Matrix scaleMatrix = DirectX::SimpleMath::Matrix::CreateScale(1);

    UINT GetMeshesCount() const;

    std::shared_ptr<Material> GetMeshMaterial(UINT index);

    std::shared_ptr<GMesh> GetMesh(UINT index);

    std::wstring GetName() const;;

    GModel(const std::shared_ptr<NativeModel>& model, std::shared_ptr<GCommandList> uploadCmdList);
    void SetMeshMaterial(UINT index, const std::shared_ptr<Material>& material);

    GModel(const GModel& copy);;

    ~GModel();

    void Draw(const std::shared_ptr<GCommandList>& cmdList) const;

    std::shared_ptr<GModel> Dublicate(std::shared_ptr<GCommandList> otherDeviceCmdList) const;
};
