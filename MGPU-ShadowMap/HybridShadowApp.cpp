#include "HybridShadowApp.h"

#include <array>
#include <filesystem>
#include <fstream>
#include "CameraController.h"
#include "d3dUtil.h"
#include "GameObject.h"
#include "GCrossAdapterResource.h"
#include "Rotater.h"
#include "SkyBox.h"
#include "Transform.h"
#include "Window.h"
#include <chrono>
#include <thread>
#include "GCommandList.h"

using namespace SimpleMath;
using namespace PEPEngine;
using namespace Utils;
using namespace Graphics;

HybridShadowApp::HybridShadowApp(const HINSTANCE hInstance): D3DApp(hInstance), gpuTimes{}, fullRect()
{
    mSceneBounds.Center = Vector3(0.0f, 0.0f, 0.0f);
    mSceneBounds.Radius = 200;
}

void HybridShadowApp::InitDevices()
{
    devices.resize(GraphicAdapterCount);

    auto allDevices = GDeviceFactory::GetAllDevices(true);

    const auto firstDevice = allDevices[0];
    const auto otherDevice = allDevices[1];

    if (!(firstDevice->GetName().find(L"NVIDIA") != std::wstring::npos))
    {
        if (otherDevice->GetName().find(L"NVIDIA") != std::wstring::npos)
        {
            devices[GraphicAdapterPrimary] = otherDevice;
            devices[GraphicAdapterSecond] = firstDevice;
        }
    }
    else
    {
        devices[GraphicAdapterPrimary] = firstDevice;
        devices[GraphicAdapterSecond] = otherDevice;
    }

    for (auto&& device : devices)
    {
        assets.push_back(AssetsLoader(device));
        models.push_back(MemoryAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<GModel>>());

        typedRenderer.push_back(MemoryAllocator::CreateVector<custom_vector<std::shared_ptr<Renderer>>>());

        for (int i = 0; i < (int)RenderMode::Count; ++i)
        {
            typedRenderer[typedRenderer.size() - 1].push_back(
                MemoryAllocator::CreateVector<std::shared_ptr<Renderer>>());
        }
    }

    devices[GraphicAdapterPrimary]->SharedFence(primeFence, devices[GraphicAdapterSecond], secondFence,
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

void HybridShadowApp::InitFrameResource()
{
    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<FrameResource>(devices[GraphicAdapterPrimary],
                                                                 devices[GraphicAdapterSecond], 1,
                                                                 assets[GraphicAdapterPrimary].GetMaterials().size()));
    }
    logQueue.Push(std::wstring(L"\nInit FrameResource "));
}

void HybridShadowApp::InitRootSignature()
{
    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        auto rootSignature = std::make_shared<GRootSignature>();
        CD3DX12_DESCRIPTOR_RANGE texParam[4];
        texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0); //SkyMap
        texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0); //ShadowMap
        texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::AmbientMap - 3, 0); //SsaoMap
        texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                         assets[i].GetLoadTexturesCount() > 0 ? assets[i].GetLoadTexturesCount() : 1,
                         StandardShaderSlot::TexturesMap - 3, 0);


        rootSignature->AddConstantBufferParameter(0);
        rootSignature->AddConstantBufferParameter(1);
        rootSignature->AddShaderResourceView(0, 1);
        rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->Initialize(devices[i]);

        if (i == GraphicAdapterPrimary)
        {
            primeDeviceSignature = rootSignature;
        }
        else
        {
            secondDeviceShadowMapSignature = rootSignature;
        }

        logQueue.Push(std::wstring(L"\nInit RootSignature for " + devices[i]->GetName()));
    }

    {
        ssaoPrimeRootSignature = std::make_shared<GRootSignature>();

        CD3DX12_DESCRIPTOR_RANGE texTable0;
        texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

        CD3DX12_DESCRIPTOR_RANGE texTable1;
        texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

        ssaoPrimeRootSignature->AddConstantBufferParameter(0);
        ssaoPrimeRootSignature->AddConstantParameter(1, 1);
        ssaoPrimeRootSignature->AddDescriptorParameter(&texTable0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        ssaoPrimeRootSignature->AddDescriptorParameter(&texTable1, 1, D3D12_SHADER_VISIBILITY_PIXEL);

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
            ssaoPrimeRootSignature->AddStaticSampler(sampler);
        }

        ssaoPrimeRootSignature->Initialize(devices[GraphicAdapterPrimary]);
    }
}

