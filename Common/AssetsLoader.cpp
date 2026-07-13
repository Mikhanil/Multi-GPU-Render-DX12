#include "pch.h"
#include "AssetsLoader.h"
#include "GCommandList.h"
#include "GCommandQueue.h"
#include "GeometryGenerator.h"
#include "GMesh.h"
#include "GModel.h"
#include "GraphicPSO.h"
#include "GTexture.h"
#include "Material.h"
#include "NativeModel.h"

#include "assimp/scene.h"
#include "assimp/Importer.hpp"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"

#include <filesystem>


using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;


static Assimp::Importer importer;

static std::unordered_map<std::shared_ptr<NativeMesh>, const aiMaterial*> loadedAiMaterialForMesh;
static std::unordered_map<std::shared_ptr<NativeMesh>, std::vector<UINT>> loadedTexturesForMesh;

static bool TextureFileExists(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

static std::filesystem::path ResolveAiTexturePath(const std::wstring& directory,
                                                  const std::wstring& materialTexturePath)
{
    const std::filesystem::path modelDirectory(directory);
    const std::filesystem::path texturePathFromMaterial(materialTexturePath);

    if (texturePathFromMaterial.is_absolute())
    {
        if (TextureFileExists(texturePathFromMaterial))
        {
            return texturePathFromMaterial.lexically_normal();
        }

        return (modelDirectory / texturePathFromMaterial.filename()).lexically_normal();
    }

    const auto texturePathNearModel = modelDirectory / texturePathFromMaterial;
    if (TextureFileExists(texturePathNearModel))
    {
        return texturePathNearModel.lexically_normal();
    }

    const auto texturePathByFileName = modelDirectory / texturePathFromMaterial.filename();
    if (TextureFileExists(texturePathByFileName))
    {
        return texturePathByFileName.lexically_normal();
    }

    return texturePathNearModel.lexically_normal();
}

static inline std::shared_ptr<GModel> CreateModelFromGenerated(std::shared_ptr<GCommandList> cmdList,
                                                               GeometryGenerator::MeshData generatedData, std::wstring name)
{
    auto nativeMesh = std::make_shared<NativeMesh>(generatedData.Vertices.data(), generatedData.Vertices.size(),
                                                   generatedData.Indices32.data(), generatedData.Indices32.size(),
                                                   D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto nativeModel = std::make_shared<NativeModel>(name);
    nativeModel->AddMesh(std::move(nativeMesh));

    return std::make_shared<GModel>(nativeModel, cmdList);
}

std::shared_ptr<GModel>& AssetsLoader::GenerateSphere(const std::shared_ptr<GCommandList>& cmdList, const float radius,
                                                      const UINT sliceCount,
                                                      const UINT stackCount)
{
    const GeometryGenerator::MeshData sphere = geoGen.CreateSphere(radius, sliceCount, stackCount);

    std::shared_ptr<GModel> model = CreateModelFromGenerated(cmdList, sphere, L"sphere");
    modelMap.emplace(L"sphere", model);
    return modelMap[L"sphere"];
}

std::shared_ptr<GModel>& AssetsLoader::GenerateQuad(const std::shared_ptr<GCommandList>& cmdList, const float x, const float y, const float w,
                                                    const float h, const float depth)
{
    const GeometryGenerator::MeshData genMesh = geoGen.CreateQuad(x, y, w, h, depth);

    auto model = CreateModelFromGenerated(cmdList, genMesh, L"quad");
    modelMap.emplace(L"quad", model);
    return modelMap[L"quad"];
}

static void GetData(const aiMesh* mesh, std::vector<Vertex>& OutVertices, std::vector<DWORD>& OutIndices)
{
    //Get vertices
    for (UINT i = 0; i < mesh->mNumVertices; i++)
    {
        Vertex vertex{};

        vertex.Position.x = mesh->mVertices[i].x;
        vertex.Position.y = mesh->mVertices[i].y;
        vertex.Position.z = mesh->mVertices[i].z;

        vertex.Normal.x = mesh->mNormals[i].x;
        vertex.Normal.y = mesh->mNormals[i].y;
        vertex.Normal.z = mesh->mNormals[i].z;

        if (mesh->HasTextureCoords(0))
        {
            vertex.TexCord.x = mesh->mTextureCoords[0][i].x;
            vertex.TexCord.y = mesh->mTextureCoords[0][i].y;
        }

        if (mesh->HasTangentsAndBitangents())
        {
            vertex.TangentU.x = mesh->mTangents[i].x;
            vertex.TangentU.y = mesh->mTangents[i].y;
            vertex.TangentU.z = mesh->mTangents[i].z;
        }

        OutVertices.emplace_back(vertex);
    }

    for (UINT i = 0; i < mesh->mNumFaces; i++)
    {
        const aiFace face = mesh->mFaces[i];

        for (UINT j = 0; j < face.mNumIndices; j++)
        {
            OutIndices.emplace_back(face.mIndices[j]);
        }
    }
}

static std::shared_ptr<NativeMesh> CreateSubMesh(const aiMesh* mesh, const std::wstring& modelName)
{
    // Data to fill
    std::vector<Vertex> vertices;
    std::vector<DWORD> indices;
    GetData(mesh, vertices, indices);

    RecalculateTangent(indices.data(), indices.size(), vertices.data());

    auto nativeMesh = std::make_shared<NativeMesh>(vertices.data(),
                                                   vertices.size(), indices.data(), indices.size(),
                                                   D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, modelName + L" " + AnsiToWString(mesh->mName.C_Str()));

    return nativeMesh;
}

static std::shared_ptr<GTexture> LoadTextureByAiMaterial(const aiMaterial* material, const aiTextureType type,
                                                         const std::wstring& directory,
                                                         const std::shared_ptr<GCommandList>& cmdList, AssetsLoader* loader)
{
    aiString str;
    material->GetTexture(type, 0, &str);

    std::wstring modelTexturePath(AnsiToWString(str.C_Str()));

    for (auto& ch : modelTexturePath)
    {
        if (ch == L'/')
        {
            ch = L'\\';
        }
    }

    const auto texturePath = ResolveAiTexturePath(directory, modelTexturePath);
    const std::wstring textureName = texturePath.wstring();

    if (std::shared_ptr<GTexture>* texture; loader->TryGetTexture(textureName, texture))
    {
        return *texture;
    }

    OutputDebugStringW((textureName + L"\n").c_str());

    auto texture = GTexture::LoadTextureFromFile(textureName, cmdList,
                                                 type == aiTextureType_DIFFUSE
                                                     ? TextureUsage::Albedo
                                                     : TextureUsage::Normalmap);
    texture->SetName(textureName);
    loader->AddTexture(texture);
    return texture;
}

std::shared_ptr<GTexture> AssetsLoader::LoadTextureByPath(const std::wstring& name,
                                                          const std::wstring& fullPath,
                                                          const std::shared_ptr<GCommandList>& cmdList, const TextureUsage usage)
{
    const auto it = texturesMap.find(name);
    if (it != texturesMap.end()) return textures[it->second];

    OutputDebugStringW((name + L"\n").c_str());

    auto texture = GTexture::LoadTextureFromFile(fullPath, cmdList, usage);
    texture->SetName(name);

    textures.push_back(texture);

    texturesMap[name] = textures.size() - 1;

    return texture;
}

void AssetsLoader::LoadTextureForModel(const std::shared_ptr<GModel>& model, const std::shared_ptr<GCommandList>& cmdList)
{
    for (int i = 0; i < model->GetMeshesCount(); ++i)
    {
        auto nativeMesh = model->GetMesh(i)->GetMeshData();

        const aiMaterial* aiMaterial = loadedAiMaterialForMesh[nativeMesh];

        assert(aiMaterial != nullptr);

        aiString name;
        aiMaterial->Get(AI_MATKEY_NAME, name);

        auto materialName = model->GetName() + L" " + AnsiToWString(name.C_Str());

        auto it = materialsMap.find(materialName);

        std::shared_ptr<Material> material;

        if (it == materialsMap.end())
        {
            material = std::make_shared<Material>(materialName);
            material->FresnelR0 = Vector3::One * 0.05;
            material->Roughness = 0.95;

            const auto modelDirectory = model->GetName().substr(0, model->GetName().find_last_of('\\'));

            auto textureCount = aiMaterial->GetTextureCount(aiTextureType_DIFFUSE);

            std::shared_ptr<GTexture> texture;

            if (textureCount > 0)
            {
                texture = LoadTextureByAiMaterial(aiMaterial, aiTextureType_DIFFUSE, modelDirectory, cmdList, this);
            }
            else
            {
                texture = LoadTextureByPath(L"seamless", L"Data\\Textures\\seamless_grass.jpg", cmdList,
                                            TextureUsage::Diffuse);
            }

            loadedTexturesForMesh[nativeMesh].push_back(texturesMap[texture->GetName()]);

            material->SetDiffuseTexture(texture, texturesMap[texture->GetName()]);

            textureCount = aiMaterial->GetTextureCount(aiTextureType_HEIGHT);

            if (textureCount > 0)
            {
                texture = LoadTextureByAiMaterial(aiMaterial, aiTextureType_HEIGHT, modelDirectory, cmdList, this);
            }
            else
            {
                textureCount = aiMaterial->GetTextureCount(aiTextureType_NORMALS);

                if (textureCount > 0)
                {
                    texture = LoadTextureByAiMaterial(aiMaterial, aiTextureType_NORMALS, modelDirectory, cmdList, this);
                }
                else
                {
                    texture = LoadTextureByPath(L"defaultNormalMap", L"Data\\Textures\\default_nmap.dds", cmdList,
                                                TextureUsage::Normalmap);
                }
            }

            loadedTexturesForMesh[nativeMesh].push_back(texturesMap[texture->GetName()]);
            material->SetNormalMap(texture, texturesMap[texture->GetName()]);
            AddMaterial(material);
        }
        else
        {
            material = materials[it->second];
        }

        model->SetMeshMaterial(i, material);
    }
}

static void RecursivlyLoadMeshes(const std::shared_ptr<NativeModel>& model, const aiNode* node, const aiScene* scene)
{
    for (UINT i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* aMesh = scene->mMeshes[node->mMeshes[i]];
        auto nativeMesh = CreateSubMesh(aMesh, model->GetName());
        loadedAiMaterialForMesh[nativeMesh] = scene->mMaterials[aMesh->mMaterialIndex];
        model->AddMesh((nativeMesh));
    }

    for (UINT i = 0; i < node->mNumChildren; i++)
    {
        RecursivlyLoadMeshes(model, node->mChildren[i], scene);
    }
}

std::shared_ptr<GModel> AssetsLoader::CreateModelFromFile(const std::shared_ptr<GCommandList>& cmdList,
                                                          const std::string& filePath)
{
    return CreateModelFromFile(cmdList, filePath,
                               aiProcess_Triangulate | aiProcess_GenNormals |
                               aiProcess_ConvertToLeftHanded);
}

std::shared_ptr<GModel> AssetsLoader::CreateModelFromFile(const std::shared_ptr<GCommandList>& cmdList,
                                                          const std::string& filePath,
                                                          const unsigned int postProcessFlags)
{
    auto it = modelMap.find(AnsiToWString(filePath));
    if (it != modelMap.end())
    {
        return it->second;
    }


    const aiScene* sceneModel = importer.ReadFile(filePath,
                                                  postProcessFlags);

    assert(sceneModel != nullptr && "Model Path dosen't exist or wrong file");

    auto modelFromFile = std::make_shared<NativeModel>(AnsiToWString(filePath));

    RecursivlyLoadMeshes(modelFromFile, sceneModel->mRootNode, sceneModel);


    auto renderModel = std::make_shared<GModel>(modelFromFile, cmdList);
    LoadTextureForModel(renderModel, cmdList);
    loadedAiMaterialForMesh.clear();
    loadedTexturesForMesh.clear();
    modelMap[AnsiToWString(filePath)] = renderModel;
    return renderModel;
}


AssetsLoader::AssetsLoader(const std::shared_ptr<GDevice>& device): device(device)
{
}


UINT AssetsLoader::GetTextureIndex(const std::wstring& name)
{
    auto it = texturesMap.find(name);
    if (it == texturesMap.end())
    {
        return -1;
    }
    return it->second;
}

UINT AssetsLoader::GetMaterialIndex(const std::wstring& name)
{
    auto it = materialsMap.find(name);
    if (it == materialsMap.end())
    {
        return -1;
    }
    return it->second;
}

std::shared_ptr<GTexture>& AssetsLoader::GetTexture(const UINT index)
{
    return textures[index];
}

std::shared_ptr<Material>& AssetsLoader::GetMaterial(const UINT index)
{
    return materials[index];
}

size_t AssetsLoader::GetLoadTexturesCount() const
{
    return texturesMap.size();
}

void AssetsLoader::AddMaterial(const std::shared_ptr<Material>& material)
{
    materialsMap[material->GetName()] = materials.size();
    materials.push_back((material));
}

void AssetsLoader::AddTexture(const std::shared_ptr<GTexture>& texture)
{
    texturesMap[texture->GetName()] = textures.size();
    textures.push_back((texture));
}

std::vector<std::shared_ptr<Material>>& AssetsLoader::GetMaterials()
{
    return materials;
}

std::vector<std::shared_ptr<GTexture>>& AssetsLoader::GetTextures()
{
    return textures;
}

bool AssetsLoader::TryGetTexture(const std::wstring& name, std::shared_ptr<GTexture>*& OutTexture)
{
    const auto it = texturesMap.find(name);
    if (it == texturesMap.end())
    {
        return false;
    }
    OutTexture = &textures[it->second];
    return true;
}

void AssetsLoader::ClearTrackedObjects()
{
    trackGeneratedData.clear();
}
