#include "HybridSSAOApp.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <valarray>

#include "CameraController.h"
#include "GameObject.h"
#include "GDeviceFactory.h"
#include "GModel.h"
#include "imgui.h"
#include "ModelRenderer.h"
#include "Rotater.h"
#include "SharedHBAO.h"
#include "SkyBox.h"
#include "Transform.h"
#include "Window.h"
#include "Services/States/WaitState.h"

HybridSSAOApp::HybridSSAOApp(const HINSTANCE hInstance) : D3DApp(hInstance), debugLogger(FileQueueWriter(std::filesystem::current_path() / "log.txt"))
{
    mSceneBounds.Center = Vector3(0.0f, 0.0f, 0.0f);
    mSceneBounds.Radius = 200;
}

HybridSSAOApp::~HybridSSAOApp() = default;

void HybridSSAOApp::SwitchDevice()
{
    Flush();
    IsUsingSharedSSAO = !IsUsingSharedSSAO;
}

void HybridSSAOApp::ChangeAOMethod()
{
    Flush();
    IsUseHBAO = !IsUseHBAO;
}

void HybridSSAOApp::ResetCamera() const
{
    auto& CamTrans = camera->gameObject->GetTransform();
    if (auto* Rotater = CamTrans->GetParent())
    {
        Rotater->SetLocalMatrix(RotaterSaveMatrix);
    }
    CamTrans->SetLocalMatrix(CameraSaveMatrix);
}

void HybridSSAOApp::Update(const GameTimer& gt)
{
    const UINT olderIndex = currentFrameResourceIndex - 1 > globalCountFrameResources
                                ? 0
                                : static_cast<UINT>(currentFrameResourceIndex);
    primeGPURenderingTime = primeDevice->GetCommandQueue()->GetTimestamp(olderIndex);
    secondGPURenderingTime = secondDevice->GetCommandQueue()->GetTimestamp(olderIndex);

    const auto primeQueue = primeDevice->GetCommandQueue(GQueueType::Graphics);
    const auto secondQueue = secondDevice->GetCommandQueue(GQueueType::Graphics);

    currentFrameResource = frameResources[currentFrameResourceIndex];

    if (currentFrameResource->PrimeRenderFenceValue != 0 && !primeQueue->IsFinish(
        currentFrameResource->PrimeRenderFenceValue))
    {
        primeQueue->WaitForFenceValue(currentFrameResource->PrimeRenderFenceValue);
    }
    else
    {
        primeDevice->ReleaseStaleDescriptors(currentFrameResource->PrimeRenderFenceValue);
    }

    if (currentFrameResource->SecondRenderFenceValue != 0 && !secondQueue->IsFinish(
        currentFrameResource->SecondRenderFenceValue))
    {
        secondQueue->WaitForFenceValue(currentFrameResource->SecondRenderFenceValue);
    }
    else
    {
        secondDevice->ReleaseStaleDescriptors(currentFrameResource->SecondRenderFenceValue);
    }

    mLightRotationAngle += 0.1f * gt.DeltaTime();

    Matrix R = Matrix::CreateRotationY(mLightRotationAngle);
    for (int i = 0; i < 3; ++i)
    {
        auto lightDir = mBaseLightDirections[i];
        lightDir = Vector3::TransformNormal(lightDir, R);
        mRotatedLightDirections[i] = lightDir;
    }

    for (const auto& go : gameObjects)
    {
        go->Update();
    }

    UpdateMaterials();
    UpdateShadowTransform(gt);
    UpdateMainPassCB(gt);
    UpdateShadowPassCB(gt);
    UpdateSsaoCB(gt);
    UIPath->Update();
    benchmark.Tick(gt.DeltaTime());
}

void HybridSSAOApp::PopulateShadowMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //cmdList->SetRootSignature(*primeDeviceSignature.get());
    cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::ShadowMapOpaque));
    cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                       *currentFrameResource->MaterialBuffer, 1);
    cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory);
    cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                       *currentFrameResource->PrimePassConstantUploadBuffer, 1);

    shadowPath->PopulatePreRenderCommands(cmdList);


    PopulateDrawCommands(cmdList, RenderMode::Opaque);
    PopulateDrawCommands(cmdList, RenderMode::OpaqueAlphaDrop);

    cmdList->TransitionBarrier(shadowPath->GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

void HybridSSAOApp::PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Draw Normals
    {
        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::DrawNormalsOpaque));
        cmdList->SetDescriptorsHeap(&srvTexturesMemory);
        //cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffer);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory);

        cmdList->SetViewports(&fullViewport, 1);
        cmdList->SetScissorRects(&fullRect, 1);


        GTexture normalMap;
        GTexture normalDepthMap;
        const GDescriptor* normalMapRtv;
        const GDescriptor* normalMapDsv;


        if (IsUseHBAO)
        {
            const HBAOResources& Resources = hbaoPass->GetPrimeResources();
            normalMap = Resources.GetNormalMap();
            normalDepthMap = Resources.GetDepthMap();
            normalMapRtv = Resources.GetNormalMapRTV();
            normalMapDsv = Resources.GetDepthMapDSV();
        }
        else
        {
            const SSAOResources& Resources = ssaoPass->GetPrimeResources();
            normalMap = Resources.GetNormalMap();
            normalDepthMap = Resources.GetDepthMap();
            normalMapRtv = Resources.GetNormalMapRTV();
            normalMapDsv = Resources.GetDepthMapDSV();
        }

        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->FlushResourceBarriers();
        float clearValue[] = {0.0f, 0.0f, 1.0f, 0.0f};
        cmdList->ClearRenderTarget(normalMapRtv, 0, clearValue);
        cmdList->ClearDepthStencil(normalMapDsv, 0,
                                   D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0);

        cmdList->SetRenderTargets(1, normalMapRtv, 0, normalMapDsv);
        cmdList->SetRootConstantBufferView(1, *currentFrameResource->PrimePassConstantUploadBuffer);

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::DrawNormalsOpaque));
        PopulateDrawCommands(cmdList, RenderMode::Opaque);
        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::DrawNormalsOpaqueDrop));
        PopulateDrawCommands(cmdList, RenderMode::OpaqueAlphaDrop);


        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }
}

