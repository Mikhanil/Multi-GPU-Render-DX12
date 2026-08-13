#include "ReflectionApp.h"

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
#include "ModelRenderer.h"
#include "Orbiter.h"

using namespace SimpleMath;
using namespace PEPEngine;
using namespace Utils;
using namespace Graphics;

HybridCubeMapApp::HybridCubeMapApp(const HINSTANCE hInstance) : D3DApp(hInstance), gpuTimes{}, fullRect()
{
    mSceneBounds.Center = Vector3(0.0f, 0.0f, 0.0f);
    mSceneBounds.Radius = 200;
}

HybridCubeMapApp::~HybridCubeMapApp()
{
    HybridCubeMapApp::Flush();

    for (auto&& device : devices)
    {
        device->ResetAllocators(frameCount);
        device->TerminatedQueuesWorker();
        device.reset();
    }

    devices.clear();

    logThreadIsAlive = false;
}

void HybridCubeMapApp::InitDevices()
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
        models.push_back(std::unordered_map<std::wstring, std::shared_ptr<GModel>>());

        typedRenderer.push_back(std::vector<std::vector<std::shared_ptr<Renderer>>>());

        for (int i = 0; i < static_cast<int>(RenderMode::Count); ++i)
        {
            typedRenderer[typedRenderer.size() - 1].push_back(
                std::vector<std::shared_ptr<Renderer>>());
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

void HybridCubeMapApp::InitFrameResource()
{
    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<FrameResource>(
            devices[GraphicAdapterPrimary], devices[GraphicAdapterSecond], 8,
            static_cast<UINT>(assets[GraphicAdapterPrimary].GetMaterials().size())));
    }
    logQueue.Push(std::wstring(L"\nInit FrameResource "));
}

void HybridCubeMapApp::InitRootSignature()
{
    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        auto rootSignature = std::make_shared<GRootSignature>();
        CD3DX12_DESCRIPTOR_RANGE texParam[4];
        texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0); //SkyMap
        texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0); //ShadowMap
        texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::AmbientMap - 3, 0); //SsaoMap
        texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                         static_cast<UINT>(assets[i].GetLoadTexturesCount() > 0 ? assets[i].GetLoadTexturesCount() : 1),
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
            secondDeviceSignature = rootSignature;
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

void HybridCubeMapApp::InitPipeLineResource()
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

    const D3D12_INPUT_LAYOUT_DESC desc = {defaultInputLayout.data(), static_cast<UINT>(defaultInputLayout.size())};

    primePipelineResources = RenderModeFactory();
    primePipelineResources.LoadDefaultShaders();
    primePipelineResources.LoadDefaultPSO(devices[GraphicAdapterPrimary], primeDeviceSignature, desc,
                                          BackBufferFormat, DepthStencilFormat, ssaoPrimeRootSignature,
                                          NormalMapFormat, AmbientMapFormat);

    secondPipelineResources = RenderModeFactory();
    secondPipelineResources.LoadDefaultShaders();
    secondPipelineResources.LoadDefaultPSO(devices[GraphicAdapterSecond], secondDeviceSignature, desc,
                                           BackBufferFormat, DepthStencilFormat, nullptr,
                                           NormalMapFormat, AmbientMapFormat);

    ambientPrimePath->SetPipelineData(*primePipelineResources.GetPSO(RenderMode::Ssao),
                                      *primePipelineResources.GetPSO(RenderMode::SsaoBlur));


    logQueue.Push(std::wstring(L"\nInit PSO for " + devices[GraphicAdapterPrimary]->GetName()));

    const auto primeDeviceShadowMapPso = primePipelineResources.GetPSO(RenderMode::ShadowMapOpaque);


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
    shadowMapPSOSecondDevice->SetRootSignature(secondDeviceSignature->GetNativeSignature().Get());
    shadowMapPSOSecondDevice->Initialize(devices[GraphicAdapterSecond]);
}

void HybridCubeMapApp::CreateMaterials()
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

    auto mirror = std::make_shared<Material>(L"mirror", RenderMode::Reflection);
    mirror->FresnelR0 = Vector3(0.98f, 0.98f, 0.98f);
    mirror->Roughness = 0.02f;

    auto white = assets[GraphicAdapterPrimary].GetTextureIndex(L"seamless");
    mirror->SetDiffuseTexture(assets[GraphicAdapterPrimary].GetTexture(white), white);

    auto nrm = assets[GraphicAdapterPrimary].GetTextureIndex(L"defaultNormalMap");
    mirror->SetNormalMap(assets[GraphicAdapterPrimary].GetTexture(nrm), nrm);
    assets[GraphicAdapterPrimary].AddMaterial(mirror);
        
    models[GraphicAdapterPrimary][L"mirrorSphere"]->SetMeshMaterial(
        0, assets[GraphicAdapterPrimary].GetMaterial(assets[GraphicAdapterPrimary].GetMaterialIndex(L"mirror")));
    
    logQueue.Push(std::wstring(L"\nCreate Materials"));
}

void HybridCubeMapApp::InitSRVMemoryAndMaterials()
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

