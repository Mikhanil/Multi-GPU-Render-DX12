#include "HybridAtmosphereApp.h"

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

#include "SkyAtmosphere/SkyAtmosphereRenderer.h"
#include "CustomShaderLoader/CustomInclude.h"
#include "CustomShaderLoader/GShaderCustomInclude.h"

HybridAtmosphereApp::HybridAtmosphereApp(const HINSTANCE hInstance) : D3DApp(hInstance), debugLogger(FileQueueWriter(std::filesystem::current_path() / "log.txt"))
{
    mSceneBounds.Center = Vector3(0.0f, 0.0f, 0.0f);
    mSceneBounds.Radius = 200;
}

HybridAtmosphereApp::~HybridAtmosphereApp() = default;

void HybridAtmosphereApp::SwitchDevice()
{
    Flush();
    IsUsingSharedAtmosphere = !IsUsingSharedAtmosphere;
}

void HybridAtmosphereApp::ChangeConfiguration()
{
    Flush();
    IsSharedTerrain = !IsSharedTerrain;
}

void HybridAtmosphereApp::ResetCamera() const
{
    auto& CamTrans = camera->gameObject->GetTransform();
    if (auto* Rotater = CamTrans->GetParent())
    {
        Rotater->SetLocalMatrix(RotaterSaveMatrix);
    }
    CamTrans->SetLocalMatrix(CameraSaveMatrix);
}

void HybridAtmosphereApp::Update(const GameTimer& gt)
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
        primeDevice->ReleaseSlateDescriptors(currentFrameResource->PrimeRenderFenceValue);
    }

    if (currentFrameResource->SecondRenderFenceValue != 0 && !secondQueue->IsFinish(
        currentFrameResource->SecondRenderFenceValue))
    {
        secondQueue->WaitForFenceValue(currentFrameResource->SecondRenderFenceValue);
    }
    else
    {
        secondDevice->ReleaseSlateDescriptors(currentFrameResource->SecondRenderFenceValue);
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

    UpdateAtmosphere();

    UpdateMaterials();
    UpdateShadowTransform(gt);
    UpdateMainPassCB(gt);
    UpdateShadowPassCB(gt);
    UpdateSsaoCB(gt);
    UIPath->Update();
    
    benchmark.Tick(gt.DeltaTime());
}

