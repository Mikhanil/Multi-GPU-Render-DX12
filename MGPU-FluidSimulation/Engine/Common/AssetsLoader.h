#pragma once
#include "GeometryGenerator.h"
#include "GTexture.h"
#include "MemoryAllocator.h"

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;

class GModel;
class GMesh;
class NativeMesh;
class NativeModel;
class Material;

using TexturesMap = std::unordered_map<std::wstring, UINT>;
using MaterialMap = std::unordered_map<std::wstring, UINT>;
using ModelMap = std::unordered_map<std::wstring, std::shared_ptr<GModel>>;

class AssetsLoader
{
    GeometryGenerator geoGen;

    std::vector<std::shared_ptr<GTexture>> textures;
    std::vector<std::shared_ptr<Material>> materials;


    ModelMap modelMap;
    TexturesMap texturesMap;
    MaterialMap materialsMap;


    std::vector<GeometryGenerator::MeshData> trackGeneratedData;

    std::shared_ptr<GTexture> LoadTextureByPath(const std::wstring& name, const std::wstring& fullPath,
                                                const std::shared_ptr<GCommandList>& cmdList, TextureUsage usage);

    void LoadTextureForModel(const std::shared_ptr<GModel>& model, const std::shared_ptr<GCommandList>& cmdList);


    std::shared_ptr<GDevice> device;

public:
    AssetsLoader(const std::shared_ptr<GDevice>& device);

    UINT GetTextureIndex(const std::wstring& name);

    UINT GetMaterialIndex(const std::wstring& name);

    size_t GetLoadTexturesCount() const;

    void AddMaterial(const std::shared_ptr<Material>& material);

    void AddTexture(const std::shared_ptr<GTexture>& texture);

    const std::vector<std::shared_ptr<Material>>& GetMaterials();

    const std::vector<std::shared_ptr<GTexture>>& GetTextures();

    bool TryGetTexture(const std::wstring& name, std::shared_ptr<GTexture>*& OutTexture);
    std::shared_ptr<GTexture>& GetTexture(UINT index);
    std::shared_ptr<Material>& GetMaterial(UINT index);

    std::shared_ptr<GModel>& GenerateSphere(const std::shared_ptr<GCommandList>& cmdList, float radius = 1.0f,
                                            UINT sliceCount = 20, UINT stackCount = 20);

    std::shared_ptr<GModel>& GenerateQuad(const std::shared_ptr<GCommandList>& cmdList, float x = 1.0f, float y = 1.0f,
                                          float w = 1.0f, float h = 1.0f, float depth = 0.0);

    std::shared_ptr<GModel>& CreateModelFromFile(const std::shared_ptr<GCommandList>& cmdList, const std::string& filePath);


    void ClearTrackedObjects();
};
