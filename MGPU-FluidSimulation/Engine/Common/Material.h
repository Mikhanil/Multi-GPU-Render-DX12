#pragma once

#include <DirectXColors.h>
#include "d3dUtil.h"
#include "d3dx12.h"
#include "GraphicPSO.h"
#include "ShaderBuffersData.h"
#include "GTexture.h"

using namespace Microsoft::WRL;

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

class Material
{
    static UINT materialIndexGlobal;

    UINT materialIndex = -1;

    std::wstring Name;

    RenderMode type = RenderMode::Opaque;

    MaterialConstants matConstants{};

    UINT NumFramesDirty = globalCountFrameResources;

    std::shared_ptr<GTexture> diffuseMap = nullptr;
    std::shared_ptr<GTexture> normalMap = nullptr;


    UINT DiffuseMapIndex = -1;
    UINT NormalMapIndex = -1;

    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuTextureHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuTextureHandle;

public:
    UINT GetMaterialIndex() const
    {
        return materialIndex;
    }

    void SetMaterialIndex(const UINT index)
    {
        materialIndex = index;
    }

    std::shared_ptr<GTexture> GetDiffuseTexture() const
    {
        return diffuseMap;
    }

    std::shared_ptr<GTexture> GetNormalTexture() const
    {
        return normalMap;
    }

    UINT GetDiffuseMapIndex() const
    {
        return DiffuseMapIndex;
    }

    UINT GetNormalMapDiffuseIndex() const
    {
        return NormalMapIndex;
    }

    MaterialConstants& GetMaterialConstantData();

    UINT GetIndex() const;

    void SetDirty();

    RenderMode GetPSO() const;

    void SetNormalMap(const std::shared_ptr<GTexture>& texture, UINT index);

    void SetType(RenderMode pso);

    void SetDiffuseTexture(const std::shared_ptr<GTexture>& texture, UINT index);

    Material(std::wstring name, RenderMode pso = RenderMode::Opaque);

    void InitMaterial(GDescriptor* textureHeap) const;

    void Update();

    std::wstring& GetName();

    Vector4 DiffuseAlbedo = DirectX::XMFLOAT4(DirectX::Colors::White);
    Vector3 FresnelR0 = {0.01f, 0.01f, 0.01f};
    float Roughness = .25f;
    Matrix MatTransform = Matrix::Identity;
};
