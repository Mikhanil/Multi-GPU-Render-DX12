#include "ReflectionRenderer.h"

#include "Camera.h"
#include "d3dUtil.h"
#include "d3dx12.h"
#include "GameObject.h"
#include "GCommandList.h"
#include "Rotater.h"
#include "SkyBox.h"
#include "Transform.h"
#include "Window.h"

using namespace DirectX::SimpleMath;
using namespace PEPEngine;
using namespace Utils;
using namespace Graphics;

namespace
{
constexpr UINT kPointLightsSlot = StandardShaderSlot::Count;
constexpr UINT kSpotLightsSlot = StandardShaderSlot::Count + 1;
}

ReflectionRenderer::ReflectionRenderer(std::shared_ptr<Common::Window> windowValue,
                                       std::vector<std::shared_ptr<GDevice>>& devicesValue,
                                       std::vector<std::unique_ptr<Common::Scene>>& scenesValue,
                                       const DXGI_FORMAT backBufferFormatValue,
                                       const DXGI_FORMAT depthStencilFormatValue)
    : window(std::move(windowValue)), devices(devicesValue), scenes(scenesValue),
      backBufferFormat(backBufferFormatValue), depthStencilFormat(depthStencilFormatValue) {}

void ReflectionRenderer::Initialize()
{
    mSceneBounds = scenes[GraphicAdapterPrimary]->GetBounds();
    hasSecondaryAdapter = devices.size() == ReflectionAdapterCount && scenes.size() == ReflectionAdapterCount;
    if (hasSecondaryAdapter)
        devices[GraphicAdapterPrimary]->SharedFence(primeFence, devices[GraphicAdapterSecond], secondFence, sharedFenceValue);
    InitRenderPaths(); Flush(); InitRootSignature(); Flush(); InitPipeLineResource(); Flush(); InitFrameResource(); Flush();
}

void ReflectionRenderer::ResetBenchmarkAnimation()
{
    mLightRotationAngle = 0.0f;
    for (int i = 0; i < 3; ++i)
        mRotatedLightDirections[i] = mBaseLightDirections[i];
}

void ReflectionRenderer::SetSsaaMultiplier(const UINT value)
{
    multi = value;
    if (antiAliasingPrimePath) antiAliasingPrimePath->SetMultiplier(multi, window->GetClientWidth(), window->GetClientHeight());
}

void ReflectionRenderer::OnResize(const float aspectRatio)
{
    fullViewport = { 0.0f, 0.0f, static_cast<float>(window->GetClientWidth()),
                     static_cast<float>(window->GetClientHeight()), 0.0f, 1.0f };
    fullRect = { 0, 0, window->GetClientWidth(), window->GetClientHeight() };

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = GetSRGBFormat(backBufferFormat);
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    for (int i = 0; i < globalCountFrameResources; ++i)
        window->GetBackBuffer(i).CreateRenderTargetView(&rtvDesc, &frameResources[i]->BackBufferRTVMemory);

    scenes[GraphicAdapterPrimary]->GetCamera()->SetAspectRatio(aspectRatio);
    if (ambientPrimePath)
    {
        ambientPrimePath->OnResize(window->GetClientWidth(), window->GetClientHeight());
        ambientPrimePath->RebuildDescriptors();
    }
    if (antiAliasingPrimePath)
        antiAliasingPrimePath->OnResize(window->GetClientWidth(), window->GetClientHeight());
    currentFrameResourceIndex = window->GetCurrentBackBufferIndex();
}

void ReflectionRenderer::Flush()
{
    for (const auto& device : devices)
        if (device) device->Flush();
}

void ReflectionRenderer::InitFrameResource()
{
    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<FrameResource>(
            devices[GraphicAdapterPrimary], hasSecondaryAdapter ? devices[GraphicAdapterSecond] : nullptr, 8,
            static_cast<UINT>(scenes[GraphicAdapterPrimary]->GetMaterialCount()),
            std::max(scenes[GraphicAdapterPrimary]->GetLightCount(Point),
                     hasSecondaryAdapter ? scenes[GraphicAdapterSecond]->GetLightCount(Point) : 0),
            std::max(scenes[GraphicAdapterPrimary]->GetLightCount(Spot),
                     hasSecondaryAdapter ? scenes[GraphicAdapterSecond]->GetLightCount(Spot) : 0)));
    }
}