void HybridShadowApp::InitPipeLineResource()
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

    defaultPrimePipelineResources = RenderModeFactory();
    defaultPrimePipelineResources.LoadDefaultShaders();
    defaultPrimePipelineResources.LoadDefaultPSO(devices[GraphicAdapterPrimary], primeDeviceSignature, desc,
                                                 BackBufferFormat, DepthStencilFormat, ssaoPrimeRootSignature,
                                                 NormalMapFormat, AmbientMapFormat);

    ambientPrimePath->SetPipelineData(*defaultPrimePipelineResources.GetPSO(RenderMode::Ssao),
                                      *defaultPrimePipelineResources.GetPSO(RenderMode::SsaoBlur));


    logQueue.Push(std::wstring(L"\nInit PSO for " + devices[GraphicAdapterPrimary]->GetName()));

    const auto primeDeviceShadowMapPso = defaultPrimePipelineResources.GetPSO(RenderMode::ShadowMapOpaque);


    auto descPSO = primeDeviceShadowMapPso->GetPsoDescription();


    D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;

    ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    basePsoDesc.InputLayout = descPSO.InputLayout;
    basePsoDesc.VS = descPSO.VS;
    basePsoDesc.RasterizerState = descPSO.RasterizerState;
    basePsoDesc.BlendState = descPSO.BlendState;
    basePsoDesc.DepthStencilState = descPSO.DepthStencilState;
    basePsoDesc.SampleMask = descPSO.SampleMask;
    basePsoDesc.PrimitiveTopologyType = descPSO.PrimitiveTopologyType;
    basePsoDesc.NumRenderTargets = 0;
    basePsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    basePsoDesc.SampleDesc = descPSO.SampleDesc;
    basePsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;


    shadowMapPSOSecondDevice = std::make_shared<GraphicPSO>();
    shadowMapPSOSecondDevice->SetPsoDesc(basePsoDesc);
    shadowMapPSOSecondDevice->SetRootSignature(secondDeviceShadowMapSignature->GetNativeSignature().Get());
    shadowMapPSOSecondDevice->Initialize(devices[GraphicAdapterSecond]);
}

void HybridShadowApp::CreateMaterials()
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

void HybridShadowApp::InitSRVMemoryAndMaterials()
{
    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        srvTexturesMemory.push_back(
            devices[i]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, assets[i].GetTextures().size()));

        auto materials = assets[i].GetMaterials();

        for (int j = 0; j < materials.size(); ++j)
        {
            auto material = materials[j];

            material->InitMaterial(&srvTexturesMemory[i]);
        }

        logQueue.Push(std::wstring(L"\nInit Views for " + devices[i]->GetName()));
    }
    ambientPrimePath->BuildDescriptors();
}

void HybridShadowApp::InitRenderPaths()
{
    auto commandQueue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Graphics);
    auto cmdList = commandQueue->GetCommandList();

    ambientPrimePath = (std::make_shared<SSAO>(
        devices[GraphicAdapterPrimary],
        cmdList,
        MainWindow->GetClientWidth(), MainWindow->GetClientHeight()));

    antiAliasingPrimePath = (std::make_shared<SSAA>(devices[GraphicAdapterPrimary], multi, MainWindow->GetClientWidth(),
                                                    MainWindow->GetClientHeight()));
    antiAliasingPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

    commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));

    logQueue.Push(std::wstring(L"\nInit Render path data for " + devices[GraphicAdapterPrimary]->GetName()));

    shadowPathPrimeDevice = (std::make_shared<ShadowMap>(devices[GraphicAdapterPrimary], 2048, 2048));

    shadowPathSecondDevice = (std::make_shared<ShadowMap>(devices[GraphicAdapterSecond], 2048, 2048));

    auto shadowMapDesc = shadowPathSecondDevice->GetTexture().GetD3D12ResourceDesc();

    crossAdapterShadowMap = std::make_shared<GCrossAdapterResource>(shadowMapDesc, devices[GraphicAdapterPrimary],
                                                                    devices[GraphicAdapterSecond],
                                                                    L"Shared Shadow Map");

    primeCopyShadowMapSRV = devices[GraphicAdapterPrimary]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    srvDesc.Texture2D.PlaneSlice = 0;

    primeCopyShadowMap = GTexture(devices[GraphicAdapterPrimary],
                                  shadowPathSecondDevice->GetTexture().GetD3D12ResourceDesc());

    primeCopyShadowMap.CreateShaderResourceView(&srvDesc, &primeCopyShadowMapSRV);
}