void HybridCubeMapApp::InitRenderPaths()
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

    dynamicCubeMap = std::make_shared<CubeMapRenderTarget>(
        devices[GraphicAdapterPrimary], DynamicCubeMapSize, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT);

    dynamicCubeMapSecond = std::make_shared<CubeMapRenderTarget>(
        devices[GraphicAdapterSecond], DynamicCubeMapSize, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT);

    bakedCubeMapSecond = std::make_shared<BakedCubeMapRenderTarget>(
        devices[GraphicAdapterSecond], BakedCubeMapSize, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT);
    
    CreateDynamicTextures(GraphicAdapterSecond);
    
    // cubeMapDesc.DepthOrArraySize = 1;

    auto desc = dynamicCubeMapSecond->GetCubeMap().GetD3D12ResourceDesc();
    desc.DepthOrArraySize = 1;
    for (UINT face = 0; face < CubeMapRenderTarget::FaceCount; ++face)
    {
        crossAdapterCubeMaps[face] = std::make_shared<GCrossAdapterResource>(desc,
                                                                             devices[GraphicAdapterPrimary],
                                                                             devices[GraphicAdapterSecond],
                                                                             L"Shared Cube Map " +
                                                                             std::to_wstring(face));
    }

    primeCopyCubeMapSRV = devices[GraphicAdapterPrimary]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void HybridCubeMapApp::LoadStudyTexture()
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

    auto white1x1 = GTexture::LoadTextureFromFile(L"Data\\Textures\\white1x1.dds", cmdList);
    white1x1->SetName(L"white1x1Tex");
    assets[GraphicAdapterPrimary].AddTexture(white1x1);


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


    logQueue.Push(std::wstring(L"\nLoad DDS Texture"));
}

void HybridCubeMapApp::LoadModels()
{
    
        auto queue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Compute);
        auto cmdList = queue->GetCommandList();

        auto nano = assets[GraphicAdapterPrimary].CreateModelFromFile(
            cmdList, "Data\\Objects\\Nanosuit\\Nanosuit.obj");
        models[GraphicAdapterPrimary][L"nano"] = std::move(nano);

        auto atlas = assets[GraphicAdapterPrimary].CreateModelFromFile(
            cmdList, "Data\\Objects\\Atlas\\Atlas.obj");
        models[GraphicAdapterPrimary][L"atlas"] = std::move(atlas);
        auto pbody = assets[GraphicAdapterPrimary].CreateModelFromFile(
            cmdList, "Data\\Objects\\P-Body\\P-Body.obj");
        models[GraphicAdapterPrimary][L"pbody"] = std::move(pbody);

        auto griffon = assets[GraphicAdapterPrimary].CreateModelFromFile(
            cmdList, "Data\\Objects\\Griffon\\Griffon.FBX");
        griffon->scaleMatrix = Matrix::CreateScale(0.1f);
        models[GraphicAdapterPrimary][L"griffon"] = std::move(griffon);

        auto mountDragon = assets[GraphicAdapterPrimary].CreateModelFromFile(
            cmdList, "Data\\Objects\\MOUNTAIN_DRAGON\\MOUNTAIN_DRAGON.FBX");
        mountDragon->scaleMatrix = Matrix::CreateScale(0.1f);
        models[GraphicAdapterPrimary][L"mountDragon"] = std::move(mountDragon);

        auto desertDragon = assets[GraphicAdapterPrimary].CreateModelFromFile(
            cmdList, "Data\\Objects\\DesertDragon\\DesertDragon.FBX");
        desertDragon->scaleMatrix = Matrix::CreateScale(0.1f);
        models[GraphicAdapterPrimary][L"desertDragon"] = std::move(desertDragon);

        auto sphere = assets[GraphicAdapterPrimary].GenerateSphere(cmdList);
        models[GraphicAdapterPrimary][L"sphere"] = std::move(sphere);
        models[GraphicAdapterPrimary][L"mirrorSphere"] = assets[GraphicAdapterPrimary].GenerateSphere(cmdList);

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

        auto doom = assets[GraphicAdapterPrimary].CreateModelFromFile(
            cmdList, "Data\\Objects\\DoomSlayer\\doommarine.obj");
        models[GraphicAdapterPrimary][L"doom"] = std::move(doom);

        queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
        queue->Flush();

        logQueue.Push(std::wstring(L"\nLoad Models Data"));
    
}

void HybridCubeMapApp::MipMasGenerate()
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

