#include "Scene.h"

#include "Camera.h"
#include "CameraController.h"
#include "GameObject.h"
#include "GCommandList.h"
#include "GCommandQueue.h"
#include "GDevice.h"
#include "GTexture.h"
#include "Material.h"
#include "ModelRenderer.h"
#include "Orbiter.h"
#include "Rotater.h"
#include "SkyBox.h"
#include "Transform.h"
#include <algorithm>

using namespace DirectX::SimpleMath;
using namespace PEPEngine::Graphics;

namespace Common
{
    Scene::Scene(const std::shared_ptr<GDevice>& device) : device(device), loader(device)
    {
        bounds.Center = Vector3::Zero;
        bounds.Radius = 200.0f;
        typedRenderers.resize(static_cast<size_t>(RenderMode::Count));
    }

    void Scene::Initialize(const float aspectRatio, const Vector3& directionalLightDirection)
    {
        const auto queue = device->GetCommandQueue(GQueueType::Compute);

        auto cmdList = queue->GetCommandList();
        LoadTextures(cmdList);
        queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));

        cmdList = queue->GetCommandList();
        LoadModels(cmdList);
        queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
        queue->Flush();

        GenerateMipMaps();
        BuildTextureHeap();
        BuildMaterials();
        BuildObjects(aspectRatio, directionalLightDirection);
        SortObjects();
    }

    void Scene::LoadTextures(const std::shared_ptr<GCommandList>& cmdList)
    {
        struct TextureDesc { const wchar_t* name; const wchar_t* path; TextureUsage usage; };
        const TextureDesc textures[] = {
            {L"bricksTex", L"Data\\Textures\\bricks2.dds", TextureUsage::Albedo},
            {L"stoneTex", L"Data\\Textures\\stone.dds", TextureUsage::Albedo},
            {L"tileTex", L"Data\\Textures\\tile.dds", TextureUsage::Albedo},
            {L"fenceTex", L"Data\\Textures\\WireFence.dds", TextureUsage::Albedo},
            {L"waterTex", L"Data\\Textures\\water1.dds", TextureUsage::Albedo},
            {L"skyTex", L"Data\\Textures\\skymap.dds", TextureUsage::Albedo},
            {L"grassTex", L"Data\\Textures\\grass.dds", TextureUsage::Albedo},
            {L"treeArrayTex", L"Data\\Textures\\treeArray2.dds", TextureUsage::Albedo},
            {L"seamless", L"Data\\Textures\\seamless_grass.jpg", TextureUsage::Albedo},
            {L"white1x1Tex", L"Data\\Textures\\white1x1.dds", TextureUsage::Albedo},
            {L"bricksNormalMap", L"Data\\Textures\\bricks2_nmap.dds", TextureUsage::Normalmap},
            {L"tileNormalMap", L"Data\\Textures\\tile_nmap.dds", TextureUsage::Normalmap},
            {L"defaultNormalMap", L"Data\\Textures\\default_nmap.dds", TextureUsage::Normalmap},
        };
        for (const auto& textureDesc : textures)
        {
            auto texture = GTexture::LoadTextureFromFile(textureDesc.path, cmdList, textureDesc.usage);
            texture->SetName(textureDesc.name);
            loader.AddTexture(texture);
        }
    }

    void Scene::LoadModels(const std::shared_ptr<GCommandList>& cmdList)
    {
        auto load = [this, &cmdList](const wchar_t* key, const char* path)
        { models[key] = loader.CreateModelFromFile(cmdList, path); };
        load(L"nano", "Data\\Objects\\Nanosuit\\Nanosuit.obj");
        load(L"atlas", "Data\\Objects\\Atlas\\Atlas.obj");
        load(L"pbody", "Data\\Objects\\P-Body\\P-Body.obj");
        load(L"griffon", "Data\\Objects\\Griffon\\Griffon.FBX");
        load(L"mountDragon", "Data\\Objects\\MOUNTAIN_DRAGON\\MOUNTAIN_DRAGON.FBX");
        load(L"desertDragon", "Data\\Objects\\DesertDragon\\DesertDragon.FBX");
        load(L"stair", "Data\\Objects\\Temple\\SM_AsianCastle_A.FBX");
        load(L"columns", "Data\\Objects\\Temple\\SM_AsianCastle_E.FBX");
        load(L"fountain", "Data\\Objects\\Temple\\SM_Fountain.FBX");
        load(L"platform", "Data\\Objects\\Temple\\SM_PlatformSquare.FBX");
        load(L"doom", "Data\\Objects\\DoomSlayer\\doommarine.obj");
        models[L"griffon"]->scaleMatrix = Matrix::CreateScale(0.1f);
        models[L"mountDragon"]->scaleMatrix = Matrix::CreateScale(0.1f);
        models[L"desertDragon"]->scaleMatrix = Matrix::CreateScale(0.1f);
        models[L"sphere"] = loader.GenerateSphere(cmdList);
        models[L"mirrorSphere"] = loader.GenerateSphere(cmdList);
        models[L"quad"] = loader.GenerateQuad(cmdList);
    }

    void Scene::GenerateMipMaps()
    {
        std::vector<GTexture*> generated;
        for (const auto& texture : loader.GetTextures())
            if (texture->GetD3D12Resource()->GetDesc().Flags == D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS && !texture->HasMipMap)
                generated.push_back(texture.get());
        if (!generated.empty())
        {
            const auto graphicsQueue = device->GetCommandQueue(GQueueType::Graphics);
            auto graphicsList = graphicsQueue->GetCommandList();
            for (auto* texture : generated)
            {
                graphicsList->TransitionBarrier(texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            graphicsQueue->WaitForFenceValue(graphicsQueue->ExecuteCommandList(graphicsList));

            const auto computeQueue = device->GetCommandQueue(GQueueType::Compute);
            auto computeList = computeQueue->GetCommandList();
            GTexture::GenerateMipMaps(computeList, generated.data(), generated.size());
            computeQueue->WaitForFenceValue(computeQueue->ExecuteCommandList(computeList));

            graphicsList = graphicsQueue->GetCommandList();
            for (auto* texture : generated)
            {
                graphicsList->TransitionBarrier(
                    texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
            graphicsQueue->WaitForFenceValue(graphicsQueue->ExecuteCommandList(graphicsList));
        }
        for (const auto& texture : loader.GetTextures()) texture->ClearTrack();
    }

    void Scene::BuildTextureHeap()
    {
        srvHeap = std::move(device->AllocateDescriptors(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            static_cast<UINT>(loader.GetLoadTexturesCount())));
    }

    void Scene::BuildMaterials()
    {
        auto seamless = std::make_shared<Material>(L"seamless", RenderMode::Opaque);
        seamless->FresnelR0 = Vector3(0.02f); seamless->Roughness = 0.1f;
        auto diffuse = loader.GetTextureIndex(L"seamless");
        auto normal = loader.GetTextureIndex(L"defaultNormalMap");
        seamless->SetDiffuseTexture(loader.GetTexture(diffuse), diffuse); seamless->SetNormalMap(loader.GetTexture(normal), normal);
        loader.AddMaterial(seamless);
        models[L"quad"]->SetMeshMaterial(0, seamless);
        auto mirror = std::make_shared<Material>(L"mirror", RenderMode::Reflection);
        mirror->FresnelR0 = Vector3(0.98f); mirror->Roughness = 0.02f;
        mirror->SetDiffuseTexture(loader.GetTexture(diffuse), diffuse); mirror->SetNormalMap(loader.GetTexture(normal), normal);
        loader.AddMaterial(mirror);
        models[L"mirrorSphere"]->SetMeshMaterial(0, mirror);
        for (const auto& material : loader.GetMaterials()) material->InitMaterial(&srvHeap);
    }

    std::shared_ptr<Renderer> Scene::CreateRenderer(const std::wstring& modelName) const
    { return std::make_shared<ModelRenderer>(device, models.at(modelName)); }
    void Scene::AddObjectRenderer(GameObject* object, const std::wstring& modelName, const RenderMode mode)
    { auto renderer = CreateRenderer(modelName); object->AddComponent(renderer); typedRenderers[static_cast<size_t>(mode)].push_back(renderer); }

    void Scene::BuildObjects(const float aspectRatio, const Vector3& directionalLightDirection)
    {
        auto skySphere = std::make_unique<GameObject>("Sky");
        skySphere->GetTransform()->SetScale({500.0f, 500.0f, 500.0f});
        const auto skyIndex = loader.GetTextureIndex(L"skyTex");
        auto skyRenderer = std::make_shared<SkyBox>(device, models[L"sphere"], *loader.GetTexture(skyIndex), &srvHeap, skyIndex);
        skySphere->AddComponent(skyRenderer);
        typedRenderers[static_cast<size_t>(RenderMode::SkyBox)].push_back(skyRenderer);
        gameObjects.push_back(std::move(skySphere));

        auto mirrorSphere = std::make_unique<GameObject>("MirrorSphere");
        mirrorSphere->GetTransform()->SetPosition({0.0f, 20.0f, 0.0f});
        mirrorSphere->GetTransform()->SetScale({2.0f, 2.0f, 2.0f});
        mirrorSphereTransform = mirrorSphere->GetTransform();
        AddObjectRenderer(mirrorSphere.get(), L"mirrorSphere", RenderMode::Reflection);
        gameObjects.push_back(std::move(mirrorSphere));

        auto quad = std::make_unique<GameObject>("Quad");
        AddObjectRenderer(quad.get(), L"quad", RenderMode::Debug);
        typedRenderers[static_cast<size_t>(RenderMode::Quad)].push_back(
            typedRenderers[static_cast<size_t>(RenderMode::Debug)].back());
        gameObjects.push_back(std::move(quad));

        auto sun = std::make_unique<GameObject>("Directional Light");
        auto light = std::make_shared<Light>(Directional);
        light->Direction(directionalLightDirection);
        light->Strength({0.8f, 0.8f, 0.8f});
        sun->AddComponent(light);
        gameObjects.push_back(std::move(sun));

        auto orbitNano = std::make_unique<GameObject>("OrbitNano");
        orbitNano->SetScale(0.5f);
        AddObjectRenderer(orbitNano.get(), L"nano", RenderMode::DynamicOpaque);
        orbitNano->AddComponent(std::make_shared<Orbiter>(
            mirrorSphereTransform, Vector3(5.0f, -5.0f, 0.0f), 0.8f, Vector3(0.0f, 90.0f, 0.0f)));
        benchmarkMovingObject = orbitNano.get();
        benchmarkMovingObjectMatrix = orbitNano->GetTransform()->GetLocalMatrix();
        gameObjects.push_back(std::move(orbitNano));

        for (int j = 0; j < 11; ++j)
        {
            const auto index = static_cast<float>(j);
            auto nano = std::make_unique<GameObject>();
            nano->GetTransform()->SetPosition(Vector3::Right * -15.0f + Vector3::Forward * 12.0f * index);
            nano->GetTransform()->SetEulerRotate({0.0f, -90.0f, 0.0f});
            AddObjectRenderer(nano.get(), L"nano", RenderMode::Opaque);
            gameObjects.push_back(std::move(nano));

            auto doom = std::make_unique<GameObject>();
            doom->SetScale(0.08f);
            doom->GetTransform()->SetPosition(Vector3::Right * 15.0f + Vector3::Forward * 12.0f * index);
            doom->GetTransform()->SetEulerRotate({0.0f, 90.0f, 0.0f});
            AddObjectRenderer(doom.get(), L"doom", RenderMode::Opaque);
            gameObjects.push_back(std::move(doom));
        }

        for (int j = 0; j < 12; ++j)
        {
            for (int k = 0; k < 3; ++k)
            {
                const auto row = static_cast<float>(j);
                const auto column = static_cast<float>(k);
                auto atlas = std::make_unique<GameObject>();
                atlas->GetTransform()->SetPosition(Vector3::Right * -60.0f + Vector3::Right * -30.0f * column + Vector3::Up * 11.0f + Vector3::Forward * 10.0f * row);
                AddObjectRenderer(atlas.get(), L"atlas", RenderMode::Opaque);
                gameObjects.push_back(std::move(atlas));

                auto pbody = std::make_unique<GameObject>();
                pbody->GetTransform()->SetPosition(Vector3::Right * 130.0f + Vector3::Right * -30.0f * column + Vector3::Up * 11.0f + Vector3::Forward * 10.0f * row);
                AddObjectRenderer(pbody.get(), L"pbody", RenderMode::Opaque);
                gameObjects.push_back(std::move(pbody));
            }
        }

        auto platform = std::make_unique<GameObject>();
        platform->SetScale(0.2f);
        platform->GetTransform()->SetEulerRotate({90.0f, 90.0f, 0.0f});
        platform->GetTransform()->SetPosition(Vector3::Backward * -130.0f);
        AddObjectRenderer(platform.get(), L"platform", RenderMode::Opaque);

        auto rotater = std::make_unique<GameObject>();
        rotater->GetTransform()->SetParent(platform->GetTransform().get());
        rotater->GetTransform()->SetPosition(Vector3::Forward * 325.0f + Vector3::Left * 625.0f);
        rotater->GetTransform()->SetEulerRotate({0.0f, -90.0f, 90.0f});

        auto cameraObject = std::make_unique<GameObject>("MainCamera");
        cameraObject->GetTransform()->SetParent(rotater->GetTransform().get());
        cameraObject->GetTransform()->SetEulerRotate({-30.0f, 180.0f, 0.0f});
        cameraObject->GetTransform()->SetPosition({0.0f, -200.0f, -20.0f});
        sceneCamera = std::make_shared<Camera>(aspectRatio);
        cameraObject->AddComponent(sceneCamera);
#if defined(DEBUG) || defined(_DEBUG)
        cameraObject->AddComponent(std::make_shared<CameraController>());
#else
        rotater->AddComponent(std::make_shared<Rotater>(10.0f));
#endif
        benchmarkCamera = cameraObject.get();
        benchmarkCameraOrbit = rotater.get();
        benchmarkCameraMatrix = cameraObject->GetTransform()->GetLocalMatrix();
        benchmarkCameraOrbitMatrix = rotater->GetTransform()->GetLocalMatrix();
        gameObjects.push_back(std::move(cameraObject));
        gameObjects.push_back(std::move(rotater));

        auto stair = std::make_unique<GameObject>();
        stair->GetTransform()->SetParent(platform->GetTransform().get());
        stair->SetScale(0.2f);
        stair->GetTransform()->SetEulerRotate({0.0f, 0.0f, 90.0f});
        stair->GetTransform()->SetPosition(Vector3::Left * 700.0f);
        AddObjectRenderer(stair.get(), L"stair", RenderMode::Opaque);

        auto columns = std::make_unique<GameObject>();
        columns->GetTransform()->SetParent(stair->GetTransform().get());
        columns->SetScale(0.8f);
        columns->GetTransform()->SetEulerRotate({0.0f, 0.0f, 90.0f});
        columns->GetTransform()->SetPosition(Vector3::Up * 2000.0f + Vector3::Forward * 900.0f);
        AddObjectRenderer(columns.get(), L"columns", RenderMode::Opaque);

        auto fountain = std::make_unique<GameObject>();
        fountain->SetScale(0.005f);
        fountain->GetTransform()->SetEulerRotate({90.0f, 0.0f, 0.0f});
        fountain->GetTransform()->SetPosition(Vector3::Up * 35.0f + Vector3::Backward * 77.0f);
        AddObjectRenderer(fountain.get(), L"fountain", RenderMode::Opaque);

        gameObjects.push_back(std::move(platform));
        gameObjects.push_back(std::move(stair));
        gameObjects.push_back(std::move(columns));
        gameObjects.push_back(std::move(fountain));

        const auto addDragon = [this](const wchar_t* model, const Vector3& position)
        {
            auto dragon = std::make_unique<GameObject>();
            dragon->GetTransform()->SetEulerRotate({90.0f, 0.0f, 0.0f});
            dragon->GetTransform()->SetPosition(position);
            AddObjectRenderer(dragon.get(), model, RenderMode::Opaque);
            gameObjects.push_back(std::move(dragon));
        };
        addDragon(L"mountDragon", Vector3::Right * -960.0f + Vector3::Up * 45.0f + Vector3::Backward * 775.0f);
        addDragon(L"desertDragon", Vector3::Right * 960.0f + Vector3::Up * -5.0f + Vector3::Backward * 775.0f);

        for (const float x : {-355.0f, 355.0f})
        {
            auto griffon = std::make_unique<GameObject>();
            griffon->SetScale(0.8f);
            griffon->GetTransform()->SetEulerRotate({90.0f, 0.0f, 0.0f});
            griffon->GetTransform()->SetPosition(Vector3::Right * x + Vector3::Up * -7.0f + Vector3::Backward * 17.0f);
            AddObjectRenderer(griffon.get(), L"griffon", RenderMode::OpaqueAlphaDrop);
            gameObjects.push_back(std::move(griffon));
        }
    }

    void Scene::SortObjects()
    {
        for (const auto& object : gameObjects)
        {
            if (const auto light = object->GetComponent<Light>())
            {
                lights.push_back(light.get());
            }
            if (const auto camera = object->GetComponent<Camera>())
            {
                sceneCamera = camera;
            }
        }
    }

    void Scene::Update()
    {
        for (const auto& object : gameObjects)
        {
            object->Update();
        }
    }

    void Scene::ResetBenchmarkAnimation()
    {
        if (benchmarkCamera)
            benchmarkCamera->GetTransform()->SetLocalMatrix(benchmarkCameraMatrix);
        if (benchmarkCameraOrbit)
        {
            benchmarkCameraOrbit->GetTransform()->SetLocalMatrix(benchmarkCameraOrbitMatrix);
            if (const auto rotater = benchmarkCameraOrbit->GetComponent<Rotater>())
                rotater->Reset();
        }
        if (benchmarkMovingObject)
        {
            benchmarkMovingObject->GetTransform()->SetLocalMatrix(benchmarkMovingObjectMatrix);
            if (const auto orbiter = benchmarkMovingObject->GetComponent<Orbiter>())
                orbiter->Reset();
        }
    }

    void Scene::UpdateMaterials(FrameResource* frameResource, const bool useSecondGpuBuffer)
    {
        const auto& buffer = frameResource->MaterialBuffers[useSecondGpuBuffer ? 1 : 0];
        for (const auto& material : loader.GetMaterials())
        {
            material->Update();
            buffer->CopyData(material->GetIndex(), material->GetMaterialConstantData());
        }
    }

    void Scene::Draw(const std::shared_ptr<GCommandList>& cmdList, const RenderMode mode) const
    {
        for (const auto& renderer : typedRenderers[static_cast<size_t>(mode)])
        {
            renderer->Draw(cmdList);
        }
    }

    GDescriptor* Scene::GetSrvHeap() { return &srvHeap; }
    std::shared_ptr<Camera> Scene::GetCamera() const { return sceneCamera; }
    const std::vector<Light*>& Scene::GetLights() const { return lights; }
    UINT Scene::GetLightCount(const LightType type) const
    {
        return static_cast<UINT>(std::count_if(lights.begin(), lights.end(),
            [type](const Light* light) { return light->Type() == type; }));
    }
    DirectX::BoundingSphere Scene::GetBounds() const { return bounds; }
    size_t Scene::GetObjectCount() const { return gameObjects.size(); }
    size_t Scene::GetMaterialCount() { return loader.GetMaterials().size(); }
    size_t Scene::GetTextureCount() { return loader.GetTextures().size(); }
    UINT Scene::GetTextureIndex(const std::wstring& name) { return loader.GetTextureIndex(name); }
    Vector3 Scene::GetMirrorSpherePosition() const { return mirrorSphereTransform->GetWorldPosition(); }
}