void ReflectionRenderer::InitRootSignature()
{
    for (UINT i = 0; i < devices.size(); ++i)
    {
        auto rootSignature = std::make_shared<GRootSignature>();
        CD3DX12_DESCRIPTOR_RANGE texParam[4];
        texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0); //SkyMap
        texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0); //ShadowMap
        texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::AmbientMap - 3, 0); //SsaoMap
        texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                         static_cast<UINT>(std::max<size_t>(scenes[i]->GetTextureCount(), 1)),
                         StandardShaderSlot::TexturesMap - 3, 0);


        rootSignature->AddConstantBufferParameter(0);
        rootSignature->AddConstantBufferParameter(1);
        rootSignature->AddShaderResourceView(0, 1);
        rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddShaderResourceView(1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->AddShaderResourceView(2, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootSignature->Initialize(devices[i]);

        if (i == GraphicAdapterPrimary)
        {
            primeDeviceSignature = rootSignature;
        }
        else
        {
            secondDeviceSignature = rootSignature;
        }

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

void ReflectionRenderer::InitPipeLineResource()
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
                                          backBufferFormat, depthStencilFormat, ssaoPrimeRootSignature,
                                          NormalMapFormat, AmbientMapFormat);

    if (hasSecondaryAdapter)
    {
        secondPipelineResources = RenderModeFactory();
        secondPipelineResources.LoadDefaultShaders();
        secondPipelineResources.LoadDefaultPSO(devices[GraphicAdapterSecond], secondDeviceSignature, desc,
                                               backBufferFormat, depthStencilFormat, nullptr,
                                               NormalMapFormat, AmbientMapFormat);
    }

    ambientPrimePath->SetPipelineData(*primePipelineResources.GetPSO(RenderMode::Ssao),
                                      *primePipelineResources.GetPSO(RenderMode::SsaoBlur));



}

void ReflectionRenderer::InitRenderPaths()
{
    auto commandQueue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Graphics);
    auto cmdList = commandQueue->GetCommandList();

    ambientPrimePath = (std::make_shared<SSAO>(
        devices[GraphicAdapterPrimary],
        cmdList,
        window->GetClientWidth(), window->GetClientHeight()));

    antiAliasingPrimePath = (std::make_shared<SSAA>(devices[GraphicAdapterPrimary], multi, window->GetClientWidth(),
                                                    window->GetClientHeight()));
    antiAliasingPrimePath->OnResize(window->GetClientWidth(), window->GetClientHeight());

    commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));


    shadowPathPrimeDevice = (std::make_shared<ShadowMap>(devices[GraphicAdapterPrimary], 2048, 2048));

    dynamicCubeMap = std::make_shared<CubeMapRenderTarget>(
        devices[GraphicAdapterPrimary], DynamicCubeMapSize, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT);

    if (hasSecondaryAdapter)
    {
        bakedCubeMapSecond = std::make_shared<BakedCubeMapRenderTarget>(
            devices[GraphicAdapterSecond], BakedCubeMapSize, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT);
        CreateDynamicTextures(GraphicAdapterSecond);

        // cubeMapDesc.DepthOrArraySize = 1;
        auto desc = dynamicCubeMap->GetCubeMap().GetD3D12ResourceDesc();
        desc.DepthOrArraySize = 1;
        for (UINT face = 0; face < CubeMapRenderTarget::FaceCount; ++face)
        {
            crossAdapterCubeMaps[face] = std::make_shared<GCrossAdapterResource>(desc,
                                                                                  devices[GraphicAdapterPrimary],
                                                                                  devices[GraphicAdapterSecond],
                                                                                  L"Shared Cube Map " +
                                                                                  std::to_wstring(face));
        }
    }

}

void ReflectionRenderer::Update(const GameTimer& gt)
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
            for (UINT i = 0; i < devices.size(); ++i)
            {
                gpuTimes[i] = devices[i]->GetCommandQueue()->GetTimestamp(olderIndex);
            }
        }
    }

    const auto commandQueue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Graphics);
    const auto copyQueue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Copy);
    const auto secondQueue = hasSecondaryAdapter ? devices[GraphicAdapterSecond]->GetCommandQueue(GQueueType::Graphics) : nullptr;

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

    if (hasSecondaryAdapter && currentFrameResource->SecondRenderFenceValue != 0 && !secondQueue->IsFinish(
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

    for (UINT i = 0; i < scenes.size(); ++i)
    {
        scenes[i]->Update();
        scenes[i]->UpdateMaterials(currentFrameResource.get(), i == GraphicAdapterSecond);
    }

    UpdateShadowTransform();
    UpdateLightBuffers();
    UpdateMainPassCB(gt);
    UpdateShadowPassCB();
    UpdateSsaoCB();
}

void ReflectionRenderer::UpdateShadowTransform()
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

void ReflectionRenderer::UpdateShadowPassCB()
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

    UINT w = shadowPathPrimeDevice->Width();
    UINT h = shadowPathPrimeDevice->Height();
    shadowPassCB.RenderTargetSize = Vector2(static_cast<float>(w), static_cast<float>(h));
    shadowPassCB.InvRenderTargetSize = Vector2(1.0f / w, 1.0f / h);

    if (hasSecondaryAdapter)
    {
        auto currPassCB = currentFrameResource->SecondPassConstantUploadBuffer;
        currPassCB->CopyData(0, shadowPassCB);
    }

    auto currPassCB = currentFrameResource->PrimePassConstantUploadBuffer;
    currPassCB->CopyData(1, shadowPassCB);
}