void HybridCubeMapApp::DublicateResource()
{
    for (int i = GraphicAdapterPrimary + 1; i < GraphicAdapterCount; ++i)
    {
        logQueue.Push(std::wstring(L"\nStart Dublicate Resource for " + devices[i]->GetName()));
        try
        {
            auto queue = devices[i]->GetCommandQueue(GQueueType::Compute);
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

                for (UINT j = 0; j < model.second->GetMeshesCount(); ++j)
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

void HybridCubeMapApp::SortGO()
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

std::shared_ptr<Renderer> HybridCubeMapApp::CreateRenderer(const UINT deviceIndex, std::shared_ptr<GModel> model)
{
    auto renderer = std::make_shared<ModelRenderer>(devices[deviceIndex], model);
    return renderer;
}

void HybridCubeMapApp::AddMultiDeviceOpaqueRenderComponent(GameObject* object, const std::wstring& modelName,
                                                          RenderMode psoType)
{
    for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        auto renderer = CreateRenderer(i, models[i][modelName]);
        object->AddComponent(renderer);
        typedRenderer[i][static_cast<int>(psoType)].push_back(renderer);
    }
}

void HybridCubeMapApp::CreateGO()
{
    
    
        logQueue.Push(std::wstring(L"\nStart Create GO"));
        auto skySphere = std::make_unique<GameObject>("Sky");
        skySphere->GetTransform()->SetScale({500, 500, 500});
        for (int i = 0; i < GraphicAdapterCount; ++i)
        {
            auto renderer = std::make_shared<SkyBox>(devices[i],
                                                     models[i][L"sphere"],
                                                     *assets[i].GetTexture(
                                                         assets[i].
                                                         GetTextureIndex(L"skyTex")).get(),
                                                     &srvTexturesMemory[i],
                                                     assets[i].GetTextureIndex(L"skyTex"));

            skySphere->AddComponent(renderer);
            typedRenderer[i][static_cast<int>(RenderMode::SkyBox)].push_back((renderer));
        }
        gameObjects.push_back(std::move(skySphere));
    

    auto mirrorSphere = std::make_unique<GameObject>("MirrorSphere");
    mirrorSphere->GetTransform()->SetPosition(Vector3(0.0f, 20.0f, 0.0f));
    mirrorSphere->GetTransform()->SetScale(Vector3(2.0f, 2.0f, 2.0f));

    auto mirrorRenderer = std::make_shared<ModelRenderer>(devices[GraphicAdapterPrimary],
                                                          models[GraphicAdapterPrimary][L"mirrorSphere"]);
    mirrorSphere->AddComponent(mirrorRenderer);
    typedRenderer[GraphicAdapterPrimary][static_cast<int>(RenderMode::Reflection)].push_back(mirrorRenderer);

    mirrorSphereTransform = mirrorSphere->GetTransform();

    gameObjects.push_back(std::move(mirrorSphere));

    
    
        auto quadRitem = std::make_unique<GameObject>("Quad");
    auto renderer = std::make_shared<ModelRenderer>(devices[GraphicAdapterPrimary],
                                        models[GraphicAdapterPrimary][L"quad"]);
    renderer->SetModel(models[GraphicAdapterPrimary][L"quad"]);
    quadRitem->AddComponent(renderer);
    typedRenderer[GraphicAdapterPrimary][static_cast<int>(RenderMode::Debug)].push_back(renderer);
    typedRenderer[GraphicAdapterPrimary][static_cast<int>(RenderMode::Quad)].push_back(renderer);
    gameObjects.push_back(std::move(quadRitem));

        auto sun1 = std::make_unique<GameObject>("Directional Light");
        auto light = std::make_shared<Light>(Directional);
        light->Direction({0.57735f, -0.57735f, 0.57735f});
        light->Strength({0.8f, 0.8f, 0.8f});
        sun1->AddComponent(light);
        gameObjects.push_back(std::move(sun1));

    
    auto orbitNano = std::make_unique<GameObject>("OrbitNano");
    orbitNano->SetScale(0.5f);
    AddMultiDeviceOpaqueRenderComponent(orbitNano.get(), L"nano", RenderMode::DynamicOpaque);
    
    //auto orbitRenderer = std::make_shared<ModelRenderer>(devices[GraphicAdapterPrimary],
    //                                                     models[GraphicAdapterPrimary][L"nano"]);
    //typedRenderer[GraphicAdapterPrimary][static_cast<int>(RenderMode::Opaque)].push_back(orbitRenderer);

    auto orbit = std::make_shared<Orbiter>(
        mirrorSphereTransform,
        Vector3(5.0f, -5.0f, 0.0f),
        0.8f,
        Vector3(0.0f, 90.0f, 0.0f));
    orbitNano->AddComponent(orbit);
    gameObjects.push_back(std::move(orbitNano));
    
    for (int j = 0; j < 11; ++j)
    {
        auto nano = std::make_unique<GameObject>();
        nano->GetTransform()->SetPosition(Vector3::Right * -15.0f + Vector3::Forward * 12.0f * static_cast<float>(j));
        nano->GetTransform()->SetEulerRotate(Vector3(0, -90, 0));
        AddMultiDeviceOpaqueRenderComponent(nano.get(), L"nano");
        gameObjects.push_back(std::move(nano));

        auto doom = std::make_unique<GameObject>();
        doom->SetScale(0.08f);
        doom->GetTransform()->SetPosition(Vector3::Right * 15.0f + Vector3::Forward * 12.0f * static_cast<float>(j));
        doom->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
        AddMultiDeviceOpaqueRenderComponent(doom.get(), L"doom");
        gameObjects.push_back(std::move(doom));
    }

    for (int j = 0; j < 12; ++j)
    {
        for (int k = 0; k < 3; ++k)
        {
            auto atlas = std::make_unique<GameObject>();
            atlas->GetTransform()->SetPosition(
                Vector3::Right * -60.0f + Vector3::Right * -30.0f * static_cast<float>(k) + Vector3::Up * 11.0f + Vector3::Forward * 10.0f * static_cast<float>(j));
            AddMultiDeviceOpaqueRenderComponent(atlas.get(), L"atlas");
            gameObjects.push_back(std::move(atlas));


            auto pbody = std::make_unique<GameObject>();
            pbody->GetTransform()->SetPosition(
                Vector3::Right * 130.0f + Vector3::Right * -30.0f * static_cast<float>(k) + Vector3::Up * 11.0f + Vector3::Forward * 10.0f * static_cast<float>(j));
            AddMultiDeviceOpaqueRenderComponent(pbody.get(), L"pbody");
            gameObjects.push_back(std::move(pbody));
        }
    }

    auto platform = std::make_unique<GameObject>();
    platform->SetScale(0.2f);
    platform->GetTransform()->SetEulerRotate(Vector3(90, 90, 0));
    platform->GetTransform()->SetPosition(Vector3::Backward * -130);
    AddMultiDeviceOpaqueRenderComponent(platform.get(), L"platform");


    auto rotater = std::make_unique<GameObject>();
    rotater->GetTransform()->SetParent(platform->GetTransform().get());
    rotater->GetTransform()->SetPosition(Vector3::Forward * 325 + Vector3::Left * 625);
    rotater->GetTransform()->SetEulerRotate(Vector3(0, -90, 90));
    //rotater->AddComponent(std::make_shared<Rotater>(10)); // comment to disable auto camera rotation

    auto camera = std::make_unique<GameObject>("MainCamera");
    camera->GetTransform()->SetParent(rotater->GetTransform().get());
    camera->GetTransform()->SetEulerRotate(Vector3(-30, 180, 0));
    camera->GetTransform()->SetPosition(Vector3(0, -200, -20));
    camera->AddComponent(std::make_shared<Camera>(AspectRatio()));

#if defined(DEBUG) || defined(_DEBUG)
    camera->AddComponent(std::make_shared<CameraController>());
#else
    rotater->AddComponent(std::make_shared<Rotater>(10.0f));
#endif

    gameObjects.push_back(std::move(camera));
    gameObjects.push_back(std::move(rotater));

    auto stair = std::make_unique<GameObject>();
    stair->GetTransform()->SetParent(platform->GetTransform().get());
    stair->SetScale(0.2f);
    stair->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
    stair->GetTransform()->SetPosition(Vector3::Left * 700);
    AddMultiDeviceOpaqueRenderComponent(stair.get(), L"stair");

    auto columns = std::make_unique<GameObject>();
    columns->GetTransform()->SetParent(stair->GetTransform().get());
    columns->SetScale(0.8f);
    columns->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
    columns->GetTransform()->SetPosition(Vector3::Up * 2000 + Vector3::Forward * 900);
    AddMultiDeviceOpaqueRenderComponent(columns.get(), L"columns");

    auto fountain = std::make_unique<GameObject>();
    fountain->SetScale(0.005f);
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
    griffon->SetScale(0.8f);
    griffon->GetTransform()->SetPosition(Vector3::Right * -355 + Vector3::Up * -7 + Vector3::Backward * 17);
    AddMultiDeviceOpaqueRenderComponent(griffon.get(), L"griffon", RenderMode::OpaqueAlphaDrop);
    gameObjects.push_back(std::move(griffon));

    griffon = std::make_unique<GameObject>();
    griffon->SetScale(0.8f);
    griffon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    griffon->GetTransform()->SetPosition(Vector3::Right * 355 + Vector3::Up * -7 + Vector3::Backward * 17);
    AddMultiDeviceOpaqueRenderComponent(griffon.get(), L"griffon", RenderMode::OpaqueAlphaDrop);
    gameObjects.push_back(std::move(griffon));

    logQueue.Push(std::wstring(L"\nFinish create GO"));
}

void HybridCubeMapApp::CalculateFrameStats()
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

void HybridCubeMapApp::LogWriting()
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

bool HybridCubeMapApp::Initialize()
{
    InitDevices();
    InitMainWindow();

    LoadStudyTexture();
    Flush();
    LoadModels();
    Flush();
    CreateMaterials();
    Flush();
    DublicateResource();
    Flush();
    MipMasGenerate();
    Flush();
    InitRenderPaths();
    Flush();
    InitSRVMemoryAndMaterials();
    Flush();
    InitRootSignature();
    Flush();
    InitPipeLineResource();
    Flush();
    CreateGO();
    Flush();
    SortGO();
    Flush();
    InitFrameResource();
    Flush();
    OnResize();

    Flush();

    MainWindow->SetWindowTitle(
        MainWindow->GetWindowName() + L" SSAA X" + std::to_wstring(multi) + L" Calculate Part Shadow Map:" +
        std::to_wstring(!UseOnlyPrime));


    return true;
}

void HybridCubeMapApp::UpdateMaterials()
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

void HybridCubeMapApp::Update(const GameTimer& gt)
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
    const auto copyQueue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Copy);
    const auto secondQueue = devices[GraphicAdapterSecond]->GetCommandQueue(GQueueType::Graphics);

    currentFrameResource = frameResources[currentFrameResourceIndex];

    if (currentFrameResource->PrimeRenderFenceValue != 0 && !commandQueue->IsFinish(
        currentFrameResource->PrimeRenderFenceValue))
    {
        commandQueue->WaitForFenceValue(currentFrameResource->PrimeRenderFenceValue);
    }

    if (currentFrameResource->PrimeCopyFenceValue != 0 && !copyQueue->IsFinish(
        currentFrameResource->PrimeCopyFenceValue))
    {
        copyQueue->WaitForFenceValue(currentFrameResource->PrimeCopyFenceValue);
    }

    if (currentFrameResource->SecondRenderFenceValue != 0 && !secondQueue->IsFinish(
        currentFrameResource->SecondRenderFenceValue))
    {
        secondQueue->WaitForFenceValue(currentFrameResource->SecondRenderFenceValue);
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

void HybridCubeMapApp::UpdateShadowTransform(const GameTimer& gt)
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

void HybridCubeMapApp::UpdateShadowPassCB(const GameTimer& gt)
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

    auto currPassCB = currentFrameResource->SecondPassConstantUploadBuffer;
    currPassCB->CopyData(0, shadowPassCB);

    currPassCB = currentFrameResource->PrimePassConstantUploadBuffer;
    currPassCB->CopyData(1, shadowPassCB);
}

void HybridCubeMapApp::UpdateMainPassCB(const GameTimer& gt)
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
    mainPassCB.debugMap = static_cast<float>(pathMapShow);
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

void HybridCubeMapApp::UpdateSsaoCB(const GameTimer& gt)
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

void HybridCubeMapApp::PopulateShadowMapCommands(const GraphicsAdapter adapter, std::shared_ptr<GCommandList> cmdList)
{
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory[GraphicAdapterPrimary]);
        cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                           *currentFrameResource->PrimePassConstantUploadBuffer, 1);

        shadowPathPrimeDevice->PopulatePreRenderCommands(cmdList);

        cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::ShadowMapOpaque));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::Opaque);
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::OpaqueAlphaDrop);

        cmdList->TransitionBarrier(shadowPathPrimeDevice->GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->FlushResourceBarriers();    
}

