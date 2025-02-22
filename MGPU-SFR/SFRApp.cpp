#include "SFRApp.h"

#include <array>
#include <filesystem>
#include <fstream>
#include "CameraController.h"
#include "GameObject.h"
#include "GCrossAdapterResource.h"
#include "Rotater.h"
#include "SkyBox.h"
#include "Transform.h"
#include "Window.h"
#include "d3dUtil.h"

using namespace PEPEngine;
using namespace Utils;

SFRApp::SFRApp(const HINSTANCE hInstance): D3DApp(hInstance)
{
    mSceneBounds.Center = Vector3(0.0f, 0.0f, 0.0f);
    mSceneBounds.Radius = 175;
}

void SFRApp::InitDevices()
{
    devices.resize(GraphicAdapterCount);

    auto allDevices = GDeviceFactory::GetAllDevices(true);


    const auto firstDevice = allDevices[0];
    const auto otherDevice = allDevices[1];

    if (otherDevice->IsCrossAdapterTextureSupported())
    {
        devices[GraphicAdapterPrimary] = otherDevice;
        devices[GraphicAdapterSecond] = firstDevice;
    }
    else
    {
        devices[GraphicAdapterPrimary] = firstDevice;
        devices[GraphicAdapterSecond] = otherDevice;
    }

    devices[GraphicAdapterPrimary] = firstDevice;
    devices[GraphicAdapterSecond] = otherDevice;

    for (auto&& device : devices)
    {
        adapterRects.push_back(D3D12_RECT{});
        assets.push_back(AssetsLoader(device));
        models.push_back(MemoryAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<GModel>>());

        typedRenderer.push_back(MemoryAllocator::CreateVector<custom_vector<std::shared_ptr<Renderer>>>());

        for (int i = 0; i < static_cast<uint8_t>(RenderMode::Count); ++i)
        {
            typedRenderer[typedRenderer.size() - 1].push_back(
                MemoryAllocator::CreateVector<std::shared_ptr<Renderer>>());
        }
    }

    devices[GraphicAdapterPrimary]->SharedFence(primeFence, devices[GraphicAdapterSecond], sharedFence,
                                                sharedFenceValue);

    logQueue.Push(L"\nPrime Device: " + (devices[GraphicAdapterPrimary]->GetName()));
    logQueue.Push(
        L"\t\n Cross Adapter Texture Support: " + std::to_wstring(
            devices[GraphicAdapterPrimary]->IsCrossAdapterTextureSupported()));
    logQueue.Push(L"\nSecond Device: " + (devices[GraphicAdapterSecond]->GetName()));
    logQueue.Push(
        L"\t\n Cross Adapter Texture Support: " + std::to_wstring(
            devices[GraphicAdapterSecond]->IsCrossAdapterTextureSupported()));
}

void SFRApp::InitFrameResource()
{
    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<SplitFrameResource>(devices.data(), devices.size(), 2,
                                                                      assets[GraphicAdapterPrimary].GetMaterials().
                                                                      size()));

        auto backBufferDesc = MainWindow->GetBackBuffer(i).GetD3D12ResourceDesc();
        backBufferDesc.Width = backBufferDesc.Width / 2;

        frameResources[i]->PrimeDeviceBackBuffer = GTexture(devices[GraphicAdapterPrimary], backBufferDesc,
                                                            L"Prime device Back Buffer" + std::to_wstring(i),
                                                            TextureUsage::RenderTarget);
    }
    logQueue.Push(std::wstring(L"\nInit FrameResource "));
}

void SFRApp::InitRootSignature()
{
    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        auto rootSignature = std::make_shared<GRootSignature>();
        CD3DX12_DESCRIPTOR_RANGE texParam[4];
        texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0); //SkyMap
        texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0); //ShadowMap
        texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::AmbientMap - 3, 0); //SsaoMap
        texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, assets[i].GetLoadTexturesCount(),
                         StandardShaderSlot::TexturesMap - 3, 0);


        rootSignature->AddConstantBufferParameter(0);
        rootSignature->AddConstantBufferParameter(1);
        rootSignature->AddShaderResourceView(0, 1);
        rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->Initialize(devices[i]);

        rootSignatures.push_back(std::move(rootSignature));

        logQueue.Push(std::wstring(L"\nInit RootSignature for " + devices[i]->GetName()));
    }

    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        auto ssaoRootSignature = std::make_shared<GRootSignature>();

        CD3DX12_DESCRIPTOR_RANGE texTable0;
        texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

        CD3DX12_DESCRIPTOR_RANGE texTable1;
        texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

        ssaoRootSignature->AddConstantBufferParameter(0);
        ssaoRootSignature->AddConstantParameter(1, 1);
        ssaoRootSignature->AddDescriptorParameter(&texTable0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        ssaoRootSignature->AddDescriptorParameter(&texTable1, 1, D3D12_SHADER_VISIBILITY_PIXEL);

        const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
            0, // shaderRegister
            D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

        const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
            1, // shaderRegister
            D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

        const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
            2, // shaderRegister
            D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
            D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressU
            D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressV
            D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressW
            0.0f,
            0,
            D3D12_COMPARISON_FUNC_LESS_EQUAL,
            D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

        const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
            3, // shaderRegister
            D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
            D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
            D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
            D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

        std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
        {
            pointClamp, linearClamp, depthMapSam, linearWrap
        };

        for (auto&& sampler : staticSamplers)
        {
            ssaoRootSignature->AddStaticSampler(sampler);
        }

        ssaoRootSignature->Initialize(devices[i]);

        ssaoRootSignatures.push_back(std::move(ssaoRootSignature));
    }
}

void SFRApp::InitPipeLineResource()
{
    defaultInputLayout =
    {
        {
            "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
        {
            "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
        {
            "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
        {
            "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
    };

    const D3D12_INPUT_LAYOUT_DESC desc = {defaultInputLayout.data(), defaultInputLayout.size()};

    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        defaultPipelineResources.push_back(std::move(RenderModeFactory()));
        defaultPipelineResources[i].LoadDefaultShaders();
        defaultPipelineResources[i].LoadDefaultPSO(devices[i], rootSignatures[i], desc, BackBufferFormat,
                                                   DepthStencilFormat, ssaoRootSignatures[i], NormalMapFormat,
                                                   AmbientMapFormat);


        ambientPaths[i]->SetPipelineData(*defaultPipelineResources[i].GetPSO(RenderMode::Ssao),
                                         *defaultPipelineResources[i].GetPSO(RenderMode::SsaoBlur));

        logQueue.Push(std::wstring(L"\nInit PSO for " + devices[i]->GetName()));
    }
}

void SFRApp::CreateMaterials()
{
    {
        auto seamless = std::make_shared<Material>(L"seamless", RenderMode::Opaque);
        seamless->FresnelR0 = Vector3(0.02f, 0.02f, 0.02f);
        seamless->Roughness = 0.1f;

        auto tex = assets[GraphicAdapterPrimary].GetTextureIndex(L"seamless");
        seamless->SetDiffuseTexture(assets[GraphicAdapterPrimary].GetTexture(tex), tex);

        tex = assets[GraphicAdapterPrimary].GetTextureIndex(L"defaultNormalMap");

        seamless->SetNormalMap(assets[GraphicAdapterPrimary].GetTexture(tex), tex);
        assets[GraphicAdapterPrimary].AddMaterial(seamless);


        models[GraphicAdapterPrimary][L"quad"]->SetMeshMaterial(
            0, assets[GraphicAdapterPrimary].GetMaterial(assets[GraphicAdapterPrimary].GetMaterialIndex(L"seamless")));
    }

    logQueue.Push(std::wstring(L"\nCreate Materials"));
}

void SFRApp::InitSRVMemoryAndMaterials()
{
    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        srvTexturesMemory.push_back(
            devices[i]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, assets[i].GetTextures().size()));

        ambientPaths[i]->BuildDescriptors();


        auto materials = assets[i].GetMaterials();

        for (int j = 0; j < materials.size(); ++j)
        {
            auto material = materials[j];

            material->InitMaterial(&srvTexturesMemory[i]);
        }

        logQueue.Push(std::wstring(L"\nInit Views for " + devices[i]->GetName()));
    }
}

void SFRApp::InitRenderPaths()
{
    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        auto commandQueue = devices[i]->GetCommandQueue(GQueueType::Graphics);
        auto cmdList = commandQueue->GetCommandList();

        shadowPaths.push_back(std::make_shared<ShadowMap>(devices[i], 4096, 4096));

        ambientPaths.push_back(std::make_shared<SSAO>(
            devices[i],
            cmdList,
            MainWindow->GetClientWidth(), MainWindow->GetClientHeight()));

        antiAliasingPaths.push_back(
            std::make_shared<SSAA>(devices[i], 1, MainWindow->GetClientWidth(), MainWindow->GetClientHeight()));
        antiAliasingPaths[i]->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

        commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));

        logQueue.Push(std::wstring(L"\nInit Render path data for " + devices[i]->GetName()));
    }
}

