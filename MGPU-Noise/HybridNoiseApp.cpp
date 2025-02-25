#include "HybridNoiseApp.h"

#include <array>
#include <filesystem>
#include <fstream>
#include "CameraController.h"
#include "CrossAdapterParticleEmitter.h"
#include "GameObject.h"
#include "GDeviceFactory.h"
#include "GModel.h"
#include "imgui.h"
#include "MathHelper.h"
#include "ModelRenderer.h"
#include "ParticleEmitter.h"
#include "Rotater.h"
#include "SkyBox.h"
#include "Transform.h"
#include "Window.h"

HybridNoiseApp::HybridNoiseApp(const HINSTANCE hInstance): D3DApp(hInstance)
{
    mSceneBounds.Center = Vector3(0.0f, 0.0f, 0.0f);
    mSceneBounds.Radius = 200;
}

HybridNoiseApp::~HybridNoiseApp()
{
}

void HybridNoiseApp::Update(const GameTimer& gt)
{
    const UINT olderIndex = currentFrameResourceIndex - 1 > globalCountFrameResources
                                ? 0
                                : static_cast<UINT>(currentFrameResourceIndex);
    primeGPURenderingTime = primeDevice->GetCommandQueue()->GetTimestamp(olderIndex);
    secondGPURenderingTime = secondDevice->GetCommandQueue()->GetTimestamp(olderIndex);

    primeGPUComputingTime = primeDevice->GetCommandQueue(GQueueType::Compute)->GetTimestamp(olderIndex);
    secondGPUComputingTime = secondDevice->GetCommandQueue(GQueueType::Compute)->GetTimestamp(olderIndex);

    const auto commandQueue = primeDevice->GetCommandQueue(GQueueType::Graphics);

    currentFrameResource = frameResources[currentFrameResourceIndex];

    if (currentFrameResource->PrimeRenderFenceValue != 0 && !commandQueue->IsFinish(
        currentFrameResource->PrimeRenderFenceValue))
    {
        commandQueue->WaitForFenceValue(currentFrameResource->PrimeRenderFenceValue);
    }

    mLightRotationAngle += 0.1f * gt.DeltaTime();

    const Matrix R = Matrix::CreateRotationY(mLightRotationAngle);
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

void HybridNoiseApp::PopulateShadowMapCommands(std::shared_ptr<GCommandList> cmdList)
{
    cmdList->SetRootSignature(*primeDeviceSignature.get());
    cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                       *currentFrameResource->MaterialBuffer, 1);
    cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory);
    cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                       *currentFrameResource->PrimePassConstantUploadBuffer, 1);

    shadowPath->PopulatePreRenderCommands(cmdList);

    cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::ShadowMapOpaque));
    PopulateDrawCommands(cmdList, RenderMode::Opaque);
    PopulateDrawCommands(cmdList, RenderMode::OpaqueAlphaDrop);

    cmdList->TransitionBarrier(shadowPath->GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

void HybridNoiseApp::PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Draw Normals
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory);
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffer);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory);

        cmdList->SetViewports(&fullViewport, 1);
        cmdList->SetScissorRects(&fullRect, 1);

        const auto normalMap = ambientPrimePath->NormalMap();
        const auto normalDepthMap = ambientPrimePath->NormalDepthMap();
        const auto normalMapRtv = ambientPrimePath->NormalMapRtv();
        const auto normalMapDsv = ambientPrimePath->NormalMapDSV();

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
        PopulateDrawCommands(cmdList, RenderMode::Opaque);
        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::DrawNormalsOpaqueDrop));
        PopulateDrawCommands(cmdList, RenderMode::OpaqueAlphaDrop);


        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }
}

void HybridNoiseApp::PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList) const
{
    //Draw Ambient
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory);
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffer);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory);

        cmdList->SetRootSignature(*ssaoPrimeRootSignature.get());
        ambientPrimePath->ComputeSsao(cmdList, currentFrameResource->SsaoConstantUploadBuffer, 3);
    }
}

void HybridNoiseApp::PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Forward Path with SSAA
    {
        cmdList->SetRootSignature(*primeDeviceSignature.get());
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
                                   D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

        cmdList->SetRenderTargets(1, antiAliasingPrimePath->GetRTV(), 0,
                                  antiAliasingPrimePath->GetDSV());


        cmdList->
            SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                      *currentFrameResource->PrimePassConstantUploadBuffer);


        if (UseSecondApproach)
        {
            cmdList->SetDescriptorsHeap(cloudGeneratorV2->GetCloudSRV());
            cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, cloudGeneratorV2->GetCloudSRV());
        }
        else
        {
            cmdList->SetDescriptorsHeap(cloudGenerator->GetCloudSRV());
            cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, cloudGenerator->GetCloudSRV());
        }

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::SkyBox));
        PopulateDrawCommands(cmdList, (RenderMode::SkyBox));

        cmdList->SetDescriptorsHeap(&srvTexturesMemory);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowPath->GetSrv());
        cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ambientPrimePath->AmbientMapSrv(), 0);


        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Opaque));
        PopulateDrawCommands(cmdList, (RenderMode::Opaque));

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::OpaqueAlphaDrop));
        PopulateDrawCommands(cmdList, (RenderMode::OpaqueAlphaDrop));

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Transparent));
        PopulateDrawCommands(cmdList, (RenderMode::Transparent));


        if (!IsCalibration)
        {
            cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                               *currentFrameResource->PrimePassConstantUploadBuffer.get(), 0);
            PopulateDrawCommands(cmdList, RenderMode::Particle);
        }

        cmdList->TransitionBarrier(antiAliasingPrimePath->GetRenderTarget(),
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier((antiAliasingPrimePath->GetDepthMap()), D3D12_RESOURCE_STATE_DEPTH_READ);
        cmdList->FlushResourceBarriers();
    }
}

