#include "ReflectionRenderer.h"

#include "Camera.h"
#include "d3dUtil.h"
#include "d3dx12.h"
#include "GameObject.h"
#include "GCommandList.h"
#include "GCommandListMarker.h"
#include "UILayer.h"
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
constexpr UINT kSsrInputsSlot = StandardShaderSlot::Count + 2;

struct ProbeUpdateBatch
{
    UINT FirstProbe = 0;
    UINT ProbeCount = 0;
    UINT FirstFace = 0;
    UINT FaceCount = 0;
};

ProbeUpdateBatch SelectProbeUpdateBatch(const UINT firstAssignedProbe, const UINT assignedProbeCount,
                                        const ReflectionProbeUpdateMode mode, UINT& nextProbe, UINT& nextFace)
{
    if (assignedProbeCount == 0)
    {
        return {};
    }

    const UINT endProbe = firstAssignedProbe + assignedProbeCount;
    if (nextProbe < firstAssignedProbe || nextProbe >= endProbe)
    {
        nextProbe = firstAssignedProbe;
        nextFace = 0;
    }

    if (mode == ReflectionProbeUpdateMode::AllProbesPerFrame)
    {
        return {firstAssignedProbe, assignedProbeCount, 0, CubeMapRenderTarget::FaceCount};
    }

    ProbeUpdateBatch batch{nextProbe, 1, 0, CubeMapRenderTarget::FaceCount};
    if (mode == ReflectionProbeUpdateMode::OneProbePerFrame)
    {
        ++nextProbe;
        if (nextProbe >= endProbe)
        {
            nextProbe = firstAssignedProbe;
        }
        return batch;
    }

    batch.FirstFace = nextFace;
    batch.FaceCount = 1;
    ++nextFace;
    if (nextFace >= CubeMapRenderTarget::FaceCount)
    {
        nextFace = 0;
        ++nextProbe;
        if (nextProbe >= endProbe)
        {
            nextProbe = firstAssignedProbe;
        }
    }
    return batch;
}
}

ReflectionRenderer::ReflectionRenderer(std::shared_ptr<Common::Window> windowValue,
                                       std::vector<std::shared_ptr<GDevice>>& devicesValue,
                                       std::vector<std::unique_ptr<Common::Scene>>& scenesValue,
                                       const DXGI_FORMAT backBufferFormatValue,
                                       const DXGI_FORMAT depthStencilFormatValue)
    : window(std::move(windowValue)), devices(devicesValue), scenes(scenesValue),
      backBufferFormat(backBufferFormatValue), depthStencilFormat(depthStencilFormatValue) {}

ReflectionRenderer::~ReflectionRenderer() = default;

void ReflectionRenderer::Initialize()
{
    mSceneBounds = scenes[GraphicAdapterPrimary]->GetBounds();
    hasSecondaryAdapter = devices.size() == ReflectionAdapterCount && scenes.size() == ReflectionAdapterCount;
    InitCubeFacePasses();
    InitRenderPaths(); Flush(); InitRootSignature(); Flush(); InitPipeLineResource(); Flush(); InitFrameResource(); Flush();
    uiLayer = std::make_unique<UILayer>(devices[GraphicAdapterPrimary], window->GetWindowHandle(),
                                        GetSRGBFormat(backBufferFormat), *this);
}

void ReflectionRenderer::ResetBenchmarkAnimation()
{
    mLightRotationAngle = 0.0f;
    for (int i = 0; i < 3; ++i)
        mRotatedLightDirections[i] = mBaseLightDirections[i];
}

void ReflectionRenderer::SetBenchmarkDisplayState(ReflectionBenchmarkDisplayState state)
{
    benchmarkDisplayState = std::move(state);
}

void ReflectionRenderer::SetUseOnlyPrime(const bool value)
{
    auto configuration = probeConfiguration;
    configuration.PrimaryProbeCount = value || !hasSecondaryAdapter ? ReflectionProbeCount : 0;
    SetReflectionProbeConfiguration(configuration);
}

void ReflectionRenderer::SetSsrExecutionMode(const SsrExecutionMode mode)
{
    auto configuration = probeConfiguration;
    configuration.SsrMode = mode;
    SetReflectionProbeConfiguration(configuration);
}

void ReflectionRenderer::SetReflectionProbeConfiguration(ReflectionProbeConfiguration configuration)
{
    configuration.PrimaryProbeCount = hasSecondaryAdapter
                                          ? std::min(configuration.PrimaryProbeCount, ReflectionProbeCount)
                                          : ReflectionProbeCount;
    if (!hasSecondaryAdapter && configuration.SsrMode == SsrExecutionMode::Secondary)
        configuration.SsrMode = SsrExecutionMode::Primary;
    probeConfiguration = configuration;

    if (probeConfiguration.CaptureMode == ReflectionProbeCaptureMode::BakedDynamicOverlay)
    {
        for (UINT probeIndex = 0; probeIndex < ReflectionProbeCount; ++probeIndex)
        {
            if (bakedCubeMapsPrime[probeIndex] == nullptr)
            {
                bakedCubeMapsPrime[probeIndex] = std::make_shared<BakedCubeMapRenderTarget>(
                    devices[GraphicAdapterPrimary], BakedCubeMapSize, DXGI_FORMAT_R8G8B8A8_UNORM,
                    DXGI_FORMAT_D32_FLOAT);
            }
            if (hasSecondaryAdapter && bakedCubeMapsSecond[probeIndex] == nullptr)
            {
                bakedCubeMapsSecond[probeIndex] = std::make_shared<BakedCubeMapRenderTarget>(
                    devices[GraphicAdapterSecond], BakedCubeMapSize, DXGI_FORMAT_R8G8B8A8_UNORM,
                    DXGI_FORMAT_D32_FLOAT);
            }
        }
    }
    else
    {
        bakedCubeMapsPrime = {};
        bakedCubeMapsSecond = {};
        primeProbeBaked = {};
        secondProbeBaked = {};
        hasBakedSecondaryLighting = false;
    }
    ResetProbeScheduling();
}