void HybridShadowApp::LoadStudyTexture()
{
    {
        auto queue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Compute);

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

void HybridShadowApp::LoadModels()
{
    auto queue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Compute);
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

void HybridShadowApp::MipMasGenerate()
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

            for (auto&& device : devices)
            {
                device->Flush();
            }
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

void HybridShadowApp::DublicateResource()
{
    for (int i = GraphicAdapterPrimary + 1; i < GraphicAdapterCount; ++i)
    {
        logQueue.Push(std::wstring(L"\nStart Dublicate Resource for " + devices[i]->GetName()));
        try
        {
            auto queue = devices[i]->GetCommandQueue(GQueueType::Copy);
            const auto cmdList = queue->GetCommandList();


            logQueue.Push(std::wstring(L"\nGet CmdList For " + devices[i]->GetName()));

            for (auto&& model : models[GraphicAdapterPrimary])
            {
                auto modelCopy = model.second->Dublicate(cmdList);

                models[i][model.first] = std::move(modelCopy);
            }
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

void HybridShadowApp::SortGO()
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

std::shared_ptr<Renderer> HybridShadowApp::CreateRenderer(const UINT deviceIndex, std::shared_ptr<GModel> model)
{
    auto renderer = std::make_shared<ModelRenderer>(devices[deviceIndex], model);
    return renderer;
}

void HybridShadowApp::AddMultiDeviceOpaqueRenderComponent(GameObject* object, const std::wstring& modelName,
                                                       RenderMode psoType)
{
    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        auto renderer = CreateRenderer(i, models[i][modelName]);
        object->AddComponent(renderer);
        typedRenderer[i][(int)RenderMode::OpaqueAlphaDrop].push_back(renderer);
    }
}

void HybridShadowApp::CreateGO()
{
    logQueue.Push(std::wstring(L"\nStart Create GO"));
    auto skySphere = std::make_unique<GameObject>("Sky");
    skySphere->GetTransform()->SetScale({500, 500, 500});
    {
        auto renderer = std::make_shared<SkyBox>(devices[GraphicAdapterPrimary],
                                                 models[GraphicAdapterPrimary][L"sphere"],
                                                 *assets[GraphicAdapterPrimary].GetTexture(
                                                     assets[GraphicAdapterPrimary].
                                                     GetTextureIndex(L"skyTex")).get(),
                                                 &srvTexturesMemory[GraphicAdapterPrimary],
                                                 assets[GraphicAdapterPrimary].GetTextureIndex(L"skyTex"));

        skySphere->AddComponent(renderer);
        typedRenderer[GraphicAdapterPrimary][(int)RenderMode::SkyBox].push_back((renderer));
    }
    gameObjects.push_back(std::move(skySphere));

    auto quadRitem = std::make_unique<GameObject>("Quad");
    {
        auto renderer = std::make_shared<ModelRenderer>(devices[GraphicAdapterPrimary],
                                                        models[GraphicAdapterPrimary][L"quad"]);
        renderer->SetModel(models[GraphicAdapterPrimary][L"quad"]);
        quadRitem->AddComponent(renderer);
        typedRenderer[GraphicAdapterPrimary][(int)RenderMode::Debug].push_back(renderer);
        typedRenderer[GraphicAdapterPrimary][(int)RenderMode::Quad].push_back(renderer);
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

void HybridShadowApp::CalculateFrameStats()
{
    static float minFps = std::numeric_limits<float>::max();
    static float minMspf = std::numeric_limits<float>::max();
    static float maxFps = std::numeric_limits<float>::min();
    static float maxMspf = std::numeric_limits<float>::min();
    static UINT writeStaticticCount = 0;
    static UINT64 primeGPUTimeMax = std::numeric_limits<UINT64>::min();
    static UINT64 primeGPUTimeMin = std::numeric_limits<UINT64>::max();
    static UINT64 secondGPUTimeMax = std::numeric_limits<UINT64>::min();
    static UINT64 secondGPUTimeMin = std::numeric_limits<UINT64>::max();
    frameCount++;

    if ((timer.TotalTime() - timeElapsed) >= 1.0f)
    {
        float fps = static_cast<float>(frameCount); // fps = frameCnt / 1
        float mspf = 1000.0f / fps;

        minFps = std::min(fps, minFps);
        minMspf = std::min(mspf, minMspf);
        maxFps = std::max(fps, maxFps);
        maxMspf = std::max(mspf, maxMspf);

        primeGPUTimeMin = std::min(gpuTimes[GraphicAdapterPrimary], primeGPUTimeMin);
        primeGPUTimeMax = std::max(gpuTimes[GraphicAdapterPrimary], primeGPUTimeMax);
        secondGPUTimeMin = std::min(gpuTimes[GraphicAdapterSecond], secondGPUTimeMin);
        secondGPUTimeMax = std::max(gpuTimes[GraphicAdapterSecond], secondGPUTimeMax);


        if (writeStaticticCount >= 60)
        {
            const std::wstring staticticStr = L"\nTotal SSAA X" + std::to_wstring(multi) +
                L"\n\tCalculate Part Shadow Map:" + std::to_wstring(!UseOnlyPrime) +
                L"\n\tMin FPS:" + std::to_wstring(minFps)
                + L"\n\tMin MSPF:" + std::to_wstring(minMspf)
                + L"\n\tMax FPS:" + std::to_wstring(maxFps)
                + L"\n\tMax MSPF:" + std::to_wstring(maxMspf)
                + L"\n\tMax Prime GPU Rendering Time:" + std::to_wstring(primeGPUTimeMax) +
                +L"\n\tMin Prime GPU Rendering Time:" + std::to_wstring(primeGPUTimeMin) +
                +L"\n\tMax Second GPU Rendering Time:" + std::to_wstring(secondGPUTimeMax)
                + L"\n\tMin Second GPU Rendering Time:" + std::to_wstring(secondGPUTimeMin);

            logQueue.Push(staticticStr);


            if (UseOnlyPrime)
            {
                Flush();
                UseOnlyPrime = !UseOnlyPrime;
                OnResize();
            }
            else
            {
                if (multi < 8)
                {
                    Flush();
                    multi = multi + 1;
                    antiAliasingPrimePath->SetMultiplier(multi, MainWindow->GetClientWidth(),
                                                         MainWindow->GetClientHeight());
                    UseOnlyPrime = true;
                    OnResize();
                }
                else
                {
                    finishTest = true;
                }
            }

            MainWindow->SetWindowTitle(
                MainWindow->GetWindowName() + L" SSAA X" + std::to_wstring(multi) + L" Calculate Part Shadow Map:" +
                std::to_wstring(!UseOnlyPrime));

            writeStaticticCount = 0;

            minFps = std::numeric_limits<float>::max();
            minMspf = std::numeric_limits<float>::max();
            maxFps = std::numeric_limits<float>::min();
            maxMspf = std::numeric_limits<float>::min();
            primeGPUTimeMax = std::numeric_limits<UINT64>::min();
            primeGPUTimeMin = std::numeric_limits<UINT64>::max();
            secondGPUTimeMax = std::numeric_limits<UINT64>::min();
            secondGPUTimeMin = std::numeric_limits<UINT64>::max();
        }
        else
        {
            const std::wstring staticticStr = L"\nStep SSAA X" + std::to_wstring(multi) +
                L"\n\tCalculate Part Shadow Map:" + std::to_wstring(!UseOnlyPrime) +
                L"\n\tFPS:" + std::to_wstring(fps)
                + L"\n\tMSPF:" + std::to_wstring(mspf)
                + L"\n\tPrime GPU Rendering Time:" + std::to_wstring(gpuTimes[GraphicAdapterPrimary])
                + L"\n\tSecond GPU Rendering Time:" + std::to_wstring(gpuTimes[GraphicAdapterSecond]);

            logQueue.Push(staticticStr);

            writeStaticticCount++;
        }
        frameCount = 0;
        timeElapsed += 1.0f;
    }
}

void HybridShadowApp::LogWriting()
{
    const std::filesystem::path filePath(
        L"PartShadow " + devices[0]->GetName() + L"+" + devices[1]->GetName() + L".txt");

    const auto path = std::filesystem::current_path().wstring() + L"\\" + filePath.wstring();

    OutputDebugStringW(path.c_str());

    std::wofstream fileSteam;
    fileSteam.open(path.c_str(), std::ios::out | std::ios::in | std::ios::binary | std::ios::trunc);
    if (fileSteam.is_open())
    {
        fileSteam << L"Information" << std::endl;
    }

    std::wstring line;

    while (logQueue.Size() > 0)
    {
        while (logQueue.TryPop(line))
        {
            fileSteam << line;
        }
    }

    fileSteam << L"\nFinish Logs" << std::endl;

    fileSteam.flush();
    fileSteam.close();
}

bool HybridShadowApp::Initialize()
{
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

    MainWindow->SetWindowTitle(
        MainWindow->GetWindowName() + L" SSAA X" + std::to_wstring(multi) + L" Calculate Part Shadow Map:" +
        std::to_wstring(!UseOnlyPrime));
    

    return true;
}

void HybridShadowApp::UpdateMaterials()
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

void HybridShadowApp::Update(const GameTimer& gt)
{
    UINT olderIndex = currentFrameResourceIndex - 1 > globalCountFrameResources
                          ? 0
                          : static_cast<UINT>(currentFrameResourceIndex);
    {
        if (UseOnlyPrime)
        {
            gpuTimes[GraphicAdapterPrimary] = devices[GraphicAdapterPrimary]->GetCommandQueue()->GetTimestamp(
                olderIndex);
            gpuTimes[GraphicAdapterSecond] = 0;
        }
        else
        {
            for (int i = 0; i < GraphicAdapterCount; ++i)
            {
                gpuTimes[i] = devices[i]->GetCommandQueue()->GetTimestamp(olderIndex);
            }
        }
    }

    const auto commandQueue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Graphics);

    currentFrameResource = frameResources[currentFrameResourceIndex];

    if (currentFrameResource->PrimeRenderFenceValue != 0 && !commandQueue->IsFinish(
        currentFrameResource->PrimeRenderFenceValue))
    {
        commandQueue->WaitForFenceValue(currentFrameResource->PrimeRenderFenceValue);
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

void HybridShadowApp::UpdateShadowTransform(const GameTimer& gt)
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

void HybridShadowApp::UpdateShadowPassCB(const GameTimer& gt)
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

    UINT w = shadowPathSecondDevice->Width();
    UINT h = shadowPathSecondDevice->Height();
    shadowPassCB.RenderTargetSize = Vector2(static_cast<float>(w), static_cast<float>(h));
    shadowPassCB.InvRenderTargetSize = Vector2(1.0f / w, 1.0f / h);

    auto currPassCB = currentFrameResource->ShadowPassConstantUploadBuffer;
    currPassCB->CopyData(0, shadowPassCB);

    currPassCB = currentFrameResource->PrimePassConstantUploadBuffer;
    currPassCB->CopyData(1, shadowPassCB);
}

void HybridShadowApp::UpdateMainPassCB(const GameTimer& gt)
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

    {
        auto currentPassCB = currentFrameResource->PrimePassConstantUploadBuffer;
        currentPassCB->CopyData(0, mainPassCB);
    }
}

void HybridShadowApp::UpdateSsaoCB(const GameTimer& gt)
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

    //for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        ambientPrimePath->GetOffsetVectors(ssaoCB.OffsetVectors);

        auto blurWeights = ambientPrimePath->CalcGaussWeights(2.5f);
        ssaoCB.BlurWeights[0] = Vector4(&blurWeights[0]);
        ssaoCB.BlurWeights[1] = Vector4(&blurWeights[4]);
        ssaoCB.BlurWeights[2] = Vector4(&blurWeights[8]);

        ssaoCB.InvRenderTargetSize = Vector2(1.0f / ambientPrimePath->SsaoMapWidth(),
                                             1.0f / ambientPrimePath->SsaoMapHeight());

        // Coordinates given in view space.
        ssaoCB.OcclusionRadius = 0.5f;
        ssaoCB.OcclusionFadeStart = 0.2f;
        ssaoCB.OcclusionFadeEnd = 1.0f;
        ssaoCB.SurfaceEpsilon = 0.05f;

        auto currSsaoCB = currentFrameResource->SsaoConstantUploadBuffer;
        currSsaoCB->CopyData(0, ssaoCB);
    }
}

void HybridShadowApp::PopulateShadowMapCommands(const GraphicsAdapter adapter, std::shared_ptr<GCommandList> cmdList)
{
    if (UseOnlyPrime)
    {
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory[GraphicAdapterPrimary]);
        cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                           *currentFrameResource->PrimePassConstantUploadBuffer, 1);

        shadowPathPrimeDevice->PopulatePreRenderCommands(cmdList);

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::ShadowMapOpaque));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::Opaque);
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::OpaqueAlphaDrop);

        cmdList->TransitionBarrier(shadowPathPrimeDevice->GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->FlushResourceBarriers();
    }
    else
    {
        if (adapter == GraphicAdapterPrimary)
        {
            cmdList->CopyResource(primeCopyShadowMap, crossAdapterShadowMap->GetPrimeResource());
        }
        else
        {
            //Draw Shadow Map
            cmdList->SetRootSignature(*secondDeviceShadowMapSignature.get());

            cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                               *currentFrameResource->MaterialBuffers[GraphicAdapterSecond]);
            cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                               *currentFrameResource->ShadowPassConstantUploadBuffer);

            shadowPathSecondDevice->PopulatePreRenderCommands(cmdList);

            cmdList->SetPipelineState(*shadowMapPSOSecondDevice.get());
            PopulateDrawCommands(GraphicAdapterSecond, cmdList, RenderMode::Opaque);
            PopulateDrawCommands(GraphicAdapterSecond, cmdList, RenderMode::OpaqueAlphaDrop);

            PopulateCopyResource(cmdList, shadowPathSecondDevice->GetTexture(),
                                 crossAdapterShadowMap->GetSharedResource());
        }
    }
}