void HybridCubeMapApp::PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList)
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

        cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::DrawNormalsOpaque));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::Opaque);
        cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::DrawNormalsOpaqueDrop));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::OpaqueAlphaDrop);


        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }
}

void HybridCubeMapApp::PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList)
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

void HybridCubeMapApp::PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList)
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

        cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowPathPrimeDevice->GetSrv());
        cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ambientPrimePath->AmbientMapSrv(), 0);


        cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::SkyBox));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::SkyBox));
        
        cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Opaque));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::Opaque);
        
        cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Opaque));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::DynamicOpaque);
        
        cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::OpaqueAlphaDrop));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::OpaqueAlphaDrop));
        
        cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Reflection));
        cmdList->SetRootDescriptorTable(StandardShaderSlot::SkyMap, dynamicCubeMap->GetSRV());
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::Reflection);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::SkyMap, srvTexturesMemory.data(), assets[GraphicAdapterPrimary].GetTextureIndex(L"skyTex"));
        
        cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Transparent));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::Transparent));

        switch (pathMapShow)
        {
        case 1:
            {
                cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap,shadowPathPrimeDevice->GetSrv());
                cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Debug));
                PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::Debug));
                break;
            }
        case 2:
            {
                cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ambientPrimePath->AmbientMapSrv(), 0);
                cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Debug));
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

