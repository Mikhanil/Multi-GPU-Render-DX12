#include "SampleApp.h"
#include <DirectXTex.h>
#include <ppl.h>
#include "AssetsLoader.h"
#include "Camera.h"
#include "CameraController.h"
#include "d3dx12.h"
#include "GameObject.h"
#include "GCommandList.h"
#include "GCommandQueue.h"
#include "GDevice.h"
#include "GDeviceFactory.h"
#include "GDescriptor.h"
#include "GResourceStateTracker.h"
#include "ModelRenderer.h"
#include "ObjectMover.h"
#include "Renderer.h"
#include "Rotater.h"
#include "SkyBox.h"
#include "Transform.h"
#include "Window.h"
#include <array>

using namespace DirectX::SimpleMath;

namespace Common
{
    SampleApp::SampleApp(const HINSTANCE hInstance)
        : D3DApp(hInstance), loader(AssetsLoader(GDeviceFactory::GetDevice()))
    {
        mSceneBounds.Center = Vector3(0.0f, 0.0f, 0.0f);
        mSceneBounds.Radius = 175;

        for (int i = 0; i < static_cast<uint8_t>(RenderMode::Count); ++i)
        {
            typedGameObjects.push_back(MemoryAllocator::CreateVector<GameObject*>());
        }
    }

    SampleApp::~SampleApp()
    {
        Flush();
    }