void HybridNoiseApp::PopulateDrawCommands(const std::shared_ptr<GCommandList>& cmdList, RenderMode type) const
{
    for (auto&& renderer : typedRenderer[static_cast<int>(type)])
    {
        renderer->Draw(cmdList);
    }
}

void HybridNoiseApp::PopulateInitRenderTarget(const std::shared_ptr<GCommandList>& cmdList, const GTexture& renderTarget,
                                                const GDescriptor* rtvMemory, const UINT offsetRTV) const
{
    cmdList->SetViewports(&fullViewport, 1);
    cmdList->SetScissorRects(&fullRect, 1);

    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();
    cmdList->ClearRenderTarget(rtvMemory, offsetRTV, Colors::Black);

    cmdList->SetRenderTargets(1, rtvMemory, offsetRTV);
}

void HybridNoiseApp::PopulateDrawFullQuadTexture(const std::shared_ptr<GCommandList>& cmdList,
                                                   const GDescriptor* renderTextureSRVMemory,
                                                   const UINT renderTextureMemoryOffset, const GraphicPSO& pso)
{
    cmdList->SetRootSignature(*primeDeviceSignature.get());
    cmdList->SetDescriptorsHeap(renderTextureSRVMemory);

    cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, renderTextureSRVMemory, renderTextureMemoryOffset);

    cmdList->SetPipelineState(pso);
    PopulateDrawCommands(cmdList, (RenderMode::Quad));
}


UINT64 HybridNoiseApp::ComputeEmitters(const UINT timestampHeapIndex, const std::shared_ptr<GCommandQueue>& computeQueue) const
{
    primeComputeQueue->Wait(renderQueue);

    const auto cmdList = computeQueue->GetCommandList();

    cmdList->EndQuery(timestampHeapIndex);

    for (auto emitter : crossEmitter)
    {
        emitter->Dispatch(cmdList);
    }

    cmdList->EndQuery(timestampHeapIndex + 1);
    cmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));


    return computeQueue->ExecuteCommandList(cmdList);
}

UINT64 HybridNoiseApp::RenderScene(const UINT timestampHeapIndex, const UINT computeCloudFenceValue)
{
    const auto cmdList = renderQueue->GetCommandList();

    cmdList->EndQuery(timestampHeapIndex);

    PopulateNormalMapCommands(cmdList);
    PopulateAmbientMapCommands(cmdList);
    PopulateShadowMapCommands(cmdList);


    if (UseSecondApproach)
    {
        if (UseCrossAdapter)
            cloudGeneratorV2->PrimeCopy(cmdList);
    }
    else
    {
        if (UseCrossAdapter)
        {
            if (secondComputeQueue->IsFinish(computeCloudFenceValue))
                cloudGenerator->PrimeCopy(cmdList);
        }
    }

    PopulateForwardPathCommands(cmdList);
    PopulateInitRenderTarget(cmdList, MainWindow->GetCurrentBackBuffer(),
                             &currentFrameResource->BackBufferRTVMemory, 0);
    PopulateDrawFullQuadTexture(cmdList, antiAliasingPrimePath->GetSRV(),
                                0, *defaultPrimePipelineResources.GetPSO(RenderMode::Quad));


    cmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
    cmdList->FlushResourceBarriers();
    cmdList->EndQuery(timestampHeapIndex + 1);
    cmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));

    if (UseSecondApproach)
    {
        renderQueue->Wait(primeComputeQueue);
        renderQueue->Wait(primeComputeFence, currentFrameResource->CloudComputeFenceValue);
    }
    else
        renderQueue->Wait(primeComputeQueue);

    return renderQueue->ExecuteCommandList(cmdList);
}

UINT64 HybridNoiseApp::ComputeClouds(const UINT timestampHeapIndex) const
{
    if (UseSecondApproach)
    {
        if (UseCrossAdapter)
        {
            const auto cmdList = secondComputeQueue->GetCommandList();

            cmdList->EndQuery(timestampHeapIndex);

            cloudGeneratorV2->SecondCompute(cmdList, deltaTimeCloud);

            cmdList->EndQuery(timestampHeapIndex + 1);
            cmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));

            const auto fenceValue = secondComputeQueue->ExecuteCommandList(cmdList);

            secondComputeQueue->Signal(secondComputeFence, fenceValue);
            return fenceValue;
        }
        const auto cmdList = primeComputeQueue->GetCommandList();
        cloudGeneratorV2->PrimeCompute(cmdList, deltaTimeCloud);

        const auto fenceValue = primeComputeQueue->ExecuteCommandList(cmdList);

        primeComputeQueue->Signal(primeComputeFence, fenceValue);

        return fenceValue;
    }
    if (UseCrossAdapter)
    {
        if (currentFrameResource->CloudComputeFenceValue == 0 || secondComputeQueue->IsFinish(
            currentFrameResource->CloudComputeFenceValue))
        {
            const auto cmdList = secondComputeQueue->GetCommandList();

            cmdList->EndQuery(timestampHeapIndex);

            cloudGenerator->SecondCompute(cmdList, deltaTimeCloud);

            cmdList->EndQuery(timestampHeapIndex + 1);
            cmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));

            return secondComputeQueue->ExecuteCommandList(cmdList);
        }
        return currentFrameResource->CloudComputeFenceValue;
    }
    const auto cmdList = primeComputeQueue->GetCommandList();

    cloudGenerator->PrimeCompute(cmdList, deltaTimeCloud);

    return primeComputeQueue->ExecuteCommandList(cmdList);
}