void HybridAtmosphereApp::UpdateAtmosphere()
{
    float dt = Common::D3DApp::GetApp().GetTimer()->DeltaTime();

#if defined(DEBUG) | defined(_DEBUG)
	auto& app = static_cast<Common::D3DApp&>(Common::D3DApp::GetApp());
	auto keyboard = app.GetKeyboard();

	float atmosphereMoveSpeed = 1.0f;
	float terrainMoveSpeed = 1.0f;
	if (keyboard->KeyIsPressed(VK_SHIFT))
	{
		atmosphereMoveSpeed *= 10;
		terrainMoveSpeed *= 10;
	}
	if (keyboard->KeyIsPressed('O'))
	{
        skyAtmosphere->mCamPosFinal.z += atmosphereMoveSpeed * dt;
	}
	if (keyboard->KeyIsPressed('L'))
	{
        skyAtmosphere->mCamPosFinal.z -= atmosphereMoveSpeed * dt;
	}

	if (keyboard->KeyIsPressed('U'))
	{
        skyAtmosphere->mTerrainPos += terrainMoveSpeed * Vector3::Up * dt;
    }
    if (keyboard->KeyIsPressed('J'))
    {
        skyAtmosphere->mTerrainPos += terrainMoveSpeed * Vector3::Down * dt;
    }
    if (keyboard->KeyIsPressed('H'))
    {
        skyAtmosphere->mTerrainPos += terrainMoveSpeed * Vector3::Left * dt;
    }
    if (keyboard->KeyIsPressed('K'))
    {
        skyAtmosphere->mTerrainPos += terrainMoveSpeed * Vector3::Right * dt;
    }
    if (keyboard->KeyIsPressed('Y'))
    {
        skyAtmosphere->mTerrainPos += terrainMoveSpeed * Vector3::Forward * dt;
    }
    if (keyboard->KeyIsPressed('I'))
    {
        skyAtmosphere->mTerrainPos += terrainMoveSpeed * Vector3::Backward * dt;
	}
#else
    float terrainAbsoluteLow = -1.63f;
    float terrainAbsoluteUp = -12.5f;

    float atmosphereAbsoluteLow = 0.1f;
    float atmosphereAbsoluteUp = 99.9f;

    float terrainLocalLow = terrainAbsoluteLow - atmosphereAbsoluteLow;
    float terrainLocalUp = terrainAbsoluteUp - atmosphereAbsoluteLow;

    float atmosphereSpeed = 0.5f;
    float terrainSpeed = 0.37f;

    skyAtmosphere->mCamPosFinal.z = atmosphereAbsoluteLow +
        (atmosphereAbsoluteUp - atmosphereAbsoluteLow) * 0.5 * (1 - cosf(atmosphereSpeed * timer.TotalTime()));

    skyAtmosphere->mTerrainPos.z = skyAtmosphere->mCamPosFinal.z + terrainLocalLow +
        (terrainLocalUp - terrainLocalLow) * 0.5 * (1 - cosf(terrainSpeed * timer.TotalTime()));

#endif
    skyAtmosphere->mShadowmapViewProjMat = shadowPassCB.ViewProj;

    Vector3 forward = camera->gameObject->GetTransform()->GetForwardVector();
    float viewPitchSin = forward.y;
    float viewPitchCos_YawSin = forward.x;
    float viewPitchCos_YawCos = forward.z;
    float viewPitch = asinf(viewPitchSin);
    float viewYaw = atan2f(viewPitchCos_YawSin, viewPitchCos_YawCos);
    Vector3 _viewDir;
    XMMATRIX BBB = XMMatrixRotationRollPitchYaw(-viewPitch, viewYaw, 0.0f);
    XMStoreFloat3(&_viewDir, BBB.r[2]);
    skyAtmosphere->mViewDir = _viewDir;
    skyAtmosphere->mViewDir.x = -_viewDir.x;
    skyAtmosphere->mViewDir.y = _viewDir.z;
    skyAtmosphere->mViewDir.z = _viewDir.y;

    Vector3 tmp = mRotatedLightDirections[0];
    tmp = -tmp;
    skyAtmosphere->mSunDir = tmp;
    skyAtmosphere->mSunDir.x = -tmp.x;
    skyAtmosphere->mSunDir.y = tmp.z;
    skyAtmosphere->mSunDir.z = tmp.y;

    {
        Vector3 focusPosition = skyAtmosphere->mCamPosFinal + skyAtmosphere->mViewDir;
        Vector3 eyePosition = skyAtmosphere->mCamPosFinal;
        Vector3 upDirection = Vector3{ 0.0f, 0.0f, 1.0f };	// Unreal z-up

        skyAtmosphere->mViewMat = XMMatrixLookAtLH(eyePosition, focusPosition, upDirection);
        skyAtmosphere->mProjMat = XMMatrixPerspectiveFovLH(
            DirectX::XMConvertToRadians(camera->GetFov()),
            camera->GetAspectRatio(), 0.1f, 20000.0f);

        skyAtmosphere->mViewProjMat = skyAtmosphere->mViewMat * skyAtmosphere->mProjMat;
    }

    float sunIlluminanceScale = 1.0f;
    int NumScatteringOrder = 4;

    skyAtmosphere->mCommonConstanants.viewProjMat = skyAtmosphere->mViewProjMat;
    skyAtmosphere->mCommonConstanants.color = { 0.0, 1.0, 1.0, 1.0 };
    skyAtmosphere->mCommonConstanants.sunIlluminance = { 1.0f * sunIlluminanceScale, 1.0f * sunIlluminanceScale, 1.0f * sunIlluminanceScale };
    skyAtmosphere->mCommonConstanants.scatteringMaxPathDepth = NumScatteringOrder;
    skyAtmosphere->mCommonConstanants.frameTimeSec = timer.DeltaTime();
    skyAtmosphere->mCommonConstanants.timeSec = timer.TotalTime();
    skyAtmosphere->mCommonConstanants.frameId = gFrameId;
    skyAtmosphere->mCommonConstanants.screenshotCaptureActive = false;
    skyAtmosphere->mCommonConstanants.terrainPosDelta = skyAtmosphere->mTerrainPos;

    skyAtmosphere->UpdateSkyAtmosphereBuffer();

	currentFrameResource->PrimeAtmosphereUploadBuffer->CopyData(0, skyAtmosphere->mAtmosphereConstants);
	currentFrameResource->SecondAtmosphereUploadBuffer->CopyData(0, skyAtmosphere->mAtmosphereConstants);

	currentFrameResource->PrimeAtmosphereCommonUploadBuffer->CopyData(0, skyAtmosphere->mCommonConstanants);
    currentFrameResource->SecondAtmosphereCommonUploadBuffer->CopyData(0, skyAtmosphere->mCommonConstanants);
}