void SFRApp::LoadStudyTexture()
{
    {
        auto queue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Copy);

        auto cmdList = queue->GetCommandList();

        auto bricksTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\bricks2.dds", cmdList);
        bricksTex->SetName(L"bricksTex");
        assets[GraphicAdapterPrimary].AddTexture(bricksTex);

        auto stoneTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\stone.dds", cmdList);
        stoneTex->SetName(L"stoneTex");
        assets[GraphicAdapterPrimary].AddTexture(stoneTex);

        auto tileTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\tile.dds", cmdList);
        tileTex->SetName(L"tileTex");
        assets[GraphicAdapterPrimary].AddTexture(tileTex);

        auto fenceTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\WireFence.dds", cmdList);
        fenceTex->SetName(L"fenceTex");
        assets[GraphicAdapterPrimary].AddTexture(fenceTex);

        auto waterTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\water1.dds", cmdList);
        waterTex->SetName(L"waterTex");
        assets[GraphicAdapterPrimary].AddTexture(waterTex);

        auto skyTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\skymap.dds", cmdList);
        skyTex->SetName(L"skyTex");
        assets[GraphicAdapterPrimary].AddTexture(skyTex);

        auto grassTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\grass.dds", cmdList);
        grassTex->SetName(L"grassTex");
        assets[GraphicAdapterPrimary].AddTexture(grassTex);

        auto treeArrayTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\treeArray2.dds", cmdList);
        treeArrayTex->SetName(L"treeArrayTex");
        assets[GraphicAdapterPrimary].AddTexture(treeArrayTex);

        auto seamless = GTexture::LoadTextureFromFile(L"Data\\Textures\\seamless_grass.jpg", cmdList);
        seamless->SetName(L"seamless");
        assets[GraphicAdapterPrimary].AddTexture(seamless);


        std::vector<std::wstring> texNormalNames =
        {
            L"bricksNormalMap",
            L"tileNormalMap",
            L"defaultNormalMap"
        };

        std::vector<std::wstring> texNormalFilenames =
        {
            L"Data\\Textures\\bricks2_nmap.dds",
            L"Data\\Textures\\tile_nmap.dds",
            L"Data\\Textures\\default_nmap.dds"
        };

        for (int j = 0; j < texNormalNames.size(); ++j)
        {
            auto texture = GTexture::LoadTextureFromFile(texNormalFilenames[j], cmdList, TextureUsage::Normalmap);
            texture->SetName(texNormalNames[j]);
            assets[GraphicAdapterPrimary].AddTexture(texture);
        }

        queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
    }

    logQueue.Push(std::wstring(L"\nLoad DDS Texture"));
}

void SFRApp::LoadModels()
{
    auto queue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Copy);
    auto cmdList = queue->GetCommandList();

    auto nano = assets[GraphicAdapterPrimary].CreateModelFromFile(cmdList, "Data\\Objects\\Nanosuit\\Nanosuit.obj");
    models[GraphicAdapterPrimary][L"nano"] = std::move(nano);

    auto atlas = assets[GraphicAdapterPrimary].CreateModelFromFile(cmdList, "Data\\Objects\\Atlas\\Atlas.obj");
    models[GraphicAdapterPrimary][L"atlas"] = std::move(atlas);
    auto pbody = assets[GraphicAdapterPrimary].CreateModelFromFile(cmdList, "Data\\Objects\\P-Body\\P-Body.obj");
    models[GraphicAdapterPrimary][L"pbody"] = std::move(pbody);

    auto griffon = assets[GraphicAdapterPrimary].CreateModelFromFile(cmdList, "Data\\Objects\\Griffon\\Griffon.FBX");
    griffon->scaleMatrix = Matrix::CreateScale(0.1);
    models[GraphicAdapterPrimary][L"griffon"] = std::move(griffon);

    auto mountDragon = assets[GraphicAdapterPrimary].CreateModelFromFile(
        cmdList, "Data\\Objects\\MOUNTAIN_DRAGON\\MOUNTAIN_DRAGON.FBX");
    mountDragon->scaleMatrix = Matrix::CreateScale(0.1);
    models[GraphicAdapterPrimary][L"mountDragon"] = std::move(mountDragon);

    auto desertDragon = assets[GraphicAdapterPrimary].CreateModelFromFile(
        cmdList, "Data\\Objects\\DesertDragon\\DesertDragon.FBX");
    desertDragon->scaleMatrix = Matrix::CreateScale(0.1);
    models[GraphicAdapterPrimary][L"desertDragon"] = std::move(desertDragon);

    auto sphere = assets[GraphicAdapterPrimary].GenerateSphere(cmdList);
    models[GraphicAdapterPrimary][L"sphere"] = std::move(sphere);

    auto quad = assets[GraphicAdapterPrimary].GenerateQuad(cmdList);
    models[GraphicAdapterPrimary][L"quad"] = std::move(quad);

    auto stair = assets[GraphicAdapterPrimary].CreateModelFromFile(
        cmdList, "Data\\Objects\\Temple\\SM_AsianCastle_A.FBX");
    models[GraphicAdapterPrimary][L"stair"] = std::move(stair);

    auto columns = assets[GraphicAdapterPrimary].CreateModelFromFile(
        cmdList, "Data\\Objects\\Temple\\SM_AsianCastle_E.FBX");
    models[GraphicAdapterPrimary][L"columns"] = std::move(columns);

    auto fountain = assets[GraphicAdapterPrimary].
        CreateModelFromFile(cmdList, "Data\\Objects\\Temple\\SM_Fountain.FBX");
    models[GraphicAdapterPrimary][L"fountain"] = std::move(fountain);

    auto platform = assets[GraphicAdapterPrimary].CreateModelFromFile(
        cmdList, "Data\\Objects\\Temple\\SM_PlatformSquare.FBX");
    models[GraphicAdapterPrimary][L"platform"] = std::move(platform);

    auto doom = assets[GraphicAdapterPrimary].CreateModelFromFile(cmdList, "Data\\Objects\\DoomSlayer\\doommarine.obj");
    models[GraphicAdapterPrimary][L"doom"] = std::move(doom);

    queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
    queue->Flush();

    logQueue.Push(std::wstring(L"\nLoad Models Data"));
}