void ReflectionRenderer::UpdateMainPassCB(const GameTimer& gt)
{
    const auto camera = scenes[GraphicAdapterPrimary]->GetCamera();
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
    mainPassCB.RenderTargetSize = Vector2(static_cast<float>(window->GetClientWidth()),
                                          static_cast<float>(window->GetClientHeight()));
    mainPassCB.InvRenderTargetSize = Vector2(1.0f / mainPassCB.RenderTargetSize.x,
                                             1.0f / mainPassCB.RenderTargetSize.y);
    mainPassCB.NearZ = 1.0f;
    mainPassCB.FarZ = 1000.0f;
    mainPassCB.TotalTime = gt.TotalTime();
    mainPassCB.DeltaTime = gt.DeltaTime();
    mainPassCB.AmbientLight = Vector4{0.25f, 0.25f, 0.35f, 1.0f};

    mainPassCB.DirectionalLight = {};
    mainPassCB.DirectionalLight.Direction = mRotatedLightDirections[0];
    mainPassCB.DirectionalLight.Strength = Vector3{0.9f, 0.8f, 0.7f};

    {
        auto currentPassCB = currentFrameResource->PrimePassConstantUploadBuffer;
        currentPassCB->CopyData(0, mainPassCB);
        if (hasSecondaryAdapter)
            currentFrameResource->SecondPassConstantUploadBuffer->CopyData(0, mainPassCB);
    }
}

void ReflectionRenderer::UpdateLightBuffers()
{
    UINT pointLightCount = 0;
    UINT spotLightCount = 0;

    for (UINT adapter = 0; adapter < scenes.size(); ++adapter)
    {
        UINT pointLightIndex = 0;
        UINT spotLightIndex = 0;
        for (const auto* light : scenes[adapter]->GetLights())
        {
            if (light->Type() == Point && pointLightIndex < currentFrameResource->PointLightCapacity)
                currentFrameResource->PointLightBuffers[adapter]->CopyData(pointLightIndex++, light->GetData());
            else if (light->Type() == Spot && spotLightIndex < currentFrameResource->SpotLightCapacity)
                currentFrameResource->SpotLightBuffers[adapter]->CopyData(spotLightIndex++, light->GetData());
        }

        if (adapter == GraphicAdapterPrimary)
        {
            pointLightCount = pointLightIndex;
            spotLightCount = spotLightIndex;
        }
    }

    mainPassCB.PointLightCount = pointLightCount;
    mainPassCB.SpotLightCount = spotLightCount;
}

void ReflectionRenderer::UpdateSsaoCB()
{
    const auto camera = scenes[GraphicAdapterPrimary]->GetCamera();
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

    // Secondary-only SSAO data is intentionally not allocated for the primary-only fallback.
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

void ReflectionRenderer::PopulateShadowMapCommands(std::shared_ptr<GCommandList> cmdList)
{
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootShaderResourceView(kPointLightsSlot,
                                           *currentFrameResource->PointLightBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootShaderResourceView(kSpotLightsSlot,
                                           *currentFrameResource->SpotLightBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scenes[GraphicAdapterPrimary]->GetSrvHeap());
        cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                           *currentFrameResource->PrimePassConstantUploadBuffer, 1);

        shadowPathPrimeDevice->PopulatePreRenderCommands(cmdList);

        cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::ShadowMapOpaque));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::Opaque);
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::OpaqueAlphaDrop);

        cmdList->TransitionBarrier(shadowPathPrimeDevice->GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->FlushResourceBarriers();
}