void HybridAtmosphereApp::PopulateAtmosphereCommands(const std::shared_ptr<GCommandList>& cmdList,
	const Atmosphere::SkyAtmosphereResources& Resources)
{

    if (IsUsingSharedAtmosphere)
    {
        if (!IsSharedTerrain)
		{
			// Must be before RayMarchingCommands
			skyAtmosphere->PopulateTerrainCommands(cmdList,
				currentFrameResource->PrimeAtmosphereCommonUploadBuffer,
				currentFrameResource->PrimeAtmosphereUploadBuffer, Resources);

            {
                const auto& Resources = skyAtmosphere->GetPrimeResources();
                const auto& CrossResources = skyAtmosphere->GetCrossResources();

                cmdList->CopyResource(CrossResources.GetTerrainRenderTarget().GetPrimeResource(), Resources.GetTerrainRender());
                cmdList->CopyResource(CrossResources.GetDepthMap().GetPrimeResource(), Resources.GetDepthMap());

                cmdList->CopyResource(Resources.GetRayMarchingResult(), CrossResources.GetRayMarchingResult().GetPrimeResource());
                cmdList->CopyResource(Resources.GetTransmittanceLut(), CrossResources.GetTransmittanceLut().GetPrimeResource());

            }
            auto secondQueue = secondDevice->GetCommandQueue();

            if (currentFrameResource->SecondRenderFenceValue == 0 || secondQueue->IsFinish(currentFrameResource->SecondRenderFenceValue))
            {
                const auto& SecondResources = skyAtmosphere->GetSecondResource();
                const auto& SecondCrossResources = skyAtmosphere->GetCrossResources();
                const auto secondCmdList = secondQueue->GetCommandList();

                secondCmdList->CopyResource(SecondResources.GetTerrainRender(), SecondCrossResources.GetTerrainRenderTarget().GetSharedResource());
                secondCmdList->CopyResource(SecondResources.GetDepthMap(), SecondCrossResources.GetDepthMap().GetSharedResource());

                skyAtmosphere->ComputeAtmosphere(secondCmdList,
                    currentFrameResource->SecondAtmosphereCommonUploadBuffer,
                    currentFrameResource->SecondAtmosphereUploadBuffer, SecondResources);

                secondCmdList->CopyResource(SecondCrossResources.GetRayMarchingResult().GetSharedResource(), SecondResources.GetRayMarchingResult());
                secondCmdList->CopyResource(SecondCrossResources.GetTransmittanceLut().GetSharedResource(), SecondResources.GetTransmittanceLut());

                currentFrameResource->SecondRenderFenceValue = secondQueue->ExecuteCommandList(secondCmdList);
            }
        }
        else
        {
			{
				const auto& Resources = skyAtmosphere->GetPrimeResources();
				const auto& CrossResources = skyAtmosphere->GetCrossResources();

				cmdList->CopyResource(Resources.GetRayMarchingResult(), CrossResources.GetRayMarchingResult().GetPrimeResource());

			}
			auto secondQueue = secondDevice->GetCommandQueue();

			if (currentFrameResource->SecondRenderFenceValue == 0 || secondQueue->IsFinish(currentFrameResource->SecondRenderFenceValue))
			{
				const auto& SecondResources = skyAtmosphere->GetSecondResource();
				const auto& SecondCrossResources = skyAtmosphere->GetCrossResources();
				const auto secondCmdList = secondQueue->GetCommandList();

				// Must be before RayMarchingCommands
				skyAtmosphere->PopulateTerrainCommands(secondCmdList,
					currentFrameResource->SecondAtmosphereCommonUploadBuffer,
					currentFrameResource->SecondAtmosphereUploadBuffer, SecondResources);

				skyAtmosphere->ComputeAtmosphere(secondCmdList,
					currentFrameResource->SecondAtmosphereCommonUploadBuffer,
					currentFrameResource->SecondAtmosphereUploadBuffer, SecondResources);

				secondCmdList->CopyResource(SecondCrossResources.GetRayMarchingResult().GetSharedResource(), SecondResources.GetRayMarchingResult());

				currentFrameResource->SecondRenderFenceValue = secondQueue->ExecuteCommandList(secondCmdList);
			}
        }
    }
    else
	{
		// Must be before RayMarchingCommands
		skyAtmosphere->PopulateTerrainCommands(cmdList,
			currentFrameResource->PrimeAtmosphereCommonUploadBuffer,
			currentFrameResource->PrimeAtmosphereUploadBuffer, Resources);

		skyAtmosphere->ComputeAtmosphere(cmdList,
			currentFrameResource->PrimeAtmosphereCommonUploadBuffer,
			currentFrameResource->PrimeAtmosphereUploadBuffer, Resources);
    }

}

void HybridAtmosphereApp::PopulateShadowMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    cmdList->StartMark(L"ShadowMapCommands");
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
    cmdList->EndMark();
}