void HybridSSAOApp::PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList) const
{
    if (IsUsingSharedSSAO)
    {
        if (IsUseHBAO)
        {
            {
                const auto& Resources = hbaoPass->GetPrimeResources();
                const auto& CrossResource = hbaoPass->GetCrossResources();
                cmdList->CopyResource(CrossResource.GetDepthMap().GetPrimeResource(), Resources.GetDepthMap());
                cmdList->CopyResource(Resources.GetAmbientMap(), CrossResource.GetAmbientMap().GetPrimeResource());
            }
            {
                auto secondQueue = secondDevice->GetCommandQueue();
                if (currentFrameResource->SecondRenderFenceValue == 0 || secondQueue->IsFinish(currentFrameResource->SecondRenderFenceValue))
                {
                    const auto& Resources = hbaoPass->GetSecondResources();
                    const auto& CrossResource = hbaoPass->GetCrossResources();
                    const auto secondCmdList = secondQueue->GetCommandList();
                    secondCmdList->CopyResource(Resources.GetDepthMap(), CrossResource.GetDepthMap().GetSharedResource());

                    hbaoPass->Compute(secondCmdList, currentFrameResource->SecondHBAOConstantUploadBuffer, Resources);

                    secondCmdList->CopyResource(CrossResource.GetAmbientMap().GetSharedResource(), Resources.GetAmbientMap());

                    currentFrameResource->SecondRenderFenceValue = secondQueue->ExecuteCommandList(secondCmdList);
                }
            }
        }
        else
        {
            {
                const auto& Resources = ssaoPass->GetPrimeResources();
                const auto& CrossResource = ssaoPass->GetCrossResources();
                cmdList->CopyResource(CrossResource.GetDepthMap().GetPrimeResource(), Resources.GetDepthMap());
                cmdList->CopyResource(CrossResource.GetNormalMap().GetPrimeResource(), Resources.GetNormalMap());
                cmdList->CopyResource(Resources.GetAmbientMap(), CrossResource.GetAmbientMap().GetPrimeResource());
            }
            {
                auto secondQueue = secondDevice->GetCommandQueue();
                if (currentFrameResource->SecondRenderFenceValue == 0 || secondQueue->IsFinish(currentFrameResource->SecondRenderFenceValue))
                {
                    const auto& Resources = ssaoPass->GetSecondResource();
                    const auto& CrossResource = ssaoPass->GetCrossResources();
                    const auto secondCmdList = secondQueue->GetCommandList();
                    secondCmdList->CopyResource(Resources.GetNormalMap(), CrossResource.GetNormalMap().GetSharedResource());
                    secondCmdList->CopyResource(Resources.GetDepthMap(), CrossResource.GetDepthMap().GetSharedResource());

                    ssaoPass->ComputeSsao(secondCmdList, currentFrameResource->SecondSsaoConstantUploadBuffer, Resources, 1);

                    secondCmdList->CopyResource(CrossResource.GetAmbientMap().GetSharedResource(), Resources.GetAmbientMap());

                    currentFrameResource->SecondRenderFenceValue = secondQueue->ExecuteCommandList(secondCmdList);
                }
            }
        }
    }
    else
    {
        if (IsUseHBAO)
            hbaoPass->Compute(cmdList, currentFrameResource->PrimeHBAOConstantUploadBuffer, hbaoPass->GetPrimeResources());
        else
            ssaoPass->ComputeSsao(cmdList, currentFrameResource->PrimeSsaoConstantUploadBuffer, ssaoPass->GetPrimeResources(), 3);
    }
}

void HybridSSAOApp::PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Forward Path with SSAA
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory);
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffer);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory);

        cmdList->SetViewports(&antiAliasingPrimePath->GetViewPort(), 1);
        cmdList->SetScissorRects(&antiAliasingPrimePath->GetRect(), 1);

        cmdList->TransitionBarrier((antiAliasingPrimePath->GetRenderTarget()), D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(antiAliasingPrimePath->GetDepthMap(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->FlushResourceBarriers();

        cmdList->ClearRenderTarget(antiAliasingPrimePath->GetRTV(), 0, Colors::Black);
        cmdList->ClearDepthStencil(antiAliasingPrimePath->GetDSV(), 0,
                                   D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0);

        cmdList->SetRenderTargets(1, antiAliasingPrimePath->GetRTV(), 0,
                                  antiAliasingPrimePath->GetDSV());

        cmdList->
            SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                      *currentFrameResource->PrimePassConstantUploadBuffer);

        cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowPath->GetSrv());
        if (IsUseHBAO)
            cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, hbaoPass->GetPrimeResources().GetAmbientMapSRV());
        else
            cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ssaoPass->GetPrimeResources().GetAmbientMapSRV(), 0);


        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::SkyBox));
        PopulateDrawCommands(cmdList, (RenderMode::SkyBox));

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Opaque));
        PopulateDrawCommands(cmdList, (RenderMode::Opaque));

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::OpaqueAlphaDrop));
        PopulateDrawCommands(cmdList, (RenderMode::OpaqueAlphaDrop));

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Transparent));
        PopulateDrawCommands(cmdList, (RenderMode::Transparent));

        cmdList->
            SetGraphicsRootConstantBufferView(StandardShaderSlot::CameraData,
                                              *currentFrameResource->PrimePassConstantUploadBuffer);
        PopulateDrawCommands(cmdList, (RenderMode::Particle));


        cmdList->TransitionBarrier(antiAliasingPrimePath->GetRenderTarget(),
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier((antiAliasingPrimePath->GetDepthMap()), D3D12_RESOURCE_STATE_DEPTH_READ);
        cmdList->FlushResourceBarriers();
    }
}