void HybridCubeMapApp::PopulateDrawCommands(const GraphicsAdapter adapterIndex,
                                           const std::shared_ptr<GCommandList>& cmdList,
                                           RenderMode type)
{
    for (auto&& renderer : typedRenderer[adapterIndex][static_cast<int>(type)])
    {
        renderer->Draw(cmdList);
    }
}

void HybridCubeMapApp::PopulateDrawQuadCommand(const std::shared_ptr<GCommandList>& cmdList,
                                              const GTexture& renderTarget, const GDescriptor* rtvMemory, const UINT offsetRTV)
{
    cmdList->SetViewports(&fullViewport, 1);
    cmdList->SetScissorRects(&fullRect, 1);

    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();
    cmdList->ClearRenderTarget(rtvMemory, offsetRTV, Colors::Black);

    cmdList->SetRenderTargets(1, rtvMemory, offsetRTV);

    cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, antiAliasingPrimePath->GetSRV());

    cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Quad));
    PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::Quad));

    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_PRESENT);
    cmdList->FlushResourceBarriers();
}

void HybridCubeMapApp::PopulateCopyResource(const std::shared_ptr<GCommandList>& cmdList, const GResource& srcResource,
                                           const GResource& dstResource)
{
    cmdList->CopyResource(dstResource, srcResource);
    cmdList->TransitionBarrier(dstResource,
                               D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(srcResource, D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();
}


void HybridCubeMapApp::Draw(const GameTimer& gt)
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
            PopulateDynamicCubeMapCommands(GraphicAdapterSecond, shadowMapSecondCmdList);
            shadowMapSecondCmdList->EndQuery(timestampHeapIndex + 1);
            shadowMapSecondCmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));
            currentFrameResource->SecondRenderFenceValue = secondRenderQueue->ExecuteCommandList(shadowMapSecondCmdList);
        }
    }

    auto primeRenderQueue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Graphics);
    auto primeCmdList = primeRenderQueue->GetCommandList();
    primeCmdList->EndQuery(timestampHeapIndex);
    PopulateNormalMapCommands(primeCmdList);
    PopulateAmbientMapCommands(primeCmdList);
    PopulateShadowMapCommands(GraphicAdapterPrimary, primeCmdList);
    PopulateDynamicCubeMapCommands(GraphicAdapterPrimary, primeCmdList);
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