void HybridNoiseApp::Calibration()
{
    static constexpr UINT64 runs = 6;
    static constexpr float calibrationTickTime = 10.0f;

    IsCalibration = true;

    MainWindow->SetWindowTitle(L"Calibration ");

    int textureSize = 1024;

    constexpr int textureSizeStep = 64;

    timer.Stop();
    timer.Reset();
    timer.Start();

    cloudGeneratorV2->ChangeTextureSize(textureSize, textureSize);

    constexpr float epsilon = 5 / 100.0f;

    UINT64 emptyGpuWorkAvarageFPS;
    while (true)
    {
        iRotaster->GetTransform()->SetPosition(initialPosition);
        iRotaster->GetTransform()->SetEulerRotate(initialRotation);
        emptyGpuWorkAvarageFPS = GPUEmptyWorkFPS(runs, calibrationTickTime);
        Flush();

        iRotaster->GetTransform()->SetPosition(initialPosition);
        iRotaster->GetTransform()->SetEulerRotate(initialRotation);
        UINT64 cloudCalibrateAvarageFPS = CalibrateCloudTextureSizeWork(runs, calibrationTickTime);
        Flush();

        if (cloudCalibrateAvarageFPS + std::round((emptyGpuWorkAvarageFPS * epsilon)) > emptyGpuWorkAvarageFPS)
        {
            iRotaster->GetTransform()->SetPosition(initialPosition);
            iRotaster->GetTransform()->SetEulerRotate(initialRotation);
            emptyGpuWorkAvarageFPS = GPUEmptyWorkFPS(runs * 2, calibrationTickTime);
            Flush();

            iRotaster->GetTransform()->SetPosition(initialPosition);
            iRotaster->GetTransform()->SetEulerRotate(initialRotation);
            cloudCalibrateAvarageFPS = CalibrateCloudTextureSizeWork(runs * 2, calibrationTickTime);
            Flush();

            if (cloudCalibrateAvarageFPS + std::round((emptyGpuWorkAvarageFPS * epsilon)) > emptyGpuWorkAvarageFPS)
            {
                logQueue.Push(L"\n Cloud Texture Size for second approach: " + std::to_wstring(textureSize));
                break;
            }
        }
        else
        {
            textureSize -= textureSizeStep;

            if (textureSize < textureSizeStep)
            {
                logQueue.Push(L"\nCalibration error");
                IsStop = true;
                IsCalibration = false;
                return;
            }

            cloudGeneratorV2->ChangeTextureSize(textureSize, textureSize);
        }
    }

    UseCrossAdapter = false;
    UseSecondApproach = false;

    UINT particleCount = 15000;
    const UINT particlesPerTick = particleCount / 10;

    for (auto&& emitter : crossEmitter)
    {
        emitter->ChangeParticleCount(particleCount);
    }

    while (true)
    {
        iRotaster->GetTransform()->SetPosition(initialPosition);
        iRotaster->GetTransform()->SetEulerRotate(initialRotation);
        emptyGpuWorkAvarageFPS = GPUEmptyWorkFPS(runs, calibrationTickTime);
        Flush();

        iRotaster->GetTransform()->SetPosition(initialPosition);
        iRotaster->GetTransform()->SetEulerRotate(initialRotation);
        UINT64 particleComputeFPS = GPUParticleWorkFPS(runs, calibrationTickTime);
        Flush();

        if (particleComputeFPS + std::round((emptyGpuWorkAvarageFPS * epsilon)) >= emptyGpuWorkAvarageFPS)
        {
            iRotaster->GetTransform()->SetPosition(initialPosition);
            iRotaster->GetTransform()->SetEulerRotate(initialRotation);
            particleComputeFPS = GPUParticleWorkFPS(runs * 2, calibrationTickTime);
            Flush();

            iRotaster->GetTransform()->SetPosition(initialPosition);
            iRotaster->GetTransform()->SetEulerRotate(initialRotation);
            emptyGpuWorkAvarageFPS = GPUEmptyWorkFPS(runs * 2, calibrationTickTime);
            Flush();


            if (particleComputeFPS + std::round((emptyGpuWorkAvarageFPS * epsilon)) >= emptyGpuWorkAvarageFPS)
            {
                logQueue.Push(L"\n Particle count for second approach: " + std::to_wstring(particleCount));
                break;
            }
        }

        particleCount -= particlesPerTick;

        if (particleCount == 0)
        {
            logQueue.Push(L"\nCalibration error");
            IsStop = true;
            IsCalibration = false;
            return;
        }

        for (auto&& emitter : crossEmitter)
        {
            emitter->ChangeParticleCount(particleCount);
        }
    }

    for (auto&& emitter : crossEmitter)
    {
        emitter->ChangeParticleCount(particleCount);
    }
    iRotaster->GetTransform()->SetPosition(initialPosition);
    iRotaster->GetTransform()->SetEulerRotate(initialRotation);

    IsCalibration = false;

    frameCount = 0;
    timeElapsed = 0.0f;
}