void SFRApp::MipMasGenerate()
{
    try
    {
        for (int i = 0; i < GraphicAdapterCount; ++i)
        {
            std::vector<GTexture*> generatedMipTextures;

            auto textures = assets[i].GetTextures();

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

            const auto computeQueue = devices[i]->GetCommandQueue(GQueueType::Compute);
            auto computeList = computeQueue->GetCommandList();
            GTexture::GenerateMipMaps(computeList, generatedMipTextures.data(), generatedMipTextures.size());
            computeQueue->WaitForFenceValue(computeQueue->ExecuteCommandList(computeList));
            logQueue.Push(std::wstring(L"\nMip Map Generation for " + devices[i]->GetName()));

            computeList = computeQueue->GetCommandList();
            for (auto&& texture : generatedMipTextures)
                computeList->TransitionBarrier(texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
            computeList->FlushResourceBarriers();
            logQueue.Push(std::wstring(L"\nTexture Barrier Generation for " + devices[i]->GetName()));
            computeQueue->WaitForFenceValue(computeQueue->ExecuteCommandList(computeList));

            logQueue.Push(std::wstring(L"\nMipMap Generation cmd list executing " + devices[i]->GetName()));
            for (auto&& pair : textures)
                pair->ClearTrack();
            logQueue.Push(std::wstring(L"\nFinish Mip Map Generation for " + devices[i]->GetName()));
        }
    }
    catch (DxException& e)
    {
        logQueue.Push(L"\n" + e.Filename + L" " + e.FunctionName + L" " + std::to_wstring(e.LineNumber));
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
    }
    catch (...)
    {
        logQueue.Push(L"\nWTF???? How It Fix");
    }
}

void SFRApp::DublicateResource()
{
    for (int i = GraphicAdapterPrimary + 1; i < GraphicAdapterCount; ++i)
    {
        logQueue.Push(std::wstring(L"\nStart Dublicate Resource for " + devices[i]->GetName()));
        try
        {
            auto queue = devices[i]->GetCommandQueue(GQueueType::Copy);
            auto cmdList = queue->GetCommandList();


            logQueue.Push(std::wstring(L"\nGet CmdList For " + devices[i]->GetName()));

            for (auto&& texture : assets[GraphicAdapterPrimary].GetTextures())
            {
                texture->ClearTrack();

                auto tex = GTexture::LoadTextureFromFile(texture->GetFilePath(), cmdList);

                tex->SetName(texture->GetName());
                tex->ClearTrack();

                assets[i].AddTexture(std::move(tex));

                logQueue.Push(std::wstring(L"\nLoad Texture " + texture->GetName() + L" for " + devices[i]->GetName()));
            }

            logQueue.Push(std::wstring(L"\nDublicate texture Resource for " + devices[i]->GetName()));

            for (auto&& material : assets[GraphicAdapterPrimary].GetMaterials())
            {
                auto copy = std::make_shared<Material>(material->GetName(), material->GetPSO());

                copy->SetMaterialIndex(material->GetMaterialIndex());

                auto index = assets[i].GetTextureIndex(material->GetDiffuseTexture()->GetName());
                auto texture = assets[i].GetTexture(index);
                copy->SetDiffuseTexture(texture, index);

                index = assets[i].GetTextureIndex(material->GetNormalTexture()->GetName());
                texture = assets[i].GetTexture(index);
                copy->SetNormalMap(texture, index);

                copy->DiffuseAlbedo = material->DiffuseAlbedo;
                copy->FresnelR0 = material->FresnelR0;
                copy->Roughness = material->Roughness;
                copy->MatTransform = material->MatTransform;


                assets[i].AddMaterial(std::move(copy));
            }
            logQueue.Push(std::wstring(L"\nDublicate material Resource for " + devices[i]->GetName()));

            for (auto&& model : models[GraphicAdapterPrimary])
            {
                auto modelCopy = model.second->Dublicate(cmdList);

                for (int j = 0; j < model.second->GetMeshesCount(); ++j)
                {
                    auto originMaterial = model.second->GetMeshMaterial(j);

                    if (originMaterial != nullptr)
                        modelCopy->SetMeshMaterial(
                            j, assets[i].GetMaterial(assets[i].GetMaterialIndex(originMaterial->GetName())));
                }

                models[i][model.first] = std::move(modelCopy);
            }

            logQueue.Push(std::wstring(L"\nDublicate models Resource for " + devices[i]->GetName()));

            queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));

            logQueue.Push(std::wstring(L"\nDublicate Resource for " + devices[i]->GetName()));
        }
        catch (DxException& e)
        {
            logQueue.Push(L"\n" + e.Filename + L" " + e.FunctionName + L" " + std::to_wstring(e.LineNumber));
        }
        catch (...)
        {
            logQueue.Push(L"\nWTF???? How It Fix");
        }
    }
}

void SFRApp::SortGO()
{
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
            camera = (cam);
        }
    }
}

std::shared_ptr<Renderer> SFRApp::CreateRenderer(const UINT deviceIndex, std::shared_ptr<GModel> model)
{
    auto renderer = std::make_shared<ModelRenderer>(devices[deviceIndex], model);
    return renderer;
}

void SFRApp::AddMultiDeviceOpaqueRenderComponent(GameObject* object, const std::wstring& modelName,
                                                        RenderMode psoType)
{
    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        auto renderer = CreateRenderer(i, models[i][modelName]);
        object->AddComponent(renderer);
        typedRenderer[i][(int)RenderMode::OpaqueAlphaDrop].push_back(renderer);
    }
}

void SFRApp::CreateGO()
{
    logQueue.Push(std::wstring(L"\nStart Create GO"));
    auto skySphere = std::make_unique<GameObject>("Sky");
    skySphere->GetTransform()->SetScale({500, 500, 500});
    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        auto renderer = std::make_shared<SkyBox>(devices[i], models[i][L"sphere"],
                                                 *assets[i].GetTexture(assets[i].GetTextureIndex(L"skyTex")).get(),
                                                 &srvTexturesMemory[i], assets[i].GetTextureIndex(L"skyTex"));

        skySphere->AddComponent(renderer);
        typedRenderer[i][(int)RenderMode::SkyBox].push_back((renderer));
    }
    gameObjects.push_back(std::move(skySphere));

    auto quadRitem = std::make_unique<GameObject>("Quad");
    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        auto renderer = std::make_shared<ModelRenderer>(devices[i], models[i][L"quad"]);
        renderer->SetModel(models[i][L"quad"]);
        quadRitem->AddComponent(renderer);
        typedRenderer[i][(int)RenderMode::Debug].push_back(renderer);
        typedRenderer[i][(int)RenderMode::Quad].push_back(renderer);
    }
    gameObjects.push_back(std::move(quadRitem));


    auto sun1 = std::make_unique<GameObject>("Directional Light");
    auto light = std::make_shared<Light>(Directional);
    light->Direction({0.57735f, -0.57735f, 0.57735f});
    light->Strength({0.8f, 0.8f, 0.8f});
    sun1->AddComponent(light);
    gameObjects.push_back(std::move(sun1));

    for (int i = 0; i < 11; ++i)
    {
        auto nano = std::make_unique<GameObject>();
        nano->GetTransform()->SetPosition(Vector3::Right * -15 + Vector3::Forward * 12 * i);
        nano->GetTransform()->SetEulerRotate(Vector3(0, -90, 0));
        AddMultiDeviceOpaqueRenderComponent(nano.get(), L"nano");
        gameObjects.push_back(std::move(nano));


        auto doom = std::make_unique<GameObject>();
        doom->SetScale(0.08);
        doom->GetTransform()->SetPosition(Vector3::Right * 15 + Vector3::Forward * 12 * i);
        doom->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
        AddMultiDeviceOpaqueRenderComponent(doom.get(), L"doom");
        gameObjects.push_back(std::move(doom));
    }

    for (int i = 0; i < 12; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            auto atlas = std::make_unique<GameObject>();
            atlas->GetTransform()->SetPosition(
                Vector3::Right * -60 + Vector3::Right * -30 * j + Vector3::Up * 11 + Vector3::Forward * 10 * i);
            AddMultiDeviceOpaqueRenderComponent(atlas.get(), L"atlas");
            gameObjects.push_back(std::move(atlas));


            auto pbody = std::make_unique<GameObject>();
            pbody->GetTransform()->SetPosition(
                Vector3::Right * 130 + Vector3::Right * -30 * j + Vector3::Up * 11 + Vector3::Forward * 10 * i);
            AddMultiDeviceOpaqueRenderComponent(pbody.get(), L"pbody");
            gameObjects.push_back(std::move(pbody));
        }
    }


    auto platform = std::make_unique<GameObject>();
    platform->SetScale(0.2);
    platform->GetTransform()->SetEulerRotate(Vector3(90, 90, 0));
    platform->GetTransform()->SetPosition(Vector3::Backward * -130);
    AddMultiDeviceOpaqueRenderComponent(platform.get(), L"platform");


    auto rotater = std::make_unique<GameObject>();
    rotater->GetTransform()->SetParent(platform->GetTransform().get());
    rotater->GetTransform()->SetPosition(Vector3::Forward * 325 + Vector3::Left * 625);
    rotater->GetTransform()->SetEulerRotate(Vector3(0, -90, 90));
    rotater->AddComponent(std::make_shared<Rotater>(10));

    auto camera = std::make_unique<GameObject>("MainCamera");
    camera->GetTransform()->SetParent(rotater->GetTransform().get());
    camera->GetTransform()->SetEulerRotate(Vector3(-30, 270, 0));
    camera->GetTransform()->SetPosition(Vector3(-1000, 190, -32));
    camera->AddComponent(std::make_shared<Camera>(AspectRatio()));
    camera->AddComponent(std::make_shared<CameraController>());

    gameObjects.push_back(std::move(camera));
    gameObjects.push_back(std::move(rotater));


    auto stair = std::make_unique<GameObject>();
    stair->GetTransform()->SetParent(platform->GetTransform().get());
    stair->SetScale(0.2);
    stair->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
    stair->GetTransform()->SetPosition(Vector3::Left * 700);
    AddMultiDeviceOpaqueRenderComponent(stair.get(), L"stair");


    auto columns = std::make_unique<GameObject>();
    columns->GetTransform()->SetParent(stair->GetTransform().get());
    columns->SetScale(0.8);
    columns->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
    columns->GetTransform()->SetPosition(Vector3::Up * 2000 + Vector3::Forward * 900);
    AddMultiDeviceOpaqueRenderComponent(columns.get(), L"columns");

    auto fountain = std::make_unique<GameObject>();
    fountain->SetScale(0.005);
    fountain->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    fountain->GetTransform()->SetPosition(Vector3::Up * 35 + Vector3::Backward * 77);
    AddMultiDeviceOpaqueRenderComponent(fountain.get(), L"fountain");

    gameObjects.push_back(std::move(platform));
    gameObjects.push_back(std::move(stair));
    gameObjects.push_back(std::move(columns));
    gameObjects.push_back(std::move(fountain));


    auto mountDragon = std::make_unique<GameObject>();
    mountDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    mountDragon->GetTransform()->SetPosition(Vector3::Right * -960 + Vector3::Up * 45 + Vector3::Backward * 775);
    AddMultiDeviceOpaqueRenderComponent(mountDragon.get(), L"mountDragon");
    gameObjects.push_back(std::move(mountDragon));


    auto desertDragon = std::make_unique<GameObject>();
    desertDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    desertDragon->GetTransform()->SetPosition(Vector3::Right * 960 + Vector3::Up * -5 + Vector3::Backward * 775);
    AddMultiDeviceOpaqueRenderComponent(desertDragon.get(), L"desertDragon");
    gameObjects.push_back(std::move(desertDragon));

    auto griffon = std::make_unique<GameObject>();
    griffon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    griffon->SetScale(0.8);
    griffon->GetTransform()->SetPosition(Vector3::Right * -355 + Vector3::Up * -7 + Vector3::Backward * 17);
    AddMultiDeviceOpaqueRenderComponent(griffon.get(), L"griffon", RenderMode::OpaqueAlphaDrop);
    gameObjects.push_back(std::move(griffon));

    griffon = std::make_unique<GameObject>();
    griffon->SetScale(0.8);
    griffon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    griffon->GetTransform()->SetPosition(Vector3::Right * 355 + Vector3::Up * -7 + Vector3::Backward * 17);
    AddMultiDeviceOpaqueRenderComponent(griffon.get(), L"griffon", RenderMode::OpaqueAlphaDrop);
    gameObjects.push_back(std::move(griffon));

    logQueue.Push(std::wstring(L"\nFinish create GO"));
}