void HybridCubeMapApp::OnResize()
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

bool HybridCubeMapApp::InitMainWindow()
{
    MainWindow = CreateRenderWindow(devices[GraphicAdapterPrimary], mainWindowCaption, 1920, 1080, false);
    logQueue.Push(std::wstring(L"\nInit Window"));
    return true;
}

int HybridCubeMapApp::Run()
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

void HybridCubeMapApp::Flush()
{
    for (auto&& device : devices)
    {
        device->Flush();
    }
}

LRESULT HybridCubeMapApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
    switch (msg)
    {
    case WM_KEYUP:
        {
            auto keycode = static_cast<char>(wParam);
            keyboard.OnKeyReleased(keycode);
            return 0;
        }
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

    case WM_KEYDOWN:
        {
            auto keycode = static_cast<char>(wParam);
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


            return 0;
        }
    }

    return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}

std::array<PassConstants, HybridCubeMapApp::DynamicCubeMapFaceCount> HybridCubeMapApp::BuildCubeFacePassCBs(
    const Vector3& center) const
{
    constexpr float nearZ = 0.1f;
    constexpr float farZ = 500.0f;

    Matrix proj = XMMatrixPerspectiveFovLH(0.5f * XM_PI, 1.0f, nearZ, farZ);

    std::array<Vector3, DynamicCubeMapFaceCount> targets =
    {
        center + Vector3(1.0f, 0.0f, 0.0f),
        center + Vector3(-1.0f, 0.0f, 0.0f),
        center + Vector3(0.0f, 1.0f, 0.0f),
        center + Vector3(0.0f, -1.0f, 0.0f),
        center + Vector3(0.0f, 0.0f, 1.0f),
        center + Vector3(0.0f, 0.0f, -1.0f)
    };

    std::array<Vector3, DynamicCubeMapFaceCount> ups =
    {
        Vector3(0.0f, 1.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f),
        Vector3(0.0f, 0.0f, -1.0f),
        Vector3(0.0f, 0.0f, 1.0f),
        Vector3(0.0f, 1.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f)
    };

    Matrix T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);

    std::array<PassConstants, DynamicCubeMapFaceCount> out{};

    for (UINT i = 0; i < DynamicCubeMapFaceCount; ++i)
    {
        auto eye = XMVectorSet(center.x, center.y, center.z, 1.0f);
        auto at = XMVectorSet(targets[i].x, targets[i].y, targets[i].z, 1.0f);
        auto up = XMVectorSet(ups[i].x, ups[i].y, ups[i].z, 0.0f);

        Matrix view = XMMatrixLookAtLH(eye, at, up);
        Matrix viewProj = view * proj;

        PassConstants pass = mainPassCB;
        pass.View = view.Transpose();
        pass.InvView = view.Invert().Transpose();
        pass.Proj = proj.Transpose();
        pass.InvProj = proj.Invert().Transpose();
        pass.ViewProj = viewProj.Transpose();
        pass.InvViewProj = viewProj.Invert().Transpose();
        pass.ViewProjTex = (viewProj * T).Transpose();
        pass.EyePosW = center;
        pass.RenderTargetSize = Vector2(static_cast<float>(DynamicCubeMapSize), static_cast<float>(DynamicCubeMapSize));
        pass.InvRenderTargetSize = Vector2(1.0f / DynamicCubeMapSize, 1.0f / DynamicCubeMapSize);
        pass.NearZ = nearZ;
        pass.FarZ = farZ;

        out[i] = pass;
    }

    return out;
}