void HybridNoiseApp::GPUEmptyWork()
{
    const UINT timestampHeapIndex = 2 * currentFrameResourceIndex;

    currentFrameResource->PrimeRenderFenceValue = RenderScene(timestampHeapIndex, 0);

    currentFrameResourceIndex = MainWindow->Present();
}

UINT64 HybridNoiseApp::GPUEmptyWorkFPS(const UINT64 runs, const float runTime)
{
    UseCrossAdapter = false;
    UseSecondApproach = false;

    UINT64 emptyWorkFps = 0;
    for (int i = 0; i < runs; ++i)
    {
        frameCount = 0;
        timeElapsed = 0.0f;
        timer.Stop();
        timer.Reset();
        timer.Start();

        float currentTime = 0.0f;
        while (currentTime <= runTime)
        {
            CalculateFrameStats();
            Update(timer);

            GPUEmptyWork();

            timer.Tick();

            currentTime += timer.DeltaTime();
        }

        emptyWorkFps += frameCount;
    }

    return emptyWorkFps / runs;
}

void HybridNoiseApp::CalibrateCloudWork()
{
    const UINT timestampHeapIndex = 2 * currentFrameResourceIndex;

    currentFrameResource->CloudComputeFenceValue = ComputeClouds(timestampHeapIndex);

    currentFrameResource->PrimeRenderFenceValue = RenderScene(timestampHeapIndex, 0);

    currentFrameResourceIndex = MainWindow->Present();
}

UINT64 HybridNoiseApp::CalibrateCloudTextureSizeWork(const UINT64 runs, const float runTime)
{
    UseCrossAdapter = true;
    UseSecondApproach = true;

    UINT64 emptyWorkFps = 0;
    for (int i = 0; i < runs; ++i)
    {
        frameCount = 0;
        timeElapsed = 0.0f;
        timer.Stop();
        timer.Reset();
        timer.Start();

        float currentTime = 0.0f;
        while (currentTime <= runTime)
        {
            CalculateFrameStats();
            Update(timer);

            CalibrateCloudWork();

            timer.Tick();

            currentTime += timer.DeltaTime();
        }

        emptyWorkFps += frameCount;
    }

    return emptyWorkFps / runs;
}

void HybridNoiseApp::GPUParticleWork()
{
    const UINT timestampHeapIndex = 2 * currentFrameResourceIndex;

    currentFrameResource->PrimeComputeParticleFenceValue = ComputeEmitters(timestampHeapIndex, primeComputeQueue);

    currentFrameResource->PrimeRenderFenceValue = RenderScene(timestampHeapIndex, 0);

    currentFrameResourceIndex = MainWindow->Present();
}

UINT64 HybridNoiseApp::GPUParticleWorkFPS(const UINT64 runs, const float runTime)
{
    UseCrossAdapter = false;
    UseSecondApproach = false;

    UINT64 emptyWorkFps = 0;
    for (int i = 0; i < runs; ++i)
    {
        frameCount = 0;
        timeElapsed = 0.0f;
        timer.Stop();
        timer.Reset();
        timer.Start();

        float currentTime = 0.0f;
        while (currentTime <= runTime)
        {
            CalculateFrameStats();
            Update(timer);

            GPUParticleWork();

            timer.Tick();

            currentTime += timer.DeltaTime();
        }

        emptyWorkFps += frameCount;
    }

    return emptyWorkFps / runs;
}


void HybridNoiseApp::Draw(const GameTimer& gt)
{
    if (isResizing) return;

    const UINT timestampHeapIndex = 2 * currentFrameResourceIndex;


    currentFrameResource->PrimeComputeParticleFenceValue = ComputeEmitters(timestampHeapIndex, primeComputeQueue);

    const UINT lastComputeFence = currentFrameResource->CloudComputeFenceValue;

    currentFrameResource->CloudComputeFenceValue = ComputeClouds(timestampHeapIndex);


    currentFrameResource->PrimeRenderFenceValue = RenderScene(timestampHeapIndex, lastComputeFence);
    currentFrameResourceIndex = MainWindow->Present();
}

bool HybridNoiseApp::Initialize()
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

    return true;
}

void HybridNoiseApp::InitDevices()
{
    auto allDevices = GDeviceFactory::GetAllDevices(true);

    const auto firstDevice = allDevices[0];
    const auto otherDevice = allDevices[1];

    if (!(firstDevice->GetName().find(L"NVIDIA") != std::wstring::npos))
    {
        if (otherDevice->GetName().find(L"NVIDIA") != std::wstring::npos)
        {
            primeDevice = otherDevice;
            secondDevice = firstDevice;
        }
    }
    else
    {
        primeDevice = firstDevice;
        secondDevice = otherDevice;
    }


    assets = std::make_shared<AssetsLoader>(primeDevice);


    for (int i = 0; i < static_cast<uint8_t>(RenderMode::Count); ++i)
    {
        typedRenderer.push_back(
            MemoryAllocator::CreateVector<std::shared_ptr<Renderer>>());
    }

    primeDevice->SharedFence(primeComputeFence, secondDevice, secondComputeFence, sharedComputeFenceValue);
    primeDevice->SharedFence(primeRenderFence, secondDevice, secondRenderFence, sharedRenderFenceValue);


    logQueue.Push(L"\nPrime Device: " + (primeDevice->GetName()));
    logQueue.Push(
        L"\t\n Cross Adapter Texture Support: " + std::to_wstring(
            primeDevice->IsCrossAdapterTextureSupported()));
    logQueue.Push(L"\nSecond Device: " + (secondDevice->GetName()));
    logQueue.Push(
        L"\t\n Cross Adapter Texture Support: " + std::to_wstring(
            secondDevice->IsCrossAdapterTextureSupported()));


    primeComputeQueue = primeDevice->GetCommandQueue(GQueueType::Compute);
    renderQueue = primeDevice->GetCommandQueue(GQueueType::Graphics);

    secondComputeQueue = secondDevice->GetCommandQueue(GQueueType::Compute);
}