void HybridAtmosphereApp::PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    cmdList->StartMark(L"NormalMapCommands");
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

        /*
        if (IsUseHBAO)
        {
            const HBAOResources& Resources = hbaoPass->GetPrimeResources();
            normalMap = Resources.GetNormalMap();
            normalDepthMap = Resources.GetDepthMap();
            normalMapRtv = Resources.GetNormalMapRTV();
            normalMapDsv = Resources.GetDepthMapDSV();
        }
        else
        */
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
        // 
        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::DrawNormalsOpaqueDrop));
        PopulateDrawCommands(cmdList, RenderMode::OpaqueAlphaDrop);


        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }
    cmdList->EndMark();
}

void HybridAtmosphereApp::PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList) const
{
    /*
	if (IsUseHBAO)
		hbaoPass->Compute(cmdList, currentFrameResource->PrimeHBAOConstantUploadBuffer, hbaoPass->GetPrimeResources());
	else
    */
		ssaoPass->ComputeSsao(cmdList, currentFrameResource->PrimeSsaoConstantUploadBuffer, ssaoPass->GetPrimeResources(), 3);
}

void HybridAtmosphereApp::PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    cmdList->StartMark(L"ForwardPathCommands");
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

        cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ssaoPass->GetPrimeResources().GetAmbientMapSRV(), 0);

        // cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::SkyBox));
        // PopulateDrawCommands(cmdList, (RenderMode::SkyBox));

        cmdList->SetPipelineState(*mCustomAppPSOs[RenderMode::AtmospherePostProcess]);
        PopulateDrawCommands(cmdList, (RenderMode::AtmospherePostProcess));

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
    cmdList->EndMark();
}

void HybridAtmosphereApp::PopulateDrawCommands(const std::shared_ptr<GCommandList>& cmdList,
                                         RenderMode type) const
{
    for (auto&& renderer : typedRenderer[static_cast<int>(type)])
    {
        renderer->Draw(cmdList);
    }
}

void HybridAtmosphereApp::PopulateInitRenderTarget(const std::shared_ptr<GCommandList>& cmdList, const GTexture& renderTarget,
                                             const GDescriptor* rtvMemory, const UINT offsetRTV) const
{
    cmdList->SetViewports(&fullViewport, 1);
    cmdList->SetScissorRects(&fullRect, 1);

    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();
    cmdList->ClearRenderTarget(rtvMemory, offsetRTV, Colors::Black);

    cmdList->SetRenderTargets(1, rtvMemory, offsetRTV);
}

void HybridAtmosphereApp::PopulateDrawFullQuadTexture(const std::shared_ptr<GCommandList>& cmdList,
                                                const GDescriptor* renderTextureSRVMemory, const UINT renderTextureMemoryOffset,
                                                const GraphicPSO& pso) const
{
    cmdList->SetPipelineState(pso);

    cmdList->SetDescriptorsHeap(renderTextureSRVMemory);
    cmdList->SetGraphicsRootDescriptorTable(StandardShaderSlot::AmbientMap, renderTextureSRVMemory, renderTextureMemoryOffset);

    PopulateDrawCommands(cmdList, (RenderMode::Quad));
}

void HybridAtmosphereApp::PopulateDebugCommands(const std::shared_ptr<GCommandList>& cmdList)
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
        /*
            if (IsUseHBAO)
                PopulateDrawFullQuadTexture(cmdList, hbaoPass->GetPrimeResources().GetAmbientMapSRV(),
                                            0, *defaultPrimePipelineResources.GetPSO(RenderMode::Quad));

            else
        */
                PopulateDrawFullQuadTexture(cmdList, ssaoPass->GetPrimeResources().GetAmbientMapSRV(),
                                            0, *defaultPrimePipelineResources.GetPSO(RenderMode::Quad));


            break;
        }
    }
}