void HybridCubeMapApp::PopulateDynamicCubeMapCommands(const GraphicsAdapter adapter,
                                                     const std::shared_ptr<GCommandList>& cmdList)
{
    if (UseOnlyPrime)
    {
        if (dynamicCubeMap == nullptr) return;
        if (mirrorSphereTransform == nullptr) return;

        Vector3 center = mirrorSphereTransform->GetWorldPosition();

        cmdList->SetDescriptorsHeap(&srvTexturesMemory[GraphicAdapterPrimary]);
        cmdList->SetRootSignature(*primeDeviceSignature.get());       

        auto vp = dynamicCubeMap->GetViewport();
        auto rect = dynamicCubeMap->GetScissorRect();
        cmdList->SetViewports(&vp, 1);
        cmdList->SetScissorRects(&rect, 1);

        cmdList->SetGraphicsRootShaderResourceView(StandardShaderSlot::MaterialData,
                                                   *currentFrameResource->MaterialBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory[GraphicAdapterPrimary]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowPathPrimeDevice->GetSrv());

        auto whiteSsao = assets[GraphicAdapterPrimary].GetTextureIndex(L"white1x1Tex");
        cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap,
                                        &srvTexturesMemory[GraphicAdapterPrimary],
                                        whiteSsao);

        auto cubePasses = BuildCubeFacePassCBs(center);

        cmdList->TransitionBarrier(dynamicCubeMap->GetCubeMap(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(dynamicCubeMap->GetDepthMap(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->FlushResourceBarriers();
        
        for (UINT face = 0; face < DynamicCubeMapFaceCount; ++face)
        {
            UINT passIndex = DynamicCubeMapFirstPassIndex + face;
            currentFrameResource->PrimePassConstantUploadBuffer->CopyData(passIndex, cubePasses[face]);
            cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                               *currentFrameResource->PrimePassConstantUploadBuffer,
                                               passIndex);

            cmdList->ClearRenderTarget(&dynamicCubeMap->GetRTV(face), 0, Colors::Black);
            cmdList->ClearDepthStencil(dynamicCubeMap->GetDSV(), 0,
                                       D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);
            cmdList->SetRenderTargets(1, &dynamicCubeMap->GetRTV(face), 0, dynamicCubeMap->GetDSV());

            cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::SkyBox));
            PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::SkyBox);

            cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Opaque));
            PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::Opaque);
            
            cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Opaque));
            PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::DynamicOpaque);

            cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::OpaqueAlphaDrop));
            PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::OpaqueAlphaDrop);
            
            cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Reflection));
            cmdList->SetRootDescriptorTable(StandardShaderSlot::SkyMap, dynamicCubeMap->GetSRV());
            PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::Reflection);
            cmdList->SetRootDescriptorTable(StandardShaderSlot::SkyMap, srvTexturesMemory.data(),
                                            assets[GraphicAdapterPrimary].GetTextureIndex(L"skyTex"));

            cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Transparent));
            PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::Transparent);
        }

        cmdList->TransitionBarrier(dynamicCubeMap->GetCubeMap(), D3D12_RESOURCE_STATE_GENERIC_READ);
        cmdList->TransitionBarrier(dynamicCubeMap->GetDepthMap(), D3D12_RESOURCE_STATE_GENERIC_READ);
        cmdList->FlushResourceBarriers();
    }
    else
    {
        if (adapter == GraphicAdapterPrimary)
        {
            cmdList->TransitionBarrier(dynamicCubeMap->GetCubeMap(), D3D12_RESOURCE_STATE_COMMON);
            cmdList->FlushResourceBarriers();
            for (UINT face = 0; face < CubeMapRenderTarget::FaceCount; ++face)
            {
                cmdList->CopyResourceToCubeMap(dynamicCubeMap->GetCubeMap(),
                                               crossAdapterCubeMaps[face]->GetPrimeResource(), face);
            }
            cmdList->TransitionBarrier(dynamicCubeMap->GetCubeMap(), D3D12_RESOURCE_STATE_GENERIC_READ);
            cmdList->FlushResourceBarriers();
        }
        else
        {
            Vector3 center = mirrorSphereTransform->GetWorldPosition();

            cmdList->SetDescriptorsHeap(&srvTexturesMemory[GraphicAdapterSecond]);
            cmdList->SetRootSignature(*secondDeviceSignature.get());
            
            if (!isBaked)
            {
                auto vp = bakedCubeMapSecond->GetViewport();
                auto rect = bakedCubeMapSecond->GetScissorRect();
                cmdList->SetViewports(&vp, 1);
                cmdList->SetScissorRects(&rect, 1);
                
                cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                   *currentFrameResource->MaterialBuffers[GraphicAdapterSecond]);
                cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory[GraphicAdapterSecond]);


                auto whiteSsao = assets[GraphicAdapterSecond].GetTextureIndex(L"white1x1Tex");

                cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, &srvTexturesMemory[GraphicAdapterSecond],
                    whiteSsao);

                cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap,
                                                &srvTexturesMemory[GraphicAdapterSecond],
                                                whiteSsao);

                auto cubePasses = BuildCubeFacePassCBs(center);
                
                cmdList->TransitionBarrier(bakedCubeMapSecond->GetCubeMap(), D3D12_RESOURCE_STATE_RENDER_TARGET);
                cmdList->TransitionBarrier(bakedCubeMapSecond->GetDepthMap(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
                cmdList->FlushResourceBarriers();
                
                for (UINT face = 0; face < CubeMapRenderTarget::FaceCount; ++face)
                {
                    UINT passIndex = BakedCubeMapFirstPassIndex + face;
                    currentFrameResource->SecondPassConstantUploadBuffer->CopyData(passIndex, cubePasses[face]);
                    cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                                       *currentFrameResource->SecondPassConstantUploadBuffer,
                                                       passIndex);                

                    cmdList->ClearRenderTarget(&bakedCubeMapSecond->GetRTV(face), 0, Colors::Black);
                    cmdList->ClearDepthStencil(&bakedCubeMapSecond->GetDSV(face), 0,
                                               D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);
                    cmdList->SetRenderTargets(1, &bakedCubeMapSecond->GetRTV(face), 0, &bakedCubeMapSecond->GetDSV(face));

                    cmdList->SetPipelineState(*secondPipelineResources.GetPSO(RenderMode::SkyBox));
                    PopulateDrawCommands(GraphicAdapterSecond, cmdList, RenderMode::SkyBox);
                
                    cmdList->SetPipelineState(*secondPipelineResources.GetPSO(RenderMode::Opaque));
                    PopulateDrawCommands(GraphicAdapterSecond, cmdList, RenderMode::Opaque);

                    cmdList->SetPipelineState(*secondPipelineResources.GetPSO(RenderMode::OpaqueAlphaDrop));
                    PopulateDrawCommands(GraphicAdapterSecond, cmdList, RenderMode::OpaqueAlphaDrop);

                    cmdList->SetPipelineState(*secondPipelineResources.GetPSO(RenderMode::Transparent));
                    PopulateDrawCommands(GraphicAdapterSecond, cmdList, RenderMode::Transparent);
                }
                cmdList->TransitionBarrier(bakedCubeMapSecond->GetCubeMap(), D3D12_RESOURCE_STATE_GENERIC_READ);
                cmdList->TransitionBarrier(bakedCubeMapSecond->GetDepthMap(), D3D12_RESOURCE_STATE_GENERIC_READ);
                cmdList->FlushResourceBarriers();
                isBaked = true;
            }

            
            auto vp = dynamicCubeMap->GetViewport();
            auto rect = dynamicCubeMap->GetScissorRect();
            cmdList->SetViewports(&vp, 1);
            cmdList->SetScissorRects(&rect, 1);

            cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                               *currentFrameResource->MaterialBuffers[GraphicAdapterSecond]);
            cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory[GraphicAdapterSecond]);
            
            auto whiteSsao = assets[GraphicAdapterSecond].GetTextureIndex(L"white1x1Tex");

            cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, &srvTexturesMemory[GraphicAdapterSecond],
                whiteSsao);

            cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap,
                                            &srvTexturesMemory[GraphicAdapterSecond],
                                            whiteSsao);

            auto cubePasses = BuildCubeFacePassCBs(center);
            
            for (UINT face = 0; face < CubeMapRenderTarget::FaceCount; ++face)
            {
                UINT passIndex = BakedCubeMapFirstPassIndex + face;
                currentFrameResource->SecondPassConstantUploadBuffer->CopyData(passIndex, cubePasses[face]);
                cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                                   *currentFrameResource->SecondPassConstantUploadBuffer,
                                                   passIndex);                
                
                cmdList->CopyResourceFromCubeMap(dynamicCubeMapFaceColor,
                  bakedCubeMapSecond->GetCubeMap(),
                  face);
                
                cmdList->CopyResourceFromCubeMap(dynamicCubeMapFaceDepth,
                                  bakedCubeMapSecond->GetDepthMap(),
                                  face);
                
                cmdList->TransitionBarrier(dynamicCubeMapFaceColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
                cmdList->TransitionBarrier(dynamicCubeMapFaceDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                cmdList->FlushResourceBarriers();
                
                cmdList->SetRenderTargets(1, &dynamicCubeMapFaceRtv, 0, &dynamicCubeMapFaceDsv);
                
                cmdList->SetPipelineState(*secondPipelineResources.GetPSO(RenderMode::Opaque));
                PopulateDrawCommands(GraphicAdapterSecond, cmdList, RenderMode::DynamicOpaque);
                
                cmdList->CopyResource(crossAdapterCubeMaps[face]->GetSharedResource(), dynamicCubeMapFaceColor);
            }

            cmdList->TransitionBarrier(dynamicCubeMapFaceColor, D3D12_RESOURCE_STATE_GENERIC_READ);
            cmdList->TransitionBarrier(dynamicCubeMapFaceDepth, D3D12_RESOURCE_STATE_GENERIC_READ);
            cmdList->FlushResourceBarriers();

        }
    }
}