    void SampleApp::GeneratedMipMap()
    {
        std::vector<GTexture*> generatedMipTextures;

        auto textures = loader.GetTextures();

        for (auto&& texture : textures)
        {
            texture->ClearTrack();

            /*������ �� ��� ����� ������������ ��� UAV*/
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

    bool SampleApp::Initialize()
    {
        if (!D3DApp::Initialize())
            return false;

        auto commandQueue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Graphics);
        auto cmdList = commandQueue->GetCommandList();

        shadowMap = std::make_unique<ShadowMap>(GDeviceFactory::GetDevice(), 4096, 4096);

        ssao = std::make_unique<SSAO>(
            GDeviceFactory::GetDevice(),
            cmdList,
            MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

        ssaa = std::make_unique<SSAA>(GDeviceFactory::GetDevice(), 1, MainWindow->GetClientWidth(),
                                      MainWindow->GetClientHeight());
        ssaa->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

        commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));

        LoadTextures(cmdList);
        LoadModels();
        GeneratedMipMap();
        
        commandQueue->Flush();
        commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));
        GDeviceFactory::GetDevice()->Flush();
        
        BuildTexturesHeap();
        BuildShadersAndInputLayout();
        BuildRootSignature();
        BuildSsaoRootSignature();
        BuildShapeGeometry();
        BuildPSOs();
        BuildMaterials();
        BuildGameObjects();
        BuildFrameResources();
        SortGO();

        ssao->SetPipelineData(*psos[RenderMode::Ssao], *psos[RenderMode::SsaoBlur]);
        loader.ClearTrackedObjects();
        
        GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Graphics)->SignalWithNewFenceValue(1000);
        GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Compute)->SignalWithNewFenceValue(1000);
        GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Copy)->SignalWithNewFenceValue(1000);
        GDeviceFactory::GetDevice()->Flush();
        
        return true;
    }

    LRESULT SampleApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
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
                        pathMapShow = (pathMapShow + 1) % maxPathMap;
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

    void SampleApp::OnResize()
    {
        D3DApp::OnResize();

        viewport.Height = static_cast<float>(MainWindow->GetClientHeight());
        viewport.Width = static_cast<float>(MainWindow->GetClientWidth());
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        rect = {0, 0, MainWindow->GetClientWidth(), MainWindow->GetClientHeight()};

        if (renderTargetMemory.IsNull())
        {
            renderTargetMemory = GDeviceFactory::GetDevice()->AllocateDescriptors(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV, globalCountFrameResources);
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = GetSRGBFormat(backBufferFormat);
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        for (int i = 0; i < globalCountFrameResources; ++i)
        {
            MainWindow->GetBackBuffer(i).CreateRenderTargetView(&rtvDesc, &renderTargetMemory, i);
        }


        if (camera != nullptr)
        {
            camera->SetAspectRatio(AspectRatio());
        }

        if (ssao != nullptr)
        {
            ssao->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
            ssao->RebuildDescriptors();
        }

        if (ssaa != nullptr)
        {
            ssaa->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
        }
    }

    void SampleApp::Update(const GameTimer& gt)
    {
        auto commandQueue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Graphics);

        // Cycle through the circular frame resource array.
        currentFrameResourceIndex = (currentFrameResourceIndex + 1) % globalCountFrameResources;
        currentFrameResource = frameResources[currentFrameResourceIndex].get();

        // Has the GPU finished processing the commands of the current frame resource?
        // If not, wait until the GPU has completed commands up to this fence point.
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


        UpdateGameObjects(gt);
        UpdateMaterial(gt);
        UpdateShadowTransform(gt);
        UpdateMainPassCB(gt);
        UpdateShadowPassCB(gt);
        UpdateSsaoCB(gt);
    }

    void SampleApp::DrawSSAAMap(const std::shared_ptr<GCommandList>& cmdList)
    {
        cmdList->SetViewports(&ssaa->GetViewPort(), 1);
        cmdList->SetScissorRects(&ssaa->GetRect(), 1);

        cmdList->TransitionBarrier((ssaa->GetRenderTarget()), D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(ssaa->GetDepthMap(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->FlushResourceBarriers();

        cmdList->ClearRenderTarget(ssaa->GetRTV());
        cmdList->ClearDepthStencil(ssaa->GetDSV(), 0,
                                   D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

        cmdList->SetRenderTargets(1, ssaa->GetRTV(), 0,
                                  ssaa->GetDSV());

        cmdList->SetRootSignature(*rootSignature.get());
        cmdList->SetDescriptorsHeap(shadowMap->GetSrv());
        cmdList->SetDescriptorsHeap(ssao->AmbientMapSrv());
        cmdList->SetDescriptorsHeap(&srvHeap);
        cmdList->UpdateDescriptorHeaps();
        
        //Main Path Data
        cmdList->
            SetRootConstantBufferView(StandardShaderSlot::CameraData, *currentFrameResource->PassConstantUploadBuffer);

        cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowMap->GetSrv());
        cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ssao->AmbientMapSrv(), 0);

        /*Bind all Diffuse Textures*/
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap,
                                        &srvHeap);


        cmdList->SetPipelineState(*psos[RenderMode::SkyBox]);
        DrawGameObjects(cmdList, typedGameObjects[static_cast<int>(RenderMode::SkyBox)]);

        cmdList->SetPipelineState(*psos[RenderMode::Opaque]);
        DrawGameObjects(cmdList, typedGameObjects[static_cast<int>(RenderMode::Opaque)]);

        cmdList->SetPipelineState(*psos[RenderMode::OpaqueAlphaDrop]);
        DrawGameObjects(cmdList, typedGameObjects[static_cast<int>(RenderMode::OpaqueAlphaDrop)]);

        cmdList->SetPipelineState(*psos[RenderMode::Transparent]);
        DrawGameObjects(cmdList, typedGameObjects[static_cast<int>(RenderMode::Transparent)]);

        switch (pathMapShow)
        {
        case 1:
            {
                cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, shadowMap->GetSrv());
                cmdList->SetPipelineState(*psos[RenderMode::Debug]);
                DrawGameObjects(cmdList, typedGameObjects[static_cast<int>(RenderMode::Debug)]);
                break;
            }
        case 2:
            {
                cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ssao->AmbientMapSrv(), 0);
                cmdList->SetPipelineState(*psos[RenderMode::Debug]);
                DrawGameObjects(cmdList, typedGameObjects[static_cast<int>(RenderMode::Debug)]);
                break;
            }
        }

        cmdList->TransitionBarrier(ssaa->GetRenderTarget(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier((ssaa->GetDepthMap()), D3D12_RESOURCE_STATE_DEPTH_READ);
        cmdList->FlushResourceBarriers();
    }

    void SampleApp::DrawToWindowBackBuffer(const std::shared_ptr<GCommandList>& cmdList)
    {
        cmdList->SetViewports(&viewport, 1);
        cmdList->SetScissorRects(&rect, 1);

        cmdList->TransitionBarrier((MainWindow->GetCurrentBackBuffer()), D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->FlushResourceBarriers();

        cmdList->ClearRenderTarget(&renderTargetMemory, MainWindow->GetCurrentBackBufferIndex());

        cmdList->SetRenderTargets(1, &renderTargetMemory, MainWindow->GetCurrentBackBufferIndex());

        cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ssaa->GetSRV());

        cmdList->SetPipelineState(*psos[RenderMode::Quad]);
        DrawGameObjects(cmdList, typedGameObjects[static_cast<int>(RenderMode::Quad)]);

        cmdList->TransitionBarrier((MainWindow->GetCurrentBackBuffer()), D3D12_RESOURCE_STATE_PRESENT);
        cmdList->FlushResourceBarriers();
    }

    void SampleApp::Draw(const GameTimer& gt)
    {
        if (isResizing) return;

        GDeviceFactory::GetDevice()->Flush();
        auto commandQueue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Graphics);

        auto cmdList = commandQueue->GetCommandList();

        commandQueue->StartPixEvent(L"Prepare Render 3D");
        cmdList->SetRootSignature(*rootSignature.get());
        cmdList->SetDescriptorsHeap(&srvHeap);
        /*Bind all materials*/
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData, *currentFrameResource->MaterialBuffer);
        /*Bind all Diffuse Textures*/
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvHeap);
        commandQueue->EndPixEvent();


        commandQueue->StartPixEvent(L"Render 3D");

        commandQueue->StartPixEvent(L"Shadow Map Pass");
        DrawSceneToShadowMap(cmdList);
        commandQueue->EndPixEvent();

        commandQueue->StartPixEvent(L"Normal and Depth Pass");
        DrawNormals(cmdList);
        commandQueue->EndPixEvent();

        commandQueue->StartPixEvent(L"Compute SSAO");
        ssao->ComputeSsao(cmdList, currentFrameResource->SsaoConstantUploadBuffer, 3);
        commandQueue->EndPixEvent();

        commandQueue->StartPixEvent(L"Main Pass");
        DrawSSAAMap(cmdList);
        DrawToWindowBackBuffer(cmdList);
        currentFrameResource->FenceValue = commandQueue->ExecuteCommandList(cmdList);
        commandQueue->EndPixEvent();
        commandQueue->EndPixEvent();

        backBufferIndex = MainWindow->Present();
    }

    void SampleApp::UpdateGameObjects(const GameTimer& gt)
    {
        const float dt = gt.DeltaTime();

        for (auto& e : gameObjects)
        {
            e->Update();
        }
    }

    void SampleApp::UpdateMaterial(const GameTimer& gt)
    {
        auto currentMaterialBuffer = currentFrameResource->MaterialBuffer.get();

        for (auto&& material : loader.GetMaterials())
        {
            material->Update();
            auto constantData = material->GetMaterialConstantData();
            currentMaterialBuffer->CopyData(material->GetIndex(), constantData);
        }
    }

    void SampleApp::UpdateShadowTransform(const GameTimer& gt)
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

    void SampleApp::UpdateShadowPassCB(const GameTimer& gt)
    {
        auto view = mLightView;
        auto proj = mLightProj;

        auto viewProj = (view * proj);
        auto invView = view.Invert();
        auto invProj = proj.Invert();
        auto invViewProj = viewProj.Invert();

        UINT w = shadowMap->Width();
        UINT h = shadowMap->Height();

        mShadowPassCB.View = view.Transpose();
        mShadowPassCB.InvView = invView.Transpose();
        mShadowPassCB.Proj = proj.Transpose();
        mShadowPassCB.InvProj = invProj.Transpose();
        mShadowPassCB.ViewProj = viewProj.Transpose();
        mShadowPassCB.InvViewProj = invViewProj.Transpose();
        mShadowPassCB.EyePosW = mLightPosW;
        mShadowPassCB.RenderTargetSize = Vector2(static_cast<float>(w), static_cast<float>(h));
        mShadowPassCB.InvRenderTargetSize = Vector2(1.0f / w, 1.0f / h);
        mShadowPassCB.NearZ = mLightNearZ;
        mShadowPassCB.FarZ = mLightFarZ;

        auto currPassCB = currentFrameResource->PassConstantUploadBuffer.get();
        currPassCB->CopyData(1, mShadowPassCB);
    }

    void SampleApp::UpdateMainPassCB(const GameTimer& gt)
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


        auto currentPassCB = currentFrameResource->PassConstantUploadBuffer.get();
        currentPassCB->CopyData(0, mainPassCB);
    }

    void SampleApp::UpdateSsaoCB(const GameTimer& gt)
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

        ssao->GetOffsetVectors(ssaoCB.OffsetVectors);

        auto blurWeights = ssao->CalcGaussWeights(2.5f);
        ssaoCB.BlurWeights[0] = Vector4(&blurWeights[0]);
        ssaoCB.BlurWeights[1] = Vector4(&blurWeights[4]);
        ssaoCB.BlurWeights[2] = Vector4(&blurWeights[8]);

        ssaoCB.InvRenderTargetSize = Vector2(1.0f / ssao->SsaoMapWidth(), 1.0f / ssao->SsaoMapHeight());

        // Coordinates given in view space.
        ssaoCB.OcclusionRadius = 0.5f;
        ssaoCB.OcclusionFadeStart = 0.2f;
        ssaoCB.OcclusionFadeEnd = 1.0f;
        ssaoCB.SurfaceEpsilon = 0.05f;

        auto currSsaoCB = currentFrameResource->SsaoConstantUploadBuffer.get();
        currSsaoCB->CopyData(0, ssaoCB);
    }

    void SampleApp::LoadStudyTexture(const std::shared_ptr<GCommandList>& cmdList)
    {
        auto bricksTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\bricks2.dds", cmdList);
        bricksTex->SetName(L"bricksTex");
        loader.AddTexture(bricksTex);

        auto stoneTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\stone.dds", cmdList);
        stoneTex->SetName(L"stoneTex");
        loader.AddTexture(stoneTex);

        auto tileTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\tile.dds", cmdList);
        tileTex->SetName(L"tileTex");
        loader.AddTexture(tileTex);

        auto fenceTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\WireFence.dds", cmdList);
        fenceTex->SetName(L"fenceTex");
        loader.AddTexture(fenceTex);

        auto waterTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\water1.dds", cmdList);
        waterTex->SetName(L"waterTex");
        loader.AddTexture(waterTex);

        auto skyTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\skymap.dds", cmdList);
        skyTex->SetName(L"skyTex");
        loader.AddTexture(skyTex);

        auto grassTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\grass.dds", cmdList);
        grassTex->SetName(L"grassTex");
        loader.AddTexture(grassTex);

        auto treeArrayTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\treeArray2.dds", cmdList);
        treeArrayTex->SetName(L"treeArrayTex");
        loader.AddTexture(treeArrayTex);

        auto seamless = GTexture::LoadTextureFromFile(L"Data\\Textures\\seamless_grass.jpg", cmdList);
        seamless->SetName(L"seamless");
        loader.AddTexture(seamless);


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

        for (int i = 0; i < texNormalNames.size(); ++i)
        {
            auto texture = GTexture::LoadTextureFromFile(texNormalFilenames[i], cmdList, TextureUsage::Normalmap);
            texture->SetName(texNormalNames[i]);
            loader.AddTexture(texture);
        }
    }

    void SampleApp::SetDefaultMaterialForModel(ModelRenderer* renderer)
    {
    }

    void SampleApp::LoadModels()
    {
        auto queue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Compute);
        auto cmd = queue->GetCommandList();

        auto nano = loader.CreateModelFromFile(cmd, "Data\\Objects\\Nanosuit\\Nanosuit.obj");


        models[L"nano"] = std::move(nano);


        auto doom = loader.CreateModelFromFile(cmd, "Data\\Objects\\DoomSlayer\\doommarine.obj");
        models[L"doom"] = std::move(doom);

        auto atlas = loader.CreateModelFromFile(cmd, "Data\\Objects\\Atlas\\Atlas.obj");
        models[L"atlas"] = std::move(atlas);

        auto pbody = loader.CreateModelFromFile(cmd, "Data\\Objects\\P-Body\\P-Body.obj");
        models[L"pbody"] = std::move(pbody);

        auto golem = loader.CreateModelFromFile(cmd, "Data\\Objects\\StoneGolem\\Stone.obj");
        models[L"golem"] = std::move(golem);

        auto griffon = loader.CreateModelFromFile(cmd, "Data\\Objects\\Griffon\\Griffon.FBX");
        griffon->scaleMatrix = Matrix::CreateScale(0.1);
        models[L"griffon"] = std::move(griffon);

        auto griffonOld = loader.CreateModelFromFile(cmd, "Data\\Objects\\GriffonOld\\Griffon.FBX");
        griffonOld->scaleMatrix = Matrix::CreateScale(0.1);
        models[L"griffonOld"] = std::move(griffonOld);

        auto mountDragon = loader.CreateModelFromFile(cmd, "Data\\Objects\\MOUNTAIN_DRAGON\\MOUNTAIN_DRAGON.FBX");
        mountDragon->scaleMatrix = Matrix::CreateScale(0.1);
        models[L"mountDragon"] = std::move(mountDragon);

        auto desertDragon = loader.CreateModelFromFile(cmd, "Data\\Objects\\DesertDragon\\DesertDragon.FBX");
        desertDragon->scaleMatrix = Matrix::CreateScale(0.1);
        models[L"desertDragon"] = std::move(desertDragon);

        auto stair = loader.CreateModelFromFile(cmd, "Data\\Objects\\Temple\\SM_AsianCastle_A.FBX");
        models[L"stair"] = std::move(stair);

        auto columns = loader.CreateModelFromFile(cmd, "Data\\Objects\\Temple\\SM_AsianCastle_E.FBX");
        models[L"columns"] = std::move(columns);

        auto fountain = loader.CreateModelFromFile(cmd, "Data\\Objects\\Temple\\SM_Fountain.FBX");
        models[L"fountain"] = std::move(fountain);

        auto platform = loader.CreateModelFromFile(cmd, "Data\\Objects\\Temple\\SM_PlatformSquare.FBX");
        models[L"platform"] = std::move(platform);

        queue->WaitForFenceValue(queue->ExecuteCommandList(cmd));
        queue->Flush();
    }

    void SampleApp::LoadTextures(std::shared_ptr<GCommandList> cmdList)
    {
        auto queue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Compute);

        auto graphicCmdList = queue->GetCommandList();
        LoadStudyTexture(graphicCmdList);
        queue->WaitForFenceValue(queue->ExecuteCommandList(graphicCmdList));
    }

    void SampleApp::BuildRootSignature()
    {
        rootSignature = std::make_unique<GRootSignature>();

        CD3DX12_DESCRIPTOR_RANGE texParam[4];
        texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0); //SkyMap
        texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0); //ShadowMap
        texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::AmbientMap - 3, 0); //SsaoMap
        texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, loader.GetLoadTexturesCount(),
                         StandardShaderSlot::TexturesMap - 3, 0);


        rootSignature->AddConstantBufferParameter(0);
        rootSignature->AddConstantBufferParameter(1);
        rootSignature->AddShaderResourceView(0, 1);
        rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);

        rootSignature->Initialize(GDeviceFactory::GetDevice());
    }

    void SampleApp::BuildSsaoRootSignature()
    {
        ssaoRootSignature = std::make_unique<GRootSignature>();

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

        ssaoRootSignature->Initialize(GDeviceFactory::GetDevice());
    }

    void SampleApp::BuildTexturesHeap()
    {
        srvHeap = std::move(
            GDeviceFactory::GetDevice(GraphicAdapterPrimary)->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                             loader.GetLoadTexturesCount()));

        ssao->BuildDescriptors();
    }

    void SampleApp::BuildShadersAndInputLayout()
    {
        constexpr D3D_SHADER_MACRO defines[] =
        {
            "FOG", "1",
            nullptr, nullptr
        };

        constexpr D3D_SHADER_MACRO alphaTestDefines[] =
        {
            "FOG", "1",
            "ALPHA_TEST", "1",
            nullptr, nullptr
        };

        shaders["StandardVertex"] = std::move(
            std::make_unique<GShader>(L"Shaders\\Default.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
        shaders["AlphaDrop"] = std::move(
            std::make_unique<GShader>(L"Shaders\\Default.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));
        shaders["shadowVS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\Shadows.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
        shaders["shadowOpaquePS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\Shadows.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));
        shaders["shadowOpaqueDropPS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\Shadows.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));
        shaders["OpaquePixel"] = std::move(
            std::make_unique<GShader>(L"Shaders\\Default.hlsl", PixelShader, defines, "PS", "ps_5_1"));
        shaders["SkyBoxVertex"] = std::move(
            std::make_unique<GShader>(L"Shaders\\SkyBoxShader.hlsl", VertexShader, defines, "SKYMAP_VS", "vs_5_1"));
        shaders["SkyBoxPixel"] = std::move(
            std::make_unique<GShader>(L"Shaders\\SkyBoxShader.hlsl", PixelShader, defines, "SKYMAP_PS", "ps_5_1"));

        shaders["treeSpriteVS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\TreeSprite.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
        shaders["treeSpriteGS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\TreeSprite.hlsl", GeometryShader, nullptr, "GS", "gs_5_1"));
        shaders["treeSpritePS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\TreeSprite.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));


        shaders["drawNormalsVS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\DrawNormals.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
        shaders["drawNormalsPS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\DrawNormals.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));
        shaders["drawNormalsAlphaDropPS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\DrawNormals.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));


        shaders["ssaoVS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\Ssao.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
        shaders["ssaoPS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\Ssao.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

        shaders["ssaoBlurVS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\SsaoBlur.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
        shaders["ssaoBlurPS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\SsaoBlur.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

        shaders["quadVS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\Quad.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
        shaders["quadPS"] = std::move(
            std::make_unique<GShader>(L"Shaders\\Quad.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

        for (auto&& pair : shaders)
        {
            pair.second->LoadAndCompile();
        }


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

        treeSpriteInputLayout =
        {
            {
                "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
            {
                "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
        };
    }

    void SampleApp::BuildShapeGeometry()
    {
        auto queue = GDeviceFactory::GetDevice()->GetCommandQueue();
        auto cmdList = queue->GetCommandList();

        auto sphere = loader.GenerateSphere(cmdList);
        models[L"sphere"] = std::move(sphere);

        auto quad = loader.GenerateQuad(cmdList);
        models[L"quad"] = std::move(quad);

        queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
    }

    void SampleApp::BuildPSOs()
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;

        ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
        basePsoDesc.InputLayout = {defaultInputLayout.data(), static_cast<UINT>(defaultInputLayout.size())};
        basePsoDesc.pRootSignature = rootSignature->GetNativeSignature().Get();
        basePsoDesc.VS = shaders["StandardVertex"]->GetShaderResource();
        basePsoDesc.PS = shaders["OpaquePixel"]->GetShaderResource();
        basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        basePsoDesc.SampleMask = UINT_MAX;
        basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        basePsoDesc.NumRenderTargets = 1;
        basePsoDesc.RTVFormats[0] = GetSRGBFormat(backBufferFormat);
        basePsoDesc.SampleDesc.Count = isM4xMsaa ? 4 : 1;
        basePsoDesc.SampleDesc.Quality = isM4xMsaa ? (m4xMsaaQuality - 1) : 0;
        basePsoDesc.DSVFormat = depthStencilFormat;

        auto depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        auto rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);


        auto opaquePSO = std::make_unique<GraphicPSO>();
        opaquePSO->SetPsoDesc(basePsoDesc);
        depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        opaquePSO->SetDepthStencilState(depthStencilDesc);

        auto alphaDropPso = std::make_unique<GraphicPSO>(RenderMode::OpaqueAlphaDrop);
        alphaDropPso->SetPsoDesc(opaquePSO->GetPsoDescription());
        alphaDropPso->SetShader(shaders["AlphaDrop"].get());
        rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
        alphaDropPso->SetRasterizationState(rasterizedDesc);


        auto shadowMapPSO = std::make_unique<GraphicPSO>(RenderMode::ShadowMapOpaque);
        basePsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        shadowMapPSO->SetPsoDesc(basePsoDesc);
        shadowMapPSO->SetShader(shaders["shadowVS"].get());
        shadowMapPSO->SetShader(shaders["shadowOpaquePS"].get());
        shadowMapPSO->SetRTVFormat(0, DXGI_FORMAT_UNKNOWN);
        shadowMapPSO->SetRenderTargetsCount(0);

        rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        rasterizedDesc.DepthBias = 100000;
        rasterizedDesc.DepthBiasClamp = 0.0f;
        rasterizedDesc.SlopeScaledDepthBias = 1.0f;
        depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        depthStencilDesc.StencilEnable = false;
        depthStencilDesc.DepthEnable = true;
        shadowMapPSO->SetDepthStencilState(depthStencilDesc);
        shadowMapPSO->SetRasterizationState(rasterizedDesc);


        auto shadowMapDropPSO = std::make_unique<GraphicPSO>(RenderMode::ShadowMapOpaqueDrop);
        shadowMapDropPSO->SetPsoDesc(shadowMapPSO->GetPsoDescription());
        shadowMapDropPSO->SetShader(shaders["shadowOpaqueDropPS"].get());


        auto drawNormalsPso = std::make_unique<GraphicPSO>(RenderMode::DrawNormalsOpaque);
        basePsoDesc.DSVFormat = depthStencilFormat;
        drawNormalsPso->SetPsoDesc(basePsoDesc);
        drawNormalsPso->SetShader(shaders["drawNormalsVS"].get());
        drawNormalsPso->SetShader(shaders["drawNormalsPS"].get());
        drawNormalsPso->SetRTVFormat(0, SSAO::NormalMapFormat);
        drawNormalsPso->SetSampleCount(1);
        drawNormalsPso->SetSampleQuality(0);
        drawNormalsPso->SetDSVFormat(depthStencilFormat);

        auto drawNormalsDropPso = std::make_unique<GraphicPSO>(RenderMode::DrawNormalsOpaqueDrop);
        drawNormalsDropPso->SetPsoDesc(drawNormalsPso->GetPsoDescription());
        drawNormalsDropPso->SetShader(shaders["drawNormalsAlphaDropPS"].get());
        rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
        drawNormalsDropPso->SetRasterizationState(rasterizedDesc);

        auto ssaoPSO = std::make_unique<GraphicPSO>(RenderMode::Ssao);
        ssaoPSO->SetPsoDesc(basePsoDesc);
        ssaoPSO->SetInputLayout({nullptr, 0});
        ssaoPSO->SetRootSignature(*ssaoRootSignature.get());
        ssaoPSO->SetShader(shaders["ssaoVS"].get());
        ssaoPSO->SetShader(shaders["ssaoPS"].get());
        ssaoPSO->SetRTVFormat(0, SSAO::AmbientMapFormat);
        ssaoPSO->SetSampleCount(1);
        ssaoPSO->SetSampleQuality(0);
        ssaoPSO->SetDSVFormat(DXGI_FORMAT_UNKNOWN);
        depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        depthStencilDesc.DepthEnable = false;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        ssaoPSO->SetDepthStencilState(depthStencilDesc);


        auto ssaoBlurPSO = std::make_unique<GraphicPSO>(RenderMode::SsaoBlur);
        ssaoBlurPSO->SetPsoDesc(ssaoPSO->GetPsoDescription());
        ssaoBlurPSO->SetRootSignature(*ssaoRootSignature.get());
        ssaoBlurPSO->SetShader(shaders["ssaoBlurVS"].get());
        ssaoBlurPSO->SetShader(shaders["ssaoBlurPS"].get());

        auto skyBoxPSO = std::make_unique<GraphicPSO>(RenderMode::SkyBox);
        skyBoxPSO->SetPsoDesc(basePsoDesc);
        skyBoxPSO->SetShader(shaders["SkyBoxVertex"].get());
        skyBoxPSO->SetShader(shaders["SkyBoxPixel"].get());

        depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        skyBoxPSO->SetDepthStencilState(depthStencilDesc);
        rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
        skyBoxPSO->SetRasterizationState(rasterizedDesc);


        auto transperentPSO = std::make_unique<GraphicPSO>(RenderMode::Transparent);
        transperentPSO->SetPsoDesc(basePsoDesc);
        D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc = {};
        transparencyBlendDesc.BlendEnable = true;
        transparencyBlendDesc.LogicOpEnable = false;
        transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
        transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
        transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
        transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
        transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        transperentPSO->SetRenderTargetBlendState(0, transparencyBlendDesc);


        auto treeSprite = std::make_unique<GraphicPSO>(RenderMode::AlphaSprites);
        treeSprite->SetPsoDesc(basePsoDesc);
        treeSprite->SetShader(shaders["treeSpriteVS"].get());
        treeSprite->SetShader(shaders["treeSpriteGS"].get());
        treeSprite->SetShader(shaders["treeSpritePS"].get());
        treeSprite->SetInputLayout({treeSpriteInputLayout.data(), static_cast<UINT>(treeSpriteInputLayout.size())});
        treeSprite->SetPrimitiveType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
        rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
        treeSprite->SetRasterizationState(rasterizedDesc);

        auto debugPso = std::make_unique<GraphicPSO>(RenderMode::Debug);
        debugPso->SetPsoDesc(basePsoDesc);
        debugPso->SetShader(shaders["quadVS"].get());
        debugPso->SetShader(shaders["quadPS"].get());

        auto quadPso = std::make_unique<GraphicPSO>(RenderMode::Quad);
        quadPso->SetPsoDesc(basePsoDesc);
        quadPso->SetShader(shaders["quadVS"].get());
        quadPso->SetShader(shaders["quadPS"].get());
        quadPso->SetSampleCount(1);
        quadPso->SetSampleQuality(0);
        quadPso->SetDSVFormat(DXGI_FORMAT_UNKNOWN);
        depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        depthStencilDesc.DepthEnable = false;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        quadPso->SetDepthStencilState(depthStencilDesc);


        psos[opaquePSO->GetRenderMode()] = std::move(opaquePSO);
        psos[transperentPSO->GetRenderMode()] = std::move(transperentPSO);
        psos[alphaDropPso->GetRenderMode()] = std::move(alphaDropPso);
        psos[skyBoxPSO->GetRenderMode()] = std::move(skyBoxPSO);
        psos[treeSprite->GetRenderMode()] = std::move(treeSprite);
        psos[shadowMapPSO->GetRenderMode()] = std::move(shadowMapPSO);
        psos[shadowMapDropPSO->GetRenderMode()] = std::move(shadowMapDropPSO);
        psos[drawNormalsPso->GetRenderMode()] = std::move(drawNormalsPso);
        psos[drawNormalsDropPso->GetRenderMode()] = std::move(drawNormalsDropPso);
        psos[ssaoPSO->GetRenderMode()] = std::move(ssaoPSO);
        psos[ssaoBlurPSO->GetRenderMode()] = std::move(ssaoBlurPSO);
        psos[debugPso->GetRenderMode()] = std::move(debugPso);
        psos[quadPso->GetRenderMode()] = std::move(quadPso);

        for (auto& pso : psos)
        {
            pso.second->Initialize(GDeviceFactory::GetDevice());
        }
    }

    void SampleApp::BuildFrameResources()
    {
        for (int i = 0; i < globalCountFrameResources; ++i)
        {
            frameResources.push_back(
                std::make_unique<FrameResource>(GDeviceFactory::GetDevice(), 2, gameObjects.size(),
                                                loader.GetMaterials().size()));
        }        
    }

    void SampleApp::BuildMaterials()
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


        auto materials = loader.GetMaterials();

        for (auto pair : materials)
        {
            pair->InitMaterial(&srvHeap);
        }
    }

    void SampleApp::BuildGameObjects()
    {
        auto quadRitem = std::make_unique<GameObject>("Quad");
        auto renderer = std::make_shared<ModelRenderer>(GDeviceFactory::GetDevice(), models[L"quad"]);
        models[L"quad"]->SetMeshMaterial(0, loader.GetMaterial(loader.GetMaterialIndex(L"seamless")));
        quadRitem->AddComponent(renderer);
        typedGameObjects[(uint8_t)RenderMode::Debug].push_back(quadRitem.get());
        typedGameObjects[(uint8_t)RenderMode::Quad].push_back(quadRitem.get());
        gameObjects.push_back(std::move(quadRitem));

        auto skySphere = std::make_unique<GameObject>("Sky");
        skySphere->GetTransform()->SetScale({500, 500, 500});
        renderer = std::make_shared<SkyBox>(GDeviceFactory::GetDevice(), models[L"sphere"],
                                            *loader.GetTexture(loader.GetTextureIndex(L"skyTex")).get(), &srvHeap,
                                            loader.GetTextureIndex(L"skyTex"));
        models[L"sphere"]->SetMeshMaterial(0, loader.GetMaterial(loader.GetMaterialIndex(L"sky")));
        skySphere->AddComponent(renderer);
        typedGameObjects[(uint8_t)RenderMode::SkyBox].push_back(skySphere.get());
        gameObjects.push_back(std::move(skySphere));


        auto sun1 = std::make_unique<GameObject>("Directional Light");
        auto light = std::make_shared<Light>(Directional);
        light->Direction({0.57735f, -0.57735f, 0.57735f});
        light->Strength({0.8f, 0.8f, 0.8f});
        sun1->AddComponent(light);
        gameObjects.push_back(std::move(sun1));

        auto sun2 = std::make_unique<GameObject>();
        light = std::make_shared<Light>();
        light->Direction({-0.57735f, -0.57735f, 0.57735f});
        light->Strength({0.4f, 0.4f, 0.4f});
        sun2->AddComponent(light);
        gameObjects.push_back(std::move(sun2));

        auto sun3 = std::make_unique<GameObject>();
        light = std::make_shared<Light>();
        light->Direction({0.0f, -0.707f, -0.707f});
        light->Strength({0.2f, 0.2f, 0.2f});
        sun3->AddComponent(light);
        gameObjects.push_back(std::move(sun3));


        for (int i = 0; i < 11; ++i)
        {
            auto nano = CreateGOWithRenderer(models[L"nano"]);
            nano->GetTransform()->SetPosition(Vector3::Right * -15 + Vector3::Forward * 12 * i);
            nano->GetTransform()->SetEulerRotate(Vector3(0, -90, 0));

            typedGameObjects[(uint8_t)RenderMode::Opaque].push_back(nano.get());
            gameObjects.push_back(std::move(nano));


            auto doom = CreateGOWithRenderer(models[L"doom"]);
            doom->SetScale(0.08);
            doom->GetTransform()->SetPosition(Vector3::Right * 15 + Vector3::Forward * 12 * i);
            doom->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));

            typedGameObjects[(uint8_t)RenderMode::Opaque].push_back(doom.get());
            gameObjects.push_back(std::move(doom));
        }

        for (int i = 0; i < 12; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                auto atlas = CreateGOWithRenderer(models[L"atlas"]);
                atlas->GetTransform()->SetPosition(
                    Vector3::Right * -60 + Vector3::Right * -30 * j + Vector3::Up * 11 + Vector3::Forward * 10 * i);
                typedGameObjects[(uint8_t)RenderMode::Opaque].push_back(atlas.get());
                gameObjects.push_back(std::move(atlas));


                auto pbody = CreateGOWithRenderer(models[L"pbody"]);
                pbody->GetTransform()->SetPosition(
                    Vector3::Right * 130 + Vector3::Right * -30 * j + Vector3::Up * 11 + Vector3::Forward * 10 * i);
                typedGameObjects[(uint8_t)RenderMode::Opaque].push_back(pbody.get());
                gameObjects.push_back(std::move(pbody));
            }
        }


        auto platform = CreateGOWithRenderer(models[L"platform"]);
        platform->SetScale(0.2);
        platform->GetTransform()->SetEulerRotate(Vector3(90, 90, 0));
        platform->GetTransform()->SetPosition(Vector3::Backward * -130);
        typedGameObjects[(uint8_t)RenderMode::Opaque].push_back(platform.get());

        auto rotater = std::make_unique<GameObject>();
        rotater->GetTransform()->SetParent(platform->GetTransform().get());
        rotater->GetTransform()->SetPosition(Vector3::Forward * 325 + Vector3::Left * 625);
        rotater->GetTransform()->SetEulerRotate(Vector3(0, -90, 90));
        //rotater->AddComponent(new Rotater(10));	


        auto camera = std::make_unique<GameObject>("MainCamera");
        camera->GetTransform()->SetParent(rotater->GetTransform().get());
        camera->GetTransform()->SetEulerRotate(Vector3(-30, 270, 0));
        camera->GetTransform()->SetPosition(Vector3(-1000, 190, -32));
        camera->AddComponent(std::make_shared<Camera>(AspectRatio()));
        camera->AddComponent(std::make_shared<CameraController>());

        gameObjects.push_back(std::move(camera));
        gameObjects.push_back(std::move(rotater));


        auto stair = CreateGOWithRenderer(models[L"stair"]);
        stair->GetTransform()->SetParent(platform->GetTransform().get());
        stair->SetScale(0.2);
        stair->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
        stair->GetTransform()->SetPosition(Vector3::Left * 700);
        typedGameObjects[(uint8_t)RenderMode::Opaque].push_back(stair.get());


        auto columns = CreateGOWithRenderer(models[L"columns"]);
        columns->GetTransform()->SetParent(stair->GetTransform().get());
        columns->SetScale(0.8);
        columns->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
        columns->GetTransform()->SetPosition(Vector3::Up * 2000 + Vector3::Forward * 900);
        typedGameObjects[(uint8_t)RenderMode::Opaque].push_back(columns.get());

        gameObjects.push_back(std::move(columns));
        gameObjects.push_back(std::move(stair));
        gameObjects.push_back(std::move(platform));

        auto fountain = CreateGOWithRenderer(models[L"fountain"]);
        fountain->SetScale(0.005);
        fountain->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
        fountain->GetTransform()->SetPosition(Vector3::Up * 35 + Vector3::Backward * 77);
        typedGameObjects[(uint8_t)RenderMode::Opaque].push_back(fountain.get());
        gameObjects.push_back(std::move(fountain));

        auto mountDragon = CreateGOWithRenderer(models[L"mountDragon"]);
        mountDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
        mountDragon->GetTransform()->SetPosition(Vector3::Right * -960 + Vector3::Up * 45 + Vector3::Backward * 775);


        typedGameObjects[(uint8_t)RenderMode::Opaque].push_back(mountDragon.get());
        gameObjects.push_back(std::move(mountDragon));


        auto desertDragon = CreateGOWithRenderer(models[L"desertDragon"]);
        desertDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
        desertDragon->GetTransform()->SetPosition(Vector3::Right * 960 + Vector3::Up * -5 + Vector3::Backward * 775);


        typedGameObjects[(uint8_t)RenderMode::Opaque].push_back(desertDragon.get());
        gameObjects.push_back(std::move(desertDragon));

        auto griffon = CreateGOWithRenderer(models[L"griffon"]);
        griffon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
        griffon->SetScale(0.8);
        griffon->GetTransform()->SetPosition(Vector3::Right * -355 + Vector3::Up * -7 + Vector3::Backward * 17);


        typedGameObjects[(uint8_t)RenderMode::OpaqueAlphaDrop].push_back(griffon.get());
        gameObjects.push_back(std::move(griffon));

        auto griffon1 = CreateGOWithRenderer(models[L"griffonOld"]);
        griffon1->SetScale(0.8);
        griffon1->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
        griffon1->GetTransform()->SetPosition(Vector3::Right * 355 + Vector3::Up * -7 + Vector3::Backward * 17);

        typedGameObjects[(uint8_t)RenderMode::OpaqueAlphaDrop].push_back(griffon1.get());
        gameObjects.push_back(std::move(griffon1));
    }

    std::unique_ptr<GameObject> SampleApp::CreateGOWithRenderer(std::shared_ptr<GModel> model)
    {
        auto man = std::make_unique<GameObject>();
        auto renderer = std::make_shared<ModelRenderer>(GDeviceFactory::GetDevice(), model);
        man->AddComponent(renderer);
        return man;
    }

    void SampleApp::DrawGameObjects(const std::shared_ptr<GCommandList>& cmdList, const custom_vector<GameObject*>& ritems)
    {
        // For each render item...
        for (auto& ri : ritems)
        {
            ri->Draw(cmdList);
        }
    }

    void SampleApp::DrawSceneToShadowMap(std::shared_ptr<GCommandList> cmdList)
    {
        //Shadow Path Data
        cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                           *currentFrameResource->PassConstantUploadBuffer, 1);

        shadowMap->PopulatePreRenderCommands(cmdList);

        cmdList->SetPipelineState(*psos[RenderMode::ShadowMapOpaque]);
        DrawGameObjects(cmdList, typedGameObjects[(uint8_t)RenderMode::Opaque]);

        cmdList->SetPipelineState(*psos[RenderMode::ShadowMapOpaqueDrop]);
        DrawGameObjects(cmdList, typedGameObjects[(uint8_t)RenderMode::OpaqueAlphaDrop]);

        cmdList->TransitionBarrier(shadowMap->GetTexture(), D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }

    void SampleApp::DrawNormals(const std::shared_ptr<GCommandList>& cmdList)
    {
        cmdList->SetViewports(&viewport, 1);
        cmdList->SetScissorRects(&rect, 1);

        auto normalMap = ssao->NormalMap();
        auto normalDepthMap = ssao->NormalDepthMap();
        auto normalMapRtv = ssao->NormalMapRtv();
        auto normalMapDsv = ssao->NormalMapDSV();

        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        cmdList->FlushResourceBarriers();

        float clearValue[] = {0.0f, 0.0f, 1.0f, 0.0f};
        cmdList->ClearRenderTarget(normalMapRtv, 0, clearValue);
        cmdList->ClearDepthStencil(normalMapDsv, 0,
                                   D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

        cmdList->SetRenderTargets(1, normalMapRtv, 0, normalMapDsv);

        //Main Path Data
        cmdList->SetRootConstantBufferView(1, *currentFrameResource->PassConstantUploadBuffer);

        cmdList->SetPipelineState(*psos[RenderMode::DrawNormalsOpaque]);
        DrawGameObjects(cmdList, typedGameObjects[(uint8_t)RenderMode::Opaque]);
        cmdList->SetPipelineState(*psos[RenderMode::DrawNormalsOpaqueDrop]);
        DrawGameObjects(cmdList, typedGameObjects[(uint8_t)RenderMode::OpaqueAlphaDrop]);


        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }

    void SampleApp::SortGO()
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

        std::sort(lights.begin(), lights.end(), [](const Light* a, const Light* b) { return a->Type() < b->Type(); });
    }
}
