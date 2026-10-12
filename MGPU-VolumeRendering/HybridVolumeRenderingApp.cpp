#include "HybridVolumeRenderingApp.h"

#include <array>
#include <chrono>
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

HybridVolumeRenderingApp::HybridVolumeRenderingApp(const HINSTANCE hInstance) : D3DApp(hInstance), debugLogger(FileQueueWriter(std::filesystem::current_path() / "log.txt"))
{
    mSceneBounds.Center = Vector3(0.0f, 0.0f, 0.0f);
    mSceneBounds.Radius = 200;
}

HybridVolumeRenderingApp::~HybridVolumeRenderingApp() = default;

void HybridVolumeRenderingApp::SwitchDevice()
{
    Flush();
    IsUsingSharedSSAO = !IsUsingSharedSSAO;
}

void HybridVolumeRenderingApp::ChangeAOMethod()
{
    Flush();
    IsUseHBAO = !IsUseHBAO;
}

void HybridVolumeRenderingApp::SwitchFogMode()
{
    Flush();
    IsUsingSharedFog = !IsUsingSharedFog;
}

void HybridVolumeRenderingApp::ResetCamera() const
{
    auto& CamTrans = camera->gameObject->GetTransform();
    if (auto* Rotater = CamTrans->GetParent())  
    {
        Rotater->SetLocalMatrix(RotaterSaveMatrix);
    }
    CamTrans->SetLocalMatrix(CameraSaveMatrix);
}

void HybridVolumeRenderingApp::Update(const GameTimer& gt)
{
    const UINT olderIndex =
        (currentFrameResourceIndex + globalCountFrameResources - 1)
        % globalCountFrameResources;

    const auto primeQueue = primeDevice->GetCommandQueue(GQueueType::Graphics);
    const auto secondQueue = secondDevice->GetCommandQueue(GQueueType::Graphics);

    currentFrameResource = frameResources[currentFrameResourceIndex];

    const auto primeWaitStart = std::chrono::high_resolution_clock::now();
    if (currentFrameResource->PrimeRenderFenceValue != 0 && !primeQueue->IsFinish(
        currentFrameResource->PrimeRenderFenceValue))
    {
        primeQueue->WaitForFenceValue(currentFrameResource->PrimeRenderFenceValue);
    }
    else
    {
        primeDevice->ReleaseSlateDescriptors(currentFrameResource->PrimeRenderFenceValue);
    }
    const auto primeWaitEnd = std::chrono::high_resolution_clock::now();
    debugPrimeWaitMs = std::chrono::duration<float, std::milli>(primeWaitEnd - primeWaitStart).count();

    const auto secondWaitStart = std::chrono::high_resolution_clock::now();
    if (currentFrameResource->SecondRenderFenceValue != 0 && !secondQueue->IsFinish(
        currentFrameResource->SecondRenderFenceValue))
    {
        secondQueue->WaitForFenceValue(currentFrameResource->SecondRenderFenceValue);
    }
    const auto secondWaitEnd = std::chrono::high_resolution_clock::now();
    debugSecondWaitMs = std::chrono::duration<float, std::milli>(secondWaitEnd - secondWaitStart).count();

    const UINT64 completedSecondFence = secondQueue->GetFence()->GetCompletedValue();

    if (completedSecondFence != 0)
    {
        secondDevice->ReleaseSlateDescriptors(completedSecondFence);
    }

    for (UINT offset = 0; offset < globalCountFrameResources; ++offset)
    {
        const UINT frameIndex =
            (olderIndex + globalCountFrameResources - offset)
            % globalCountFrameResources;
        auto& debugFrame = debugPrimaryFrames[frameIndex];

        if (debugFrame.FenceValue != 0 && !debugFrame.Counted && primeQueue->IsFinish(debugFrame.FenceValue))
        {
            primeGPUFrameTimeMs = static_cast<float>(primeQueue->GetTimestamp(frameIndex)) / 1000.0f;
            minPrimeGPUFrameTimeMs = minPrimeGPUFrameTimeMs == 0.0f
                                         ? primeGPUFrameTimeMs
                                         : std::min(minPrimeGPUFrameTimeMs, primeGPUFrameTimeMs);
            maxPrimeGPUFrameTimeMs = std::max(maxPrimeGPUFrameTimeMs, primeGPUFrameTimeMs);

            debugFrame.Counted = true;
            debugPrimaryCompletedThisSecond++;
            debugLatestCompletedPrimaryFrameId = std::max(debugLatestCompletedPrimaryFrameId, debugFrame.FrameId);
        }
    }

    for (UINT offset = 0; offset < globalCountFrameResources; ++offset)
    {
        const UINT frameIndex =
            (olderIndex + globalCountFrameResources - offset)
            % globalCountFrameResources;
        auto& debugFrame = debugSecondFrames[frameIndex];

        if (debugFrame.FenceValue != 0 && !debugFrame.Counted && secondQueue->IsFinish(debugFrame.FenceValue))
        {
            secondGPUFrameTimeMs = static_cast<float>(secondQueue->GetTimestamp(frameIndex)) / 1000.0f;
            debugFrame.Counted = true;
        }
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
    UpdateFogCB(gt);
    UpdateVolumeCB(gt);
    UIPath->Update();
    benchmark.Tick(gt.DeltaTime());

    fpsTimeAccumulator += gt.DeltaTime();
    fpsFrameCounter++;
    debugCPUFrameId++;

    if (fpsTimeAccumulator >= 1.0f)
    {
        const float cpuFps = static_cast<float>(fpsFrameCounter) / fpsTimeAccumulator;
        const float presentPerSecond = static_cast<float>(debugPresentsThisSecond) / fpsTimeAccumulator;
        const float primaryCompletedPerSecond = static_cast<float>(debugPrimaryCompletedThisSecond) / fpsTimeAccumulator;

        wchar_t title[512];
        swprintf_s(title,
                   L"MGPU Volume Rendering | CPU: %.1f FPS | Present/s: %.1f | PrimaryDone/s: %.1f | GPU0: %.2f ms | GPU1: %.2f ms | PWait: %.2f | SWait: %.2f | Present: %.2f",
                   cpuFps, presentPerSecond, primaryCompletedPerSecond, primeGPUFrameTimeMs, secondGPUFrameTimeMs,
                   debugPrimeWaitMs, debugSecondWaitMs, debugPresentMs);

        if (const HWND hwnd = MainWindow->GetWindowHandle())
        {
            SetWindowTextW(hwnd, title);
        }

        wchar_t debugLine[256];
        swprintf_s(debugLine,
                   L"[MGPU Volume Diagnostics] CPUFrameId=%llu PresentedFrameId=%llu LatestCompletedPrimaryFrameId=%llu\n",
                   debugCPUFrameId, debugPresentCount, debugLatestCompletedPrimaryFrameId);
        OutputDebugStringW(debugLine);

        fpsTimeAccumulator = 0.0f;
        fpsFrameCounter = 0;
        debugPresentsThisSecond = 0;
        debugPrimaryCompletedThisSecond = 0;
    }
}

void HybridVolumeRenderingApp::PopulateShadowMapCommands(const std::shared_ptr<GCommandList>& cmdList)
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

void HybridVolumeRenderingApp::PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList)
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