void HybridSSAOApp::PopulateDrawCommands(const std::shared_ptr<GCommandList>& cmdList,
                                         RenderMode type) const
{
    for (auto&& renderer : typedRenderer[static_cast<int>(type)])
    {
        renderer->Draw(cmdList);
    }
}

void HybridSSAOApp::PopulateInitRenderTarget(const std::shared_ptr<GCommandList>& cmdList, const GTexture& renderTarget,
                                             const GDescriptor* rtvMemory, const UINT offsetRTV) const
{
    cmdList->SetViewports(&fullViewport, 1);
    cmdList->SetScissorRects(&fullRect, 1);

    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();
    cmdList->ClearRenderTarget(rtvMemory, offsetRTV, Colors::Black);

    cmdList->SetRenderTargets(1, rtvMemory, offsetRTV);
}

void HybridSSAOApp::PopulateDrawFullQuadTexture(const std::shared_ptr<GCommandList>& cmdList,
                                                const GDescriptor* renderTextureSRVMemory, const UINT renderTextureMemoryOffset,
                                                const GraphicPSO& pso) const
{
    cmdList->SetPipelineState(pso);

    cmdList->SetDescriptorsHeap(renderTextureSRVMemory);
    cmdList->SetGraphicsRootDescriptorTable(StandardShaderSlot::AmbientMap, renderTextureSRVMemory, renderTextureMemoryOffset);

    PopulateDrawCommands(cmdList, (RenderMode::Quad));
}

void HybridSSAOApp::PopulateDebugCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    switch (pathMapShow)
    {
    case 1:
        {
            PopulateDrawFullQuadTexture(cmdList, shadowPath->GetSrv(),
                                        0, *defaultPrimePipelineResources.GetPSO(RenderMode::Quad));
            break;
        }
    case 2:
        {
            if (IsUseHBAO)
                PopulateDrawFullQuadTexture(cmdList, hbaoPass->GetPrimeResources().GetAmbientMapSRV(),
                                            0, *defaultPrimePipelineResources.GetPSO(RenderMode::Quad));

            else
                PopulateDrawFullQuadTexture(cmdList, ssaoPass->GetPrimeResources().GetAmbientMapSRV(),
                                            0, *defaultPrimePipelineResources.GetPSO(RenderMode::Quad));


            break;
        }
    }
}

void HybridSSAOApp::Draw(const GameTimer& gt)
{
    if (isResizing) return;

    auto primeRenderQueue = primeDevice->GetCommandQueue();
    auto primeCmdList = primeRenderQueue->GetCommandList();

    for (auto emitter : emitters)
    {
        emitter->Dispatch(primeCmdList);
    }

    PopulateNormalMapCommands(primeCmdList);
    PopulateAmbientMapCommands(primeCmdList);
    PopulateShadowMapCommands(primeCmdList);
    PopulateForwardPathCommands(primeCmdList);
    PopulateInitRenderTarget(primeCmdList, MainWindow->GetCurrentBackBuffer(),
                             &currentFrameResource->BackBufferRTVMemory, 0);
    PopulateDrawFullQuadTexture(primeCmdList, antiAliasingPrimePath->GetSRV(),
                                0, *defaultPrimePipelineResources.GetPSO(RenderMode::Quad));

    PopulateDebugCommands(primeCmdList);

    UIPath->Render(primeCmdList);

    primeCmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
    primeCmdList->FlushResourceBarriers();
    currentFrameResource->PrimeRenderFenceValue = primeRenderQueue->ExecuteCommandList(primeCmdList);

    currentFrameResourceIndex = MainWindow->Present();
}