void ReflectionRenderer::ResetProbeScheduling()
{
    nextPrimaryProbeIndex = probeConfiguration.PrimaryProbeCount > 0 ? 0 : InvalidProbeIndex;
    nextPrimaryProbeFace = 0;
    nextSecondaryProbeIndex = probeConfiguration.PrimaryProbeCount < ReflectionProbeCount
                                  ? probeConfiguration.PrimaryProbeCount
                                  : InvalidProbeIndex;
    nextSecondaryProbeFace = 0;
    nextSharedProbeIndex = probeConfiguration.PrimaryProbeCount < ReflectionProbeCount
                               ? probeConfiguration.PrimaryProbeCount
                               : InvalidProbeIndex;
    nextSharedProbeFace = 0;
}

void ReflectionRenderer::SetSsaaMultiplier(const UINT value)
{
    multi = value;
    if (antiAliasingPrimePath) antiAliasingPrimePath->SetMultiplier(multi, window->GetClientWidth(), window->GetClientHeight());
}

void ReflectionRenderer::OnResize(const float aspectRatio)
{
    // Window::OnResize flushes only the presentation device. When GPU2 owns
    // the swap chain, GPU1 must finish before its size-dependent targets are replaced.
    if (activePresentationMode == SsrExecutionMode::Secondary)
        devices[GraphicAdapterPrimary]->Flush();
    if (uiLayer) uiLayer->Invalidate();
    fullViewport = { 0.0f, 0.0f, static_cast<float>(window->GetClientWidth()),
                     static_cast<float>(window->GetClientHeight()), 0.0f, 1.0f };
    fullRect = { 0, 0, window->GetClientWidth(), window->GetClientHeight() };

    BuildPresentationViews();

    scenes[GraphicAdapterPrimary]->GetCamera()->SetAspectRatio(aspectRatio);
    if (ambientPrimePath)
    {
        ambientPrimePath->OnResize(window->GetClientWidth(), window->GetClientHeight());
        ambientPrimePath->RebuildDescriptors();
    }
    if (antiAliasingPrimePath)
        antiAliasingPrimePath->OnResize(window->GetClientWidth(), window->GetClientHeight());
    BuildSsrResources();
    currentFrameResourceIndex = window->GetCurrentBackBufferIndex();
    if (uiLayer) uiLayer->CreateDeviceObjects();
}

void ReflectionRenderer::Flush() const
{
    for (const auto& device : devices)
        if (device) device->Flush();
}

void ReflectionRenderer::InitFrameResource()
{
    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<FrameResource>(
            devices[GraphicAdapterPrimary], hasSecondaryAdapter ? devices[GraphicAdapterSecond] : nullptr,
            DynamicCubeMapFirstPassIndex + ReflectionProbeCount * CubeMapRenderTarget::FaceCount,
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
        CD3DX12_DESCRIPTOR_RANGE ssrInputs;
        ssrInputs.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 2);
        rootSignature->AddDescriptorParameter(&ssrInputs, 1, D3D12_SHADER_VISIBILITY_PIXEL);
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
    primePipelineResources.LoadDefaultShaders(true);
    primePipelineResources.LoadDefaultPSO(devices[GraphicAdapterPrimary], primeDeviceSignature, desc,
                                          backBufferFormat, depthStencilFormat, ssaoPrimeRootSignature,
                                          NormalMapFormat, AmbientMapFormat, true);

    if (hasSecondaryAdapter)
    {
        secondPipelineResources = RenderModeFactory();
        secondPipelineResources.LoadDefaultShaders(true);
        secondPipelineResources.LoadDefaultPSO(devices[GraphicAdapterSecond], secondDeviceSignature, desc,
                                               backBufferFormat, depthStencilFormat, nullptr,
                                               NormalMapFormat, AmbientMapFormat, true);
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

    for (UINT probeIndex = 0; probeIndex < ReflectionProbeCount; ++probeIndex)
    {
        dynamicCubeMaps[probeIndex] = std::make_shared<CubeMapRenderTarget>(
            devices[GraphicAdapterPrimary], DynamicCubeMapSize, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT);
    }

    if (hasSecondaryAdapter)
    {
        shadowPathSecondDevice = std::make_shared<ShadowMap>(devices[GraphicAdapterSecond],
                                                              shadowPathPrimeDevice->Width(),
                                                              shadowPathPrimeDevice->Height());
        CreateDynamicTextures(GraphicAdapterSecond);

        auto desc = dynamicCubeMaps[0]->GetCubeMap().GetD3D12ResourceDesc();
        desc.DepthOrArraySize = 1;
        for (UINT probeIndex = 0; probeIndex < ReflectionProbeCount; ++probeIndex)
        {
            for (UINT face = 0; face < CubeMapRenderTarget::FaceCount; ++face)
            {
                crossAdapterCubeMaps[probeIndex][face] = std::make_shared<GCrossAdapterResource>(
                    desc, devices[GraphicAdapterPrimary], devices[GraphicAdapterSecond],
                    L"Shared Reflection Probe " + std::to_wstring(probeIndex) + L" Face " +
                    std::to_wstring(face));
            }
        }
    }

}

void ReflectionRenderer::Update(const GameTimer& gt)
{
    ApplyPresentationDevice();
    const auto commandQueue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Graphics);
    const auto secondQueue = hasSecondaryAdapter ? devices[GraphicAdapterSecond]->GetCommandQueue(GQueueType::Graphics) : nullptr;

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

    scenes[GraphicAdapterPrimary]->Update();
    scenes[GraphicAdapterPrimary]->UpdateMaterials(currentFrameResource.get());
    if (hasSecondaryAdapter && probeConfiguration.PrimaryProbeCount < ReflectionProbeCount)
    {
        scenes[GraphicAdapterSecond]->Update();
        scenes[GraphicAdapterSecond]->UpdateMaterials(currentFrameResource.get(), true);
    }

    UpdateShadowTransform();
    UpdateMainPassCB(gt);
    UpdateShadowPassCB();
    UpdateSsaoCB();
    if (uiLayer)
        uiLayer->Update();
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

    if (hasSecondaryAdapter && probeConfiguration.PrimaryProbeCount < ReflectionProbeCount)
    {
        auto currPassCB = currentFrameResource->SecondPassConstantUploadBuffer;
        currPassCB->CopyData(1, shadowPassCB);
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
    }
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
        GCommandListMarker marker(cmdList, L"Shadow map");
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

void ReflectionRenderer::PopulateSecondaryStaticShadowMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    GCommandListMarker marker(cmdList, L"Static shadow map (secondary)");
    cmdList->SetRootSignature(*secondDeviceSignature.get());
    cmdList->SetDescriptorsHeap(scenes[GraphicAdapterSecond]->GetSrvHeap());
    cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                       *currentFrameResource->MaterialBuffers[GraphicAdapterSecond]);
    cmdList->SetRootShaderResourceView(kPointLightsSlot,
                                       *currentFrameResource->PointLightBuffers[GraphicAdapterSecond]);
    cmdList->SetRootShaderResourceView(kSpotLightsSlot,
                                       *currentFrameResource->SpotLightBuffers[GraphicAdapterSecond]);
    cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scenes[GraphicAdapterSecond]->GetSrvHeap());
    cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                       *currentFrameResource->SecondPassConstantUploadBuffer, 1);

    shadowPathSecondDevice->PopulatePreRenderCommands(cmdList);
    cmdList->SetPipelineState(*secondPipelineResources.GetPSO(RenderMode::ShadowMapOpaque));
    PopulateDrawCommands(GraphicAdapterSecond, cmdList, RenderMode::Opaque);
    cmdList->SetPipelineState(*secondPipelineResources.GetPSO(RenderMode::ShadowMapOpaqueDrop));
    PopulateDrawCommands(GraphicAdapterSecond, cmdList, RenderMode::OpaqueAlphaDrop);
    cmdList->TransitionBarrier(shadowPathSecondDevice->GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

void ReflectionRenderer::PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    GCommandListMarker marker(cmdList, L"Normals");
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
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::DynamicOpaque);
        cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::DrawNormalsOpaqueDrop));
        PopulateDrawCommands(GraphicAdapterPrimary, cmdList, RenderMode::OpaqueAlphaDrop);

        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }
}