void HybridVolumeRenderingApp::PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList) const
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

void HybridVolumeRenderingApp::PopulateFogMapCommands(const std::shared_ptr<GCommandList>& cmdList) const
{
    if (!IsUseFog)
    {
        return;
    }

    const auto& resources = fogPass->GetPrimeResources();

    GTexture depthSource;
    if (IsUseHBAO)
    {
        depthSource = hbaoPass->GetPrimeResources().GetDepthMap();
    }
    else
    {
        depthSource = ssaoPass->GetPrimeResources().GetDepthMap();
    }

    if (!IsUsingSharedFog)
    {
        cmdList->CopyResource(resources.GetDepthMap(), depthSource);
        fogPass->Compute(cmdList, currentFrameResource->PrimeFogConstantUploadBuffer, resources);
        return;
    }

    const auto& crossResources = fogPass->GetCrossResources();

    cmdList->CopyResource(crossResources.GetDepthMap().GetPrimeResource(), depthSource);
    cmdList->CopyResource(resources.GetFogMap(), crossResources.GetFogMap().GetPrimeResource());

    auto secondQueue = secondDevice->GetCommandQueue();
    if (currentFrameResource->SecondFogFenceValue == 0 || secondQueue->IsFinish(currentFrameResource->SecondFogFenceValue))
    {
        const auto& secondResources = fogPass->GetSecondResources();
        const auto secondCmdList = secondQueue->GetCommandList();

        secondCmdList->CopyResource(secondResources.GetDepthMap(), crossResources.GetDepthMap().GetSharedResource());
        fogPass->Compute(secondCmdList, currentFrameResource->SecondFogConstantUploadBuffer, secondResources);
        secondCmdList->CopyResource(crossResources.GetFogMap().GetSharedResource(), secondResources.GetFogMap());

        currentFrameResource->SecondFogFenceValue = secondQueue->ExecuteCommandList(secondCmdList);
    }
}