bool HybridSSAOApp::Initialize()
{
    InitDevices();
    InitMainWindow();

    LoadStudyTexture();
    LoadModels();
    CreateMaterials();
    MipMasGenerate();

    InitRenderPaths();
    InitSRVMemoryAndMaterials();
    InitRootSignature();
    InitPipeLineResource();
    CreateGO();
    SortGO();
    InitFrameResource();

    OnResize();

    Flush();

    int TestTime = 10;
#if !defined(DEBUG) && !defined(_DEBUG)
    TestTime = 120;
#endif


    auto& NativeSSAOState = benchmark.AddState<WaitState>(TestTime, FileQueueWriter(Benchmark::GetLogFile(L"Native SSAO ", *primeDevice, *secondDevice)));
    NativeSSAOState.OnEnter = [](FileQueueWriter& logs)
    {
        logs.PushMessage(L"FPS;MSPF;MinFPS;MinMSPF;MaxFPS;MaxMSPF");
    };

    NativeSSAOState.OnStatChanged = [this](FileQueueWriter& logs, const TimeStats& ts, float progress)
    {
        Benchmark::PrintStatsCSV(ts, logs);
        MainWindow->SetWindowTitle(L"Native SSAO Progress " + std::format(L"{:.2f}", progress * 100) + L"% FPS:" + std::to_wstring(ts.fps));
    };

    NativeSSAOState.OnExit = [this](FileQueueWriter& logs)
    {
        logs.WriteAllLog();
        Flush();
    };

    auto& HybridSSAOState = benchmark.AddState<WaitState>(TestTime, FileQueueWriter(Benchmark::GetLogFile(L"Hybrid SSAO ", *primeDevice, *secondDevice)));
    HybridSSAOState.OnEnter = [this](FileQueueWriter& logs)
    {
        ResetCamera();
        SwitchDevice();
        logs.PushMessage(L"FPS;MSPF;MinFPS;MinMSPF;MaxFPS;MaxMSPF");
    };
    HybridSSAOState.OnStatChanged = [this](FileQueueWriter& logs, const TimeStats& ts, float progress)
    {
        Benchmark::PrintStatsCSV(ts, logs);
        MainWindow->SetWindowTitle(L"Hybrid SSAO Progress " + std::format(L"{:.2f}", progress * 100) + L"% FPS:" + std::to_wstring(ts.fps));
    };
    HybridSSAOState.OnExit = [this](FileQueueWriter& logs)
    {
        logs.WriteAllLog();
        Flush();
        SwitchDevice();
    };


    auto& NativeHBAOState = benchmark.AddState<WaitState>(TestTime, FileQueueWriter(Benchmark::GetLogFile(L"Native HBAO ", *primeDevice, *secondDevice)));
    NativeHBAOState.OnEnter = [this](FileQueueWriter& logs)
    {
        ResetCamera();
        logs.PushMessage(L"FPS;MSPF;MinFPS;MinMSPF;MaxFPS;MaxMSPF");
    };
    NativeHBAOState.OnStatChanged = [this](FileQueueWriter& logs, const TimeStats& ts, float progress)
    {
        Benchmark::PrintStatsCSV(ts, logs);
        MainWindow->SetWindowTitle(L"Native HBAO Progress " + std::format(L"{:.2f}", progress * 100) + L"% FPS:" + std::to_wstring(ts.fps));
    };
    NativeHBAOState.OnExit = [this](FileQueueWriter& logs)
    {
        logs.WriteAllLog();
        Flush();
    };

    auto& HybridHBAOState = benchmark.AddState<WaitState>(TestTime, FileQueueWriter(Benchmark::GetLogFile(L"Hybrid HBAO ", *primeDevice, *secondDevice)));
    HybridHBAOState.OnEnter = [this](FileQueueWriter& logs)
    {
        ResetCamera();
        logs.PushMessage(L"FPS;MSPF;MinFPS;MinMSPF;MaxFPS;MaxMSPF");
        SwitchDevice();
    };
    HybridHBAOState.OnStatChanged = [this](FileQueueWriter& logs, const TimeStats& ts, float progress)
    {
        Benchmark::PrintStatsCSV(ts, logs);
        MainWindow->SetWindowTitle(L"Hybrid HBAO Progress " + std::format(L"{:.2f}", progress * 100) + L"% FPS:" + std::to_wstring(ts.fps));
    };
    HybridHBAOState.OnExit = [this](FileQueueWriter& logs)
    {
        logs.WriteAllLog();
        SwitchDevice();
        Flush();
        IsStop = true;
    };


#if !defined(DEBUG) && !defined(_DEBUG)
    benchmark.Start();
#endif
    return true;
}

void HybridSSAOApp::InitDevices()
{
    auto allDevices = GDeviceFactory::GetAllDevices(true);

    const auto firstDevice = allDevices[0];
    const auto otherDevice = allDevices[1];

    primeDevice = firstDevice;
    secondDevice = otherDevice;

    if (firstDevice->GetName().find(L"NVIDIA") == std::string::npos)
    {
        if (otherDevice->GetName().find(L"NVIDIA") != std::wstring::npos)
        {
            primeDevice = otherDevice;
            secondDevice = firstDevice;
        }
        else
        {
            if (firstDevice->GetDesc().DedicatedVideoMemory > otherDevice->GetDesc().DedicatedVideoMemory)
            {
                primeDevice = firstDevice;
                secondDevice = otherDevice;
            }
            else
            {
                primeDevice = otherDevice;
                secondDevice = firstDevice;
            }
        }
    }

    assets = std::make_shared<AssetsLoader>(primeDevice);

    for (int i = 0; i < static_cast<uint8_t>(RenderMode::Count); ++i)
    {
        typedRenderer.emplace_back(MemoryAllocator::CreateVector<std::shared_ptr<Renderer>>());
    }


    debugLogger.PushMessage(L"\nPrime Device: " + (primeDevice->GetName()));
    debugLogger.PushMessage(
        L"\t\n Cross Adapter Texture Support: " + std::to_wstring(
            primeDevice->IsCrossAdapterTextureSupported()));
    debugLogger.PushMessage(L"\nSecond Device: " + (secondDevice->GetName()));
    debugLogger.PushMessage(
        L"\t\n Cross Adapter Texture Support: " + std::to_wstring(
            secondDevice->IsCrossAdapterTextureSupported()));
}

void HybridSSAOApp::InitFrameResource()
{
    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        frameResources.emplace_back(std::make_unique<FrameResource>(primeDevice, secondDevice, 2, assets->GetMaterials().size()));
    }
    debugLogger.PushMessage(std::wstring(L"\nInit FrameResource "));
}