void HybridCubeMapApp::CreateDynamicTextures(const GraphicsAdapter adapter)
{
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depthFormat = DXGI_FORMAT_D32_FLOAT;
    
    D3D12_RESOURCE_DESC colorDesc{};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Alignment = 0;
    colorDesc.Width = BakedCubeMapSize;
    colorDesc.Height = BakedCubeMapSize;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels = 1;
    colorDesc.Format = format;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.SampleDesc.Quality = 0;
    colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    constexpr float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    const CD3DX12_CLEAR_VALUE clearColor(format, clear);

    dynamicCubeMapFaceColor = GTexture(devices[adapter], colorDesc, L"DynamicCubeMapFaceColor", TextureUsage::RenderTarget, &clearColor);

    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Alignment = 0;
    depthDesc.Width = BakedCubeMapSize;
    depthDesc.Height = BakedCubeMapSize;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = depthFormat;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearDepth{};
    clearDepth.Format = depthFormat;
    clearDepth.DepthStencil.Depth = 1.0f;
    clearDepth.DepthStencil.Stencil = 0;

    dynamicCubeMapFaceDepth = GTexture(devices[adapter], depthDesc, L"DynamicCubeMapFaceDepth", TextureUsage::Depth, &clearDepth);
    
    dynamicCubeMapFaceRtv = devices[adapter]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    dynamicCubeMapFaceDsv = devices[adapter]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
    dynamicCubeMapFaceSrv = devices[adapter]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    dynamicCubeMapFaceColor.CreateShaderResourceView(&srvDesc, &dynamicCubeMapFaceSrv);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    dynamicCubeMapFaceColor.CreateRenderTargetView(&rtvDesc, &dynamicCubeMapFaceRtv);
    
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = depthFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.Texture2D.MipSlice = 0;
    dynamicCubeMapFaceDepth.CreateDepthStencilView(&dsvDesc, &dynamicCubeMapFaceDsv);
}