void HybridNoiseApp::InitFrameResource()
{
    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<FrameResource>(primeDevice,
                                                                 primeDevice, 2,
                                                                 assets->GetMaterials().size()));
    }
    logQueue.Push(std::wstring(L"\nInit FrameResource "));
}

void HybridNoiseApp::InitRootSignature()
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


    logQueue.Push(std::wstring(L"\nInit RootSignature for " + primeDevice->GetName()));

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

    ssaoPrimeRootSignature->Initialize(primeDevice);
}

void HybridNoiseApp::InitPipeLineResource()
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
                                                 BackBufferFormat, DepthStencilFormat, ssaoPrimeRootSignature,
                                                 NormalMapFormat, AmbientMapFormat);

    ambientPrimePath->SetPipelineData(*defaultPrimePipelineResources.GetPSO(RenderMode::Ssao),
                                      *defaultPrimePipelineResources.GetPSO(RenderMode::SsaoBlur));


    logQueue.Push(std::wstring(L"\nInit PSO for " + primeDevice->GetName()));
}

void HybridNoiseApp::CreateMaterials()
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

    logQueue.Push(std::wstring(L"\nCreate Materials"));
}

void HybridNoiseApp::InitSRVMemoryAndMaterials()
{
    srvTexturesMemory =
        primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, assets->GetTextures().size());

    auto materials = assets->GetMaterials();

    for (int j = 0; j < materials.size(); ++j)
    {
        auto material = materials[j];

        material->InitMaterial(&srvTexturesMemory);
    }

    logQueue.Push(std::wstring(L"\nInit Views for " + primeDevice->GetName()));
    ambientPrimePath->BuildDescriptors();
}

void HybridNoiseApp::InitRenderPaths()
{
    auto commandQueue = primeDevice->GetCommandQueue(GQueueType::Graphics);
    auto cmdList = commandQueue->GetCommandList();

    ambientPrimePath = (std::make_shared<SSAO>(
        primeDevice,
        cmdList,
        MainWindow->GetClientWidth(), MainWindow->GetClientHeight()));

    antiAliasingPrimePath = (std::make_shared<SSAA>(primeDevice, 4, MainWindow->GetClientWidth(),
                                                    MainWindow->GetClientHeight()));
    antiAliasingPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

    commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));

    logQueue.Push(std::wstring(L"\nInit Render path data for " + primeDevice->GetName()));

    shadowPath = (std::make_shared<ShadowMap>(primeDevice, 2048, 2048));

    cloudGenerator = std::make_shared<CloudGenerator>(primeDevice, secondDevice, 4096, 4096);
    cloudGeneratorV2 = std::make_shared<CloudGenerator>(primeDevice, secondDevice, 512, 512);
}

void HybridNoiseApp::LoadStudyTexture()
{
    auto queue = primeDevice->GetCommandQueue(GQueueType::Copy);

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

    logQueue.Push(std::wstring(L"\nLoad DDS Texture"));
}

void HybridNoiseApp::LoadModels()
{
    auto queue = primeDevice->GetCommandQueue(GQueueType::Copy);
    const auto cmdList = queue->GetCommandList();

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
    queue->Flush();

    logQueue.Push(std::wstring(L"\nLoad Models Data"));
}