void HybridVolumeRenderingApp::PopulateVolumeMapCommands(
    const std::shared_ptr<GCommandList>& cmdList)
{
    if (!IsUsingSharedVolume)
    {
        return;
    }
    
    // source depth from normal/depth prepass
    GTexture depthSource;

    if (IsUseHBAO)
    {
        depthSource =
            hbaoPass
            ->GetPrimeResources()
            .GetDepthMap();
    }
    else
    {
        depthSource =
            ssaoPass
            ->GetPrimeResources()
            .GetDepthMap();
    }


    const auto& primeResources =
        volumePass->GetPrimeResources();

    const auto& secondResources =
        volumePass->GetSecondResources();

    const auto& crossResources =
        volumePass->GetCrossResources();


    const auto primeQueue =
        primeDevice->GetCommandQueue();

    const auto secondQueue =
        secondDevice->GetCommandQueue();

    const UINT timestampHeapIndex = 2 * currentFrameResourceIndex;
    
    // release slots whose readback on GPU0 has finished
    for (auto& slot : volumeSlots)
    {
        if (
            slot.State == VolumeSlotState::ReadbackPending
            &&
            primeQueue->IsFinish(slot.PrimeFence))
        {
            slot.State =
                VolumeSlotState::Free;
        }
    }


    // check completed GPU2 jobs
    for (auto& slot : volumeSlots)
    {
        if (slot.State == VolumeSlotState::GPU2Working
            &&
            secondQueue->IsFinish(slot.SecondFence))
        {
            slot.State = VolumeSlotState::ResultReady;
        }
    }


    // ========================================================
    // GPU0
    //
    // Read latest completed GPU2 result.
    //
    // NO WAIT.
    // If GPU2 has no completed result, prime VolumeMap simply
    // keeps the previously received frame.
    // ========================================================

    int latestReadySlot = -1;

    UINT64 latestReadyFrameId = latestPresentedVolumeFrameId;


    for (UINT i = 0; i < volumeSlots.size(); ++i)
    {
        const auto& slot = volumeSlots[i];

        if (slot.State == VolumeSlotState::ResultReady
            &&
            slot.FrameId > latestReadyFrameId)
        {
            latestReadyFrameId = slot.FrameId;
            latestReadySlot = static_cast<int>(i);
        }
    }


    if (latestReadySlot >= 0)
    {
        // discard completed but older results
        for (UINT i = 0; i < volumeSlots.size(); ++i)
        {
            auto& slot = volumeSlots[i];

            if (slot.State == VolumeSlotState::ResultReady
                &&
                static_cast<int>(i) != latestReadySlot)
            {
                slot.State = VolumeSlotState::Free;
            }
        }
        
        auto& slot = volumeSlots[latestReadySlot];
        
        cmdList->CopyResource(primeResources.GetVolumeMap(),
            crossResources.GetVolumeMap(latestReadySlot).GetPrimeResource());

        slot.State = VolumeSlotState::ReadbackPending;

        volumePendingReadbackSlot = latestReadySlot;


        latestPresentedVolumeFrameId = slot.FrameId;
    }
    else
    {
        // Any result older than the one already displayed
        // is no longer useful.
        for (auto& slot : volumeSlots)
        {
            if (slot.State == VolumeSlotState::ResultReady
                &&
                slot.FrameId <= latestPresentedVolumeFrameId)
            {
                slot.State = VolumeSlotState::Free;
            }
        }
    }


    // ========================================================
    // GPU1
    //
    // Start rendering a depth slot only when GPU0 has finished
    // writing that depth.
    //
    // Again: no waiting.
    // ========================================================

    for (UINT i = 0; i < volumeSlots.size(); ++i)
    {
        auto& slot = volumeSlots[i];

        if (slot.State != VolumeSlotState::DepthPending
            ||
            !primeQueue->IsFinish(slot.PrimeFence))
        {
            continue;
        }


        const auto secondCmdList =
            secondQueue->GetCommandList();

        secondCmdList->EndQuery(timestampHeapIndex);

        // cross depth -> local GPU2 depth
        secondCmdList->CopyResource(secondResources.GetDepthMap(),
            crossResources.GetDepthMap(i).GetSharedResource());

        // render actual volume on GPU2
        volumePass->Render(
            secondCmdList,
            slot.ObjectConstantUploadBuffer,
            slot.PassConstantUploadBuffer,
            slot.VolumeConstantUploadBuffer,
            secondResources);

        // GPU2 VolumeMap -> cross adapter
        secondCmdList->CopyResource(crossResources.GetVolumeMap(i).GetSharedResource(), secondResources.GetVolumeMap());

        secondCmdList->EndQuery(timestampHeapIndex + 1);
        secondCmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));
        
        slot.SecondFence = secondQueue->ExecuteCommandList(secondCmdList);
        currentFrameResource->SecondVolumeFenceValue = slot.SecondFence;
        debugSecondFrames[currentFrameResourceIndex] = {slot.SecondFence, false};
        slot.State = VolumeSlotState::GPU2Working;
        
        break;
    }


    // ========================================================
    // GPU0
    //
    // Send current frame depth into any free cross-adapter slot.
    //
    // If both slots are busy, do nothing.
    // GPU0 NEVER waits for GPU2.
    // ========================================================

    for (UINT attempt = 0; attempt < volumeSlots.size(); ++attempt)
    {
        const UINT i = (volumeWriteSlot + attempt) % volumeSlots.size();
        auto& slot = volumeSlots[i];


        if (slot.State != VolumeSlotState::Free)
        {
            continue;
        }

        slot.PassConstantUploadBuffer->CopyData(0, currentVolumePassConstants);

        slot.VolumeConstantUploadBuffer->CopyData(0, currentVolumeConstants);

        slot.ObjectConstantUploadBuffer->CopyData(0, currentVolumeObjectConstants);

        slot.FrameId = volumeFrameId++;

        cmdList->CopyResource(
            crossResources
            .GetDepthMap(i)
            .GetPrimeResource(),

            depthSource);


        slot.State = VolumeSlotState::DepthPending;
        volumePendingDepthSlot = static_cast<int>(i);
        volumeWriteSlot = static_cast<int>((i + 1) % volumeSlots.size());
        break;
    }
}