void ReflectionRenderer::PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    GCommandListMarker marker(cmdList, L"SSAO / ambient");
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
    GCommandListMarker marker(cmdList, L"Forward path");
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
        for (UINT probeIndex = 0; probeIndex < ReflectionProbeCount; ++probeIndex)
        {
            cmdList->SetRootDescriptorTable(StandardShaderSlot::SkyMap, dynamicCubeMaps[probeIndex]->GetSRV());
            scenes[GraphicAdapterPrimary]->DrawReflectionProbe(cmdList, probeIndex);
        }
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
    GCommandListMarker marker(cmdList, L"Final quad / composite");
    cmdList->SetViewports(&fullViewport, 1);
    cmdList->SetScissorRects(&fullRect, 1);

    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();
    cmdList->ClearRenderTarget(rtvMemory, offsetRTV, Colors::Black);

    cmdList->SetRenderTargets(1, rtvMemory, offsetRTV);

    cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, antiAliasingPrimePath->GetSRV());

    cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Quad));
    PopulateDrawCommands(GraphicAdapterPrimary, cmdList, (RenderMode::Quad));

}

void ReflectionRenderer::PopulateSsrCommands(const std::shared_ptr<GCommandList>& cmdList,
                                             const GraphicsAdapter adapter,
                                             const SsrInputSet& inputs,
                                             GTexture& renderTarget,
                                             const GDescriptor* renderTargetRtv)
{
    GCommandListMarker marker(cmdList, adapter == GraphicAdapterPrimary ? L"SSR (primary)" : L"SSR (secondary)");
    cmdList->SetViewports(&fullViewport, 1);
    cmdList->SetScissorRects(&fullRect, 1);
    cmdList->TransitionBarrier(*inputs.SceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(*inputs.SceneDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(*inputs.SceneNormal, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();
    cmdList->SetRenderTargets(1, renderTargetRtv);

    const auto& signature = adapter == GraphicAdapterPrimary ? primeDeviceSignature : secondDeviceSignature;
    cmdList->SetRootSignature(*signature);
    cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
        adapter == GraphicAdapterPrimary
            ? *currentFrameResource->PrimePassConstantUploadBuffer
            : *currentFrameResource->SecondPassConstantUploadBuffer);
    cmdList->SetDescriptorsHeap(inputs.Srvs);
    cmdList->SetRootDescriptorTable(kSsrInputsSlot, inputs.Srvs);
    cmdList->SetPipelineState(*(adapter == GraphicAdapterPrimary
                                   ? primePipelineResources.GetPSO(RenderMode::Ssr)
                                   : secondPipelineResources.GetPSO(RenderMode::Ssr)));
    cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->Draw(3, 1, 0, 0);
}

void ReflectionRenderer::BuildSsrResources()
{
    if (!ambientPrimePath || !antiAliasingPrimePath || window->GetClientWidth() <= 0 || window->GetClientHeight() <= 0)
        return;

    const UINT width = static_cast<UINT>(window->GetClientWidth());
    const UINT height = static_cast<UINT>(window->GetClientHeight());
    if (composedSceneColor.IsValid())
    {
        const auto currentDesc = composedSceneColor.GetD3D12ResourceDesc();
        const bool secondaryResourcesReady = !hasSecondaryAdapter ||
            (sharedSsrSceneColor && sharedSsrSceneDepth && sharedSsrSceneNormal);
        if (currentDesc.Width == width && currentDesc.Height == height && secondaryResourcesReady)
            return;
    }
    auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D(GetSRGBFormat(backBufferFormat), width, height, 1, 1, 1, 0,
                                                  D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    const auto clearValue = CD3DX12_CLEAR_VALUE(GetSRGBFormat(backBufferFormat), Colors::Black);
    composedSceneColor = GTexture(devices[GraphicAdapterPrimary], colorDesc, L"SSR Composed Scene",
                                  TextureUsage::RenderTarget, &clearValue);
    composedSceneColorRtv = devices[GraphicAdapterPrimary]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    primarySsrInputSrvs = devices[GraphicAdapterPrimary]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = GetSRGBFormat(backBufferFormat);
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    composedSceneColor.CreateRenderTargetView(&rtvDesc, &composedSceneColorRtv);
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Format = GetSRGBFormat(backBufferFormat);
    composedSceneColor.CreateShaderResourceView(&srvDesc, &primarySsrInputSrvs, 0);
    
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    ambientPrimePath->NormalDepthMap().CreateShaderResourceView(
        &srvDesc, &primarySsrInputSrvs, 1);

    srvDesc.Format = NormalMapFormat;
    ambientPrimePath->NormalMap().CreateShaderResourceView(
        &srvDesc, &primarySsrInputSrvs, 2);

    if (!hasSecondaryAdapter)
        return;

    const auto createSecondTexture = [this](GTexture& destination, const GTexture& source,
                                            const std::wstring& name, const TextureUsage usage)
    {
        destination = GTexture(devices[GraphicAdapterSecond], source.GetD3D12ResourceDesc(), name, usage);
    };
    createSecondTexture(secondSsrSceneColor, composedSceneColor, L"Secondary SSR Scene Color", TextureUsage::RenderTarget);
    createSecondTexture(secondSsrSceneDepth, ambientPrimePath->NormalDepthMap(), L"Secondary SSR Scene Depth", TextureUsage::Depth);
    createSecondTexture(secondSsrSceneNormal, ambientPrimePath->NormalMap(), L"Secondary SSR Scene Normal", TextureUsage::Normalmap);
    secondSsrInputSrvs = devices[GraphicAdapterSecond]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);
    srvDesc.Format = GetSRGBFormat(backBufferFormat);
    secondSsrSceneColor.CreateShaderResourceView(&srvDesc, &secondSsrInputSrvs, 0);
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    secondSsrSceneDepth.CreateShaderResourceView(&srvDesc, &secondSsrInputSrvs, 1);
    srvDesc.Format = secondSsrSceneNormal.GetD3D12ResourceDesc().Format;
    secondSsrSceneNormal.CreateShaderResourceView(&srvDesc, &secondSsrInputSrvs, 2);

    auto sharedColorDesc = composedSceneColor.GetD3D12ResourceDesc();
    auto sharedDepthDesc = ambientPrimePath->NormalDepthMap().GetD3D12ResourceDesc();
    auto sharedNormalDesc = ambientPrimePath->NormalMap().GetD3D12ResourceDesc();
    sharedSsrSceneColor = std::make_shared<GCrossAdapterResource>(sharedColorDesc, devices[GraphicAdapterPrimary],
        devices[GraphicAdapterSecond], L"Shared SSR Scene Color");
    sharedSsrSceneDepth = std::make_shared<GCrossAdapterResource>(sharedDepthDesc, devices[GraphicAdapterPrimary],
        devices[GraphicAdapterSecond], L"Shared SSR Scene Depth");
    sharedSsrSceneNormal = std::make_shared<GCrossAdapterResource>(sharedNormalDesc, devices[GraphicAdapterPrimary],
        devices[GraphicAdapterSecond], L"Shared SSR Scene Normal");
}

void ReflectionRenderer::BuildPresentationViews()
{
    if (frameResources.empty())
        return;
    const auto adapter = activePresentationMode == SsrExecutionMode::Secondary && hasSecondaryAdapter
                             ? GraphicAdapterSecond : GraphicAdapterPrimary;
    presentationBackBufferRtvs = devices[adapter]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                                                        globalCountFrameResources);
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = GetSRGBFormat(backBufferFormat);
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    for (UINT index = 0; index < globalCountFrameResources; ++index)
        window->GetBackBuffer(index).CreateRenderTargetView(&rtvDesc, &presentationBackBufferRtvs, index);
}

void ReflectionRenderer::ApplyPresentationDevice()
{
    const auto requestedMode = probeConfiguration.SsrMode == SsrExecutionMode::Secondary && hasSecondaryAdapter
                                   ? SsrExecutionMode::Secondary : SsrExecutionMode::Primary;
    if (requestedMode == activePresentationMode)
        return;
    activePresentationMode = requestedMode;
    const auto adapter = activePresentationMode == SsrExecutionMode::Secondary
                             ? GraphicAdapterSecond : GraphicAdapterPrimary;
    window->SetPresentationDevice(devices[adapter]);
    uiLayer.reset();
    BuildPresentationViews();
    currentFrameResourceIndex = window->GetCurrentBackBufferIndex();
    uiLayer = std::make_unique<UILayer>(devices[adapter], window->GetWindowHandle(),
                                        GetSRGBFormat(backBufferFormat), *this);
}

void ReflectionRenderer::Draw(const GameTimer& gt)
{
    currentFrameResource = frameResources[currentFrameResourceIndex];

    {
        auto commandQueue = devices[GraphicAdapterPrimary]->GetCommandQueue(GQueueType::Graphics);
        auto cmdList = commandQueue->GetCommandList();
        PopulateNormalMapCommands(cmdList);
        PopulateAmbientMapCommands(cmdList);
        PopulateShadowMapCommands(cmdList);
        CopySharedProbeOutputsToPrimary(cmdList);
        PopulatePrimaryProbeCommands(cmdList);
        PopulateForwardPathCommands(cmdList);
        PopulateDrawQuadCommand(cmdList, composedSceneColor, &composedSceneColorRtv, 0);

        if (activePresentationMode == SsrExecutionMode::Secondary && hasSecondaryAdapter)
        {
            cmdList->CopyResource(sharedSsrSceneColor->GetPrimeResource(), composedSceneColor);
            cmdList->CopyResource(sharedSsrSceneDepth->GetPrimeResource(), ambientPrimePath->NormalDepthMap());
            cmdList->CopyResource(sharedSsrSceneNormal->GetPrimeResource(), ambientPrimePath->NormalMap());
        }
        else
        {
            const auto backBufferIndex = window->GetCurrentBackBufferIndex();
            const auto backBufferRtv = presentationBackBufferRtvs.Offset(backBufferIndex);
            const SsrInputSet inputs{
                &composedSceneColor, &ambientPrimePath->NormalDepthMap(),
                &ambientPrimePath->NormalMap(), &primarySsrInputSrvs
            };
            PopulateSsrCommands(cmdList, GraphicAdapterPrimary, inputs, window->GetCurrentBackBuffer(),
                                &backBufferRtv);
            if (uiLayer)
            {
                GCommandListMarker marker(cmdList, L"UI");
                uiLayer->Render(cmdList);
            }
            cmdList->TransitionBarrier(window->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
            cmdList->FlushResourceBarriers();
        }
        currentFrameResource->PrimeRenderFenceValue = commandQueue->ExecuteCommandList(cmdList);
    }

    if (hasSecondaryAdapter)
    {
        auto secondQueue = devices[GraphicAdapterSecond]->GetCommandQueue();
        if (currentFrameResource->SecondRenderFenceValue == 0 ||
            secondQueue->IsFinish(currentFrameResource->SecondRenderFenceValue))
        {
            const auto secondCmdList = secondQueue->GetCommandList();
            
            if (probeConfiguration.PrimaryProbeCount < ReflectionProbeCount)
            {                
                PopulateSecondaryProbeCommands(secondCmdList);
            }
            
            if (activePresentationMode == SsrExecutionMode::Secondary)
            {
                currentFrameResource->SecondPassConstantUploadBuffer->CopyData(0, mainPassCB);
                secondCmdList->CopyResource(secondSsrSceneColor, sharedSsrSceneColor->GetSharedResource());
                secondCmdList->CopyResource(secondSsrSceneDepth, sharedSsrSceneDepth->GetSharedResource());
                secondCmdList->CopyResource(secondSsrSceneNormal, sharedSsrSceneNormal->GetSharedResource());
                const auto backBufferIndex = window->GetCurrentBackBufferIndex();
                const auto backBufferRtv = presentationBackBufferRtvs.Offset(backBufferIndex);
                const SsrInputSet inputs{&secondSsrSceneColor, &secondSsrSceneDepth,
                    &secondSsrSceneNormal, &secondSsrInputSrvs};
                PopulateSsrCommands(secondCmdList, GraphicAdapterSecond, inputs,
                                    window->GetCurrentBackBuffer(), &backBufferRtv);
                if (uiLayer)
                {
                    GCommandListMarker marker(secondCmdList, L"UI");
                    uiLayer->Render(secondCmdList);
                }
                secondCmdList->TransitionBarrier(window->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
                secondCmdList->FlushResourceBarriers();
            }            
            currentFrameResource->SecondRenderFenceValue = secondQueue->ExecuteCommandList(secondCmdList);
        }
    }
    
    currentFrameResourceIndex = window->Present();
}

void ReflectionRenderer::ForwardUiMessage(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam) const
{
    if (uiLayer) uiLayer->ForwardMessage(hwnd, msg, wParam, lParam);
}

bool ReflectionRenderer::UiWantsMouseCapture() const { return uiLayer && uiLayer->WantsMouseCapture(); }
bool ReflectionRenderer::UiWantsKeyboardCapture() const { return uiLayer && uiLayer->WantsKeyboardCapture(); }

void ReflectionRenderer::InitCubeFacePasses()
{
    // Probe transforms are static, so all six camera matrices are built once.
    constexpr float nearZ = 0.1f;
    constexpr float farZ = 500.0f;
    const Matrix proj = XMMatrixPerspectiveFovLH(0.5f * XM_PI, 1.0f, nearZ, farZ);
    constexpr std::array<Vector3, CubeMapRenderTarget::FaceCount> directions =
    {
        Vector3(1.0f, 0.0f, 0.0f),
        Vector3(-1.0f, 0.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f),
        Vector3(0.0f, -1.0f, 0.0f),
        Vector3(0.0f, 0.0f, 1.0f),
        Vector3(0.0f, 0.0f, -1.0f)
    };
    constexpr std::array<Vector3, CubeMapRenderTarget::FaceCount> ups =
    {
        Vector3(0.0f, 1.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f),
        Vector3(0.0f, 0.0f, -1.0f),
        Vector3(0.0f, 0.0f, 1.0f),
        Vector3(0.0f, 1.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f)
    };
    const Matrix textureTransform(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);
    const auto positions = scenes[GraphicAdapterPrimary]->GetReflectionProbePositions();
    for (UINT probeIndex = 0; probeIndex < ReflectionProbeCount; ++probeIndex)
    {
        const auto& center = positions[probeIndex];
        for (UINT face = 0; face < CubeMapRenderTarget::FaceCount; ++face)
        {
            const Matrix view = XMMatrixLookAtLH(center, center + directions[face], ups[face]);
            const Matrix viewProj = view * proj;
            auto& pass = cubeFaceCameraPasses[probeIndex][face];
            pass.View = view.Transpose();
            pass.InvView = view.Invert().Transpose();
            pass.Proj = proj.Transpose();
            pass.InvProj = proj.Invert().Transpose();
            pass.ViewProj = viewProj.Transpose();
            pass.InvViewProj = viewProj.Invert().Transpose();
            pass.ViewProjTex = (viewProj * textureTransform).Transpose();
            pass.EyePosW = center;
            pass.RenderTargetSize = Vector2(static_cast<float>(DynamicCubeMapSize),
                                            static_cast<float>(DynamicCubeMapSize));
            pass.InvRenderTargetSize = Vector2(1.0f / DynamicCubeMapSize, 1.0f / DynamicCubeMapSize);
            pass.NearZ = nearZ;
            pass.FarZ = farZ;
        }
    }
}

ReflectionPassConstants ReflectionRenderer::GetCubeFacePass(
    const UINT probeIndex, const UINT face, const bool useBakedSecondaryLighting) const
{
    // Lighting, time and shadow data remain frame-dependent; only the camera
    // portion comes from the immutable probe-face cache.
    ReflectionPassConstants pass = mainPassCB;
    if (useBakedSecondaryLighting && hasBakedSecondaryLighting)
    {
        pass.DirectionalLight = bakedSecondaryDirectionalLight;
        pass.ShadowTransform = bakedSecondaryShadowTransform;
    }

    const auto& camera = cubeFaceCameraPasses[probeIndex][face];
    pass.View = camera.View;
    pass.InvView = camera.InvView;
    pass.Proj = camera.Proj;
    pass.InvProj = camera.InvProj;
    pass.ViewProj = camera.ViewProj;
    pass.InvViewProj = camera.InvViewProj;
    pass.ViewProjTex = camera.ViewProjTex;
    pass.EyePosW = camera.EyePosW;
    pass.RenderTargetSize = camera.RenderTargetSize;
    pass.InvRenderTargetSize = camera.InvRenderTargetSize;
    pass.NearZ = camera.NearZ;
    pass.FarZ = camera.FarZ;
    return pass;
}

void ReflectionRenderer::PopulateBakedProbeCommands(const GraphicsAdapter adapter, const UINT probeIndex,
                                                      const std::shared_ptr<GCommandList>& cmdList)
{
    auto& bakedFlags = adapter == GraphicAdapterPrimary ? primeProbeBaked : secondProbeBaked;
    if (bakedFlags[probeIndex])
    {
        return;
    }

    auto& scene = *scenes[adapter];
    auto& bakedProbe = *(adapter == GraphicAdapterPrimary
                             ? bakedCubeMapsPrime[probeIndex]
                             : bakedCubeMapsSecond[probeIndex]);
    auto& passBuffer = adapter == GraphicAdapterPrimary
                           ? currentFrameResource->PrimePassConstantUploadBuffer
                           : currentFrameResource->SecondPassConstantUploadBuffer;
    auto& pipelines = adapter == GraphicAdapterPrimary ? primePipelineResources : secondPipelineResources;
    auto& shadowMap = adapter == GraphicAdapterPrimary ? shadowPathPrimeDevice : shadowPathSecondDevice;

    cmdList->SetDescriptorsHeap(scene.GetSrvHeap());
    cmdList->SetRootSignature(*(adapter == GraphicAdapterPrimary ? primeDeviceSignature : secondDeviceSignature));
    cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                       *currentFrameResource->MaterialBuffers[adapter]);
    cmdList->SetRootShaderResourceView(kPointLightsSlot, *currentFrameResource->PointLightBuffers[adapter]);
    cmdList->SetRootShaderResourceView(kSpotLightsSlot, *currentFrameResource->SpotLightBuffers[adapter]);
    cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scene.GetSrvHeap());
    cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowMap->GetSrv());
    cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, scene.GetSrvHeap(),
                                    scene.GetTextureIndex(L"white1x1Tex"));
    cmdList->SetRootDescriptorTable(StandardShaderSlot::SkyMap, scene.GetSrvHeap(),
                                    scene.GetTextureIndex(L"skyTex"));

    const auto viewport = bakedProbe.GetViewport();
    const auto rect = bakedProbe.GetScissorRect();
    cmdList->SetViewports(&viewport, 1);
    cmdList->SetScissorRects(&rect, 1);
    cmdList->TransitionBarrier(bakedProbe.GetCubeMap(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->TransitionBarrier(bakedProbe.GetDepthMap(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
    cmdList->FlushResourceBarriers();

    for (UINT face = 0; face < CubeMapRenderTarget::FaceCount; ++face)
    {
        const UINT passIndex = BakedCubeMapFirstPassIndex + probeIndex * CubeMapRenderTarget::FaceCount + face;
        passBuffer->CopyData(passIndex, GetCubeFacePass(probeIndex, face, adapter == GraphicAdapterSecond));
        cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData, *passBuffer, passIndex);

        auto faceRtv = bakedProbe.GetRTV(face);
        auto faceDsv = bakedProbe.GetDSV(face);
        cmdList->ClearRenderTarget(&faceRtv, 0, Colors::Black);
        cmdList->ClearDepthStencil(&faceDsv, 0,
                                   D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);
        cmdList->SetRenderTargets(1, &faceRtv, 0, &faceDsv);

        cmdList->SetPipelineState(*pipelines.GetPSO(RenderMode::SkyBox));
        scene.Draw(cmdList, RenderMode::SkyBox);
        cmdList->SetPipelineState(*pipelines.GetPSO(RenderMode::Opaque));
        scene.Draw(cmdList, RenderMode::Opaque);
        cmdList->SetPipelineState(*pipelines.GetPSO(RenderMode::OpaqueAlphaDrop));
        scene.Draw(cmdList, RenderMode::OpaqueAlphaDrop);
        cmdList->SetPipelineState(*pipelines.GetPSO(RenderMode::Transparent));
        scene.Draw(cmdList, RenderMode::Transparent);
    }

    cmdList->TransitionBarrier(bakedProbe.GetCubeMap(), D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->TransitionBarrier(bakedProbe.GetDepthMap(), D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->FlushResourceBarriers();
    bakedFlags[probeIndex] = true;
}

void ReflectionRenderer::PopulatePrimaryProbeCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    const auto batch = SelectProbeUpdateBatch(0, probeConfiguration.PrimaryProbeCount,
                                               probeConfiguration.UpdateMode,
                                               nextPrimaryProbeIndex, nextPrimaryProbeFace);
    if (batch.ProbeCount == 0)
    {
        return;
    }

    GCommandListMarker marker(cmdList, L"Primary reflection probe update");
    auto& scene = *scenes[GraphicAdapterPrimary];
    cmdList->SetDescriptorsHeap(scene.GetSrvHeap());
    cmdList->SetRootSignature(*primeDeviceSignature);
    cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                       *currentFrameResource->MaterialBuffers[GraphicAdapterPrimary]);
    cmdList->SetRootShaderResourceView(kPointLightsSlot,
                                       *currentFrameResource->PointLightBuffers[GraphicAdapterPrimary]);
    cmdList->SetRootShaderResourceView(kSpotLightsSlot,
                                       *currentFrameResource->SpotLightBuffers[GraphicAdapterPrimary]);
    cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scene.GetSrvHeap());
    cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowPathPrimeDevice->GetSrv());
    cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, scene.GetSrvHeap(),
                                    scene.GetTextureIndex(L"white1x1Tex"));
    cmdList->SetRootDescriptorTable(StandardShaderSlot::SkyMap, scene.GetSrvHeap(),
                                    scene.GetTextureIndex(L"skyTex"));

    for (UINT probeOffset = 0; probeOffset < batch.ProbeCount; ++probeOffset)
    {
        const UINT probeIndex = batch.FirstProbe + probeOffset;
        if (probeConfiguration.CaptureMode == ReflectionProbeCaptureMode::BakedDynamicOverlay)
        {
            PopulateBakedProbeCommands(GraphicAdapterPrimary, probeIndex, cmdList);
        }

        auto& dynamicProbe = *dynamicCubeMaps[probeIndex];
        const auto viewport = dynamicProbe.GetViewport();
        const auto rect = dynamicProbe.GetScissorRect();
        cmdList->SetViewports(&viewport, 1);
        cmdList->SetScissorRects(&rect, 1);

        for (UINT faceOffset = 0; faceOffset < batch.FaceCount; ++faceOffset)
        {
            const UINT face = batch.FirstFace + faceOffset;
            const UINT passIndex = DynamicCubeMapFirstPassIndex +
                probeIndex * CubeMapRenderTarget::FaceCount + face;
            currentFrameResource->PrimePassConstantUploadBuffer->CopyData(
                passIndex, GetCubeFacePass(probeIndex, face, false));
            cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                               *currentFrameResource->PrimePassConstantUploadBuffer, passIndex);

            if (probeConfiguration.CaptureMode == ReflectionProbeCaptureMode::BakedDynamicOverlay)
            {
                cmdList->CopyCubeMapFace(dynamicProbe.GetCubeMap(),
                                         bakedCubeMapsPrime[probeIndex]->GetCubeMap(), face);
                cmdList->CopyResourceFromCubeMap(dynamicProbe.GetDepthMap(),
                                                 bakedCubeMapsPrime[probeIndex]->GetDepthMap(), face);
            }

            cmdList->TransitionBarrier(dynamicProbe.GetCubeMap(), D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmdList->TransitionBarrier(dynamicProbe.GetDepthMap(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
            cmdList->FlushResourceBarriers();
            auto faceRtv = dynamicProbe.GetRTV(face);
            cmdList->SetRenderTargets(1, &faceRtv, 0, dynamicProbe.GetDSV());

            if (probeConfiguration.CaptureMode == ReflectionProbeCaptureMode::FullDynamic)
            {
                cmdList->ClearDepthStencil(dynamicProbe.GetDSV(), 0,
                                           D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);
                cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::SkyBox));
                scene.Draw(cmdList, RenderMode::SkyBox);
                cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Opaque));
                scene.Draw(cmdList, RenderMode::Opaque);
                scene.Draw(cmdList, RenderMode::DynamicOpaque);
                cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::OpaqueAlphaDrop));
                scene.Draw(cmdList, RenderMode::OpaqueAlphaDrop);
                cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Transparent));
                scene.Draw(cmdList, RenderMode::Transparent);
            }
            else
            {
                cmdList->SetPipelineState(*primePipelineResources.GetPSO(RenderMode::Opaque));
                scene.Draw(cmdList, RenderMode::DynamicOpaque);
            }
        }

        cmdList->TransitionBarrier(dynamicProbe.GetCubeMap(), D3D12_RESOURCE_STATE_GENERIC_READ);
        cmdList->TransitionBarrier(dynamicProbe.GetDepthMap(), D3D12_RESOURCE_STATE_GENERIC_READ);
        cmdList->FlushResourceBarriers();
    }
}