void HybridNoiseApp::MipMasGenerate()
{
    try
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
        logQueue.Push(std::wstring(L"\nMip Map Generation for " + primeDevice->GetName()));

        computeList = computeQueue->GetCommandList();
        for (auto&& texture : generatedMipTextures)
            computeList->TransitionBarrier(texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
        computeList->FlushResourceBarriers();
        logQueue.Push(std::wstring(L"\nTexture Barrier Generation for " + primeDevice->GetName()));
        computeQueue->WaitForFenceValue(computeQueue->ExecuteCommandList(computeList));

        logQueue.Push(std::wstring(L"\nMipMap Generation cmd list executing " + primeDevice->GetName()));
        for (auto&& pair : textures)
            pair->ClearTrack();
        logQueue.Push(std::wstring(L"\nFinish Mip Map Generation for " + primeDevice->GetName()));
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

void HybridNoiseApp::SortGO()
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

void HybridNoiseApp::CreateGO()
{
    logQueue.Push(std::wstring(L"\nStart Create GO"));
    auto skySphere = std::make_unique<GameObject>("Sky");
    skySphere->GetTransform()->SetScale({500, 500, 500});
    {
        const auto renderer = std::make_shared<SkyBox>(primeDevice,
                                                       models[L"sphere"],
                                                       *assets->GetTexture(
                                                           assets->
                                                           GetTextureIndex(L"skyTex")).get(),
                                                       &srvTexturesMemory,
                                                       assets->GetTextureIndex(L"skyTex"));

        skySphere->AddComponent(renderer);
        typedRenderer[(int)RenderMode::SkyBox].push_back((renderer));
    }
    gameObjects.push_back(std::move(skySphere));

    auto quadRitem = std::make_unique<GameObject>("Quad");
    {
        auto renderer = std::make_shared<ModelRenderer>(primeDevice,
                                                        models[L"quad"]);
        renderer->SetModel(models[L"quad"]);
        quadRitem->AddComponent(renderer);
        typedRenderer[(int)RenderMode::Debug].push_back(renderer);
        typedRenderer[(int)RenderMode::Quad].push_back(renderer);
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
        typedRenderer[(int)RenderMode::Opaque].push_back(renderer);
        gameObjects.push_back(std::move(nano));


        auto doom = std::make_unique<GameObject>();
        doom->SetScale(0.08);
        doom->GetTransform()->SetPosition(Vector3::Right * 15 + Vector3::Forward * 12 * i);
        doom->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
        renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"doom"]);
        doom->AddComponent(renderer);
        typedRenderer[(int)RenderMode::Opaque].push_back(renderer);
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
            typedRenderer[(int)RenderMode::Opaque].push_back(renderer);
            gameObjects.push_back(std::move(atlas));


            auto pbody = std::make_unique<GameObject>();
            pbody->GetTransform()->SetPosition(
                Vector3::Right * 130 + Vector3::Right * -30 * j + Vector3::Up * 11 + Vector3::Forward * 10 * i);
            renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"pbody"]);
            pbody->AddComponent(renderer);
            typedRenderer[(int)RenderMode::Opaque].push_back(renderer);
            gameObjects.push_back(std::move(pbody));
        }
    }

    auto particle = std::make_unique<GameObject>();
    particle->GetTransform()->SetPosition(Vector3::Up);
    const auto emitter = std::make_shared<ParticleEmitter>(primeDevice, 100000 * 0.06);
    particle->AddComponent(emitter);
    typedRenderer[(int)RenderMode::Particle].push_back(emitter);
    crossEmitter.push_back(emitter.get());
    gameObjects.push_back(std::move(particle));

    auto platform = std::make_unique<GameObject>();
    platform->SetScale(0.2);
    platform->GetTransform()->SetEulerRotate(Vector3(90, 90, 0));
    platform->GetTransform()->SetPosition(Vector3::Backward * -130);
    auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"platform"]);
    platform->AddComponent(renderer);
    typedRenderer[(int)RenderMode::Opaque].push_back(renderer);


    auto rotater = std::make_unique<GameObject>();
    rotater->GetTransform()->SetParent(platform->GetTransform().get());
    rotater->GetTransform()->SetPosition(Vector3::Forward * 325 + Vector3::Left * 625);
    rotater->GetTransform()->SetEulerRotate(Vector3(0, -90, 90));

    iRotaster = rotater.get();
    initialPosition = rotater->GetTransform()->GetLocalPosition();
    initialRotation = rotater->GetTransform()->GetEulerAngels();

    auto camera = std::make_unique<GameObject>("MainCamera");
    camera->GetTransform()->SetParent(rotater->GetTransform().get());
    camera->GetTransform()->SetEulerRotate(Vector3(-30, 270, 0));
    camera->GetTransform()->SetPosition(Vector3(-1000, 190, -32));
    camera->AddComponent(std::make_shared<Camera>(AspectRatio()));

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
    typedRenderer[(int)RenderMode::Opaque].push_back(renderer);


    auto columns = std::make_unique<GameObject>();
    columns->GetTransform()->SetParent(stair->GetTransform().get());
    columns->SetScale(0.8);
    columns->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
    columns->GetTransform()->SetPosition(Vector3::Up * 2000 + Vector3::Forward * 900);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"columns"]);
    columns->AddComponent(renderer);
    typedRenderer[(int)RenderMode::Opaque].push_back(renderer);

    auto fountain = std::make_unique<GameObject>();
    fountain->SetScale(0.005);
    fountain->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    fountain->GetTransform()->SetPosition(Vector3::Up * 35 + Vector3::Backward * 77);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"fountain"]);
    fountain->AddComponent(renderer);
    typedRenderer[(int)RenderMode::Opaque].push_back(renderer);

    gameObjects.push_back(std::move(platform));
    gameObjects.push_back(std::move(stair));
    gameObjects.push_back(std::move(columns));
    gameObjects.push_back(std::move(fountain));


    auto mountDragon = std::make_unique<GameObject>();
    mountDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    mountDragon->GetTransform()->SetPosition(Vector3::Right * -960 + Vector3::Up * 45 + Vector3::Backward * 775);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"mountDragon"]);
    mountDragon->AddComponent(renderer);
    typedRenderer[(int)RenderMode::Opaque].push_back(renderer);
    gameObjects.push_back(std::move(mountDragon));


    auto desertDragon = std::make_unique<GameObject>();
    desertDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    desertDragon->GetTransform()->SetPosition(Vector3::Right * 960 + Vector3::Up * -5 + Vector3::Backward * 775);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"desertDragon"]);
    desertDragon->AddComponent(renderer);
    typedRenderer[(int)RenderMode::Opaque].push_back(renderer);
    gameObjects.push_back(std::move(desertDragon));

    auto griffon = std::make_unique<GameObject>();
    griffon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    griffon->SetScale(0.8);
    griffon->GetTransform()->SetPosition(Vector3::Right * -355 + Vector3::Up * -7 + Vector3::Backward * 17);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"griffon"]);
    griffon->AddComponent(renderer);
    typedRenderer[(int)RenderMode::OpaqueAlphaDrop].push_back(renderer);
    gameObjects.push_back(std::move(griffon));

    griffon = std::make_unique<GameObject>();
    griffon->SetScale(0.8);
    griffon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    griffon->GetTransform()->SetPosition(Vector3::Right * 355 + Vector3::Up * -7 + Vector3::Backward * 17);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"griffon"]);
    griffon->AddComponent(renderer);
    typedRenderer[(int)RenderMode::OpaqueAlphaDrop].push_back(renderer);
    gameObjects.push_back(std::move(griffon));

    logQueue.Push(std::wstring(L"\nFinish create GO"));
}

