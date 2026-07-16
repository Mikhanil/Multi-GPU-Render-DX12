#include "Scene.h"

#include "Camera.h"
#include "CameraController.h"
#include "GameObject.h"
#include "GCommandList.h"
#include "GCommandQueue.h"
#include "GDevice.h"
#include "GDeviceFactory.h"
#include "GTexture.h"
#include "Material.h"
#include "ModelRenderer.h"
#include "SkyBox.h"
#include "Transform.h"
#include <assimp/postprocess.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

using namespace DirectX::SimpleMath;
using namespace PEPEngine::Graphics;

namespace
{
    struct SceneModelDesc
    {
        const wchar_t* ModelKey;
        Vector3 Position;
        Vector3 RotationRadians;
        Vector3 Scale;
    };

    struct StationDesc
    {
        Vector3 Position;
        Vector3 RotationRadians;
        Vector3 Scale;
    };

    struct PointLightDesc
    {
        Vector3 Position;
        Vector3 Color;
        float Intensity;
    };

    struct SpotLightDesc
    {
        Vector3 Position;
        Vector3 RotationRadians;
        Vector3 Color;
        float Intensity;
        float Cutoff;
    };

    constexpr float kSceneScale = 3.0f;

    constexpr unsigned int kDx12GeModelPostProcessFlags =
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_FlipUVs |
        aiProcess_PreTransformVertices |
        aiProcess_SortByPType |
        aiProcess_JoinIdenticalVertices;

    Matrix MakeDx12GeLocalMatrix(const Vector3& position, const Vector3& rotationRadians, const Vector3& scale)
    {
        return Matrix::CreateScale(scale) *
            Matrix::CreateRotationX(rotationRadians.x) *
            Matrix::CreateRotationY(rotationRadians.y) *
            Matrix::CreateRotationZ(rotationRadians.z) *
            Matrix::CreateTranslation(position);
    }

    void SetSceneWorldTransform(const std::unique_ptr<GameObject>& object, const Matrix& world)
    {
        object->GetTransform()->SetLocalMatrix(world);
    }

    Vector3 GetDx12GeWorldDirection(const Matrix& world)
    {
        const Vector3 position = Vector3::Transform(Vector3::Zero, world);
        Vector3 direction = Vector3::Transform(Vector3::Backward, world) - position;
        direction.Normalize();
        return direction;
    }

    float CutoffToSpotPower(const float cutoff)
    {
        if (cutoff <= 0.0f || cutoff >= 1.0f)
        {
            return 32.0f;
        }

        return std::log(0.1f) / std::log(cutoff);
    }
}

namespace Common
{
    Scene::Scene(const std::shared_ptr<GDevice>& device) : loader(device)
    {
        bounds.Center = Vector3(0.0f, 4.0f * kSceneScale, 0.0f);
        bounds.Radius = 55.0f * kSceneScale;

        typedGameObjects.resize(static_cast<size_t>(RenderMode::Count));
    }

    void Scene::Initialize(const std::shared_ptr<GCommandList>& cmdList, const float aspectRatio,
                           const Vector3& directionalLightDirection)
    {
        LoadTextures(cmdList);
        LoadModels();
        GenerateMipMaps();
        BuildTextureHeap();
        BuildShapeGeometry();
        BuildMaterials();
        BuildObjects(aspectRatio, directionalLightDirection);
        SortObjects();
        ClearTrackedObjects();
    }