void HybridAtmosphereApp::Draw(const GameTimer& gt)
{
    if (isResizing) return;

    auto primeRenderQueue = primeDevice->GetCommandQueue();
    auto primeCmdList = primeRenderQueue->GetCommandList();

    for (auto emitter : emitters)
    {
        emitter->Dispatch(primeCmdList);
    }

    // Must be before ForwardPathCommands
    PopulateAtmosphereCommands(primeCmdList, skyAtmosphere->GetPrimeResources());

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


bool HybridAtmosphereApp::Initialize()
{
    InitDevices();
    InitMainWindow();

    LoadStudyTexture();
    LoadCustomTextures();
    LoadModels();
    CreateMaterials();
    LoadCustomMaterials();
    MipMasGenerate();

    InitRenderPaths();
    InitSRVMemoryAndMaterials();
    InitRootSignature();
    InitPipeLineResource();
    CreateGO();
    CreateCustomGO();
    SortGO();
    InitFrameResource();

    OnResize();

    Flush();

    int TestTime = 10;
#if !defined(DEBUG) && !defined(_DEBUG)
    TestTime = 100;
#endif
    int lastMultiplier = 8;

    for (int multiplier = 1; multiplier <= lastMultiplier; multiplier++)
    {
        auto& NativeAtmosphereState = benchmark.AddState<WaitState>(TestTime, FileQueueWriter(Benchmark::GetLogFile(
            std::format(L"Native Atmosphere (SSAA {}) ", multiplier), *primeDevice, *secondDevice)));
        NativeAtmosphereState.OnEnter = [this, multiplier](FileQueueWriter& logs)
			{
				antiAliasingPrimePath->SetMultiplier(multiplier, MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
				logs.PushMessage(L"FPS;MSPF;MinFPS;MinMSPF;MaxFPS;MaxMSPF");
			};

        NativeAtmosphereState.OnStatChanged = [this, multiplier](FileQueueWriter& logs, const TimeStats& ts, float progress)
			{
				Benchmark::PrintStatsCSV(ts, logs);
				MainWindow->SetWindowTitle(std::format(L"Native Atmosphere (SSAA {}) Progress ", multiplier) +
					std::format(L"{:.2f}", progress * 100) + L"% FPS:" + std::to_wstring(ts.fps));
			};

        NativeAtmosphereState.OnExit = [this](FileQueueWriter& logs)
			{
				logs.WriteAllLog();
				Flush();
			};

        auto& HybridAtmosphereState = benchmark.AddState<WaitState>(TestTime, FileQueueWriter(Benchmark::GetLogFile(
            std::format(L"Hybrid Atmosphere (SSAA {}) ", multiplier), *primeDevice, *secondDevice)));
        HybridAtmosphereState.OnEnter = [this](FileQueueWriter& logs)
        {
            ResetCamera();
            SwitchDevice();
            logs.PushMessage(L"FPS;MSPF;MinFPS;MinMSPF;MaxFPS;MaxMSPF");
        };
        HybridAtmosphereState.OnStatChanged = [this, multiplier](FileQueueWriter& logs, const TimeStats& ts, float progress)
            {
                Benchmark::PrintStatsCSV(ts, logs);
                MainWindow->SetWindowTitle(std::format(L"Hybrid Atmosphere (SSAA {}) Progress ", multiplier) +
                    std::format(L"{:.2f}", progress * 100) + L"% FPS:" + std::to_wstring(ts.fps));
            };
        HybridAtmosphereState.OnExit = [this](FileQueueWriter& logs)
            {
                logs.WriteAllLog();
                Flush();
                SwitchDevice();
            };

        auto& HybridAtmosphereTerrainState = benchmark.AddState<WaitState>(TestTime, FileQueueWriter(Benchmark::GetLogFile(
            std::format(L"Hybrid Atmosphere Terrain (SSAA {}) ", multiplier), *primeDevice, *secondDevice)));
        HybridAtmosphereTerrainState.OnEnter = [this](FileQueueWriter& logs)
            {
                ResetCamera();
                logs.PushMessage(L"FPS;MSPF;MinFPS;MinMSPF;MaxFPS;MaxMSPF");
                SwitchDevice();
                ChangeConfiguration();
            };
        HybridAtmosphereTerrainState.OnStatChanged = [this, multiplier](FileQueueWriter& logs, const TimeStats& ts, float progress)
            {
                Benchmark::PrintStatsCSV(ts, logs);
                MainWindow->SetWindowTitle(std::format(L"Hybrid Atmosphere Terrain (SSAA {}) Progress ", multiplier) +
                    std::format(L"{:.2f}", progress * 100) + L"% FPS:" + std::to_wstring(ts.fps));
            };
        HybridAtmosphereTerrainState.OnExit = [this, multiplier, lastMultiplier](FileQueueWriter& logs)
            {
                logs.WriteAllLog();
                SwitchDevice();
                ChangeConfiguration();
                Flush();
                IsStop = (multiplier == lastMultiplier);
            };
    }


#if !defined(DEBUG) && !defined(_DEBUG)
    benchmark.Start();
#endif
    return true;
}

void HybridAtmosphereApp::InitDevices()
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
        typedRenderer.emplace_back(std::vector<std::shared_ptr<Renderer>>());
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

void HybridAtmosphereApp::InitFrameResource()
{
    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        frameResources.emplace_back(std::make_unique<FrameResource>(primeDevice, secondDevice, 2, assets->GetMaterials().size()));
    }
    debugLogger.PushMessage(std::wstring(L"\nInit FrameResource "));
}

void HybridAtmosphereApp::InitRootSignature()
{
    auto rootSignature = std::make_shared<GRootSignature>();
    CD3DX12_DESCRIPTOR_RANGE texParam[5];
    texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0); //SkyMap
    texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0); //ShadowMap
    texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::AmbientMap - 3, 0); //SsaoMap
    texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                     assets->GetLoadTexturesCount() > 0 ? assets->GetLoadTexturesCount() : 1,
                     StandardShaderSlot::TexturesMap - 3, 0);
    texParam[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0u, 2u); // RayMarching Result


    rootSignature->AddConstantBufferParameter(0);
    rootSignature->AddConstantBufferParameter(1);
    rootSignature->AddShaderResourceView(0, 1);
    rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);
	rootSignature->AddDescriptorParameter(&texParam[4], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->Initialize(primeDevice);

    primeDeviceSignature = rootSignature;

    debugLogger.PushMessage(std::wstring(L"\nInit RootSignature for " + primeDevice->GetName()));
}