void HybridShadowApp::PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Draw Normals
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory[GraphicAdapterPrimary]);
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory[GraphicAdapterPrimary]);

        cmdList->SetViewports(&fullViewport, 1);
        cmdList->SetScissorRects(&fullRect, 1);

        auto normalMap = ambientPrimePath->NormalMap();
        auto normalDepthMap = ambientPrimePath->NormalDepthMap();
        auto normalMapRtv = ambientPrimePath->NormalMapRtv();
        auto normalMapDsv = ambientPrimePath->NormalMapDSV();

        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->FlushResourceBarriers();
        float clearValue[] = {0.0f, 0.0f, 1.0f, 0.0f};
        cmdList->ClearRenderTarget(normalMapRtv, 0, clearValue);
        cmdList->ClearDepthStencil(normalMapDsv, 0,
                                   D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

        cmdList->SetRenderTargets(1, normalMapRtv, 0, normalMapDsv);
        cmdList->SetRootConstantBufferView(1, *currentFrameResource->PrimePassConstantUploadBuffer);

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::DrawNormalsOpaque));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::Opaque);
        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::DrawNormalsOpaqueDrop));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::OpaqueAlphaDrop);


        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }
}

void HybridShadowApp::PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Draw Ambient
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory[GraphicAdapterPrimary]);
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory[GraphicAdapterPrimary]);

        cmdList->SetRootSignature(*ssaoPrimeRootSignature.get());
        ambientPrimePath->ComputeSsao(cmdList, currentFrameResource->SsaoConstantUploadBuffer, 3);
    }
}