void HybridNoiseApp::CalculateFrameStats()
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

    static UINT64 primeGPUComputingTimeMax = std::numeric_limits<UINT64>::min();
    static UINT64 primeGPUComputingTimeMin = std::numeric_limits<UINT64>::max();
    static UINT64 secondGPUComputingTimeMax = std::numeric_limits<UINT64>::min();
    static UINT64 secondGPUComputingTimeMin = std::numeric_limits<UINT64>::max();
    frameCount++;

    if ((timer.TotalTime() - timeElapsed) >= 1.0f)
    {
        const float fps = static_cast<float>(frameCount); // fps = frameCnt / 1
        const float mspf = 1000.0f / fps;

        minFps = std::min(fps, minFps);
        minMspf = std::min(mspf, minMspf);
        maxFps = std::max(fps, maxFps);
        maxMspf = std::max(mspf, maxMspf);

        primeGPUTimeMin = std::min(primeGPURenderingTime, primeGPUTimeMin);
        primeGPUTimeMax = std::max(primeGPURenderingTime, primeGPUTimeMax);
        secondGPUTimeMin = std::min(secondGPURenderingTime, secondGPUTimeMin);
        secondGPUTimeMax = std::max(secondGPURenderingTime, secondGPUTimeMax);

        primeGPUComputingTimeMin = std::min(primeGPUComputingTime, primeGPUComputingTimeMin);
        primeGPUComputingTimeMax = std::max(primeGPUComputingTime, primeGPUComputingTimeMax);
        secondGPUComputingTimeMin = std::min(secondGPUComputingTime, secondGPUComputingTimeMin);
        secondGPUComputingTimeMax = std::max(secondGPUComputingTime, secondGPUComputingTimeMax);


        frameCount = 0;
        timeElapsed += 1.0f;

        if (!IsCalibration)
        {
            const std::wstring title = L"FPS " + std::to_wstring(fps) + L" Step:" + (
                    UseSecondApproach ? (UseCrossAdapter ? L"4" : L"3") : (UseCrossAdapter ? L"2" : L"1")) + L"/4" +
                L" Progress: " + std::to_wstring(
                    (static_cast<float>(writeStaticticCount) / StatisticStepSecondsCount) * 100.0f) + L"/" +
                std::to_wstring(100);

            if (writeStaticticCount >= StatisticStepSecondsCount)
            {
                const std::wstring staticticStr =
                    L"\nUse Cross Adapter: " + std::to_wstring(UseCrossAdapter)
                    + L"\nUse Second Variant: " + std::to_wstring(UseSecondApproach)
                    + L"\n\tMin FPS:" + std::to_wstring(minFps)
                    + L"\n\tMin MSPF:" + std::to_wstring(minMspf)
                    + L"\n\tMax FPS:" + std::to_wstring(maxFps)
                    + L"\n\tMax MSPF:" + std::to_wstring(maxMspf)
                    + L"\n\tMax Prime GPU Rendering Time:" + std::to_wstring(primeGPUTimeMax) +
                    +L"\n\tMin Prime GPU Rendering Time:" + std::to_wstring(primeGPUTimeMin) +
                    +L"\n\tMax Second GPU Rendering Time:" + std::to_wstring(secondGPUTimeMax)
                    + L"\n\tMin Second GPU Rendering Time:" + std::to_wstring(secondGPUTimeMin)
                    + L"\n\tMax Prime GPU Computing Time:" + std::to_wstring(primeGPUComputingTimeMax) +
                    +L"\n\tMin Prime GPU Computing Time:" + std::to_wstring(primeGPUComputingTimeMin) +
                    +L"\n\tMax Second GPU Computing Time:" + std::to_wstring(secondGPUComputingTimeMax)
                    + L"\n\tMin Second GPU Computing Time:" + std::to_wstring(secondGPUComputingTimeMin);

                logQueue.Push(staticticStr);


                writeStaticticCount = 0;
                minFps = std::numeric_limits<float>::max();
                minMspf = std::numeric_limits<float>::max();
                maxFps = std::numeric_limits<float>::min();
                maxMspf = std::numeric_limits<float>::min();
                primeGPUTimeMax = std::numeric_limits<UINT64>::min();
                primeGPUTimeMin = std::numeric_limits<UINT64>::max();
                secondGPUTimeMax = std::numeric_limits<UINT64>::min();
                secondGPUTimeMin = std::numeric_limits<UINT64>::max();
                primeGPUComputingTimeMax = std::numeric_limits<UINT64>::min();
                primeGPUComputingTimeMin = std::numeric_limits<UINT64>::max();
                secondGPUComputingTimeMax = std::numeric_limits<UINT64>::min();
                secondGPUComputingTimeMin = std::numeric_limits<UINT64>::max();

                if (UseCrossAdapter == false)
                {
                    Flush();

                    if (UseSecondApproach == false)
                        secondComputeQueue->SignalWithNewFenceValue(primeComputeQueue->GetFenceValue());

                    UseCrossAdapter = true;

                    iRotaster->GetTransform()->SetPosition(initialPosition);
                    iRotaster->GetTransform()->SetEulerRotate(initialRotation);
                }
                else
                {
                    Flush();

                    iRotaster->GetTransform()->SetPosition(initialPosition);
                    iRotaster->GetTransform()->SetEulerRotate(initialRotation);


                    if (UseSecondApproach)
                        IsStop = true;
                    else
                    {
                        UseSecondApproach = true;
                        UseCrossAdapter = false;
                    }
                }
            }
            else
            {
                const std::wstring staticticStr =
                    L"\n\tFPS:" + std::to_wstring(fps)
                    + L"\n\tMSPF:" + std::to_wstring(mspf)
                    + L"\n\tPrime GPU Rendering Time:" + std::to_wstring(primeGPURenderingTime)
                    + L"\n\tSecond GPU Rendering Time:" + std::to_wstring(secondGPURenderingTime)
                    + L"\n\tPrime GPU Computing Time:" + std::to_wstring(primeGPUComputingTime)
                    + L"\n\tSecond GPU Computing Time:" + std::to_wstring(secondGPUComputingTime);

                logQueue.Push(staticticStr);

                writeStaticticCount++;
            }


            MainWindow->SetWindowTitle(title);
        }
        else
        {
            MainWindow->SetWindowTitle(L"Calibration " + std::to_wstring(timer.TotalTime()));
        }
    }
}