void HybridAtmosphereApp::InitPipeLineResource()
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
    LoadCustomShaders();
    defaultPrimePipelineResources.LoadDefaultPSO(primeDevice, primeDeviceSignature, desc,
                                                 BackBufferFormat, DXGI_FORMAT_D32_FLOAT, nullptr,
                                                 NormalMapFormat, AmbientMapFormat);
    LoadCustomPSOs(primeDevice, primeDeviceSignature, desc,
        BackBufferFormat, DXGI_FORMAT_D32_FLOAT);

    debugLogger.PushMessage(std::wstring(L"\nInit PSO for " + primeDevice->GetName()));
}

void HybridAtmosphereApp::CreateMaterials()
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

void HybridAtmosphereApp::InitSRVMemoryAndMaterials()
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

void HybridAtmosphereApp::InitRenderPaths()
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

void HybridAtmosphereApp::LoadStudyTexture()
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

void HybridAtmosphereApp::LoadModels()
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

void HybridAtmosphereApp::MipMasGenerate()
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

void HybridAtmosphereApp::SortGO()
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

void HybridAtmosphereApp::CreateGO()
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

    // auto deltaUp = Vector3::Up * 100;
    auto deltaUp = Vector3::Zero * 100;

    auto sun1 = std::make_unique<GameObject>("Directional Light");
    auto light = std::make_shared<Light>(Directional);
    light->Direction({0.57735f, -0.57735f, 0.57735f});
    light->Strength({0.8f, 0.8f, 0.8f});
    sun1->AddComponent(light);
    gameObjects.push_back(std::move(sun1));

    for (int i = 0; i < 11; ++i)
    {
        auto nano = std::make_unique<GameObject>();
        nano->GetTransform()->SetPosition(Vector3::Right * -15 + Vector3::Forward * 12 * i + deltaUp);
        nano->GetTransform()->SetEulerRotate(Vector3(0, -90, 0));
        auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"nano"]);
        nano->AddComponent(renderer);
        typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
        gameObjects.push_back(std::move(nano));


        auto doom = std::make_unique<GameObject>();
        doom->SetScale(0.08);
        doom->GetTransform()->SetPosition(Vector3::Right * 15 + Vector3::Forward * 12 * i + deltaUp);
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
                Vector3::Right * -60 + Vector3::Right * -30 * j + Vector3::Up * 11 + Vector3::Forward * 10 * i
                + deltaUp);
            auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"atlas"]);
            atlas->AddComponent(renderer);
            typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
            gameObjects.push_back(std::move(atlas));


            auto pbody = std::make_unique<GameObject>();
            pbody->GetTransform()->SetPosition(
                Vector3::Right * 130 + Vector3::Right * -30 * j + Vector3::Up * 11 + Vector3::Forward * 10 * i
                + deltaUp);
            renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"pbody"]);
            pbody->AddComponent(renderer);
            typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
            gameObjects.push_back(std::move(pbody));
        }
    }

    auto particle = std::make_unique<GameObject>();
    particle->GetTransform()->SetPosition(Vector3::Up - Vector3::Right * 45 - Vector3::Forward * 36 + deltaUp);
    const auto emitter = std::make_shared<ParticleEmitter>(primeDevice, 10000);
    particle->AddComponent(emitter);
    typedRenderer[static_cast<int>(RenderMode::Particle)].push_back(emitter);
    emitters.push_back(emitter.get());
    gameObjects.push_back(std::move(particle));


    auto platform = std::make_unique<GameObject>();
    platform->SetScale(0.2);
    platform->GetTransform()->SetEulerRotate(Vector3(90, 90, 0));
    platform->GetTransform()->SetPosition(Vector3::Backward * -130 + deltaUp);
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
    //camera->GetTransform()->SetPosition(Vector3(-1000, 190, -32));
    camera->GetTransform()->SetPosition(Vector3(-1005.4f, 4.0f, -854.4f));
    //camera->GetTransform()->SetEulerRotate(Vector3(-30, 270, 0));
    camera->GetTransform()->SetEulerRotate(Vector3(0, -115, 0));
    camera->AddComponent(std::make_shared<Camera>(AspectRatio()));
    camera->GetComponent<Camera>()->SetFarZ(30000.0f);
    CameraSaveMatrix = camera->GetTransform()->GetLocalMatrix();

    camera->AddComponent(std::make_shared<CameraController>());
    /*
#if defined(DEBUG) || defined(_DEBUG)
    camera->AddComponent(std::make_shared<CameraController>());
#else
    rotater->AddComponent(std::make_shared<Rotater>(10));
#endif
    */

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
    fountain->GetTransform()->SetPosition(Vector3::Up * 35 + Vector3::Backward * 77 + deltaUp);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"fountain"]);
    fountain->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);

    gameObjects.push_back(std::move(platform));
    gameObjects.push_back(std::move(stair));
    gameObjects.push_back(std::move(columns));
    gameObjects.push_back(std::move(fountain));

    auto mountDragon = std::make_unique<GameObject>();
    mountDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    mountDragon->GetTransform()->SetPosition(Vector3::Right * -960 + Vector3::Up * 45 + Vector3::Backward * 775
        + deltaUp);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"mountDragon"]);
    mountDragon->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
    gameObjects.push_back(std::move(mountDragon));


    auto desertDragon = std::make_unique<GameObject>();
    desertDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    desertDragon->GetTransform()->SetPosition(Vector3::Right * 960 + Vector3::Up * -5 + Vector3::Backward * 775
        + deltaUp);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"desertDragon"]);
    desertDragon->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
    gameObjects.push_back(std::move(desertDragon));

    auto griffon = std::make_unique<GameObject>();
    griffon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    griffon->SetScale(0.8);
    griffon->GetTransform()->SetPosition(Vector3::Right * -355 + Vector3::Up * -7 + Vector3::Backward * 17
        + deltaUp);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"griffon"]);
    griffon->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::OpaqueAlphaDrop)].push_back(renderer);
    gameObjects.push_back(std::move(griffon));

    griffon = std::make_unique<GameObject>();
    griffon->SetScale(0.8);
    griffon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    griffon->GetTransform()->SetPosition(Vector3::Right * 355 + Vector3::Up * -7 + Vector3::Backward * 17
        + deltaUp);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"griffon"]);
    griffon->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::OpaqueAlphaDrop)].push_back(renderer);
    gameObjects.push_back(std::move(griffon));
    
    debugLogger.PushMessage(std::wstring(L"\nFinish create GO"));
}