void HybridVolumeRenderingApp::PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList)
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

        if (IsUsingSharedVolume)
        {
        // VolumeMap proccessed on GPU2 in PopulateVolumeMapCommands() and returned to GPU0.
        // blending /w forward render target.

        volumePass->Composite(
            cmdList,
            antiAliasingPrimePath->GetRenderTarget(),
            antiAliasingPrimePath->GetRTV());
                
        cmdList->SetGraphicsRootSignature(*primeDeviceSignature);

        cmdList->SetDescriptorsHeap(&srvTexturesMemory);

        cmdList->SetViewports(&antiAliasingPrimePath->GetViewPort(),
            1);

        cmdList->SetScissorRects(&antiAliasingPrimePath->GetRect(),
            1);

        cmdList->SetRenderTargets(1,
            antiAliasingPrimePath->GetRTV(),
            0,
            antiAliasingPrimePath->GetDSV());


        // Material data.
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
            *currentFrameResource->MaterialBuffer);

        // Material textures.
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap,
            &srvTexturesMemory);

        // Camera/pass data.
        cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
            *currentFrameResource->PrimePassConstantUploadBuffer);

        // Shadow map.
        cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap,
            shadowPath->GetSrv());

        // AO map.
        if (IsUseHBAO)
        {
            cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap,
                hbaoPass->GetPrimeResources().GetAmbientMapSRV());
        }
        else
        {
            cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap,
                ssaoPass->GetPrimeResources().GetAmbientMapSRV());
        }
        }
        else
        {
        // SINGLE GPU
        const SSAOResources* volumeDepthResources = nullptr;
        if (IsUseHBAO)
        {
            volumeDepthResources =
                &hbaoPass->GetPrimeResources();
        }
        else
        {
            volumeDepthResources =
                &ssaoPass->GetPrimeResources();
        }

        cmdList->TransitionBarrier(volumeDepthResources->GetDepthMap(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        cmdList->FlushResourceBarriers();
        cmdList->SetRootConstantBufferView(StandardShaderSlot::VolumeData, *currentFrameResource->PrimeVolumeConstantUploadBuffer);
        cmdList->SetRootDescriptorTable( StandardShaderSlot::VolumeDepthMap, volumeDepthResources->GetDepthMapSRV());
        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Volume));
        
        PopulateDrawCommands(cmdList, RenderMode::Volume);
        }
        
        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Transparent));
        PopulateDrawCommands(cmdList, (RenderMode::Transparent));

        cmdList->
            SetGraphicsRootConstantBufferView(StandardShaderSlot::CameraData,
                                              *currentFrameResource->PrimePassConstantUploadBuffer);
        //PopulateDrawCommands(cmdList, (RenderMode::Particle));


        cmdList->TransitionBarrier(antiAliasingPrimePath->GetRenderTarget(),
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier((antiAliasingPrimePath->GetDepthMap()), D3D12_RESOURCE_STATE_DEPTH_READ);
        cmdList->FlushResourceBarriers();
    }
}

void HybridVolumeRenderingApp::PopulateDrawCommands(const std::shared_ptr<GCommandList>& cmdList,
                                         RenderMode type) const
{
    for (auto&& renderer : typedRenderer[static_cast<int>(type)])
    {
        renderer->Draw(cmdList);
    }
}

void HybridVolumeRenderingApp::PopulateInitRenderTarget(const std::shared_ptr<GCommandList>& cmdList, const GTexture& renderTarget,
                                             const GDescriptor* rtvMemory, const UINT offsetRTV) const
{
    cmdList->SetViewports(&fullViewport, 1);
    cmdList->SetScissorRects(&fullRect, 1);

    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();
    cmdList->ClearRenderTarget(rtvMemory, offsetRTV, Colors::Black);

    cmdList->SetRenderTargets(1, rtvMemory, offsetRTV);
}

void HybridVolumeRenderingApp::PopulateDrawFullQuadTexture(const std::shared_ptr<GCommandList>& cmdList,
                                                const GDescriptor* renderTextureSRVMemory, const UINT renderTextureMemoryOffset,
                                                const GraphicPSO& pso) const
{
    cmdList->SetPipelineState(pso);

    cmdList->SetDescriptorsHeap(renderTextureSRVMemory);
    cmdList->SetGraphicsRootDescriptorTable(StandardShaderSlot::AmbientMap, renderTextureSRVMemory, renderTextureMemoryOffset);

    PopulateDrawCommands(cmdList, (RenderMode::Quad));
}

void HybridVolumeRenderingApp::PopulateDebugCommands(const std::shared_ptr<GCommandList>& cmdList)
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