void HybridSSAOApp::InitRootSignature()
{
    auto rootSignature = std::make_shared<GRootSignature>();
    CD3DX12_DESCRIPTOR_RANGE texParam[4];
    texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0); //SkyMap
    texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0); //ShadowMap
    texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::AmbientMap - 3, 0); //SsaoMap
    texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                     assets->GetLoadTexturesCount() > 0 ? assets->GetLoadTexturesCount() : 1,
                     StandardShaderSlot::TexturesMap - 3, 0);


    rootSignature->AddConstantBufferParameter(0);
    rootSignature->AddConstantBufferParameter(1);
    rootSignature->AddShaderResourceView(0, 1);
    rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->Initialize(primeDevice);

    primeDeviceSignature = rootSignature;

    debugLogger.PushMessage(std::wstring(L"\nInit RootSignature for " + primeDevice->GetName()));
}

void HybridSSAOApp::InitPipeLineResource()
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
    defaultPrimePipelineResources.LoadDefaultPSO(primeDevice, primeDeviceSignature, desc,
                                                 BackBufferFormat, DXGI_FORMAT_D32_FLOAT, nullptr,
                                                 NormalMapFormat, AmbientMapFormat);

    debugLogger.PushMessage(std::wstring(L"\nInit PSO for " + primeDevice->GetName()));
}

void HybridSSAOApp::CreateMaterials()
{
    auto seamless = std::make_shared<Material>(L"seamless", RenderMode::Opaque);
    seamless->FresnelR0 = Vector3(0.02f, 0.02f, 0.02f);
    seamless->Roughness = 0.1f;

    auto tex = assets->GetTextureIndex(L"seamless");
    seamless->SetDiffuseTexture(assets->GetTexture(tex), tex);

    tex = assets->GetTextureIndex(L"defaultNormalMap");

    seamless->SetNormalMap(assets->GetTexture(tex), tex);
    assets->AddMaterial(seamless);

    models[L"quad"]->SetMeshMaterial(0, assets->GetMaterial(assets->GetMaterialIndex(L"seamless")));

    debugLogger.PushMessage(std::wstring(L"\nCreate Materials"));
}

void HybridSSAOApp::InitSRVMemoryAndMaterials()
{
    srvTexturesMemory =
        primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, assets->GetTextures().size());

    auto materials = assets->GetMaterials();

    for (int j = 0; j < materials.size(); ++j)
    {
        auto material = materials[j];

        material->InitMaterial(&srvTexturesMemory);
    }

    debugLogger.PushMessage(std::wstring(L"\nInit Views for " + primeDevice->GetName()));
}

void HybridSSAOApp::InitRenderPaths()
{
    auto commandQueue = primeDevice->GetCommandQueue(GQueueType::Graphics);
    auto cmdList = commandQueue->GetCommandList();

    ssaoPass = std::make_shared<SharedSSAO>();
    hbaoPass = std::make_shared<SharedHBAO>();

    const D3D12_INPUT_LAYOUT_DESC layoutDesc = {defaultInputLayout.data(), defaultInputLayout.size()};


    ssaoPass->Initialize(
        primeDevice,
        secondDevice,
        layoutDesc,
        MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    ssaoPass->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

    hbaoPass->Initialize(primeDevice, secondDevice, layoutDesc, MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    hbaoPass->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

    antiAliasingPrimePath = (std::make_shared<SSAA>(primeDevice, 1, MainWindow->GetClientWidth(),
                                                    MainWindow->GetClientHeight(), DXGI_FORMAT_D32_FLOAT));
    antiAliasingPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

    shadowPath = (std::make_shared<ShadowMap>(primeDevice, 4096, 4096));

    UIPath = std::make_shared<UILayer>(primeDevice, MainWindow->GetWindowHandle());

    commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));
    commandQueue->Flush();

    debugLogger.PushMessage(std::wstring(L"\nInit Render path data for " + primeDevice->GetName()));
}

void HybridSSAOApp::LoadStudyTexture()
{
    auto queue = primeDevice->GetCommandQueue(GQueueType::Compute);

    const auto cmdList = queue->GetCommandList();

    auto bricksTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\bricks2.dds", cmdList);
    bricksTex->SetName(L"bricksTex");
    assets->AddTexture(bricksTex);

    auto stoneTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\stone.dds", cmdList);
    stoneTex->SetName(L"stoneTex");
    assets->AddTexture(stoneTex);

    auto tileTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\tile.dds", cmdList);
    tileTex->SetName(L"tileTex");
    assets->AddTexture(tileTex);

    auto fenceTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\WireFence.dds", cmdList);
    fenceTex->SetName(L"fenceTex");
    assets->AddTexture(fenceTex);

    auto waterTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\water1.dds", cmdList);
    waterTex->SetName(L"waterTex");
    assets->AddTexture(waterTex);

    auto skyTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\skymap.dds", cmdList);
    skyTex->SetName(L"skyTex");
    assets->AddTexture(skyTex);

    auto grassTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\grass.dds", cmdList);
    grassTex->SetName(L"grassTex");
    assets->AddTexture(grassTex);

    auto treeArrayTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\treeArray2.dds", cmdList);
    treeArrayTex->SetName(L"treeArrayTex");
    assets->AddTexture(treeArrayTex);

    auto seamless = GTexture::LoadTextureFromFile(L"Data\\Textures\\seamless_grass.jpg", cmdList);
    seamless->SetName(L"seamless");
    assets->AddTexture(seamless);


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
        assets->AddTexture(texture);
    }

    queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
    Flush();
    debugLogger.PushMessage(std::wstring(L"\nLoad DDS Texture"));
}