void HybridShadowApp::PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Forward Path with SSAA
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory[GraphicAdapterPrimary]);
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory[GraphicAdapterPrimary]);

        cmdList->SetViewports(&antiAliasingPrimePath->GetViewPort(), 1);
        cmdList->SetScissorRects(&antiAliasingPrimePath->GetRect(), 1);

        cmdList->TransitionBarrier((antiAliasingPrimePath->GetRenderTarget()), D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(antiAliasingPrimePath->GetDepthMap(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->FlushResourceBarriers();

        cmdList->ClearRenderTarget(antiAliasingPrimePath->GetRTV());
        cmdList->ClearDepthStencil(antiAliasingPrimePath->GetDSV(), 0,
                                   D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

        cmdList->SetRenderTargets(1, antiAliasingPrimePath->GetRTV(), 0,
                                  antiAliasingPrimePath->GetDSV());

        cmdList->
            SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                      *currentFrameResource->PrimePassConstantUploadBuffer);

        cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap,
                                        UseOnlyPrime ? shadowPathPrimeDevice->GetSrv() : &primeCopyShadowMapSRV);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ambientPrimePath->AmbientMapSrv(), 0);


        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::SkyBox));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::SkyBox));

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Opaque));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::Opaque));

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::OpaqueAlphaDrop));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::OpaqueAlphaDrop));

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Transparent));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::Transparent));

        switch (pathMapShow)
        {
        case 1:
            {
                cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap,
                                                UseOnlyPrime
                                                    ? shadowPathPrimeDevice->GetSrv()
                                                    : &primeCopyShadowMapSRV);
                cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Debug));
                PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::Debug));
                break;
            }
        case 2:
            {
                cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ambientPrimePath->AmbientMapSrv(), 0);
                cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Debug));
                PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::Debug));
                break;
            }
        }

        cmdList->TransitionBarrier(antiAliasingPrimePath->GetRenderTarget(),
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier((antiAliasingPrimePath->GetDepthMap()), D3D12_RESOURCE_STATE_DEPTH_READ);
        cmdList->FlushResourceBarriers();
    }
}