void HybridVolumeRenderingApp::Draw(const GameTimer& gt)
{
    if (isResizing) return;

    const UINT timestampHeapIndex = 2 * currentFrameResourceIndex;
    auto primeRenderQueue = primeDevice->GetCommandQueue();
    auto primeCmdList = primeRenderQueue->GetCommandList();
    primeCmdList->EndQuery(timestampHeapIndex);

    for (auto emitter : emitters)
    {
        emitter->Dispatch(primeCmdList);
    }

    PopulateNormalMapCommands(primeCmdList);
    PopulateAmbientMapCommands(primeCmdList);
    PopulateFogMapCommands(primeCmdList);
    PopulateVolumeMapCommands(primeCmdList);
    PopulateShadowMapCommands(primeCmdList);
    PopulateForwardPathCommands(primeCmdList);
    PopulateInitRenderTarget(primeCmdList, MainWindow->GetCurrentBackBuffer(),
                             &currentFrameResource->BackBufferRTVMemory, 0);

    if (IsUseFog)
    {
        fogPass->Composite(primeCmdList,
                           currentFrameResource->PrimeFogConstantUploadBuffer,
                           antiAliasingPrimePath->GetRenderTarget(),
                           MainWindow->GetCurrentBackBuffer(),
                           &currentFrameResource->BackBufferRTVMemory,
                           0);
    }
    else
    {
        PopulateDrawFullQuadTexture(primeCmdList, antiAliasingPrimePath->GetSRV(),
                                    0, *defaultPrimePipelineResources.GetPSO(RenderMode::Quad));
    }

    PopulateDebugCommands(primeCmdList);

    UIPath->Render(primeCmdList);

    primeCmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
    primeCmdList->FlushResourceBarriers();
    primeCmdList->EndQuery(timestampHeapIndex + 1);
    primeCmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));
    currentFrameResource->PrimeRenderFenceValue = primeRenderQueue->ExecuteCommandList(primeCmdList);

    const UINT64 primeFence =
    currentFrameResource->PrimeRenderFenceValue;
    const UINT64 primaryFrameId = ++debugSubmittedPrimaryFrameId;
    debugPrimaryFrames[currentFrameResourceIndex] = {primaryFrameId, primeFence, false};


    if (volumePendingDepthSlot >= 0)
    {
        volumeSlots[volumePendingDepthSlot].PrimeFence = primeFence;
        volumePendingDepthSlot = -1;
    }


    if (volumePendingReadbackSlot >= 0)
    {
        volumeSlots[volumePendingReadbackSlot].PrimeFence = primeFence;
        volumePendingReadbackSlot = -1;
    }
    
    const auto presentStart = std::chrono::high_resolution_clock::now();
    currentFrameResourceIndex = MainWindow->Present();
    const auto presentEnd = std::chrono::high_resolution_clock::now();
    debugPresentMs = std::chrono::duration<float, std::milli>(presentEnd - presentStart).count();
    debugPresentCount++;
    debugPresentsThisSecond++;
}