void SFRApp::CalculateFrameStats()
{
    static float minFps = std::numeric_limits<float>::max();
    static float minMspf = std::numeric_limits<float>::max();
    static float maxFps = std::numeric_limits<float>::min();
    static float maxMspf = std::numeric_limits<float>::min();
    static UINT writeStaticticCount = 0;

    frameCount++;

    if ((timer.TotalTime() - timeElapsed) >= 1.0f)
    {
        float fps = static_cast<float>(frameCount); // fps = frameCnt / 1
        float mspf = 1000.0f / fps;

        minFps = std::min(fps, minFps);
        minMspf = std::min(mspf, minMspf);
        maxFps = std::max(fps, maxFps);
        maxMspf = std::max(mspf, maxMspf);

        if (writeStaticticCount >= 60)
        {
            std::wstring staticticStr = L"\nMain device use on " + std::to_wstring(percentOfUsePrimeDevice) + L"%"
                + L"\n\tMin FPS: " + std::to_wstring(minFps)
                + L"\n\tMin MSPF: " + std::to_wstring(minMspf)
                + L"\n\tMax FPS: " + std::to_wstring(maxFps)
                + L"\n\tMax MSPF: " + std::to_wstring(maxMspf)
                + L"\n\tPrime GPU Rendering Time in microseconds: " + std::to_wstring(gpuTimes[GraphicAdapterPrimary]) +
                +L"\n\tSecond GPU Rendering Time in microseconds: " + std::to_wstring(gpuTimes[GraphicAdapterSecond]);

            logQueue.Push(staticticStr);

            minFps = std::numeric_limits<float>::max();
            minMspf = std::numeric_limits<float>::max();
            maxFps = std::numeric_limits<float>::min();
            maxMspf = std::numeric_limits<float>::min();
            writeStaticticCount = 0;

            if (percentOfUsePrimeDevice > 0)
            {
                percentOfUsePrimeDevice = std::clamp(percentOfUsePrimeDevice - 5, 0, 100);
                OnResize();
            }
            ++statisticCount;
        }

        frameCount = 0;
        timeElapsed += 1.0f;
        writeStaticticCount++;
    }
}

void SFRApp::LogWriting()
{
    std::time_t t = std::time(nullptr);

    tm now;
    localtime_s(&now, &t);

    std::filesystem::path filePath(
        L"LogData" + std::to_wstring(now.tm_mday) + L"-" + std::to_wstring(now.tm_mon + 1) + L"-" +
        std::to_wstring((now.tm_year + 1900)) + L"-" + std::to_wstring(now.tm_hour) + L"-" + std::to_wstring(now.tm_min)
        + L".txt");


    auto path = std::filesystem::current_path().wstring() + L"\\" + filePath.wstring();

    OutputDebugStringW(path.c_str());

    std::wofstream fileSteam;
    fileSteam.open(path.c_str(), std::ios::out | std::ios::in | std::ios::binary | std::ios::trunc);
    if (fileSteam.is_open())
    {
        fileSteam << L"Information" << std::endl;
    }

    std::wstring line;
    while (logThreadIsAlive)
    {
        while (logQueue.TryPop(line))
        {
            fileSteam << line;
        }
        fileSteam.flush();
        std::this_thread::yield();
    }

    while (logQueue.Size() > 0)
    {
        if (logQueue.TryPop(line))
        {
            fileSteam << line;
        }
    }
    fileSteam << L"\nFinish Logs" << std::endl;

    if (fileSteam.is_open())
        fileSteam.close();
}

bool SFRApp::Initialize()
{
    logThread = std::thread(&SFRApp::LogWriting, this);

    InitDevices();
    InitMainWindow();

    LoadStudyTexture();
    LoadModels();
    CreateMaterials();
    DublicateResource();
    MipMasGenerate();

    InitRenderPaths();
    InitSRVMemoryAndMaterials();
    InitRootSignature();
    InitPipeLineResource();
    CreateGO();
    SortGO();
    InitFrameResource();

    OnResize();

    for (auto&& device : devices)
    {
        device->Flush();
    }

    return true;
}

void SFRApp::UpdateMaterials()
{
    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        auto currentMaterialBuffer = currentFrameResource->MaterialBuffers[i];

        for (auto&& material : assets[i].GetMaterials())
        {
            material->Update();
            auto constantData = material->GetMaterialConstantData();
            currentMaterialBuffer->CopyData(material->GetIndex(), constantData);
        }
    }
}