void ReflectionRenderer::PopulateSecondaryProbeCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    const UINT secondaryProbeCount = ReflectionProbeCount - probeConfiguration.PrimaryProbeCount;
    const auto batch = SelectProbeUpdateBatch(probeConfiguration.PrimaryProbeCount, secondaryProbeCount,
                                               probeConfiguration.UpdateMode,
                                               nextSecondaryProbeIndex, nextSecondaryProbeFace);
    if (batch.ProbeCount == 0)
    {
        return;
    }

    GCommandListMarker marker(cmdList, L"Secondary reflection probe update");
    auto& scene = *scenes[GraphicAdapterSecond];
    if (probeConfiguration.CaptureMode == ReflectionProbeCaptureMode::FullDynamic)
    {
        PopulateSecondaryStaticShadowMapCommands(cmdList);
    }
    else if (!hasBakedSecondaryLighting)
    {
        bakedSecondaryDirectionalLight = mainPassCB.DirectionalLight;
        bakedSecondaryShadowTransform = mainPassCB.ShadowTransform;
        hasBakedSecondaryLighting = true;
        PopulateSecondaryStaticShadowMapCommands(cmdList);
    }

    cmdList->SetDescriptorsHeap(scene.GetSrvHeap());
    cmdList->SetRootSignature(*secondDeviceSignature);
    cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                       *currentFrameResource->MaterialBuffers[GraphicAdapterSecond]);
    cmdList->SetRootShaderResourceView(kPointLightsSlot,
                                       *currentFrameResource->PointLightBuffers[GraphicAdapterSecond]);
    cmdList->SetRootShaderResourceView(kSpotLightsSlot,
                                       *currentFrameResource->SpotLightBuffers[GraphicAdapterSecond]);
    cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scene.GetSrvHeap());
    cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowPathSecondDevice->GetSrv());
    cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, scene.GetSrvHeap(),
                                    scene.GetTextureIndex(L"white1x1Tex"));
    cmdList->SetRootDescriptorTable(StandardShaderSlot::SkyMap, scene.GetSrvHeap(),
                                    scene.GetTextureIndex(L"skyTex"));

    for (UINT probeOffset = 0; probeOffset < batch.ProbeCount; ++probeOffset)
    {
        const UINT probeIndex = batch.FirstProbe + probeOffset;
        if (probeConfiguration.CaptureMode == ReflectionProbeCaptureMode::BakedDynamicOverlay)
        {
            PopulateBakedProbeCommands(GraphicAdapterSecond, probeIndex, cmdList);
        }

        const auto viewport = dynamicCubeMaps[probeIndex]->GetViewport();
        const auto rect = dynamicCubeMaps[probeIndex]->GetScissorRect();
        cmdList->SetViewports(&viewport, 1);
        cmdList->SetScissorRects(&rect, 1);

        for (UINT faceOffset = 0; faceOffset < batch.FaceCount; ++faceOffset)
        {
            const UINT face = batch.FirstFace + faceOffset;
            const UINT passIndex = DynamicCubeMapFirstPassIndex +
                probeIndex * CubeMapRenderTarget::FaceCount + face;
            currentFrameResource->SecondPassConstantUploadBuffer->CopyData(
                passIndex, GetCubeFacePass(
                    probeIndex, face,
                    probeConfiguration.CaptureMode == ReflectionProbeCaptureMode::BakedDynamicOverlay));
            cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                               *currentFrameResource->SecondPassConstantUploadBuffer, passIndex);

            if (probeConfiguration.CaptureMode == ReflectionProbeCaptureMode::BakedDynamicOverlay)
            {
                cmdList->CopyResourceFromCubeMap(dynamicCubeMapFaceColor,
                                                 bakedCubeMapsSecond[probeIndex]->GetCubeMap(), face);
                cmdList->CopyResourceFromCubeMap(dynamicCubeMapFaceDepth,
                                                 bakedCubeMapsSecond[probeIndex]->GetDepthMap(), face);
            }

            cmdList->TransitionBarrier(dynamicCubeMapFaceColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmdList->TransitionBarrier(dynamicCubeMapFaceDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            cmdList->FlushResourceBarriers();
            cmdList->SetRenderTargets(1, &dynamicCubeMapFaceRtv, 0, &dynamicCubeMapFaceDsv);

            if (probeConfiguration.CaptureMode == ReflectionProbeCaptureMode::FullDynamic)
            {
                cmdList->ClearDepthStencil(&dynamicCubeMapFaceDsv, 0,
                                           D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);
                cmdList->SetPipelineState(*secondPipelineResources.GetPSO(RenderMode::SkyBox));
                scene.Draw(cmdList, RenderMode::SkyBox);
                cmdList->SetPipelineState(*secondPipelineResources.GetPSO(RenderMode::Opaque));
                scene.Draw(cmdList, RenderMode::Opaque);
                scene.Draw(cmdList, RenderMode::DynamicOpaque);
                cmdList->SetPipelineState(*secondPipelineResources.GetPSO(RenderMode::OpaqueAlphaDrop));
                scene.Draw(cmdList, RenderMode::OpaqueAlphaDrop);
                cmdList->SetPipelineState(*secondPipelineResources.GetPSO(RenderMode::Transparent));
                scene.Draw(cmdList, RenderMode::Transparent);
            }
            else
            {
                cmdList->SetPipelineState(*secondPipelineResources.GetPSO(RenderMode::Opaque));
                scene.Draw(cmdList, RenderMode::DynamicOpaque);
            }

            cmdList->CopyResource(crossAdapterCubeMaps[probeIndex][face]->GetSharedResource(),
                                  dynamicCubeMapFaceColor);
        }
    }

    cmdList->TransitionBarrier(dynamicCubeMapFaceColor, D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->TransitionBarrier(dynamicCubeMapFaceDepth, D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->FlushResourceBarriers();
}

void ReflectionRenderer::CopySharedProbeOutputsToPrimary(const std::shared_ptr<GCommandList>& cmdList)
{
    if (!hasSecondaryAdapter || probeConfiguration.PrimaryProbeCount >= ReflectionProbeCount)
    {
        return;
    }

    const UINT secondaryProbeCount = ReflectionProbeCount - probeConfiguration.PrimaryProbeCount;
    const auto batch = SelectProbeUpdateBatch(probeConfiguration.PrimaryProbeCount, secondaryProbeCount,
                                               probeConfiguration.UpdateMode,
                                               nextSharedProbeIndex, nextSharedProbeFace);
    GCommandListMarker marker(cmdList, L"Import latest shared reflection probes");
    for (UINT probeOffset = 0; probeOffset < batch.ProbeCount; ++probeOffset)
    {
        const UINT probeIndex = batch.FirstProbe + probeOffset;
        auto& cubeMap = dynamicCubeMaps[probeIndex]->GetCubeMap();
        for (UINT faceOffset = 0; faceOffset < batch.FaceCount; ++faceOffset)
        {
            const UINT face = batch.FirstFace + faceOffset;
            cmdList->CopyResourceToCubeMap(cubeMap,
                                           crossAdapterCubeMaps[probeIndex][face]->GetPrimeResource(), face);
        }
        cmdList->TransitionBarrier(cubeMap, D3D12_RESOURCE_STATE_GENERIC_READ);
    }
    cmdList->FlushResourceBarriers();
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