void HybridAtmosphereApp::OnApplicationExit()
{
    debugLogger.WriteAllLog();
}

int HybridAtmosphereApp::Run()
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
            gFrameId++;

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

void HybridAtmosphereApp::UpdateMaterials() const
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

void HybridAtmosphereApp::UpdateShadowTransform(const GameTimer& gt)
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

void HybridAtmosphereApp::UpdateShadowPassCB(const GameTimer& gt)
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

void HybridAtmosphereApp::UpdateMainPassCB(const GameTimer& gt)
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
    mainPassCB.NearZ = camera->GetNearZ();
    mainPassCB.FarZ = camera->GetFarZ();
    mainPassCB.TotalTime = gt.TotalTime();
    mainPassCB.DeltaTime = gt.DeltaTime();
    mainPassCB.SsaaMultilpier = antiAliasingPrimePath->GetMultiplier();
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

void HybridAtmosphereApp::UpdateSsaoCB(const GameTimer& gt) const
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
    }
}

bool HybridAtmosphereApp::InitMainWindow()
{
    MainWindow = CreateRenderWindow(primeDevice, mainWindowCaption, 1920, 1080, false);

    debugLogger.PushMessage(std::wstring(L"\nInit Window"));
    return true;
}

void HybridAtmosphereApp::OnResize()
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

    if (skyAtmosphere != nullptr)
    {
        skyAtmosphere->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    }

    UIPath->CreateDeviceObject();

    currentFrameResourceIndex = MainWindow->GetCurrentBackBufferIndex();
}

void HybridAtmosphereApp::Flush()
{
    primeDevice->Flush();
    secondDevice->Flush();
}