void SFRApp::Update(const GameTimer& gt)
{
    const auto commandQueue = devices[GraphicAdapterSecond]->GetCommandQueue(GQueueType::Graphics);

    auto olderIndex = currentFrameResourceIndex > globalCountFrameResources ? 0 : currentFrameResourceIndex;
    {
        for (int i = 0; i < GraphicAdapterCount; ++i)
        {
            gpuTimes[i] = devices[i]->GetCommandQueue()->GetTimestamp(olderIndex);
        }
    }

    currentFrameResourceIndex = (currentFrameResourceIndex + 1) % globalCountFrameResources;
    currentFrameResource = frameResources[currentFrameResourceIndex];

    if (currentFrameResource->FenceValue != 0 && !commandQueue->IsFinish(currentFrameResource->FenceValue))
    {
        commandQueue->WaitForFenceValue(currentFrameResource->FenceValue);
    }

    mLightRotationAngle += 0.1f * gt.DeltaTime();

    Matrix R = Matrix::CreateRotationY(mLightRotationAngle);
    for (int i = 0; i < 3; ++i)
    {
        auto lightDir = mBaseLightDirections[i];
        lightDir = Vector3::TransformNormal(lightDir, R);
        mRotatedLightDirections[i] = lightDir;
    }

    for (auto& e : gameObjects)
    {
        e->Update();
    }

    UpdateMaterials();

    UpdateShadowTransform(gt);
    UpdateMainPassCB(gt);
    UpdateShadowPassCB(gt);
    UpdateSsaoCB(gt);
}

void SFRApp::UpdateShadowTransform(const GameTimer& gt)
{
    // Only the first "main" light casts a shadow.
    Vector3 lightDir = mRotatedLightDirections[0];
    Vector3 lightPos = -2.0f * mSceneBounds.Radius * lightDir;
    Vector3 targetPos = mSceneBounds.Center;
    Vector3 lightUp = Vector3::Up;
    Matrix lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

    mLightPosW = lightPos;


    // Transform bounding sphere to light space.
    Vector3 sphereCenterLS = Vector3::Transform(targetPos, lightView);


    // Ortho frustum in light space encloses scene.
    float l = sphereCenterLS.x - mSceneBounds.Radius;
    float b = sphereCenterLS.y - mSceneBounds.Radius;
    float n = sphereCenterLS.z - mSceneBounds.Radius;
    float r = sphereCenterLS.x + mSceneBounds.Radius;
    float t = sphereCenterLS.y + mSceneBounds.Radius;
    float f = sphereCenterLS.z + mSceneBounds.Radius;

    mLightNearZ = n;
    mLightFarZ = f;
    Matrix lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

    // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
    Matrix T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);

    Matrix S = lightView * lightProj * T;
    mLightView = lightView;
    mLightProj = lightProj;
    mShadowTransform = S;
}

void SFRApp::UpdateShadowPassCB(const GameTimer& gt)
{
    auto view = mLightView;
    auto proj = mLightProj;

    auto viewProj = (view * proj);
    auto invView = view.Invert();
    auto invProj = proj.Invert();
    auto invViewProj = viewProj.Invert();


    shadowPassCB.View = view.Transpose();
    shadowPassCB.InvView = invView.Transpose();
    shadowPassCB.Proj = proj.Transpose();
    shadowPassCB.InvProj = invProj.Transpose();
    shadowPassCB.ViewProj = viewProj.Transpose();
    shadowPassCB.InvViewProj = invViewProj.Transpose();
    shadowPassCB.EyePosW = mLightPosW;
    shadowPassCB.NearZ = mLightNearZ;
    shadowPassCB.FarZ = mLightFarZ;

    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        UINT w = shadowPaths[i]->Width();
        UINT h = shadowPaths[i]->Height();
        shadowPassCB.RenderTargetSize = Vector2(static_cast<float>(w), static_cast<float>(h));
        shadowPassCB.InvRenderTargetSize = Vector2(1.0f / w, 1.0f / h);

        auto currPassCB = currentFrameResource->PassConstantUploadBuffers[i];
        currPassCB->CopyData(1, shadowPassCB);
    }
}

void SFRApp::UpdateMainPassCB(const GameTimer& gt)
{
    auto view = camera->GetViewMatrix();
    auto proj = camera->GetProjectionMatrix();

    auto viewProj = (view * proj);
    auto invView = view.Invert();
    auto invProj = proj.Invert();
    auto invViewProj = viewProj.Invert();
    auto shadowTransform = mShadowTransform;

    // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
    Matrix T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);
    Matrix viewProjTex = XMMatrixMultiply(viewProj, T);
    mainPassCB.debugMap = pathMapShow;
    mainPassCB.View = view.Transpose();
    mainPassCB.InvView = invView.Transpose();
    mainPassCB.Proj = proj.Transpose();
    mainPassCB.InvProj = invProj.Transpose();
    mainPassCB.ViewProj = viewProj.Transpose();
    mainPassCB.InvViewProj = invViewProj.Transpose();
    mainPassCB.ViewProjTex = viewProjTex.Transpose();
    mainPassCB.ShadowTransform = shadowTransform.Transpose();
    mainPassCB.EyePosW = camera->gameObject->GetTransform()->GetWorldPosition();
    mainPassCB.RenderTargetSize = Vector2(static_cast<float>(MainWindow->GetClientWidth()),
                                          static_cast<float>(MainWindow->GetClientHeight()));
    mainPassCB.InvRenderTargetSize = Vector2(1.0f / mainPassCB.RenderTargetSize.x,
                                             1.0f / mainPassCB.RenderTargetSize.y);
    mainPassCB.NearZ = 1.0f;
    mainPassCB.FarZ = 1000.0f;
    mainPassCB.TotalTime = gt.TotalTime();
    mainPassCB.DeltaTime = gt.DeltaTime();
    mainPassCB.AmbientLight = Vector4{0.25f, 0.25f, 0.35f, 1.0f};

    for (int i = 0; i < MaxLights; ++i)
    {
        if (i < lights.size())
        {
            mainPassCB.Lights[i] = lights[i]->GetData();
        }
        else
        {
            break;
        }
    }

    mainPassCB.Lights[0].Direction = mRotatedLightDirections[0];
    mainPassCB.Lights[0].Strength = Vector3{0.9f, 0.8f, 0.7f};
    mainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
    mainPassCB.Lights[1].Strength = Vector3{0.4f, 0.4f, 0.4f};
    mainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
    mainPassCB.Lights[2].Strength = Vector3{0.2f, 0.2f, 0.2f};

    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        auto currentPassCB = currentFrameResource->PassConstantUploadBuffers[i];
        currentPassCB->CopyData(0, mainPassCB);
    }
}

void SFRApp::UpdateSsaoCB(const GameTimer& gt)
{
    SsaoConstants ssaoCB;

    auto P = camera->GetProjectionMatrix();

    // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
    Matrix T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);

    ssaoCB.Proj = mainPassCB.Proj;
    ssaoCB.InvProj = mainPassCB.InvProj;
    XMStoreFloat4x4(&ssaoCB.ProjTex, XMMatrixTranspose(P * T));

    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        ambientPaths[i]->GetOffsetVectors(ssaoCB.OffsetVectors);

        auto blurWeights = ambientPaths[i]->CalcGaussWeights(2.5f);
        ssaoCB.BlurWeights[0] = Vector4(&blurWeights[0]);
        ssaoCB.BlurWeights[1] = Vector4(&blurWeights[4]);
        ssaoCB.BlurWeights[2] = Vector4(&blurWeights[8]);

        ssaoCB.InvRenderTargetSize = Vector2(1.0f / ambientPaths[i]->SsaoMapWidth(),
                                             1.0f / ambientPaths[i]->SsaoMapHeight());

        // Coordinates given in view space.
        ssaoCB.OcclusionRadius = 0.5f;
        ssaoCB.OcclusionFadeStart = 0.2f;
        ssaoCB.OcclusionFadeEnd = 1.0f;
        ssaoCB.SurfaceEpsilon = 0.05f;

        auto currSsaoCB = currentFrameResource->SsaoConstantUploadBuffers[i];
        currSsaoCB->CopyData(0, ssaoCB);
    }
}

void SFRApp::PopulateShadowMapCommands(const GraphicsAdapter adapterIndex, std::shared_ptr<GCommandList> cmdList)
{
    //Draw Shadow Map
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory[adapterIndex]);
        cmdList->SetRootSignature(*rootSignatures[adapterIndex].get());

        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[adapterIndex]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory[adapterIndex]);
        cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                           *currentFrameResource->PassConstantUploadBuffers[adapterIndex], 1);

        shadowPaths[adapterIndex]->PopulatePreRenderCommands(cmdList);

        cmdList->SetPipelineState(*defaultPipelineResources[adapterIndex].GetPSO(RenderMode::ShadowMapOpaque));
        PopulateDrawCommands(adapterIndex, cmdList, RenderMode::Opaque);

        cmdList->SetPipelineState(*defaultPipelineResources[adapterIndex].GetPSO(RenderMode::ShadowMapOpaqueDrop));
        PopulateDrawCommands(adapterIndex, cmdList, RenderMode::OpaqueAlphaDrop);

        cmdList->TransitionBarrier(shadowPaths[adapterIndex]->GetTexture(), D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }
}