bool HybridVolumeRenderingApp::Initialize()
{
    InitDevices();
    InitMainWindow();

    LoadStudyTexture();
    LoadModels();
    CreateMaterials();
    MipMasGenerate();

    InitInputLayout();
    InitRenderPaths();
    InitVolumeAsyncSlots();
    InitSRVMemoryAndMaterials();
    InitRootSignature();
    InitPipeLineResource();
    CreateGO();
    SortGO();
    InitFrameResource();

    OnResize();

    Flush();

    int TestTime = 10;
    int WarmupTime = 3;

#if !defined(DEBUG) && !defined(_DEBUG)
    TestTime = 120;
    WarmupTime = 10;
#endif

    const auto resetVolumeSlots = [this]()
    {
        for (auto& slot : volumeSlots)
        {
            slot.State = VolumeSlotState::Free;
            slot.PrimeFence = 0;
            slot.SecondFence = 0;
            slot.FrameId = 0;
        }

        volumeFrameId = 1;
        latestPresentedVolumeFrameId = 0;
        volumeWriteSlot = 0;
        volumePendingDepthSlot = -1;
        volumePendingReadbackSlot = -1;
    };

    const auto prepareVolumeBenchmarkState = [this, resetVolumeSlots](bool useSharedVolume)
    {
        ResetCamera();

        const bool needFlush =
            IsUsingSharedSSAO ||
            IsUseHBAO ||
            IsUseFog ||
            IsUsingSharedVolume != useSharedVolume;

        IsUsingSharedSSAO = false;
        IsUseHBAO = false;
        IsUseFog = false;
        IsUsingSharedFog = false;
        IsUsingSharedVolume = useSharedVolume;

        if (needFlush)
        {
            Flush();
            resetVolumeSlots();
        }
    };

    const auto resetMeasuredGPUStats = [this]()
    {
        primeGPUFrameTimeMs = 0.0f;
        secondGPUFrameTimeMs = 0.0f;
        minPrimeGPUFrameTimeMs = 0.0f;
        maxPrimeGPUFrameTimeMs = 0.0f;

        for (auto& frame : debugPrimaryFrames)
        {
            frame.Counted = true;
        }

        for (auto& frame : debugSecondFrames)
        {
            frame.Counted = true;
        }
    };

    const auto prepareMeasuredState = [prepareVolumeBenchmarkState, resetMeasuredGPUStats](FileQueueWriter& logs, bool useSharedVolume)
    {
        prepareVolumeBenchmarkState(useSharedVolume);
        resetMeasuredGPUStats();
        logs.PushMessage(L"CPU_FPS;CPU_MSPF;MinCPU_FPS;MinCPU_MSPF;MaxCPU_FPS;MaxCPU_MSPF;PrimeGPUms;SecondGPUms;MinPrimeGPUms;MaxPrimeGPUms");
    };

    const auto updateBenchmarkState = [this](FileQueueWriter& logs, const TimeStats& ts, float progress,
                                             const wchar_t* stateName)
    {
        logs.PushMessage(std::format(L"{:.2f};{:.2f};{:.2f};{:.2f};{:.2f};{:.2f};{:.2f};{:.2f};{:.2f};{:.2f}",
                                     ts.fps, ts.mspf, ts.minFps, ts.minMspf, ts.maxFps, ts.maxMspf,
                                     primeGPUFrameTimeMs, secondGPUFrameTimeMs,
                                     minPrimeGPUFrameTimeMs, maxPrimeGPUFrameTimeMs));

        MainWindow->SetWindowTitle(
            std::wstring(stateName) +
            L" Progress " + std::format(L"{:.2f}", progress * 100.0f) +
            L"% CPU FPS:" + std::to_wstring(ts.fps) +
            L" GPU0 ms:" + std::format(L"{:.2f}", primeGPUFrameTimeMs));
    };

    const auto updateWarmupState = [this](FileQueueWriter&, const TimeStats& ts, float progress,
                                          const wchar_t* stateName)
    {
        MainWindow->SetWindowTitle(
            std::wstring(stateName) +
            L" Progress " + std::format(L"{:.2f}", progress * 100.0f) +
            L"% FPS:" + std::to_wstring(ts.fps));
    };

    const auto finishBenchmarkState = [this](FileQueueWriter& logs, bool stopAfterState = false)
    {
        logs.WriteAllLog();
        Flush();

        if (stopAfterState)
        {
            IsStop = true;
        }
    };


    // native Volume warmup
    auto& NativeVolumeWarmupState = benchmark.AddState<WaitState>(
        WarmupTime,
        FileQueueWriter(Benchmark::GetLogFile(L"Warmup Native Volume ", *primeDevice, *secondDevice)));

    NativeVolumeWarmupState.OnEnter = [prepareVolumeBenchmarkState](FileQueueWriter&)
    {
        prepareVolumeBenchmarkState(false);
    };

    NativeVolumeWarmupState.OnStatChanged =
        [updateWarmupState](FileQueueWriter& logs, const TimeStats& ts, float progress)
    {
        updateWarmupState(logs, ts, progress, L"Warmup Native Volume");
    };

    NativeVolumeWarmupState.OnExit = [](FileQueueWriter&)
    {
    };


    // native Volume
    auto& NativeVolumeState = benchmark.AddState<WaitState>(
        TestTime,
        FileQueueWriter(Benchmark::GetLogFile(L"Native Volume ", *primeDevice, *secondDevice)));

    NativeVolumeState.OnEnter = [prepareMeasuredState](FileQueueWriter& logs)
    {
        prepareMeasuredState(logs, false);
    };

    NativeVolumeState.OnStatChanged =
        [updateBenchmarkState](FileQueueWriter& logs, const TimeStats& ts, float progress)
    {
        updateBenchmarkState(logs, ts, progress, L"Native Volume");
    };

    NativeVolumeState.OnExit = [finishBenchmarkState](FileQueueWriter& logs)
    {
        finishBenchmarkState(logs);
    };


    // hybrid Volume warmup

    auto& HybridVolumeWarmupState = benchmark.AddState<WaitState>(
        WarmupTime,
        FileQueueWriter(Benchmark::GetLogFile(L"Warmup Hybrid Volume ", *primeDevice, *secondDevice)));

    HybridVolumeWarmupState.OnEnter = [prepareVolumeBenchmarkState](FileQueueWriter&)
    {
        prepareVolumeBenchmarkState(true);
    };

    HybridVolumeWarmupState.OnStatChanged =
        [updateWarmupState](FileQueueWriter& logs, const TimeStats& ts, float progress)
    {
        updateWarmupState(logs, ts, progress, L"Warmup Hybrid Volume");
    };

    HybridVolumeWarmupState.OnExit = [](FileQueueWriter&)
    {
    };


    // hybrid Volume

    auto& HybridVolumeState = benchmark.AddState<WaitState>(
        TestTime,
        FileQueueWriter(Benchmark::GetLogFile(L"Hybrid Volume ", *primeDevice, *secondDevice)));

    HybridVolumeState.OnEnter = [prepareMeasuredState](FileQueueWriter& logs)
    {
        prepareMeasuredState(logs, true);
    };

    HybridVolumeState.OnStatChanged =
        [updateBenchmarkState](FileQueueWriter& logs, const TimeStats& ts, float progress)
    {
        updateBenchmarkState(logs, ts, progress, L"Hybrid Volume");
    };

    HybridVolumeState.OnExit = [finishBenchmarkState](FileQueueWriter& logs)
    {
        finishBenchmarkState(logs, true);
    };


#if !defined(DEBUG) && !defined(_DEBUG)
    benchmark.Start();
#endif
    return true;
}

void HybridVolumeRenderingApp::InitDevices()
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

void HybridVolumeRenderingApp::InitFrameResource()
{
    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        frameResources.emplace_back(std::make_unique<FrameResource>(primeDevice, secondDevice, 2, assets->GetMaterials().size()));
    }
    debugLogger.PushMessage(std::wstring(L"\nInit FrameResource "));
}

void HybridVolumeRenderingApp::InitRootSignature()
{
    auto rootSignature = std::make_shared<GRootSignature>();
    CD3DX12_DESCRIPTOR_RANGE texParam[4];
    texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0); //SkyMap
    texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0); //ShadowMap
    texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::AmbientMap - 3, 0); //SsaoMap
    texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                     assets->GetLoadTexturesCount() > 0 ? assets->GetLoadTexturesCount() : 1,
                     StandardShaderSlot::TexturesMap - 3, 0);

    CD3DX12_DESCRIPTOR_RANGE volumeDepthParam;
    volumeDepthParam.Init(
    D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
    1,
    0,
    2       // space2
    );

    rootSignature->AddConstantBufferParameter(0);
    rootSignature->AddConstantBufferParameter(1);
    rootSignature->AddShaderResourceView(0, 1);
    rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddConstantBufferParameter(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&volumeDepthParam, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->Initialize(primeDevice);

    primeDeviceSignature = rootSignature;

    debugLogger.PushMessage(std::wstring(L"\nInit RootSignature for " + primeDevice->GetName()));
}