LRESULT HybridAtmosphereApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
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
                IsUsingSharedAtmosphere = !IsUsingSharedAtmosphere;
                Flush();
            }

            if (keycode == (VK_F2) && keyboard.KeyIsPressed(VK_F2))
            {
                pathMapShow = (pathMapShow + 1) % maxPathMap;
            }

            if (keycode == (VK_F3) && keyboard.KeyIsPressed(VK_F3))
            {
                IsSharedTerrain = !IsSharedTerrain;
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

void HybridAtmosphereApp::LoadCustomShaders()
{
    // mCustomAppShaders = std::unordered_map<std::string, std::shared_ptr<GShader>>();

    LoadAtmospherePostProcessShader();

    for (auto&& pair : mCustomAppShaders)
    {
        pair.second->LoadAndCompile();
    }
}

void HybridAtmosphereApp::LoadCustomPSOs(std::shared_ptr<GDevice> device, std::shared_ptr<GRootSignature> rootSignature,
    D3D12_INPUT_LAYOUT_DESC defautlInputDesc, DXGI_FORMAT backBufferFormat,
    DXGI_FORMAT depthStencilFormat)
{
    // mCustomAppPSOs = std::unordered_map<RenderMode, std::shared_ptr<GraphicPSO>>();

	LoadAtmospherePostProcessPSO(rootSignature,
		defautlInputDesc, backBufferFormat, depthStencilFormat);

    for (auto& pso : mCustomAppPSOs)
    {
        pso.second->Initialize(device);
    }
}

void HybridAtmosphereApp::LoadCustomTextures()
{
    // Use default textures for now
    
}

void HybridAtmosphereApp::LoadCustomMaterials()
{
    
}

void HybridAtmosphereApp::CreateCustomGO()
{
    CreateAtmosphereGO();
}

void HybridAtmosphereApp::LoadAtmospherePostProcessShader()
{
	std::vector<std::string> dirs{
	"Shaders"
	};

	mCustomAppShaders["AtmospherePostVS"] = std::move(
		std::make_shared<GShaderCustomInclude>(1u, dirs,
			L"Shaders\\SkyAtmosphere\\PostProcess.hlsl", VertexShader, nullptr, "PostProcessVS", "vs_5_1"));

	mCustomAppShaders["AtmospherePostPS"] = std::move(
		std::make_shared<GShaderCustomInclude>(1u, dirs,
            L"Shaders\\SkyAtmosphere\\PostProcess.hlsl", PixelShader, nullptr, "PostProcessPS", "ps_5_1"));
}

void HybridAtmosphereApp::LoadAtmospherePostProcessPSO(std::shared_ptr<GRootSignature> rootSignature,
    D3D12_INPUT_LAYOUT_DESC defautlInputDesc, DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthStencilFormat)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC postProcPsoDesc;

	ZeroMemory(&postProcPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	postProcPsoDesc.InputLayout = defautlInputDesc;
	postProcPsoDesc.pRootSignature = rootSignature->GetNativeSignature().Get();
	postProcPsoDesc.VS = mCustomAppShaders["AtmospherePostVS"]->GetShaderResource();
	postProcPsoDesc.PS = mCustomAppShaders["AtmospherePostPS"]->GetShaderResource();
	postProcPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	postProcPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	// postProcPsoDesc.RasterizerState.DepthClipEnable = false;
	postProcPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	postProcPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	postProcPsoDesc.SampleMask = UINT_MAX;
	postProcPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	postProcPsoDesc.NumRenderTargets = 1;
	postProcPsoDesc.RTVFormats[0] = GetSRGBFormat(backBufferFormat);
	postProcPsoDesc.SampleDesc.Count = 1;
	postProcPsoDesc.SampleDesc.Quality = 0;
	postProcPsoDesc.DSVFormat = depthStencilFormat;

	auto depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

	auto postProcPSO = std::make_shared<GraphicPSO>(RenderMode::AtmospherePostProcess);
	postProcPSO->SetPsoDesc(postProcPsoDesc);
	depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
	postProcPSO->SetDepthStencilState(depthStencilDesc);

	mCustomAppPSOs[postProcPSO->GetRenderMode()] = std::move(postProcPSO);
}

void HybridAtmosphereApp::CreateAtmosphereGO()
{
	skyAtmosphere = std::make_shared<Atmosphere::SkyAtmosphere>();
    skyAtmosphere->Initialize(primeDevice, secondDevice);
	gFrameId = 0u;

    auto atmospherePost = std::make_unique<GameObject>();
    auto renderer = std::make_shared<SkyAtmosphereRenderer>(
        primeDevice,
        models[L"quad"],
        skyAtmosphere->GetPrimeResources().GetRayMarchingResultSRV()
    );
    atmospherePost->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::AtmospherePostProcess)].push_back(renderer);
    gameObjects.push_back(std::move(atmospherePost));
}