void SFRApp::PopulateNormalMapCommands(const GraphicsAdapter adapterIndex, const std::shared_ptr<GCommandList>& cmdList)
{
    //Draw Normals
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory[adapterIndex]);
        cmdList->SetRootSignature(*rootSignatures[adapterIndex].get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[adapterIndex]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory[adapterIndex]);

        cmdList->SetViewports(&fullViewport, 1);
        cmdList->SetScissorRects(&fullRect, 1);

        auto normalMap = ambientPaths[adapterIndex]->NormalMap();
        auto normalDepthMap = ambientPaths[adapterIndex]->NormalDepthMap();
        auto normalMapRtv = ambientPaths[adapterIndex]->NormalMapRtv();
        auto normalMapDsv = ambientPaths[adapterIndex]->NormalMapDSV();

        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->FlushResourceBarriers();
        float clearValue[] = {0.0f, 0.0f, 1.0f, 0.0f};
        cmdList->ClearRenderTarget(normalMapRtv, 0, clearValue);
        cmdList->ClearDepthStencil(normalMapDsv, 0,
                                   D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

        cmdList->SetRenderTargets(1, normalMapRtv, 0, normalMapDsv);
        cmdList->SetRootConstantBufferView(1, *currentFrameResource->PassConstantUploadBuffers[adapterIndex]);

        cmdList->SetPipelineState(*defaultPipelineResources[adapterIndex].GetPSO(RenderMode::DrawNormalsOpaque));
        PopulateDrawCommands(adapterIndex, cmdList, RenderMode::Opaque);
        cmdList->SetPipelineState(*defaultPipelineResources[adapterIndex].GetPSO(RenderMode::DrawNormalsOpaqueDrop));
        PopulateDrawCommands(adapterIndex, cmdList, RenderMode::OpaqueAlphaDrop);


        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }
}

void SFRApp::PopulateAmbientMapCommands(const GraphicsAdapter adapterIndex, const std::shared_ptr<GCommandList>& cmdList)
{
    //Draw Ambient
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory[adapterIndex]);
        cmdList->SetRootSignature(*rootSignatures[adapterIndex].get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[adapterIndex]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory[adapterIndex]);

        cmdList->SetRootSignature(*ssaoRootSignatures[adapterIndex].get());
        ambientPaths[adapterIndex]->ComputeSsao(cmdList, currentFrameResource->SsaoConstantUploadBuffers[adapterIndex],
                                                3);
    }
}

void SFRApp::PopulateForwardPathCommands(const GraphicsAdapter adapterIndex, const std::shared_ptr<GCommandList>& cmdList)
{
    //Forward Path with SSAA
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory[adapterIndex]);
        cmdList->SetRootSignature(*rootSignatures[adapterIndex].get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[adapterIndex]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory[adapterIndex]);

        cmdList->SetViewports(&antiAliasingPaths[adapterIndex]->GetViewPort(), 1);
        cmdList->SetScissorRects(&antiAliasingPaths[adapterIndex]->GetRect(), 1);

        cmdList->TransitionBarrier((antiAliasingPaths[adapterIndex]->GetRenderTarget()),
                                   D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(antiAliasingPaths[adapterIndex]->GetDepthMap(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->FlushResourceBarriers();

        cmdList->ClearRenderTarget(antiAliasingPaths[adapterIndex]->GetRTV());
        cmdList->ClearDepthStencil(antiAliasingPaths[adapterIndex]->GetDSV(), 0,
                                   D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

        cmdList->SetRenderTargets(1, antiAliasingPaths[adapterIndex]->GetRTV(), 0,
                                  antiAliasingPaths[adapterIndex]->GetDSV());

        cmdList->
            SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                      *currentFrameResource->PassConstantUploadBuffers[adapterIndex]);

        cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowPaths[adapterIndex]->GetSrv());
        cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ambientPaths[adapterIndex]->AmbientMapSrv(), 0);

        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap,
                                        &srvTexturesMemory[adapterIndex]);


        cmdList->SetPipelineState(*defaultPipelineResources[adapterIndex].GetPSO(RenderMode::SkyBox));
        PopulateDrawCommands(adapterIndex, cmdList, (RenderMode::SkyBox));

        cmdList->SetPipelineState(*defaultPipelineResources[adapterIndex].GetPSO(RenderMode::Opaque));
        PopulateDrawCommands(adapterIndex, cmdList, (RenderMode::Opaque));

        cmdList->SetPipelineState(*defaultPipelineResources[adapterIndex].GetPSO(RenderMode::OpaqueAlphaDrop));
        PopulateDrawCommands(adapterIndex, cmdList, (RenderMode::OpaqueAlphaDrop));

        cmdList->SetPipelineState(*defaultPipelineResources[adapterIndex].GetPSO(RenderMode::Transparent));
        PopulateDrawCommands(adapterIndex, cmdList, (RenderMode::Transparent));

        switch (pathMapShow)
        {
        case 1:
            {
                cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, shadowPaths[adapterIndex]->GetSrv());
                cmdList->SetPipelineState(*defaultPipelineResources[adapterIndex].GetPSO(RenderMode::Debug));
                PopulateDrawCommands(adapterIndex, cmdList, (RenderMode::Debug));
                break;
            }
        case 2:
            {
                cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap,
                                                ambientPaths[adapterIndex]->AmbientMapSrv(), 0);
                cmdList->SetPipelineState(*defaultPipelineResources[adapterIndex].GetPSO(RenderMode::Debug));
                PopulateDrawCommands(adapterIndex, cmdList, (RenderMode::Debug));
                break;
            }
        }

        cmdList->TransitionBarrier(antiAliasingPaths[adapterIndex]->GetRenderTarget(),
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier((antiAliasingPaths[adapterIndex]->GetDepthMap()), D3D12_RESOURCE_STATE_DEPTH_READ);
        cmdList->FlushResourceBarriers();
    }
}

void SFRApp::PopulateDrawCommands(const GraphicsAdapter adapterIndex, std::shared_ptr<GCommandList> cmdList,
                                         RenderMode type)
{
    for (auto&& renderer : typedRenderer[adapterIndex][(int)type])
    {
        renderer->Draw(cmdList);
    }
}

void SFRApp::PopulateDrawQuadCommand(const GraphicsAdapter adapterIndex, const std::shared_ptr<GCommandList>& cmdList,
                                            GTexture& renderTarget, GDescriptor* rtvMemory, const UINT offsetRTV)
{
    cmdList->SetViewports(&fullViewport, 1);
    cmdList->SetScissorRects(&adapterRects[adapterIndex], 1);

    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();
    cmdList->ClearRenderTarget(rtvMemory, offsetRTV,
                               adapterIndex == GraphicAdapterPrimary ? Colors::Red : Colors::Blue);

    cmdList->SetRenderTargets(1, rtvMemory, offsetRTV);

    cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, antiAliasingPaths[adapterIndex]->GetSRV());

    cmdList->SetPipelineState(*defaultPipelineResources[adapterIndex].GetPSO(RenderMode::Quad));
    PopulateDrawCommands(adapterIndex, cmdList, (RenderMode::Quad));

    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();
}