void HybridNoiseApp::LogWriting()
{
    const std::filesystem::path filePath(
        L"SharedCloudGeneratorV2 " + primeDevice->GetName() + L"+" + secondDevice->GetName() + L".txt");

    const auto path = std::filesystem::current_path().wstring() + L"\\" + filePath.wstring();

    OutputDebugStringW(path.c_str());

    std::wofstream fileSteam;
    fileSteam.open(path.c_str(), std::ios::out | std::ios::in | std::ios::binary | std::ios::trunc);
    if (fileSteam.is_open())
    {
        fileSteam << L"Information" << std::endl << L"Statistic step seconds:" << std::to_wstring(
            StatisticStepSecondsCount) << std::endl;
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


int HybridNoiseApp::Run()
{
    MSG msg = {nullptr};

    timer.Reset();

    Calibration();

    timer.Stop();
    timer.Reset();
    timer.Start();


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
                LogWriting();
                Quit();
                break;
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

            primeDevice->ResetAllocators(frameCount);
            secondDevice->ResetAllocators(frameCount);
        }
    }

    return static_cast<int>(msg.wParam);
}

void HybridNoiseApp::UpdateMaterials() const
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

void HybridNoiseApp::UpdateShadowTransform(const GameTimer& gt)
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

void HybridNoiseApp::UpdateShadowPassCB(const GameTimer& gt)
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

void HybridNoiseApp::UpdateMainPassCB(const GameTimer& gt)
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
    mainPassCB.CameraForwardVector = camera->gameObject->GetTransform()->GetForwardVector();
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

void HybridNoiseApp::UpdateSsaoCB(const GameTimer& gt) const
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

bool HybridNoiseApp::InitMainWindow()
{
    MainWindow = CreateRenderWindow(primeDevice, mainWindowCaption, 1920, 1080, false);

    logQueue.Push(std::wstring(L"\nInit Window"));
    return true;
}

void HybridNoiseApp::OnResize()
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

    if (ambientPrimePath != nullptr)
    {
        ambientPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
        ambientPrimePath->RebuildDescriptors();
    }

    if (antiAliasingPrimePath != nullptr)
    {
        antiAliasingPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    }

    currentFrameResourceIndex = MainWindow->GetCurrentBackBufferIndex();
}

void HybridNoiseApp::Flush()
{
    primeDevice->Flush();
    secondDevice->Flush();
}

LRESULT HybridNoiseApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
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
                const auto rawdata = std::make_unique<BYTE[]>(dataSize);
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
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnMouseMove(x, y);
            return 0;
        }
    case WM_LBUTTONDOWN:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnLeftPressed(x, y);
            return 0;
        }
    case WM_RBUTTONDOWN:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnRightPressed(x, y);
            return 0;
        }
    case WM_MBUTTONDOWN:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnMiddlePressed(x, y);
            return 0;
        }
    case WM_LBUTTONUP:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnLeftReleased(x, y);
            return 0;
        }
    case WM_RBUTTONUP:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnRightReleased(x, y);
            return 0;
        }
    case WM_MBUTTONUP:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnMiddleReleased(x, y);
            return 0;
        }
    case WM_MOUSEWHEEL:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
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
            const unsigned char keycode = static_cast<unsigned char>(wParam);
            keyboard.OnKeyReleased(keycode);


            return 0;
        }
    case WM_KEYDOWN:
        {
            {
                const unsigned char keycode = static_cast<unsigned char>(wParam);
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
            }
        }

    case WM_CHAR:
        {
            const unsigned char ch = static_cast<unsigned char>(wParam);
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