void HybridSSAOApp::LoadModels()
{
    auto queue = primeDevice->GetCommandQueue(GQueueType::Compute);
    auto cmdList = queue->GetCommandList();

    auto nano = assets->CreateModelFromFile(cmdList, "Data\\Objects\\Nanosuit\\Nanosuit.obj");
    models[L"nano"] = std::move(nano);

    auto atlas = assets->CreateModelFromFile(cmdList, "Data\\Objects\\Atlas\\Atlas.obj");
    models[L"atlas"] = std::move(atlas);
    auto pbody = assets->CreateModelFromFile(cmdList, "Data\\Objects\\P-Body\\P-Body.obj");
    models[L"pbody"] = std::move(pbody);

    auto griffon = assets->CreateModelFromFile(cmdList, "Data\\Objects\\Griffon\\Griffon.FBX");
    griffon->scaleMatrix = Matrix::CreateScale(0.1);
    models[L"griffon"] = std::move(griffon);

    auto mountDragon = assets->CreateModelFromFile(
        cmdList, "Data\\Objects\\MOUNTAIN_DRAGON\\MOUNTAIN_DRAGON.FBX");
    mountDragon->scaleMatrix = Matrix::CreateScale(0.1);
    models[L"mountDragon"] = std::move(mountDragon);

    auto desertDragon = assets->CreateModelFromFile(
        cmdList, "Data\\Objects\\DesertDragon\\DesertDragon.FBX");
    desertDragon->scaleMatrix = Matrix::CreateScale(0.1);
    models[L"desertDragon"] = std::move(desertDragon);

    auto sphere = assets->GenerateSphere(cmdList);
    models[L"sphere"] = std::move(sphere);

    auto quad = assets->GenerateQuad(cmdList);
    models[L"quad"] = std::move(quad);

    auto stair = assets->CreateModelFromFile(
        cmdList, "Data\\Objects\\Temple\\SM_AsianCastle_A.FBX");
    models[L"stair"] = std::move(stair);

    auto columns = assets->CreateModelFromFile(
        cmdList, "Data\\Objects\\Temple\\SM_AsianCastle_E.FBX");
    models[L"columns"] = std::move(columns);

    auto fountain = assets->
        CreateModelFromFile(cmdList, "Data\\Objects\\Temple\\SM_Fountain.FBX");
    models[L"fountain"] = std::move(fountain);

    auto platform = assets->CreateModelFromFile(
        cmdList, "Data\\Objects\\Temple\\SM_PlatformSquare.FBX");
    models[L"platform"] = std::move(platform);

    auto doom = assets->CreateModelFromFile(cmdList, "Data\\Objects\\DoomSlayer\\doommarine.obj");
    models[L"doom"] = std::move(doom);

    queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
    Flush();
    debugLogger.PushMessage(std::wstring(L"\nLoad Models Data"));
}

void HybridSSAOApp::MipMasGenerate()
{
    try
    {
        {
            std::vector<GTexture*> generatedMipTextures;

            auto textures = assets->GetTextures();

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

            const auto computeQueue = primeDevice->GetCommandQueue(GQueueType::Compute);
            auto computeList = computeQueue->GetCommandList();
            GTexture::GenerateMipMaps(computeList, generatedMipTextures.data(), generatedMipTextures.size());
            computeQueue->WaitForFenceValue(computeQueue->ExecuteCommandList(computeList));
            debugLogger.PushMessage(std::wstring(L"\nMip Map Generation for " + primeDevice->GetName()));

            computeList = computeQueue->GetCommandList();
            for (auto&& texture : generatedMipTextures)
                computeList->TransitionBarrier(texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
            computeList->FlushResourceBarriers();
            debugLogger.PushMessage(std::wstring(L"\nTexture Barrier Generation for " + primeDevice->GetName()));
            computeQueue->WaitForFenceValue(computeQueue->ExecuteCommandList(computeList));
            computeQueue->Flush();
            debugLogger.PushMessage(std::wstring(L"\nMipMap Generation cmd list executing " + primeDevice->GetName()));
            for (auto&& pair : textures)
                pair->ClearTrack();
            debugLogger.PushMessage(std::wstring(L"\nFinish Mip Map Generation for " + primeDevice->GetName()));
        }
    }
    catch (DxException& e)
    {
        debugLogger.PushMessage(L"\n" + e.Filename + L" " + e.FunctionName + L" " + std::to_wstring(e.LineNumber));
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
    }
    catch (...)
    {
        debugLogger.PushMessage(L"\nWTF???? How It Fix");
    }
}

void HybridSSAOApp::SortGO()
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

void HybridSSAOApp::CreateGO()
{
    debugLogger.PushMessage(std::wstring(L"\nStart Create GO"));
    auto skySphere = std::make_unique<GameObject>("Sky");
    skySphere->GetTransform()->SetScale({500, 500, 500});
    {
        auto renderer = std::make_shared<SkyBox>(primeDevice,
                                                 models[L"sphere"],
                                                 *assets->GetTexture(
                                                     assets->
                                                     GetTextureIndex(L"skyTex")).get(),
                                                 &srvTexturesMemory,
                                                 assets->GetTextureIndex(L"skyTex"));

        skySphere->AddComponent(renderer);
        typedRenderer[static_cast<int>(RenderMode::SkyBox)].push_back((renderer));
    }
    gameObjects.push_back(std::move(skySphere));

    auto quadRitem = std::make_unique<GameObject>("Quad");
    {
        auto renderer = std::make_shared<ModelRenderer>(primeDevice,
                                                        models[L"quad"]);
        renderer->SetModel(models[L"quad"]);
        quadRitem->AddComponent(renderer);
        typedRenderer[static_cast<int>(RenderMode::Debug)].push_back(renderer);
        typedRenderer[static_cast<int>(RenderMode::Quad)].push_back(renderer);
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
        auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"nano"]);
        nano->AddComponent(renderer);
        typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
        gameObjects.push_back(std::move(nano));


        auto doom = std::make_unique<GameObject>();
        doom->SetScale(0.08);
        doom->GetTransform()->SetPosition(Vector3::Right * 15 + Vector3::Forward * 12 * i);
        doom->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
        renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"doom"]);
        doom->AddComponent(renderer);
        typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
        gameObjects.push_back(std::move(doom));
    }

    for (int i = 0; i < 12; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            auto atlas = std::make_unique<GameObject>();
            atlas->GetTransform()->SetPosition(
                Vector3::Right * -60 + Vector3::Right * -30 * j + Vector3::Up * 11 + Vector3::Forward * 10 * i);
            auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"atlas"]);
            atlas->AddComponent(renderer);
            typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
            gameObjects.push_back(std::move(atlas));


            auto pbody = std::make_unique<GameObject>();
            pbody->GetTransform()->SetPosition(
                Vector3::Right * 130 + Vector3::Right * -30 * j + Vector3::Up * 11 + Vector3::Forward * 10 * i);
            renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"pbody"]);
            pbody->AddComponent(renderer);
            typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
            gameObjects.push_back(std::move(pbody));
        }
    }

    auto particle = std::make_unique<GameObject>();
    particle->GetTransform()->SetPosition(Vector3::Up);
    const auto emitter = std::make_shared<ParticleEmitter>(primeDevice, 10000);
    particle->AddComponent(emitter);
    typedRenderer[static_cast<int>(RenderMode::Particle)].push_back(emitter);
    emitters.push_back(emitter.get());
    gameObjects.push_back(std::move(particle));


    auto platform = std::make_unique<GameObject>();
    platform->SetScale(0.2);
    platform->GetTransform()->SetEulerRotate(Vector3(90, 90, 0));
    platform->GetTransform()->SetPosition(Vector3::Backward * -130);
    auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"platform"]);
    platform->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);


    auto rotater = std::make_unique<GameObject>();
    rotater->GetTransform()->SetParent(platform->GetTransform().get());
    rotater->GetTransform()->SetPosition(Vector3::Forward * 325 + Vector3::Left * 625);
    rotater->GetTransform()->SetEulerRotate(Vector3(0, -90, 90));
    RotaterSaveMatrix = rotater->GetTransform()->GetLocalMatrix();

    auto camera = std::make_unique<GameObject>("MainCamera");
    camera->GetTransform()->SetParent(rotater->GetTransform().get());
    camera->GetTransform()->SetPosition(Vector3(-1000, 190, -32));
    camera->GetTransform()->SetEulerRotate(Vector3(-30, 270, 0));
    camera->AddComponent(std::make_shared<Camera>(AspectRatio()));
    CameraSaveMatrix = camera->GetTransform()->GetLocalMatrix();