void SFRApp::PopulateCopyResource(const std::shared_ptr<GCommandList>& cmdList, const GResource& srcResource,
                                         const GResource& dstResource)
{
    cmdList->CopyResource(dstResource, srcResource);
    cmdList->TransitionBarrier(dstResource,
                               D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(srcResource, D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();
}

void SFRApp::PopulateCopyResourceRegion(const std::shared_ptr<GCommandList>& cmdList, const GResource& srcResource,
                                               const GResource& dstResource)
{
    cmdList->CopyTextureRegion(dstResource, 0, 0, 0, srcResource, &copyRegionBox);
    cmdList->TransitionBarrier(dstResource,
                               D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(srcResource, D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();
}

void SFRApp::Draw(const GameTimer& gt)
{
    if (isResizing) return;

    // Get a timestamp at the start of the command list.
    const UINT timestampHeapIndex = 2 * currentFrameResourceIndex;


    if (percentOfUsePrimeDevice == 100)
    {
        auto primeQueue = devices[GraphicAdapterPrimary]->GetCommandQueue();
        auto secondQueue = devices[GraphicAdapterSecond]->GetCommandQueue();

        const auto primeRenderCmdList = primeQueue->GetCommandList();


        primeRenderCmdList->EndQuery(timestampHeapIndex);

        PopulateShadowMapCommands(GraphicAdapterPrimary, primeRenderCmdList);
        PopulateNormalMapCommands(GraphicAdapterPrimary, primeRenderCmdList);
        PopulateAmbientMapCommands(GraphicAdapterPrimary, primeRenderCmdList);
        PopulateForwardPathCommands(GraphicAdapterPrimary, primeRenderCmdList);
        PopulateDrawQuadCommand(GraphicAdapterPrimary, primeRenderCmdList, currentFrameResource->PrimeDeviceBackBuffer,
                                &currentFrameResource->RenderTargetViewMemory[GraphicAdapterPrimary], 0);
        PopulateCopyResource(primeRenderCmdList, currentFrameResource->PrimeDeviceBackBuffer,
                             currentFrameResource->CrossAdapterBackBuffer->GetPrimeResource());

        // Get a timestamp at the end of the command list and resolve the query data.
        primeRenderCmdList->EndQuery(timestampHeapIndex + 1);
        primeRenderCmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));


        const auto secondDeviceCopyList = secondQueue->GetCommandList();
        secondDeviceCopyList->EndQuery(timestampHeapIndex);
        PopulateCopyResource(secondDeviceCopyList, currentFrameResource->CrossAdapterBackBuffer->GetSharedResource(),
                             MainWindow->GetCurrentBackBuffer());
        secondDeviceCopyList->EndQuery(timestampHeapIndex + 1);
        secondDeviceCopyList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));


        const auto primeDeviceFinishRendering = primeQueue->ExecuteCommandList(primeRenderCmdList);
        primeQueue->Signal(primeFence, primeDeviceFinishRendering);

        secondQueue->Wait(sharedFence, primeDeviceFinishRendering);
        currentFrameResource->FenceValue = secondQueue->ExecuteCommandList(secondDeviceCopyList);

        MainWindow->Present();
    }
    else
    {
        if (percentOfUsePrimeDevice == 0)
        {
            auto secondQueue = devices[GraphicAdapterSecond]->GetCommandQueue();

            const auto secondRenderCmdList = secondQueue->GetCommandList();
            secondRenderCmdList->EndQuery(timestampHeapIndex);
            PopulateShadowMapCommands(GraphicAdapterSecond, secondRenderCmdList);
            PopulateNormalMapCommands(GraphicAdapterSecond, secondRenderCmdList);
            PopulateAmbientMapCommands(GraphicAdapterSecond, secondRenderCmdList);
            PopulateForwardPathCommands(GraphicAdapterSecond, secondRenderCmdList);
            PopulateDrawQuadCommand(GraphicAdapterSecond, secondRenderCmdList, MainWindow->GetCurrentBackBuffer(),
                                    &currentFrameResource->RenderTargetViewMemory[GraphicAdapterSecond], 0);
            secondRenderCmdList->EndQuery(timestampHeapIndex + 1);
            secondRenderCmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));


            currentFrameResource->FenceValue = secondQueue->ExecuteCommandList(secondRenderCmdList);
            MainWindow->Present();
        }
        else
        {
            auto primeQueue = devices[GraphicAdapterPrimary]->GetCommandQueue();
            auto secondQueue = devices[GraphicAdapterSecond]->GetCommandQueue();


            auto primeRenderCmdList = primeQueue->GetCommandList();
            primeRenderCmdList->EndQuery(timestampHeapIndex);
            PopulateShadowMapCommands(GraphicAdapterPrimary, primeRenderCmdList);
            PopulateNormalMapCommands(GraphicAdapterPrimary, primeRenderCmdList);
            PopulateAmbientMapCommands(GraphicAdapterPrimary, primeRenderCmdList);
            PopulateForwardPathCommands(GraphicAdapterPrimary, primeRenderCmdList);
            PopulateDrawQuadCommand(GraphicAdapterPrimary, primeRenderCmdList,
                                    currentFrameResource->PrimeDeviceBackBuffer,
                                    &currentFrameResource->RenderTargetViewMemory[GraphicAdapterPrimary], 0);
            PopulateCopyResourceRegion(primeRenderCmdList, currentFrameResource->PrimeDeviceBackBuffer,
                                       currentFrameResource->CrossAdapterBackBuffer->GetPrimeResource());

            // Get a timestamp at the end of the command list and resolve the query data.
            primeRenderCmdList->EndQuery(timestampHeapIndex + 1);
            primeRenderCmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));


            auto secondRenderCmdList = secondQueue->GetCommandList();
            secondRenderCmdList->EndQuery(timestampHeapIndex);
            PopulateShadowMapCommands(GraphicAdapterSecond, secondRenderCmdList);
            PopulateNormalMapCommands(GraphicAdapterSecond, secondRenderCmdList);
            PopulateAmbientMapCommands(GraphicAdapterSecond, secondRenderCmdList);
            PopulateForwardPathCommands(GraphicAdapterSecond, secondRenderCmdList);
            PopulateDrawQuadCommand(GraphicAdapterSecond, secondRenderCmdList, MainWindow->GetCurrentBackBuffer(),
                                    &currentFrameResource->RenderTargetViewMemory[GraphicAdapterSecond], 0);

            // Get a timestamp at the end of the command list and resolve the query data.
            secondRenderCmdList->EndQuery(timestampHeapIndex + 1);
            secondRenderCmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));

            const auto secondDeviceFinishRendering = secondQueue->ExecuteCommandList(secondRenderCmdList);

            const auto primeDeviceFinishRendering = primeQueue->ExecuteCommandList(primeRenderCmdList);
            primeQueue->Signal(primeFence, primeDeviceFinishRendering);

            const auto secondCopyCmdList = secondQueue->GetCommandList();
            PopulateCopyResourceRegion(secondCopyCmdList,
                                       currentFrameResource->CrossAdapterBackBuffer->GetSharedResource(),
                                       MainWindow->GetCurrentBackBuffer());

            secondCopyCmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
            secondCopyCmdList->FlushResourceBarriers();

            secondQueue->WaitForFenceValue(secondDeviceFinishRendering);
            secondQueue->Wait(sharedFence, primeDeviceFinishRendering);

            currentFrameResource->FenceValue = secondQueue->ExecuteCommandList(secondCopyCmdList);

            MainWindow->Present();
        }
    }
}