void HybridShadowApp::PopulateDrawCommands(const GraphicsAdapter adapterIndex, const std::shared_ptr<GCommandList>& cmdList,
                                        RenderMode type)
{
    for (auto&& renderer : typedRenderer[adapterIndex][(int)type])
    {
        renderer->Draw(cmdList);
    }
}

void HybridShadowApp::PopulateDrawQuadCommand(const std::shared_ptr<GCommandList>& cmdList,
                                           GTexture& renderTarget, GDescriptor* rtvMemory, const UINT offsetRTV)
{
    cmdList->SetViewports(&fullViewport, 1);
    cmdList->SetScissorRects(&fullRect, 1);

    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();
    cmdList->ClearRenderTarget(rtvMemory, offsetRTV, Colors::Black);

    cmdList->SetRenderTargets(1, rtvMemory, offsetRTV);

    cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, antiAliasingPrimePath->GetSRV());

    cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Quad));
    PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::Quad));

    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_PRESENT);
    cmdList->FlushResourceBarriers();
}

void HybridShadowApp::PopulateCopyResource(const std::shared_ptr<GCommandList>& cmdList, const GResource& srcResource,
                                        const GResource& dstResource)
{
    cmdList->CopyResource(dstResource, srcResource);
    cmdList->TransitionBarrier(dstResource,
                               D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(srcResource, D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();
}


void HybridShadowApp::Draw(const GameTimer& gt)
{
    if (isResizing) return;

    const UINT timestampHeapIndex = 2 * currentFrameResourceIndex;


    if (!UseOnlyPrime)
    {
        auto secondRenderQueue = devices[GraphicAdapterSecond]->GetCommandQueue();
        if (currentFrameResource->SecondRenderFenceValue == 0 || secondRenderQueue->IsFinish(
            currentFrameResource->SecondRenderFenceValue))
        {
            const auto shadowMapSecondCmdList = secondRenderQueue->GetCommandList();
            shadowMapSecondCmdList->EndQuery(timestampHeapIndex);
            PopulateShadowMapCommands(GraphicAdapterSecond, shadowMapSecondCmdList);
            shadowMapSecondCmdList->EndQuery(timestampHeapIndex + 1);
            shadowMapSecondCmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));
            currentFrameResource->SecondRenderFenceValue = secondRenderQueue->
                ExecuteCommandList(shadowMapSecondCmdList);
        }

        auto copyPrimeQueue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Copy);
        if (currentFrameResource->PrimeCopyFenceValue == 0 || copyPrimeQueue->IsFinish(
            currentFrameResource->PrimeCopyFenceValue))
        {
            const auto copyShadowMapCmdList = copyPrimeQueue->GetCommandList();
            PopulateShadowMapCommands(GraphicAdapterPrimary, copyShadowMapCmdList);
            currentFrameResource->PrimeCopyFenceValue = copyPrimeQueue->ExecuteCommandList(copyShadowMapCmdList);
        }
    }

    auto primeRenderQueue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Graphics);
    auto primeCmdList = primeRenderQueue->GetCommandList();
    primeCmdList->EndQuery(timestampHeapIndex);
    PopulateNormalMapCommands(primeCmdList);
    PopulateAmbientMapCommands(primeCmdList);
    if (UseOnlyPrime)
    {
        PopulateShadowMapCommands(GraphicAdapterPrimary, primeCmdList);
    }
    PopulateForwardPathCommands(primeCmdList);
    PopulateDrawQuadCommand(primeCmdList, MainWindow->GetCurrentBackBuffer(),
                            &currentFrameResource->BackBufferRTVMemory, 0);
    primeCmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
    primeCmdList->FlushResourceBarriers();
    primeCmdList->EndQuery(timestampHeapIndex + 1);
    primeCmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));
    currentFrameResource->PrimeRenderFenceValue = primeRenderQueue->ExecuteCommandList(primeCmdList);


    currentFrameResourceIndex = MainWindow->Present();
}