#if defined(DEBUG) || defined(_DEBUG)
    camera->AddComponent(std::make_shared<CameraController>());
#else
    rotater->AddComponent(std::make_shared<Rotater>(10));
#endif

    gameObjects.push_back(std::move(camera));
    gameObjects.push_back(std::move(rotater));


    auto stair = std::make_unique<GameObject>();
    stair->GetTransform()->SetParent(platform->GetTransform().get());
    stair->SetScale(0.2);
    stair->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
    stair->GetTransform()->SetPosition(Vector3::Left * 700);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"stair"]);
    stair->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);


    auto columns = std::make_unique<GameObject>();
    columns->GetTransform()->SetParent(stair->GetTransform().get());
    columns->SetScale(0.8);
    columns->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
    columns->GetTransform()->SetPosition(Vector3::Up * 2000 + Vector3::Forward * 900);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"columns"]);
    columns->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);

    auto fountain = std::make_unique<GameObject>();
    fountain->SetScale(0.005);
    fountain->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    fountain->GetTransform()->SetPosition(Vector3::Up * 35 + Vector3::Backward * 77);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"fountain"]);
    fountain->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);

    gameObjects.push_back(std::move(platform));
    gameObjects.push_back(std::move(stair));
    gameObjects.push_back(std::move(columns));
    gameObjects.push_back(std::move(fountain));


    auto mountDragon = std::make_unique<GameObject>();
    mountDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    mountDragon->GetTransform()->SetPosition(Vector3::Right * -960 + Vector3::Up * 45 + Vector3::Backward * 775);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"mountDragon"]);
    mountDragon->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
    gameObjects.push_back(std::move(mountDragon));


    auto desertDragon = std::make_unique<GameObject>();
    desertDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    desertDragon->GetTransform()->SetPosition(Vector3::Right * 960 + Vector3::Up * -5 + Vector3::Backward * 775);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"desertDragon"]);
    desertDragon->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
    gameObjects.push_back(std::move(desertDragon));

    auto griffon = std::make_unique<GameObject>();
    griffon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    griffon->SetScale(0.8);
    griffon->GetTransform()->SetPosition(Vector3::Right * -355 + Vector3::Up * -7 + Vector3::Backward * 17);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"griffon"]);
    griffon->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::OpaqueAlphaDrop)].push_back(renderer);
    gameObjects.push_back(std::move(griffon));

    griffon = std::make_unique<GameObject>();
    griffon->SetScale(0.8);
    griffon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    griffon->GetTransform()->SetPosition(Vector3::Right * 355 + Vector3::Up * -7 + Vector3::Backward * 17);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"griffon"]);
    griffon->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::OpaqueAlphaDrop)].push_back(renderer);
    gameObjects.push_back(std::move(griffon));

    debugLogger.PushMessage(std::wstring(L"\nFinish create GO"));
}

void HybridSSAOApp::OnApplicationExit()
{
    debugLogger.WriteAllLog();
}