void SFRApp::OnResize()
{
    D3DApp::OnResize();

    fullViewport.Height = static_cast<float>(MainWindow->GetClientHeight());
    fullViewport.Width = static_cast<float>(MainWindow->GetClientWidth());
    fullViewport.MinDepth = 0.0f;
    fullViewport.MaxDepth = 1.0f;
    fullViewport.TopLeftX = 0;
    fullViewport.TopLeftY = 0;
    fullRect = D3D12_RECT{0, 0, MainWindow->GetClientWidth(), MainWindow->GetClientHeight()};

    const float currentPrimeRatio = percentOfUsePrimeDevice / 100.0f;

    static int pixelDeviceZoneSize = 2;

    int primeWidth = std::clamp(
        static_cast<int>(MainWindow->GetClientWidth() * currentPrimeRatio) - pixelDeviceZoneSize, 1,
        MainWindow->GetClientWidth() - 2 * pixelDeviceZoneSize);
    int secondWidth = primeWidth + 2 * pixelDeviceZoneSize == MainWindow->GetClientWidth()
                          ? primeWidth + 2 * pixelDeviceZoneSize - 1
                          : primeWidth + 2 * pixelDeviceZoneSize;

    if (percentOfUsePrimeDevice == 100 || percentOfUsePrimeDevice == 0)
    {
        adapterRects[GraphicAdapterPrimary] = D3D12_RECT{
            0, 0, MainWindow->GetClientWidth() - pixelDeviceZoneSize, MainWindow->GetClientHeight()
        };
        adapterRects[GraphicAdapterSecond] = D3D12_RECT{
            pixelDeviceZoneSize, 0, MainWindow->GetClientWidth(), MainWindow->GetClientHeight()
        };
    }
    else
    {
        adapterRects[GraphicAdapterPrimary] = D3D12_RECT{0, 0, primeWidth, MainWindow->GetClientHeight()};
        adapterRects[GraphicAdapterSecond] = D3D12_RECT{
            secondWidth, 0, MainWindow->GetClientWidth(), MainWindow->GetClientHeight()
        };
    }


    copyRegionBox = CD3DX12_BOX(0, 0, primeWidth + pixelDeviceZoneSize, MainWindow->GetClientHeight());


    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = GetSRGBFormat(BackBufferFormat);
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        //������� �� ���� ����� �������� �� Prime ����������
        GTexture::Resize(frameResources[i]->PrimeDeviceBackBuffer,
                         (percentOfUsePrimeDevice == 100)
                             ? MainWindow->GetClientWidth()
                             : primeWidth + pixelDeviceZoneSize, MainWindow->GetClientHeight(), 1);
        frameResources[i]->PrimeDeviceBackBuffer.CreateRenderTargetView(
            &rtvDesc, &frameResources[i]->RenderTargetViewMemory[GraphicAdapterPrimary]);


        //������ ��������� � ������� ������. ������� �� ���� ���������� ��������� ��������
        MainWindow->GetBackBuffer(i).CreateRenderTargetView(
            &rtvDesc, &frameResources[i]->RenderTargetViewMemory[GraphicAdapterSecond]);

        //������� �� ���� ����� ���������� Prime ������, � �� ������ ����� ���������� Second
        auto backBufferDesc = MainWindow->GetBackBuffer(i).GetD3D12ResourceDesc();
        backBufferDesc.Width = (percentOfUsePrimeDevice == 100)
                                   ? MainWindow->GetClientWidth()
                                   : primeWidth + pixelDeviceZoneSize;
        frameResources[i]->CrossAdapterBackBuffer.reset();
        frameResources[i]->CrossAdapterBackBuffer = std::make_shared<GCrossAdapterResource>(
            backBufferDesc, devices[GraphicAdapterPrimary], devices[GraphicAdapterSecond], L"Shared Back Buffer");
    }


    if (camera != nullptr)
    {
        camera->SetAspectRatio(AspectRatio());
    }

    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        if (ambientPaths[i] != nullptr)
        {
            ambientPaths[i]->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
            ambientPaths[i]->RebuildDescriptors();
        }

        if (antiAliasingPaths[i] != nullptr)
        {
            antiAliasingPaths[i]->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
        }
    }

    currentFrameResourceIndex = MainWindow->GetCurrentBackBufferIndex() - 1;
}

bool SFRApp::InitMainWindow()
{
    MainWindow = CreateRenderWindow(devices[GraphicAdapterSecond], mainWindowCaption, 1920, 1080, false);
    logQueue.Push(std::wstring(L"\nInit Window"));
    return true;
}

int SFRApp::Run()
{
    MSG msg = {nullptr};

    timer.Reset();

    while (msg.message != WM_QUIT)
    {
        // If there are Window messages then process them.
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        // Otherwise, do animation/game stuff.
        else
        {
            if (statisticCount > 20)
            {
                Quit();
                continue;
            }

            timer.Tick();

            //if (!isAppPaused)
            {
                CalculateFrameStats();
                Update(timer);
                Draw(timer);
            }
            //else
            {
                //Sleep(100);
            }

            for (auto&& device : devices)
            {
                device->ResetAllocators(frameCount);
            }
        }
    }

    logThreadIsAlive = false;
    logThread.join();
    return static_cast<int>(msg.wParam);
}

void SFRApp::Flush()
{
    for (auto&& device : devices)
    {
        device->Flush();
    }
}

LRESULT SFRApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
    switch (msg)
    {
    case WM_INPUT:
        {
            UINT dataSize;
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dataSize,
                            sizeof(RAWINPUTHEADER));
            //Need to populate data size first

            if (dataSize > 0)
            {
                auto rawdata = std::make_unique<BYTE[]>(dataSize);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawdata.get(), &dataSize,
                                    sizeof(RAWINPUTHEADER)) == dataSize)
                {
                    auto raw = reinterpret_cast<RAWINPUT*>(rawdata.get());
                    if (raw->header.dwType == RIM_TYPEMOUSE)
                    {
                        mouse.OnMouseMoveRaw(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
                    }
                }
            }

            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    //Mouse Messages
    case WM_MOUSEMOVE:
        {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            mouse.OnMouseMove(x, y);
            return 0;
        }
    case WM_LBUTTONDOWN:
        {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            mouse.OnLeftPressed(x, y);
            return 0;
        }
    case WM_RBUTTONDOWN:
        {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            mouse.OnRightPressed(x, y);
            return 0;
        }
    case WM_MBUTTONDOWN:
        {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            mouse.OnMiddlePressed(x, y);
            return 0;
        }
    case WM_LBUTTONUP:
        {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            mouse.OnLeftReleased(x, y);
            return 0;
        }
    case WM_RBUTTONUP:
        {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            mouse.OnRightReleased(x, y);
            return 0;
        }
    case WM_MBUTTONUP:
        {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            mouse.OnMiddleReleased(x, y);
            return 0;
        }
    case WM_MOUSEWHEEL:
        {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            if (GET_WHEEL_DELTA_WPARAM(wParam) > 0)
            {
                mouse.OnWheelUp(x, y);
            }
            else if (GET_WHEEL_DELTA_WPARAM(wParam) < 0)
            {
                mouse.OnWheelDown(x, y);
            }
            return 0;
        }
    case WM_KEYUP:

        {
            /*if ((int)wParam == VK_F2)
                Set4xMsaaState(!isM4xMsaa);*/
            unsigned char keycode = static_cast<unsigned char>(wParam);
            keyboard.OnKeyReleased(keycode);


            return 0;
        }
    case WM_KEYDOWN:
        {
            {
                unsigned char keycode = static_cast<unsigned char>(wParam);
                if (keyboard.IsKeysAutoRepeat())
                {
                    keyboard.OnKeyPressed(keycode);
                }
                else
                {
                    const bool wasPressed = lParam & 0x40000000;
                    if (!wasPressed)
                    {
                        keyboard.OnKeyPressed(keycode);
                    }
                }

                if (keycode == (VK_F2) && keyboard.KeyIsPressed(VK_F2))
                {
                    //pathMapShow = (pathMapShow + 1) % maxPathMap;
                }

                if ((keycode == (VK_LEFT) && keyboard.KeyIsPressed(VK_LEFT)) || ((keycode == ('A') && keyboard.
                    KeyIsPressed('A'))))
                {
                    break;
                    if (percentOfUsePrimeDevice > 0)
                    {
                        isAppPaused = true;
                        isResizing = true;
                        timer.Stop();
                        percentOfUsePrimeDevice = std::clamp(percentOfUsePrimeDevice - 5, 0, 100);

                        Flush();
                        isAppPaused = false;
                        isResizing = false;
                        timer.Start();
                        OnResize();
                    }
                }

                if ((keycode == (VK_RIGHT) && keyboard.KeyIsPressed(VK_RIGHT)) || ((keycode == ('D') && keyboard.
                    KeyIsPressed('D'))))
                {
                    break;
                    if (percentOfUsePrimeDevice < 100)
                    {
                        isAppPaused = true;
                        isResizing = true;
                        timer.Stop();
                        percentOfUsePrimeDevice = std::clamp(percentOfUsePrimeDevice + 5, 0, 100);

                        Flush();
                        isAppPaused = false;
                        isResizing = false;
                        timer.Start();
                        OnResize();
                    }
                }
            }
        }

    case WM_CHAR:
        {
            unsigned char ch = static_cast<unsigned char>(wParam);
            if (keyboard.IsCharsAutoRepeat())
            {
                keyboard.OnChar(ch);
            }
            else
            {
                const bool wasPressed = lParam & 0x40000000;
                if (!wasPressed)
                {
                    keyboard.OnChar(ch);
                }
            }
            return 0;
        }
    }

    return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}