void HybridVolumeRenderingApp::InitInputLayout()
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
}

void HybridVolumeRenderingApp::InitPipeLineResource()
{
    const D3D12_INPUT_LAYOUT_DESC desc = {defaultInputLayout.data(), defaultInputLayout.size()};

    defaultPrimePipelineResources = RenderModeFactory();
    defaultPrimePipelineResources.LoadDefaultShaders();
    defaultPrimePipelineResources.LoadDefaultPSO(primeDevice, primeDeviceSignature, desc,
                                                 BackBufferFormat, DXGI_FORMAT_D32_FLOAT, nullptr,
                                                 NormalMapFormat, AmbientMapFormat);

    debugLogger.PushMessage(std::wstring(L"\nInit PSO for " + primeDevice->GetName()));
}

void HybridVolumeRenderingApp::CreateMaterials()
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

void HybridVolumeRenderingApp::InitSRVMemoryAndMaterials()
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

void HybridVolumeRenderingApp::InitRenderPaths()
{
    auto commandQueue = primeDevice->GetCommandQueue(GQueueType::Graphics);
    auto cmdList = commandQueue->GetCommandList();

    ssaoPass = std::make_shared<SharedSSAO>();
    hbaoPass = std::make_shared<SharedHBAO>();

    fogPass = std::make_shared<SharedFog>();
    volumePass = std::make_shared<SharedVolume>();
    
    const D3D12_INPUT_LAYOUT_DESC layoutDesc = {defaultInputLayout.data(), defaultInputLayout.size()};

    volumePass->Initialize(
        primeDevice,
        secondDevice,
        layoutDesc,
        models[L"volumeBox"],
        MainWindow->GetClientWidth(),
        MainWindow->GetClientHeight());

    ssaoPass->Initialize(
        primeDevice,
        secondDevice,
        layoutDesc,
        MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    ssaoPass->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

    hbaoPass->Initialize(primeDevice, secondDevice, layoutDesc, MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    hbaoPass->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    
    fogPass->Initialize(primeDevice, secondDevice, layoutDesc,
                            MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    fogPass->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    antiAliasingPrimePath = (std::make_shared<SSAA>(primeDevice, 1, MainWindow->GetClientWidth(),
                                                    MainWindow->GetClientHeight(), DXGI_FORMAT_D32_FLOAT));
    antiAliasingPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

    shadowPath = (std::make_shared<ShadowMap>(primeDevice, 4096, 4096));

    UIPath = std::make_shared<UILayer>(primeDevice, MainWindow->GetWindowHandle());

    commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));
    commandQueue->Flush();

    debugLogger.PushMessage(std::wstring(L"\nInit Render path data for " + primeDevice->GetName()));
}

void HybridVolumeRenderingApp::InitVolumeAsyncSlots()
{
    for (UINT i = 0; i < volumeSlots.size(); ++i)
    {
        volumeSlots[i].PassConstantUploadBuffer = std::make_shared<ConstantUploadBuffer<PassConstants>>(secondDevice,1,secondDevice->GetName() + L" Volume Slot Pass Data Buffer " + std::to_wstring(i));
        volumeSlots[i].VolumeConstantUploadBuffer = std::make_shared<ConstantUploadBuffer<VolumeConstants>>(secondDevice,1,secondDevice->GetName() + L" Volume Slot Data Buffer " + std::to_wstring(i));
        volumeSlots[i].ObjectConstantUploadBuffer = std::make_shared<ConstantUploadBuffer<ObjectConstants>>(secondDevice,1,secondDevice->GetName() + L" Volume Slot Object Data Buffer " + std::to_wstring(i));
    }
}

void HybridVolumeRenderingApp::LoadStudyTexture()
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

void HybridVolumeRenderingApp::LoadModels()
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
    
    auto volumeBox = assets->GenerateBox(cmdList);
    models[L"volumeBox"] = std::move(volumeBox);

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

void HybridVolumeRenderingApp::MipMasGenerate()
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

void HybridVolumeRenderingApp::SortGO()
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

void HybridVolumeRenderingApp::CreateGO()
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
    
    auto volume = std::make_unique<GameObject>("Volume");
    volume->GetTransform()->SetPosition(Vector3(0.0f, 50.0f, 0.0f));
    volume->GetTransform()->SetScale(Vector3(100.0f, 50.0f, 100.0f));
    auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"volumeBox"]);
    volume->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::Volume)].push_back(renderer);
    volumeObject = volume.get();
    gameObjects.push_back(std::move(volume));
    
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
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"platform"]);
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
    auto cameraController = std::make_shared<CameraController>();
    cameraController->SetMoveSpeedMultiplier(15.0f);
    camera->AddComponent(cameraController);
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

void HybridVolumeRenderingApp::OnApplicationExit()
{
    debugLogger.WriteAllLog();
}

int HybridVolumeRenderingApp::Run()
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