void HybridShadowApp::OnResize()
{
    D3DApp::OnResize();

    fullViewport.Height = static_cast<float>(MainWindow->GetClientHeight());
    fullViewport.Width = static_cast<float>(MainWindow->GetClientWidth());
    fullViewport.MinDepth = 0.0f;
    fullViewport.MaxDepth = 1.0f;
    fullViewport.TopLeftX = 0;
    fullViewport.TopLeftY = 0;
    fullRect = D3D12_RECT{0, 0, MainWindow->GetClientWidth(), MainWindow->GetClientHeight()};


    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = GetSRGBFormat(BackBufferFormat);
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        MainWindow->GetBackBuffer(i).CreateRenderTargetView(&rtvDesc, &frameResources[i]->BackBufferRTVMemory);
    }


    if (camera != nullptr)
    {
        camera->SetAspectRatio(AspectRatio());
    }

    {
        if (ambientPrimePath != nullptr)
        {
            ambientPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
            ambientPrimePath->RebuildDescriptors();
        }

        if (antiAliasingPrimePath != nullptr)
        {
            antiAliasingPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
        }
    }

    currentFrameResourceIndex = MainWindow->GetCurrentBackBufferIndex();
}

bool HybridShadowApp::InitMainWindow()
{
    MainWindow = CreateRenderWindow(devices[GraphicAdapterPrimary], mainWindowCaption, 1920, 1080, false);
    logQueue.Push(std::wstring(L"\nInit Window"));
    return true;
}

int HybridShadowApp::Run()
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
            if (finishTest)
            {
                MainWindow->SetWindowTitle(MainWindow->GetWindowName() + L" Finished. Wait...");
                LogWriting();
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

    return static_cast<int>(msg.wParam);
}

void HybridShadowApp::Flush()
{
    for (auto&& device : devices)
    {
        device->Flush();
    }
}

LRESULT HybridShadowApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
    return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}