void ReflectionRenderer::PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Draw Normals
    {
        cmdList->SetDescriptorsHeap(scenes[GraphicAdapterPrimary]->GetSrvHeap());
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootShaderResourceView(kPointLightsSlot,
                                           *currentFrameResource->PointLightBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootShaderResourceView(kSpotLightsSlot,
                                           *currentFrameResource->SpotLightBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scenes[GraphicAdapterPrimary]->GetSrvHeap());

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

void ReflectionRenderer::PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Draw Ambient
    {
        cmdList->SetDescriptorsHeap(scenes[GraphicAdapterPrimary]->GetSrvHeap());
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootShaderResourceView(kPointLightsSlot,
                                           *currentFrameResource->PointLightBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootShaderResourceView(kSpotLightsSlot,
                                           *currentFrameResource->SpotLightBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scenes[GraphicAdapterPrimary]->GetSrvHeap());

        cmdList->SetRootSignature(*ssaoPrimeRootSignature.get());
        ambientPrimePath->ComputeSsao(cmdList, currentFrameResource->SsaoConstantUploadBuffer, 3);
    }
}

void ReflectionRenderer::PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Forward Path with SSAA
    {
        cmdList->SetDescriptorsHeap(scenes[GraphicAdapterPrimary]->GetSrvHeap());
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootShaderResourceView(kPointLightsSlot,
                                           *currentFrameResource->PointLightBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootShaderResourceView(kSpotLightsSlot,
                                           *currentFrameResource->SpotLightBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scenes[GraphicAdapterPrimary]->GetSrvHeap());

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
        cmdList->SetRootDescriptorTable(StandardShaderSlot::SkyMap, scenes[GraphicAdapterPrimary]->GetSrvHeap(), scenes[GraphicAdapterPrimary]->GetTextureIndex(L"skyTex"));

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

void ReflectionRenderer::PopulateDrawCommands(const GraphicsAdapter adapterIndex,
                                           const std::shared_ptr<GCommandList>& cmdList,
                                           RenderMode type)
{
    scenes[adapterIndex]->Draw(cmdList, type);
}

void ReflectionRenderer::PopulateDrawQuadCommand(const std::shared_ptr<GCommandList>& cmdList,
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

void ReflectionRenderer::Draw(const GameTimer& gt)
{


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
    PopulateShadowMapCommands(primeCmdList);
    PopulateDynamicCubeMapCommands(GraphicAdapterPrimary, primeCmdList);
    PopulateForwardPathCommands(primeCmdList);
    PopulateDrawQuadCommand(primeCmdList, window->GetCurrentBackBuffer(),
                            &currentFrameResource->BackBufferRTVMemory, 0);
    primeCmdList->TransitionBarrier(window->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
    primeCmdList->FlushResourceBarriers();
    primeCmdList->EndQuery(timestampHeapIndex + 1);
    primeCmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));
    currentFrameResource->PrimeRenderFenceValue = primeRenderQueue->ExecuteCommandList(primeCmdList);


    currentFrameResourceIndex = window->Present();
}

std::array<ReflectionPassConstants, CubeMapRenderTarget::FaceCount> ReflectionRenderer::BuildCubeFacePassCBs(
    const Vector3& center) const
{
    constexpr float nearZ = 0.1f;
    constexpr float farZ = 500.0f;

    Matrix proj = XMMatrixPerspectiveFovLH(0.5f * XM_PI, 1.0f, nearZ, farZ);

    std::array<Vector3, CubeMapRenderTarget::FaceCount> targets =
    {
        center + Vector3(1.0f, 0.0f, 0.0f),
        center + Vector3(-1.0f, 0.0f, 0.0f),
        center + Vector3(0.0f, 1.0f, 0.0f),
        center + Vector3(0.0f, -1.0f, 0.0f),
        center + Vector3(0.0f, 0.0f, 1.0f),
        center + Vector3(0.0f, 0.0f, -1.0f)
    };

    std::array<Vector3, CubeMapRenderTarget::FaceCount> ups =
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

    std::array<ReflectionPassConstants, CubeMapRenderTarget::FaceCount> out{};

    for (UINT i = 0; i < CubeMapRenderTarget::FaceCount; ++i)
    {
        auto eye = XMVectorSet(center.x, center.y, center.z, 1.0f);
        auto at = XMVectorSet(targets[i].x, targets[i].y, targets[i].z, 1.0f);
        auto up = XMVectorSet(ups[i].x, ups[i].y, ups[i].z, 0.0f);

        Matrix view = XMMatrixLookAtLH(eye, at, up);
        Matrix viewProj = view * proj;

        ReflectionPassConstants pass = mainPassCB;
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

void ReflectionRenderer::PopulateDynamicCubeMapCommands(const GraphicsAdapter adapter,
                                                     const std::shared_ptr<GCommandList>& cmdList)
{
    if (UseOnlyPrime)
    {
        if (dynamicCubeMap == nullptr) return;
        Vector3 center = scenes[GraphicAdapterPrimary]->GetMirrorSpherePosition();

        cmdList->SetDescriptorsHeap(scenes[GraphicAdapterPrimary]->GetSrvHeap());
        cmdList->SetRootSignature(*primeDeviceSignature.get());

        auto vp = dynamicCubeMap->GetViewport();
        auto rect = dynamicCubeMap->GetScissorRect();
        cmdList->SetViewports(&vp, 1);
        cmdList->SetScissorRects(&rect, 1);

        cmdList->SetGraphicsRootShaderResourceView(StandardShaderSlot::MaterialData,
                                                   *currentFrameResource->MaterialBuffers[GraphicAdapterPrimary]);
        cmdList->SetGraphicsRootShaderResourceView(kPointLightsSlot,
                                                   *currentFrameResource->PointLightBuffers[GraphicAdapterPrimary]);
        cmdList->SetGraphicsRootShaderResourceView(kSpotLightsSlot,
                                                   *currentFrameResource->SpotLightBuffers[GraphicAdapterPrimary]);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scenes[GraphicAdapterPrimary]->GetSrvHeap());
        cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowPathPrimeDevice->GetSrv());

        auto whiteSsao = scenes[GraphicAdapterPrimary]->GetTextureIndex(L"white1x1Tex");
        cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap,
                                        scenes[GraphicAdapterPrimary]->GetSrvHeap(),
                                        whiteSsao);

        auto cubePasses = BuildCubeFacePassCBs(center);

        cmdList->TransitionBarrier(dynamicCubeMap->GetCubeMap(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(dynamicCubeMap->GetDepthMap(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->FlushResourceBarriers();

        for (UINT face = 0; face < CubeMapRenderTarget::FaceCount; ++face)
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
            cmdList->SetRootDescriptorTable(StandardShaderSlot::SkyMap, scenes[GraphicAdapterPrimary]->GetSrvHeap(),
                                            scenes[GraphicAdapterPrimary]->GetTextureIndex(L"skyTex"));

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
            Vector3 center = scenes[GraphicAdapterSecond]->GetMirrorSpherePosition();

            cmdList->SetDescriptorsHeap(scenes[GraphicAdapterSecond]->GetSrvHeap());
            cmdList->SetRootSignature(*secondDeviceSignature.get());

            // BEGIN PROTECTED BAKED CUBEMAP
            if (!isBaked)
            {
                auto vp = bakedCubeMapSecond->GetViewport();
                auto rect = bakedCubeMapSecond->GetScissorRect();
                cmdList->SetViewports(&vp, 1);
                cmdList->SetScissorRects(&rect, 1);

                cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                   *currentFrameResource->MaterialBuffers[GraphicAdapterSecond]);
                cmdList->SetRootShaderResourceView(kPointLightsSlot,
                                                   *currentFrameResource->PointLightBuffers[GraphicAdapterSecond]);
                cmdList->SetRootShaderResourceView(kSpotLightsSlot,
                                                   *currentFrameResource->SpotLightBuffers[GraphicAdapterSecond]);
                cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scenes[GraphicAdapterSecond]->GetSrvHeap());


                auto whiteSsao = scenes[GraphicAdapterSecond]->GetTextureIndex(L"white1x1Tex");

                cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, scenes[GraphicAdapterSecond]->GetSrvHeap(),
                    whiteSsao);

                cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap,
                                                scenes[GraphicAdapterSecond]->GetSrvHeap(),
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
            cmdList->SetRootShaderResourceView(kPointLightsSlot,
                                               *currentFrameResource->PointLightBuffers[GraphicAdapterSecond]);
            cmdList->SetRootShaderResourceView(kSpotLightsSlot,
                                               *currentFrameResource->SpotLightBuffers[GraphicAdapterSecond]);
            cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scenes[GraphicAdapterSecond]->GetSrvHeap());

            auto whiteSsao = scenes[GraphicAdapterSecond]->GetTextureIndex(L"white1x1Tex");

            cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, scenes[GraphicAdapterSecond]->GetSrvHeap(),
                whiteSsao);

            cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap,
                                            scenes[GraphicAdapterSecond]->GetSrvHeap(),
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

            // END PROTECTED BAKED CUBEMAP

        }
    }
}

void ReflectionRenderer::CreateDynamicTextures(const GraphicsAdapter adapter)
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