int HybridSSAOApp::Run()
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
            if (IsStop)
            {
                MainWindow->SetWindowTitle(MainWindow->GetWindowName() + L" Finished. Wait...");
                OnApplicationExit();
                Quit();
                break;
            }

            timer.Tick();

            {
                Update(timer);
                Draw(timer);
            }

            primeDevice->ResetAllocators(frameCount);
            secondDevice->ResetAllocators(frameCount);
        }
    }

    return static_cast<int>(msg.wParam);
}

void HybridSSAOApp::UpdateMaterials() const
{
    {
        auto currentMaterialBuffer = currentFrameResource->MaterialBuffer;

        for (auto&& material : assets->GetMaterials())
        {
            material->Update();
            auto constantData = material->GetMaterialConstantData();
            currentMaterialBuffer->CopyData(material->GetIndex(), constantData);
        }
    }
}

void HybridSSAOApp::UpdateShadowTransform(const GameTimer& gt)
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

void HybridSSAOApp::UpdateShadowPassCB(const GameTimer& gt)
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

    UINT w = shadowPath->Width();
    UINT h = shadowPath->Height();
    shadowPassCB.RenderTargetSize = Vector2(static_cast<float>(w), static_cast<float>(h));
    shadowPassCB.InvRenderTargetSize = Vector2(1.0f / w, 1.0f / h);

    auto currPassCB = currentFrameResource->PrimePassConstantUploadBuffer;
    currPassCB->CopyData(1, shadowPassCB);
}

void HybridSSAOApp::UpdateMainPassCB(const GameTimer& gt)
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

    auto currentPassCB = currentFrameResource->PrimePassConstantUploadBuffer;
    currentPassCB->CopyData(0, mainPassCB);
}

void HybridSSAOApp::UpdateSsaoCB(const GameTimer& gt) const
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
        ssaoPass->GetPrimeResources().GetOffsetVectors(ssaoCB.OffsetVectors);

        auto blurWeights = ssaoPass->CalcGaussWeights(2.5f);
        ssaoCB.BlurWeights[0] = Vector4(&blurWeights[0]);
        ssaoCB.BlurWeights[1] = Vector4(&blurWeights[4]);
        ssaoCB.BlurWeights[2] = Vector4(&blurWeights[8]);

        ssaoCB.InvRenderTargetSize = Vector2(1.0f / ssaoPass->SsaoMapWidth(),
                                             1.0f / ssaoPass->SsaoMapHeight());

        // Coordinates given in view space.
        ssaoCB.OcclusionRadius = 0.5f;
        ssaoCB.OcclusionFadeStart = 0.2f;
        ssaoCB.OcclusionFadeEnd = 1.0f;
        ssaoCB.SurfaceEpsilon = 0.05f;

        currentFrameResource->PrimeSsaoConstantUploadBuffer->CopyData(0, ssaoCB);
        currentFrameResource->SecondSsaoConstantUploadBuffer->CopyData(0, ssaoCB);
    }
    {
        HBAOConstants hbaoCB;
        hbaoCB.ProjMatrix = mainPassCB.Proj;
        hbaoCB.InvProjMatrix = mainPassCB.InvProj;
        hbaoCB.ClipInfo = Vector2(1.0f / std::tan(XMConvertToRadians(camera->GetFov()) / 2.0f));
        hbaoCB.MaxRadiusPixels = 64;
        hbaoCB.TraceRadius = 2.0f;
        hbaoCB.Resolution = Vector4(MainWindow->GetClientWidth(), MainWindow->GetClientHeight(),
                                    1.0f / MainWindow->GetClientWidth(), 1.0f / MainWindow->GetClientHeight());
        hbaoCB.DiscardDistance = 300.0f;

        currentFrameResource->PrimeHBAOConstantUploadBuffer->CopyData(0, hbaoCB);
        currentFrameResource->SecondHBAOConstantUploadBuffer->CopyData(0, hbaoCB);
    }
}

bool HybridSSAOApp::InitMainWindow()
{
    MainWindow = CreateRenderWindow(primeDevice, mainWindowCaption, 1920, 1080, false);

    debugLogger.PushMessage(std::wstring(L"\nInit Window"));
    return true;
}

void HybridSSAOApp::OnResize()
{
    UIPath->Invalidate();
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

    if (ssaoPass != nullptr)
    {
        ssaoPass->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    }

    if (antiAliasingPrimePath != nullptr)
    {
        antiAliasingPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    }

    UIPath->CreateDeviceObject();

    currentFrameResourceIndex = MainWindow->GetCurrentBackBufferIndex();
}

void HybridSSAOApp::Flush()
{
    primeDevice->Flush();
    secondDevice->Flush();
}

LRESULT HybridSSAOApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
    UIPath->MsgProc(hwnd, msg, wParam, lParam);

#if defined(DEBUG) || defined(_DEBUG)
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
            auto keycode = static_cast<char>(wParam);
            keyboard.OnKeyReleased(keycode);
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

#if defined(DEBUG) || defined(_DEBUG)
            if (keycode == (VK_F1) && keyboard.KeyIsPressed(VK_F1))
            {
                IsUsingSharedSSAO = !IsUsingSharedSSAO;
                Flush();
            }

            if (keycode == (VK_F2) && keyboard.KeyIsPressed(VK_F2))
            {
                pathMapShow = (pathMapShow + 1) % maxPathMap;
            }

            if (keycode == (VK_F3) && keyboard.KeyIsPressed(VK_F3))
            {
                IsUseHBAO = !IsUseHBAO;
                Flush();
            }

            if (keycode == (VK_F9) && keyboard.KeyIsPressed(VK_F9))
            {
                Flush();
                ResetCamera();
            }
#endif

            return 0;
        }
    }
#endif
    return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}