void HybridVolumeRenderingApp::UpdateMaterials() const
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

void HybridVolumeRenderingApp::UpdateShadowTransform(const GameTimer& gt)
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

void HybridVolumeRenderingApp::UpdateShadowPassCB(const GameTimer& gt)
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

void HybridVolumeRenderingApp::UpdateMainPassCB(const GameTimer& gt)
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
    
    currentVolumePassConstants = mainPassCB;

    auto currentPassCB = currentFrameResource->PrimePassConstantUploadBuffer;
    currentPassCB->CopyData(0, mainPassCB);
    
    auto secondPassCB = currentFrameResource->SecondPassConstantUploadBuffer;
    secondPassCB->CopyData(0, mainPassCB);
}

void HybridVolumeRenderingApp::UpdateSsaoCB(const GameTimer& gt) const
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

void HybridVolumeRenderingApp::UpdateFogCB(const GameTimer& gt) const
{
    (void)gt;

    FogConstants fogCB = {};
    fogCB.InvView = mainPassCB.InvView;
    fogCB.Proj = mainPassCB.Proj;
    fogCB.InvProj = mainPassCB.InvProj;
    fogCB.Resolution = Vector4(
        static_cast<float>(MainWindow->GetClientWidth()),
        static_cast<float>(MainWindow->GetClientHeight()),
        1.0f / MainWindow->GetClientWidth(),
        1.0f / MainWindow->GetClientHeight());

    fogCB.FogColor = Vector4(0.72f, 0.78f, 0.86f, 1.0f);
    fogCB.CameraPosW = mainPassCB.EyePosW;
    fogCB.Density = 0.008f;
    fogCB.HeightFalloff = 0.015f;
    fogCB.FogBaseHeight = 0.0f;
    fogCB.StartDistance = 25.0f;
    fogCB.MaxOpacity = 0.85f;
    fogCB.NearZ = mainPassCB.NearZ;
    fogCB.FarZ = mainPassCB.FarZ;

    currentFrameResource->PrimeFogConstantUploadBuffer->CopyData(0, fogCB);
    currentFrameResource->SecondFogConstantUploadBuffer->CopyData(0, fogCB);
}

void HybridVolumeRenderingApp::UpdateVolumeCB(const GameTimer& gt)
{
    (void)gt;

    VolumeConstants volumeCB = {};
    volumeCB.BoxMinW = Vector3(-50.0f, 25.0f, -50.0f);
    volumeCB.BoxMaxW = Vector3(50.0f, 75.0f, 50.0f);
    volumeCB.Density = 0.045f;
    volumeCB.StepSize = 1.0f;
    volumeCB.Color = Vector3(0.72f, 0.78f, 0.86f);
    volumeCB.MaxOpacity = 0.99f;
    volumeCB.Resolution = Vector2(
        static_cast<float>(MainWindow->GetClientWidth()),
        static_cast<float>(MainWindow->GetClientHeight()));
    volumeCB.InvResolution = Vector2(
        1.0f / volumeCB.Resolution.x,
        1.0f / volumeCB.Resolution.y);
    
    currentVolumeConstants = volumeCB;

    VolumeConstants primeVolumeCB = volumeCB;
    primeVolumeCB.Color = Vector3(0.0f, 1.f, 0.0f); // GREEN

    VolumeConstants secondVolumeCB = volumeCB;
    secondVolumeCB.Color = Vector3(0.85f, 0.85f, 0.85f); // MAGENTA

    currentFrameResource->PrimeVolumeConstantUploadBuffer->CopyData(0, primeVolumeCB);
    currentFrameResource->SecondVolumeConstantUploadBuffer->CopyData(0, secondVolumeCB);
    ObjectConstants objectCB = {};

    objectCB.World =
        (
            volumeObject->GetTransform()->GetWorldMatrix()
            * models.at(L"volumeBox")->scaleMatrix
        ).Transpose();

    objectCB.TextureTransform =
        volumeObject
        ->GetTransform()
        ->TextureTransform
        .Transpose();
    
    currentVolumeObjectConstants = objectCB;

    currentFrameResource
        ->SecondVolumeObjectConstantUploadBuffer
        ->CopyData(0, objectCB);
}

bool HybridVolumeRenderingApp::InitMainWindow()
{
    MainWindow = CreateRenderWindow(primeDevice, mainWindowCaption, 1920, 1080, false);

    debugLogger.PushMessage(std::wstring(L"\nInit Window"));
    return true;
}

void HybridVolumeRenderingApp::OnResize()
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
    
    if (fogPass != nullptr)
    {
        fogPass->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    }
    
    if (volumePass != nullptr)
    {
        volumePass->OnResize(
            MainWindow->GetClientWidth(),
            MainWindow->GetClientHeight());
    }    

    if (antiAliasingPrimePath != nullptr)
    {
        antiAliasingPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    }

    UIPath->CreateDeviceObject();

    currentFrameResourceIndex = MainWindow->GetCurrentBackBufferIndex();
}

void HybridVolumeRenderingApp::Flush()
{
    primeDevice->Flush();
    secondDevice->Flush();
}

LRESULT HybridVolumeRenderingApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
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

            if (keycode == (VK_F4) && keyboard.KeyIsPressed(VK_F4))
            {
                SwitchFogMode();
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