    void Scene::GenerateMipMaps()
    {
        std::vector<GTexture*> generatedMipTextures;

        auto textures = loader.GetTextures();

        for (auto&& texture : textures)
        {
            texture->ClearTrack();

            if (texture->GetD3D12Resource()->GetDesc().Flags != D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
                continue;

            if (!texture->HasMipMap)
            {
                generatedMipTextures.push_back(texture.get());
            }
        }

        auto graphicQueue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Graphics);
        auto graphicList = graphicQueue->GetCommandList();

        for (auto&& texture : generatedMipTextures)
        {
            graphicList->TransitionBarrier(texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        graphicQueue->WaitForFenceValue(graphicQueue->ExecuteCommandList(graphicList));

        const auto computeQueue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Compute);
        auto computeList = computeQueue->GetCommandList();
        GTexture::GenerateMipMaps(computeList, generatedMipTextures.data(), generatedMipTextures.size());
        computeQueue->WaitForFenceValue(computeQueue->ExecuteCommandList(computeList));

        graphicList = graphicQueue->GetCommandList();
        for (auto&& texture : generatedMipTextures)
        {
            graphicList->TransitionBarrier(texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        graphicQueue->WaitForFenceValue(graphicQueue->ExecuteCommandList(graphicList));

        for (auto&& pair : textures)
        {
            pair->ClearTrack();
        }
    }

    void Scene::LoadStudyTexture(const std::shared_ptr<GCommandList>& cmdList)
    {
        auto skyTex = GTexture::LoadTextureFromFile(L"Data\\DX12GE\\Skybox Textures\\snowcube1024.dds", cmdList);
        skyTex->SetName(L"skyTex");
        loader.AddTexture(skyTex);

        auto seamless = GTexture::LoadTextureFromFile(L"Data\\Textures\\seamless_grass.jpg", cmdList);
        seamless->SetName(L"seamless");
        loader.AddTexture(seamless);

        auto defaultNormal = GTexture::LoadTextureFromFile(
            L"Data\\Textures\\default_nmap.dds", cmdList, TextureUsage::Normalmap);
        defaultNormal->SetName(L"defaultNormalMap");
        loader.AddTexture(defaultNormal);
    }

    void Scene::LoadTextures(const std::shared_ptr<GCommandList>& cmdList)
    {
        auto queue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Compute);

        auto graphicCmdList = queue->GetCommandList();
        LoadStudyTexture(graphicCmdList);
        queue->WaitForFenceValue(queue->ExecuteCommandList(graphicCmdList));
    }

    void Scene::LoadModels()
    {
        auto queue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Compute);
        auto cmd = queue->GetCommandList();

        models[L"gaz"] = loader.CreateModelFromFile(
            cmd, "Data\\DX12GE\\Models\\gaz\\scene.gltf", kDx12GeModelPostProcessFlags);
        models[L"audi"] = loader.CreateModelFromFile(
            cmd, "Data\\DX12GE\\Models\\cars\\audi\\scene.gltf", kDx12GeModelPostProcessFlags);
        models[L"buchanka"] = loader.CreateModelFromFile(
            cmd, "Data\\DX12GE\\Models\\cars\\buchanka\\scene.gltf", kDx12GeModelPostProcessFlags);
        models[L"police"] = loader.CreateModelFromFile(
            cmd, "Data\\DX12GE\\Models\\cars\\police\\scene.gltf", kDx12GeModelPostProcessFlags);
        models[L"vintage"] = loader.CreateModelFromFile(
            cmd, "Data\\DX12GE\\Models\\cars\\vintage\\scene.gltf", kDx12GeModelPostProcessFlags);
        models[L"knight"] = loader.CreateModelFromFile(
            cmd, "Data\\DX12GE\\Models\\knight-artorias\\scene.gltf", kDx12GeModelPostProcessFlags);

        queue->WaitForFenceValue(queue->ExecuteCommandList(cmd));
        queue->Flush();
    }

    void Scene::BuildTextureHeap()
    {
        srvHeap = std::move(
            GDeviceFactory::GetDevice(GraphicAdapterPrimary)->AllocateDescriptors(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, static_cast<UINT>(loader.GetLoadTexturesCount())));
    }

    void Scene::BuildShapeGeometry()
    {
        auto queue = GDeviceFactory::GetDevice()->GetCommandQueue();
        auto cmdList = queue->GetCommandList();

        auto sphere = loader.GenerateSphere(cmdList);
        models[L"sphere"] = std::move(sphere);

        auto mirrorSphere = loader.GenerateSphere(cmdList);
        models[L"mirrorSphere"] = std::move(mirrorSphere);

        auto quad = loader.GenerateQuad(cmdList);
        models[L"quad"] = std::move(quad);

        queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
    }

    void Scene::BuildMaterials()
    {
        auto seamless = std::make_shared<Material>(L"seamless", RenderMode::Opaque);
        seamless->FresnelR0 = Vector3(0.02f, 0.02f, 0.02f);
        seamless->Roughness = 0.1f;

        auto tex = loader.GetTextureIndex(L"seamless");
        seamless->SetDiffuseTexture(loader.GetTexture(tex), tex);

        tex = loader.GetTextureIndex(L"defaultNormalMap");

        seamless->SetNormalMap(loader.GetTexture(tex), tex);
        loader.AddMaterial(seamless);

        auto skyBox = std::make_shared<Material>(L"sky", RenderMode::SkyBox);
        skyBox->DiffuseAlbedo = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        skyBox->FresnelR0 = Vector3(0.1f, 0.1f, 0.1f);
        skyBox->Roughness = 1.0f;
        skyBox->SetNormalMap(loader.GetTexture(tex), tex);

        tex = loader.GetTextureIndex(L"skyTex");

        skyBox->SetDiffuseTexture(loader.GetTexture(tex), tex);
        loader.AddMaterial(skyBox);

        auto mirror = std::make_shared<Material>(L"mirror", RenderMode::Reflection);
        mirror->DiffuseAlbedo = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        mirror->FresnelR0 = Vector3(0.95f, 0.95f, 0.95f);
        mirror->Roughness = 0.0f;
        tex = loader.GetTextureIndex(L"seamless");
        mirror->SetDiffuseTexture(loader.GetTexture(tex), tex);
        tex = loader.GetTextureIndex(L"defaultNormalMap");
        mirror->SetNormalMap(loader.GetTexture(tex), tex);
        loader.AddMaterial(mirror);

        auto sceneMaterials = loader.GetMaterials();

        for (auto pair : sceneMaterials)
        {
            pair->InitMaterial(&srvHeap);
        }
    }

    void Scene::BuildObjects(float aspectRatio, const Vector3& directionalLightDirection)
    {
        auto quadRitem = std::make_unique<GameObject>("Quad");
        auto renderer = std::make_shared<ModelRenderer>(GDeviceFactory::GetDevice(), models[L"quad"]);
        models[L"quad"]->SetMeshMaterial(0, loader.GetMaterial(loader.GetMaterialIndex(L"seamless")));
        quadRitem->AddComponent(renderer);
        typedGameObjects[static_cast<uint8_t>(RenderMode::Debug)].push_back(quadRitem.get());
        typedGameObjects[static_cast<uint8_t>(RenderMode::Quad)].push_back(quadRitem.get());
        gameObjects.push_back(std::move(quadRitem));

        auto skySphere = std::make_unique<GameObject>("Sky");
        skySphere->GetTransform()->SetScale({500, 500, 500});
        renderer = std::make_shared<SkyBox>(GDeviceFactory::GetDevice(), models[L"sphere"],
                                            *loader.GetTexture(loader.GetTextureIndex(L"skyTex")).get(), &srvHeap,
                                            loader.GetTextureIndex(L"skyTex"));
        models[L"sphere"]->SetMeshMaterial(0, loader.GetMaterial(loader.GetMaterialIndex(L"sky")));
        skySphere->AddComponent(renderer);
        typedGameObjects[static_cast<uint8_t>(RenderMode::SkyBox)].push_back(skySphere.get());
        gameObjects.push_back(std::move(skySphere));

        reflectionProbeCenters =
        {
            Vector3(0.0f, 10.0f, 0.0f)
        };

        models[L"mirrorSphere"]->SetMeshMaterial(0, loader.GetMaterial(loader.GetMaterialIndex(L"mirror")));
        auto mirrorSphere = std::make_unique<GameObject>("Mirror Sphere");
        mirrorSphere->GetTransform()->SetLocalMatrix(
            MakeDx12GeLocalMatrix(Vector3(0.0f, 10.0f, 0.0f), Vector3::Zero,
                                 Vector3(2.5f, 2.5f, 2.5f)));
        renderer = std::make_shared<ModelRenderer>(GDeviceFactory::GetDevice(), models[L"mirrorSphere"]);
        mirrorSphere->AddComponent(renderer);
        typedGameObjects[static_cast<uint8_t>(RenderMode::Reflection)].push_back(mirrorSphere.get());
        gameObjects.push_back(std::move(mirrorSphere));

        auto sun = std::make_unique<GameObject>("Directional Light");
        auto light = std::make_shared<Light>(Directional);
        light->Direction(directionalLightDirection);
        light->Strength({0.55f, 0.55f, 0.55f});
        sun->AddComponent(light);
        gameObjects.push_back(std::move(sun));

        const std::array<StationDesc, 1> stations =
        {
            StationDesc{Vector3(0.0f, 0.0f, 0.0f), Vector3::Zero, Vector3::One},
            /*StationDesc{Vector3(0.0f, 0.0f, 20.0f), Vector3(0.0f, 4.71238899f, 0.0f), Vector3::One},
            StationDesc{Vector3(-20.0f, 0.0f, 20.0f), Vector3(0.0f, 17.27876282f, 0.0f), Vector3::One},
            StationDesc{Vector3(20.0f, 0.0f, 0.0f), Vector3(0.0f, 10.99557400f, 0.0f), Vector3::One},
            StationDesc{Vector3(20.0f, 0.0f, 20.0f), Vector3(0.0f, 7.85398197f, 0.0f),
                        Vector3(-1.0f, 1.0f, 1.0f)},
            StationDesc{Vector3(0.0f, 0.0f, -20.0f), Vector3(0.0f, 4.71238899f, 0.0f),
                        Vector3(1.0f, 1.0f, -1.0f)},
            StationDesc{Vector3(-20.0f, 0.0f, -20.0f), Vector3(0.0f, 15.70796585f, 0.0f),
                        Vector3(1.0f, 1.0f, -1.0f)},
            StationDesc{Vector3(20.0f, 0.0f, -20.0f), Vector3(0.0f, 6.28318548f, 0.0f), Vector3::One},
            StationDesc{Vector3(-20.0f, 0.0f, 0.0f), Vector3(0.0f, 12.56637192f, 0.0f), Vector3::One},*/
        };

        const std::array<SceneModelDesc, 1> stationChildren =
        {
            SceneModelDesc{L"audi", Vector3(-2.0f, 1.84999990f, 2.0f), Vector3::Zero,
                           Vector3(3.33332992f, 3.33332992f, 3.33332992f)},
            /*SceneModelDesc{L"buchanka", Vector3(-0.80000007f, 1.89999998f, -5.0f), Vector3::Zero,
                           Vector3(3.33299994f, 3.33299994f, 3.33299994f)},
            SceneModelDesc{L"knight", Vector3(-8.0f, 2.15000081f, 0.75f), Vector3(0.0f, 7.06858397f, 0.0f),
                           Vector3(0.00303750f, 0.00303750f, 0.00303750f)},
            SceneModelDesc{L"police", Vector3(4.0f, 2.30565000f, 8.10000038f), Vector3(0.0f, 4.71238899f, 0.0f),
                           Vector3::One},
            SceneModelDesc{L"vintage", Vector3(-6.5f, 1.85408890f, 7.75f), Vector3(0.0f, 7.85398197f, 0.0f),
                           Vector3(2.0f, 2.0f, 2.0f)},*/
        };

        auto addRenderable = [&](const SceneModelDesc& desc, const Matrix* parentWorld) -> Matrix
        {
            const auto modelIt = models.find(desc.ModelKey);
            assert(modelIt != models.end() && modelIt->second != nullptr);

            auto object = CreateGOWithRenderer(modelIt->second);
            Matrix world = MakeDx12GeLocalMatrix(desc.Position, desc.RotationRadians, desc.Scale);
            if (parentWorld != nullptr)
            {
                world = world * *parentWorld;
            }
            else
            {
                world = world * Matrix::CreateScale(kSceneScale);
            }
            SetSceneWorldTransform(object, world);
            typedGameObjects[static_cast<uint8_t>(RenderMode::Opaque)].push_back(object.get());
            gameObjects.push_back(std::move(object));
            return world;
        };

        std::array<Matrix, 9> stationWorlds{};
        for (UINT i = 0; i < stations.size(); ++i)
        {
            const auto& station = stations[i];
            stationWorlds[i] = addRenderable(
                SceneModelDesc{L"gaz", station.Position, station.RotationRadians, station.Scale}, nullptr);

            for (const auto& child : stationChildren)
            {
                addRenderable(child, &stationWorlds[i]);
            }
        }

        const std::array<PointLightDesc, 58> stationPointLights =
        {
            PointLightDesc{Vector3(1.89999998f, 4.56666660f, 1.00000000f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.52499998f},
            PointLightDesc{Vector3(1.89999998f, 5.00000000f, -7.43333340f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 3.53333354f, -6.23333311f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 4.26666689f, -6.23333311f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 5.00000000f, -6.23333311f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 3.53333354f, -5.03333282f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 4.26666689f, -5.03333282f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 5.00000000f, -5.03333282f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 3.53333354f, -3.83333302f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 4.26666689f, -3.83333302f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 5.00000000f, -3.83333302f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 2.96666646f, 1.00000000f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 3.53333354f, -2.63333321f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 4.26666689f, -2.63333321f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 5.00000000f, -2.63333321f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(2.46666670f, 6.00000000f, 4.23333311f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(2.46666670f, 5.40000010f, 4.23333311f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-8.73333359f, 6.23333359f, 3.39999986f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-8.73333359f, 6.23333359f, 2.76666665f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(1.93333340f, 6.26666641f, -8.96666622f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(0.87134171f, 6.26666641f, -8.33954239f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-0.19064999f, 6.26666641f, -7.71241808f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(6.70000029f, 6.53333330f, 3.73333335f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(-1.25264168f, 6.26666641f, -7.08529377f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-2.31463337f, 6.26666641f, -6.45816946f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-3.37662506f, 6.26666641f, -5.83104515f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-4.43861675f, 6.26666641f, -5.20392084f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-5.50060844f, 6.26666641f, -4.57679653f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-5.79999971f, 6.26666641f, -4.40000010f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-5.79999971f, 6.26666641f, -3.16666675f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-5.79999971f, 6.26666641f, -1.93333340f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-5.79999971f, 6.26666641f, -0.70000005f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-5.79999971f, 6.26666641f, 0.53333330f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(5.36666679f, 6.53333330f, 3.73333335f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(-5.79999971f, 6.26666641f, 1.76666665f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-5.79999971f, 6.26666641f, 1.96666670f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-4.56666660f, 6.26666641f, 1.96666670f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-3.33333325f, 6.26666641f, 1.96666670f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-2.09999990f, 6.26666641f, 1.96666670f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-0.86666656f, 6.26666641f, 1.96666670f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(0.36666679f, 6.26666641f, 1.96666670f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(1.60000014f, 6.26666641f, 1.96666670f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-4.90000010f, 5.93333292f, -0.06666667f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-4.90000010f, 4.75343513f, -0.42576602f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 3.53333354f, -8.63333321f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(-4.90000010f, 3.57353735f, -0.78486538f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-4.90000010f, 2.39363956f, -1.14396477f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-4.90000010f, 5.93333292f, -2.03333330f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-4.90000010f, 4.69999981f, -2.03333330f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-4.90000010f, 3.46666646f, -2.03333330f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-4.90000010f, 2.23333311f, -2.03333330f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-1.89999998f, 5.66666651f, -0.69999999f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-1.89999998f, 5.66666651f, -1.93333340f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(-1.89999998f, 5.66666651f, -3.16666675f), Vector3(0.54901999f, 0.00000000f, 0.85490000f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 4.26666689f, -8.63333321f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 5.00000000f, -8.63333321f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 3.53333354f, -7.43333340f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
            PointLightDesc{Vector3(1.89999998f, 4.26666689f, -7.43333340f), Vector3(1.00000000f, 0.87058997f, 0.71372998f), 0.37500000f},
        };

        const std::array<SpotLightDesc, 6> stationSpotLights =
        {
            SpotLightDesc{Vector3(-6.00000000f, 5.00000000f, -9.00000000f), Vector3(1.57079637f, 0.00000000f, 0.00000000f), Vector3(1.00000000f, 0.00000000f, 0.00000000f), 8.00000000f, 0.90000004f},
            SpotLightDesc{Vector3(-6.00000000f, 5.00000000f, -6.50000000f), Vector3(1.57079637f, 0.00000000f, 0.00000000f), Vector3(0.00000000f, 1.00000000f, 0.00000000f), 8.00000000f, 0.90000004f},
            SpotLightDesc{Vector3(-6.00000000f, 5.00000000f, -4.00000000f), Vector3(1.57079637f, 0.00000000f, 0.00000000f), Vector3(0.00000000f, 0.00000000f, 1.00000000f), 8.00000000f, 0.90000004f},
            SpotLightDesc{Vector3(-6.00000000f, 5.00000000f, -1.50000000f), Vector3(1.57079637f, 0.00000000f, 0.00000000f), Vector3(1.00000000f, 1.00000000f, 0.00000000f), 8.00000000f, 0.90000004f},
            SpotLightDesc{Vector3(-6.00000000f, 5.00000000f, 1.00000000f), Vector3(1.57079637f, 0.00000000f, 0.00000000f), Vector3(0.00000000f, 0.50000000f, 1.00000000f), 8.00000000f, 0.90000004f},
            SpotLightDesc{Vector3(-6.00000000f, 5.00000000f, 3.50000000f), Vector3(1.57079637f, 0.00000000f, 0.00000000f), Vector3(1.00000000f, 0.50000000f, 0.00000000f), 8.00000000f, 0.90000004f},
        };

        for (const auto& stationWorld : stationWorlds)
        {
            for (const auto& desc : stationPointLights)
            {
                auto pointLight = std::make_unique<GameObject>("Point Light");
                pointLight->GetTransform()->SetPosition(Vector3::Transform(desc.Position, stationWorld));

                auto pointLightComponent = std::make_shared<Light>(Point);
                pointLightComponent->Strength(desc.Color * (desc.Intensity * 0.45f));
                pointLightComponent->FalloffStart(1.0f * kSceneScale);
                pointLightComponent->FalloffEnd(18.0f * kSceneScale);
                pointLight->AddComponent(pointLightComponent);
                gameObjects.push_back(std::move(pointLight));
            }

            for (const auto& desc : stationSpotLights)
            {
                Matrix spotWorld = MakeDx12GeLocalMatrix(desc.Position, desc.RotationRadians, Vector3::One) * stationWorld;
                auto spotLight = std::make_unique<GameObject>("Spot Light");
                spotLight->GetTransform()->SetPosition(Vector3::Transform(Vector3::Zero, spotWorld));

                auto spotLightComponent = std::make_shared<Light>(Spot);
                spotLightComponent->Strength(desc.Color * (desc.Intensity * 0.45f));
                spotLightComponent->Direction(GetDx12GeWorldDirection(spotWorld));
                spotLightComponent->FalloffStart(1.0f * kSceneScale);
                spotLightComponent->FalloffEnd(18.0f * kSceneScale);
                spotLightComponent->SpotPower(CutoffToSpotPower(desc.Cutoff));
                spotLight->AddComponent(spotLightComponent);
                gameObjects.push_back(std::move(spotLight));
            }
        }

        {
            Matrix playerWorld =
                MakeDx12GeLocalMatrix(
                    Vector3(-6.10832024f, 2.59706879f, 1.97008371f),
                    Vector3(0.0f, 31.60341072f, 0.0f),
                    Vector3(0.33333001f, 0.33333001f, 0.33333001f)) *
                Matrix::CreateScale(kSceneScale);

            Matrix flashlightWorld =
                MakeDx12GeLocalMatrix(Vector3::Zero, Vector3::Zero, Vector3::One) * playerWorld;

            auto flashlight = std::make_unique<GameObject>("Player Flashlight");
            flashlight->GetTransform()->SetPosition(Vector3::Transform(Vector3::Zero, flashlightWorld));

            auto flashlightComponent = std::make_shared<Light>(Spot);
            flashlightComponent->Strength(Vector3(1.0f, 1.0f, 1.0f) * 2.0f);
            flashlightComponent->Direction(GetDx12GeWorldDirection(flashlightWorld));
            flashlightComponent->FalloffStart(1.0f * kSceneScale);
            flashlightComponent->FalloffEnd(18.0f * kSceneScale);
            flashlightComponent->SpotPower(CutoffToSpotPower(0.84999996f));
            flashlight->AddComponent(flashlightComponent);
            gameObjects.push_back(std::move(flashlight));
        }

        auto mainCamera = std::make_unique<GameObject>("MainCamera");
        mainCamera->GetTransform()->SetPosition(Vector3(0.0f, 20.0f, -55.0f));
        mainCamera->GetTransform()->SetEulerRotate(Vector3(18.0f, 180.0f, 0.0f));
        sceneCamera = std::make_shared<Camera>(aspectRatio);
        sceneCamera->SetFov(60.0f);
        sceneCamera->SetNearZ(0.1f);
        sceneCamera->SetFarZ(10000.0f);
        mainCamera->AddComponent(sceneCamera);
        mainCamera->AddComponent(std::make_shared<CameraController>(8.0f));
        gameObjects.push_back(std::move(mainCamera));
    }

    std::unique_ptr<GameObject> Scene::CreateGOWithRenderer(const std::shared_ptr<GModel>& model) const
    {
        assert(model != nullptr);

        auto object = std::make_unique<GameObject>();
        auto renderer = std::make_shared<ModelRenderer>(GDeviceFactory::GetDevice(), model);
        object->AddComponent(renderer);
        return object;
    }

    void Scene::SortObjects()
    {
        lights.clear();

        for (auto&& item : gameObjects)
        {
            auto light = item->GetComponent<Light>();
            if (light != nullptr)
            {
                lights.push_back(light.get());
            }

            auto cam = item->GetComponent<Camera>();
            if (cam != nullptr)
            {
                sceneCamera = cam;
            }
        }

        std::sort(lights.begin(), lights.end(), [](const Light* a, const Light* b) { return a->Type() < b->Type(); });
    }

    void Scene::ClearTrackedObjects()
    {
        loader.ClearTrackedObjects();
    }

    void Scene::Update()
    {
        for (auto& object : gameObjects)
        {
            object->Update();
        }
    }

    void Scene::UpdateMaterials(FrameResource* frameResource)
    {
        auto currentMaterialBuffer = frameResource->MaterialBuffer.get();

        for (auto&& material : loader.GetMaterials())
        {
            material->Update();
            auto constantData = material->GetMaterialConstantData();
            currentMaterialBuffer->CopyData(material->GetIndex(), constantData);
        }
    }

    void Scene::Draw(const std::shared_ptr<GCommandList>& cmdList, const RenderMode mode) const
    {
        const auto index = static_cast<size_t>(mode);
        if (index >= typedGameObjects.size())
        {
            return;
        }

        for (auto* object : typedGameObjects[index])
        {
            object->Draw(cmdList);
        }
    }

    GDescriptor* Scene::GetSrvHeap()
    {
        return &srvHeap;
    }

    std::shared_ptr<Camera> Scene::GetCamera() const
    {
        return sceneCamera;
    }

    const std::array<Vector3, Scene::ReflectionProbeCount>& Scene::GetReflectionProbeCenters() const
    {
        return reflectionProbeCenters;
    }

    const std::vector<Light*>& Scene::GetLights() const
    {
        return lights;
    }

    DirectX::BoundingSphere Scene::GetBounds() const
    {
        return bounds;
    }

    size_t Scene::GetObjectCount() const
    {
        return gameObjects.size();
    }

    size_t Scene::GetMaterialCount()
    {
        return loader.GetMaterials().size();
    }

    size_t Scene::GetTextureCount() const
    {
        return loader.GetLoadTexturesCount();
    }
}
