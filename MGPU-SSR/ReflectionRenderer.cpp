#include "ReflectionRenderer.h"

#include "Camera.h"
#include "d3dx12.h"
#include "GameObject.h"
#include "GameTimer.h"
#include "GCommandList.h"
#include "GDeviceFactory.h"
#include "GRootSignature.h"
#include "GShader.h"
#include "ShaderBuffersData.h"
#include "Transform.h"
#include "Window.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <utility>
#include <Renderer.h>

using namespace DirectX::SimpleMath;
using namespace PEPEngine::Graphics;

namespace
{
    constexpr float kSceneAmbientIntensity = 0.045f;
    constexpr UINT kMaxSsaaMultiplier = 4;
    constexpr UINT kReflectionProbeResolution = 256; // Change to 512 or 1024 for higher quality.
    constexpr float kReflectionProbeProxyHalfExtentXZ = 12.0f;
    constexpr float kReflectionProbeProxyMinY = 0.0f;
    constexpr float kReflectionProbeProxyMaxY = 30.0f;
    constexpr UINT kPointLightsSlot = StandardShaderSlot::Count;
    constexpr UINT kSpotLightsSlot = StandardShaderSlot::Count + 1;
    constexpr UINT kSsrSceneColorSlot = StandardShaderSlot::Count + 2;
    constexpr UINT kSsrSceneDepthSlot = StandardShaderSlot::Count + 3;
    constexpr UINT kSsrSceneNormalSlot = StandardShaderSlot::Count + 4;
    constexpr UINT kReflectionProbe0Slot = StandardShaderSlot::Count + 5;
    constexpr UINT kReflectionProbeConstantsSlot = StandardShaderSlot::Count + 6;
    constexpr UINT kSsrResourceSpace = 2;
    constexpr UINT kSsrSceneColorRegister = 0;
    constexpr UINT kSsrSceneDepthRegister = 1;
    constexpr UINT kSsrSceneNormalRegister = 2;
    constexpr UINT kReflectionProbeResourceSpace = 3;

    LightData MakeEmptyLightData()
    {
        LightData light{};
        light.Strength = Vector3::Zero;
        light.Direction = Vector3::Zero;
        light.Position = Vector3::Zero;
        light.FalloffStart = 0.0f;
        light.FalloffEnd = 0.0f;
        light.SpotPower = 0.0f;
        return light;
    }

    D3D12_CLEAR_FLAGS GetDepthClearFlags(const GTexture& depthStencil)
    {
        switch (depthStencil.GetD3D12ResourceDesc().Format)
        {
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
            return D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
        default:
            return D3D12_CLEAR_FLAG_DEPTH;
        }
    }
}

namespace Common
{
    ReflectionRenderer::ReflectionRenderer(std::shared_ptr<Window> window, Scene& scene,
                                           std::shared_ptr<Camera> camera,
                                           const DXGI_FORMAT backBufferFormat,
                                           const DXGI_FORMAT depthStencilFormat,
                                           const bool is4xMsaa,
                                           const UINT msaaQuality)
        : window(std::move(window)),
          scene(scene),
          camera(std::move(camera)),
          backBufferFormat(backBufferFormat),
          depthStencilFormat(depthStencilFormat),
          is4xMsaa(is4xMsaa),
          msaaQuality(msaaQuality)
    {
    }

    void ReflectionRenderer::Initialize(const std::shared_ptr<GCommandList>& cmdList)
    {
        const int width = window->GetClientWidth();
        const int height = window->GetClientHeight();

        shadowMap = std::make_unique<ShadowMap>(GDeviceFactory::GetDevice(), 4096, 4096);
        ssao = std::make_unique<SSAO>(GDeviceFactory::GetDevice(), cmdList, width, height);
        ssaa = std::make_unique<SSAA>(GDeviceFactory::GetDevice(), 1, width, height, depthStencilFormat);
        ssaa->OnResize(width, height);
        for (auto& probe : reflectionProbes)
        {
            probe = std::make_unique<CubeMapRenderTarget>(
                GDeviceFactory::GetDevice(), kReflectionProbeResolution, DXGI_FORMAT_R8G8B8A8_UNORM,
                DXGI_FORMAT_D32_FLOAT);
        }

        const auto primaryDevice = GDeviceFactory::GetDevice();
        reflectionProbeSrvTable = primaryDevice->AllocateDescriptors(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ReflectionProbeCount);
        D3D12_SHADER_RESOURCE_VIEW_DESC probeSrvDesc{};
        probeSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        probeSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        probeSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        probeSrvDesc.TextureCube.MostDetailedMip = 0;
        probeSrvDesc.TextureCube.MipLevels = 1;
        probeSrvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
        for (UINT probeIndex = 0; probeIndex < ReflectionProbeCount; ++probeIndex)
        {
            reflectionProbes[probeIndex]->GetCubeMap().CreateShaderResourceView(
                &probeSrvDesc, &reflectionProbeSrvTable, probeIndex);
        }
        BuildScreenRenderTargets();

        ssao->BuildDescriptors();

        BuildShadersAndInputLayout();
        BuildRootSignature();
        BuildSsaoRootSignature();
        BuildPSOs();
        InitializeMgpuSsr();

        ssao->SetPipelineData(*psos[RenderMode::Ssao], *psos[RenderMode::SsaoBlur]);
    }

    void ReflectionRenderer::OnResize()
    {
        viewport.Height = static_cast<float>(window->GetClientHeight());
        viewport.Width = static_cast<float>(window->GetClientWidth());
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        rect = {0, 0, window->GetClientWidth(), window->GetClientHeight()};

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
            window->GetBackBuffer(i).CreateRenderTargetView(&rtvDesc, &renderTargetMemory, i);
        }

        if (ssao != nullptr)
        {
            ssao->OnResize(window->GetClientWidth(), window->GetClientHeight());
            ssao->RebuildDescriptors();
        }

        if (ssaa != nullptr)
        {
            ssaa->OnResize(window->GetClientWidth(), window->GetClientHeight());
        }

        BuildScreenRenderTargets();
        BuildMgpuSsrResources();
    }

    void ReflectionRenderer::BuildScreenRenderTargets()
    {
        BuildColorRenderTarget(composedSceneColor, composedSceneColorRtv, composedSceneColorSrv,
                               L"Composed Scene Color");
        BuildColorRenderTarget(ssrOutputColor, ssrOutputColorRtv, ssrOutputColorSrv, L"SSR Output Color");
    }

    void ReflectionRenderer::BuildColorRenderTarget(GTexture& texture, GDescriptor& rtv, GDescriptor& srv,
                                                    const std::wstring& name) const
    {
        auto device = GDeviceFactory::GetDevice();
        const auto width = std::max(static_cast<UINT>(window->GetClientWidth()), 1u);
        const auto height = std::max(static_cast<UINT>(window->GetClientHeight()), 1u);
        const auto format = GetSRGBFormat(backBufferFormat);

        if (rtv.IsNull())
        {
            rtv = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
        }

        if (srv.IsNull())
        {
            srv = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
        }

        if (!texture.IsValid())
        {
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Alignment = 0;
            desc.Width = width;
            desc.Height = height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

            const auto clearValue = CD3DX12_CLEAR_VALUE(format, DirectX::Colors::Black);
            texture = GTexture(device, desc, name, TextureUsage::RenderTarget, &clearValue);
        }
        else
        {
            const auto desc = texture.GetD3D12ResourceDesc();
            if (desc.Width != width || desc.Height != height)
            {
                GTexture::Resize(texture, width, height, 1);
            }
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.Texture2D.PlaneSlice = 0;
        texture.CreateRenderTargetView(&rtvDesc, &rtv);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        texture.CreateShaderResourceView(&srvDesc, &srv);
    }

    void ReflectionRenderer::SetDebugMap(const UINT debugMap)
    {
#if defined(DEBUG) || defined(_DEBUG)
        this->debugMap = debugMap;
#else
        static_cast<void>(debugMap);
        this->debugMap = 0;
#endif
    }

    void ReflectionRenderer::SetFrameResource(FrameResource* frameResource)
    {
        currentFrameResource = frameResource;
    }

    void ReflectionRenderer::SetSsaaMultiplier(UINT multiplier)
    {
        if (ssaa == nullptr)
        {
            return;
        }

        if (multiplier < 1)
        {
            multiplier = 1;
        }
        else if (multiplier > kMaxSsaaMultiplier)
        {
            multiplier = kMaxSsaaMultiplier;
        }

        if (static_cast<UINT>(ssaa->GetMultiplier()) == multiplier)
        {
            return;
        }

        GDeviceFactory::GetDevice()->Flush();
        ssaa->SetMultiplier(multiplier, window->GetClientWidth(), window->GetClientHeight());
    }

    void ReflectionRenderer::SetUseMgpuSsr(const bool enabled)
    {
        useMgpuSsr = enabled;

        std::wstring message = L"[MGPU-SSR] Hybrid SSR requested: ";
        message += useMgpuSsr ? L"true" : L"false";
        message += L", available: ";
        message += mgpuSsrEnabled ? L"true" : L"false";
        message += L"\n";
        OutputDebugStringW(message.c_str());
    }

    void ReflectionRenderer::Update(const GameTimer& gt)
    {
        UpdateShadowTransform();
        UpdateLightBuffers();
        UpdateMainPassCB(gt);
        UpdateReflectionProbeCB();
        UpdateReflectionProbePassCBs();
        UpdateShadowPassCB();
        UpdateSsaoCB();
    }

    void ReflectionRenderer::Render(const std::shared_ptr<GCommandList>& cmdList)
    {
        RenderPrimaryBeforeSsr(cmdList);

        cmdList->StartMark(L"SSR Pass");
        DrawSsr(cmdList);
        cmdList->EndMark();

        cmdList->StartMark(L"Present Pass");
        DrawPresentBackBuffer(cmdList);
        cmdList->EndMark();
    }

    bool ReflectionRenderer::IsMgpuSsrEnabled() const
    {
        return mgpuSsrEnabled && useMgpuSsr;
    }

    void ReflectionRenderer::RenderMgpuPrimary(const std::shared_ptr<GCommandList>& cmdList)
    {
        if (!IsMgpuSsrEnabled())
        {
            Render(cmdList);
            return;
        }

        cmdList->StartMark(L"MGPU SSR Primary Frame");

        cmdList->StartMark(L"MGPU SSR Copy Result From Shared");
        CopySharedSsrOutputToPrimary(cmdList);
        cmdList->EndMark();

        cmdList->StartMark(L"Present Pass");
        DrawPresentBackBuffer(cmdList);
        cmdList->EndMark();

        RenderPrimaryBeforeSsr(cmdList);

        cmdList->StartMark(L"MGPU SSR Copy Inputs To Shared");
        CopyPrimarySsrInputsToShared(cmdList);
        cmdList->EndMark();

        cmdList->EndMark();
    }

    void ReflectionRenderer::RenderPrimaryBeforeSsr(const std::shared_ptr<GCommandList>& cmdList)
    {
        cmdList->StartMark(L"Prepare Render 3D");
        cmdList->SetRootSignature(*rootSignature.get());
        cmdList->SetDescriptorsHeap(scene.GetSrvHeap());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData, *currentFrameResource->MaterialBuffer);
        cmdList->SetRootShaderResourceView(kPointLightsSlot, *currentFrameResource->PointLightBuffer);
        cmdList->SetRootShaderResourceView(kSpotLightsSlot, *currentFrameResource->SpotLightBuffer);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scene.GetSrvHeap());
        cmdList->EndMark();

        cmdList->StartMark(L"Render 3D");

        cmdList->StartMark(L"Shadow Map Pass");
        DrawSceneToShadowMap(cmdList);
        cmdList->EndMark();

        cmdList->StartMark(L"Normal and Depth Pass");
        DrawNormals(cmdList);
        cmdList->EndMark();

        cmdList->StartMark(L"Compute SSAO");
        ssao->ComputeSsao(cmdList, currentFrameResource->SsaoConstantUploadBuffer, 3);
        cmdList->EndMark();

        cmdList->StartMark(L"Reflection Probe Pass (Base Scene Only - No SSR)");
        DrawReflectionProbes(cmdList);
        cmdList->EndMark();

        cmdList->StartMark(L"Main Scene Pass");
        DrawSceneToSsaaTarget(cmdList);
        cmdList->EndMark();

        cmdList->StartMark(L"Compose Downscale Pass");
        DrawSsaaTargetToComposedScene(cmdList);
        cmdList->EndMark();

        cmdList->EndMark();
    }

    void ReflectionRenderer::RenderSsrOnSecondGpu()
    {
        if (!IsMgpuSsrEnabled())
        {
            return;
        }

        auto secondQueue = secondDevice->GetCommandQueue(GQueueType::Graphics);
        if (secondGpuSsrFenceValue != 0 && !secondQueue->IsFinish(secondGpuSsrFenceValue))
        {
            return;
        }

        if (currentFrameResource->SecondMainPassConstantUploadBuffer == nullptr)
        {
            return;
        }

        currentFrameResource->SecondMainPassConstantUploadBuffer->CopyData(0, mainPassCB);

        auto secondCmdList = secondQueue->GetCommandList();

        secondCmdList->StartMark(L"MGPU SSR Pass");
        CopySharedSsrInputsToSecond(secondCmdList);
        DrawSsrOnSecondGpu(secondCmdList, *currentFrameResource->SecondMainPassConstantUploadBuffer);
        CopySecondSsrOutputToShared(secondCmdList);
        secondCmdList->EndMark();

        secondGpuSsrFenceValue = secondQueue->ExecuteCommandList(secondCmdList);
    }

    std::unique_ptr<GRootSignature> ReflectionRenderer::BuildRootSignature(const std::shared_ptr<GDevice>& device) const
    {
        auto signature = std::make_unique<GRootSignature>();

        CD3DX12_DESCRIPTOR_RANGE texParam[8];
        texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0);
        texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0);
        texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::AmbientMap - 3, 0);
        texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, static_cast<UINT>(scene.GetTextureCount()),
                         StandardShaderSlot::TexturesMap - 3, 0);
        texParam[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, kSsrSceneColorRegister, kSsrResourceSpace);
        texParam[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, kSsrSceneDepthRegister, kSsrResourceSpace);
        texParam[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, kSsrSceneNormalRegister, kSsrResourceSpace);
        texParam[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, ReflectionProbeCount, 0, kReflectionProbeResourceSpace);

        signature->AddConstantBufferParameter(0);
        signature->AddConstantBufferParameter(1);
        signature->AddShaderResourceView(0, 1);
        signature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        signature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        signature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        signature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        signature->AddShaderResourceView(1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        signature->AddShaderResourceView(2, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        signature->AddDescriptorParameter(&texParam[4], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        signature->AddDescriptorParameter(&texParam[5], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        signature->AddDescriptorParameter(&texParam[6], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        signature->AddDescriptorParameter(&texParam[7], 1, D3D12_SHADER_VISIBILITY_PIXEL);
        signature->AddConstantBufferParameter(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);

        signature->Initialize(device);
        return signature;
    }

    void ReflectionRenderer::BuildRootSignature()
    {
        rootSignature = BuildRootSignature(GDeviceFactory::GetDevice());
    }

    void ReflectionRenderer::BuildSsaoRootSignature()
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
            0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
            1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
            2, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
            D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, 0.0f, 0,
            D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);
        const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
            3, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

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

    void ReflectionRenderer::BuildShadersAndInputLayout()
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

        shaders["StandardVertex"] = std::make_unique<GShader>(L"Shaders\\Default.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
        shaders["AlphaDrop"] = std::make_unique<GShader>(L"Shaders\\Default.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1");
        shaders["shadowVS"] = std::make_unique<GShader>(L"Shaders\\Shadows.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
        shaders["shadowOpaquePS"] = std::make_unique<GShader>(L"Shaders\\Shadows.hlsl", PixelShader, nullptr, "PS", "ps_5_1");
        shaders["shadowOpaqueDropPS"] = std::make_unique<GShader>(L"Shaders\\Shadows.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1");
        shaders["OpaquePixel"] = std::make_unique<GShader>(L"Shaders\\Default.hlsl", PixelShader, defines, "PS", "ps_5_1");
        shaders["SkyBoxVertex"] = std::make_unique<GShader>(L"Shaders\\SkyBoxShader.hlsl", VertexShader, defines, "SKYMAP_VS", "vs_5_1");
        shaders["SkyBoxPixel"] = std::make_unique<GShader>(L"Shaders\\SkyBoxShader.hlsl", PixelShader, defines, "SKYMAP_PS", "ps_5_1");
        shaders["ReflectionsVertex"] = std::make_unique<GShader>(L"Shaders\\Reflections.hlsl", VertexShader, nullptr, "REFLECTIONS_VS", "vs_5_1");
        shaders["ReflectionsPixel"] = std::make_unique<GShader>(L"Shaders\\Reflections.hlsl", PixelShader, nullptr, "REFLECTIONS_PS", "ps_5_1");
        shaders["treeSpriteVS"] = std::make_unique<GShader>(L"Shaders\\TreeSprite.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
        shaders["treeSpriteGS"] = std::make_unique<GShader>(L"Shaders\\TreeSprite.hlsl", GeometryShader, nullptr, "GS", "gs_5_1");
        shaders["treeSpritePS"] = std::make_unique<GShader>(L"Shaders\\TreeSprite.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1");
        shaders["drawNormalsVS"] = std::make_unique<GShader>(L"Shaders\\DrawNormals.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
        shaders["drawNormalsPS"] = std::make_unique<GShader>(L"Shaders\\DrawNormals.hlsl", PixelShader, nullptr, "PS", "ps_5_1");
        shaders["drawNormalsAlphaDropPS"] = std::make_unique<GShader>(L"Shaders\\DrawNormals.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1");
        shaders["ssaoVS"] = std::make_unique<GShader>(L"Shaders\\Ssao.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
        shaders["ssaoPS"] = std::make_unique<GShader>(L"Shaders\\Ssao.hlsl", PixelShader, nullptr, "PS", "ps_5_1");
        shaders["ssaoBlurVS"] = std::make_unique<GShader>(L"Shaders\\SsaoBlur.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
        shaders["ssaoBlurPS"] = std::make_unique<GShader>(L"Shaders\\SsaoBlur.hlsl", PixelShader, nullptr, "PS", "ps_5_1");
        shaders["quadVS"] = std::make_unique<GShader>(L"Shaders\\Quad.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
        shaders["quadPS"] = std::make_unique<GShader>(L"Shaders\\Quad.hlsl", PixelShader, nullptr, "PS", "ps_5_1");
        shaders["presentVS"] = std::make_unique<GShader>(L"Shaders\\Present.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
        shaders["presentPS"] = std::make_unique<GShader>(L"Shaders\\Present.hlsl", PixelShader, nullptr, "PS", "ps_5_1");
        shaders["ssrVS"] = std::make_unique<GShader>(L"Shaders\\SSR.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
        shaders["ssrPS"] = std::make_unique<GShader>(L"Shaders\\SSR.hlsl", PixelShader, nullptr, "PS", "ps_5_1");
#if defined(DEBUG) || defined(_DEBUG)
        shaders["ssrDebugPS"] = std::make_unique<GShader>(L"Shaders\\SsrDebug.hlsl", PixelShader, nullptr, "PS", "ps_5_1");
#endif

        for (auto&& pair : shaders)
        {
            pair.second->LoadAndCompile();
        }

        defaultInputLayout =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        treeSpriteInputLayout =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };
    }

    void ReflectionRenderer::BuildPSOs()
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;
        ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
        basePsoDesc.InputLayout = {defaultInputLayout.data(), static_cast<UINT>(defaultInputLayout.size())};
        basePsoDesc.pRootSignature = rootSignature->GetNativeSignature().Get();
        basePsoDesc.VS = shaders["StandardVertex"]->GetShaderResource();
        basePsoDesc.PS = shaders["OpaquePixel"]->GetShaderResource();
        basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        basePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        basePsoDesc.SampleMask = UINT_MAX;
        basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        basePsoDesc.NumRenderTargets = 1;
        basePsoDesc.RTVFormats[0] = GetSRGBFormat(backBufferFormat);
        basePsoDesc.SampleDesc.Count = 1;
        basePsoDesc.SampleDesc.Quality = 0;
        basePsoDesc.DSVFormat = depthStencilFormat;

        auto depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        auto rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

        auto opaquePSO = std::make_unique<GraphicPSO>();
        opaquePSO->SetPsoDesc(basePsoDesc);
        opaquePSO->SetDepthStencilState(depthStencilDesc);

        auto reflectionPSO = std::make_unique<GraphicPSO>(RenderMode::Reflection);
        reflectionPSO->SetPsoDesc(opaquePSO->GetPsoDescription());
        reflectionPSO->SetShader(shaders["ReflectionsVertex"].get());
        reflectionPSO->SetShader(shaders["ReflectionsPixel"].get());

        auto alphaDropPso = std::make_unique<GraphicPSO>(RenderMode::OpaqueAlphaDrop);
        alphaDropPso->SetPsoDesc(opaquePSO->GetPsoDescription());
        alphaDropPso->SetShader(shaders["AlphaDrop"].get());

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

        auto transparentPSO = std::make_unique<GraphicPSO>(RenderMode::Transparent);
        transparentPSO->SetPsoDesc(basePsoDesc);
        D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc = {};
        transparencyBlendDesc.BlendEnable = true;
        transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
        transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
        transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
        transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
        transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        transparentPSO->SetRenderTargetBlendState(0, transparencyBlendDesc);

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
        quadPso->SetShader(shaders["presentVS"].get());
        quadPso->SetShader(shaders["presentPS"].get());
        quadPso->SetSampleCount(1);
        quadPso->SetSampleQuality(0);
        quadPso->SetDSVFormat(DXGI_FORMAT_UNKNOWN);
        depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        depthStencilDesc.DepthEnable = false;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        quadPso->SetDepthStencilState(depthStencilDesc);

        auto ssrPso = std::make_unique<GraphicPSO>(RenderMode::Ssr);
        ssrPso->SetPsoDesc(quadPso->GetPsoDescription());
        ssrPso->SetInputLayout({nullptr, 0});
        ssrPso->SetShader(shaders["ssrVS"].get());
        ssrPso->SetShader(shaders["ssrPS"].get());

#if defined(DEBUG) || defined(_DEBUG)
        auto ssrDebugPso = std::make_unique<GraphicPSO>(RenderMode::SsrDebug);
        ssrDebugPso->SetPsoDesc(quadPso->GetPsoDescription());
        ssrDebugPso->SetInputLayout({nullptr, 0});
        ssrDebugPso->SetShader(shaders["ssrVS"].get());
        ssrDebugPso->SetShader(shaders["ssrDebugPS"].get());
#endif

        psos[opaquePSO->GetRenderMode()] = std::move(opaquePSO);
        psos[reflectionPSO->GetRenderMode()] = std::move(reflectionPSO);
        psos[transparentPSO->GetRenderMode()] = std::move(transparentPSO);
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
        psos[ssrPso->GetRenderMode()] = std::move(ssrPso);
#if defined(DEBUG) || defined(_DEBUG)
        psos[ssrDebugPso->GetRenderMode()] = std::move(ssrDebugPso);
#endif

        for (auto& pso : psos)
        {
            pso.second->Initialize(GDeviceFactory::GetDevice());
        }
    }

    void ReflectionRenderer::BuildSecondGpuSsrPSOs()
    {
        if (secondGpuSsrRootSignature == nullptr || secondDevice == nullptr)
        {
            return;
        }

        secondGpuSsrPsos.clear();

        auto ssrPso = std::make_unique<GraphicPSO>(RenderMode::Ssr);
        ssrPso->SetPsoDesc(psos[RenderMode::Ssr]->GetPsoDescription());
        ssrPso->SetRootSignature(*secondGpuSsrRootSignature);
        ssrPso->Initialize(secondDevice);
        secondGpuSsrPsos[RenderMode::Ssr] = std::move(ssrPso);

#if defined(DEBUG) || defined(_DEBUG)
        auto ssrDebugPso = std::make_unique<GraphicPSO>(RenderMode::SsrDebug);
        ssrDebugPso->SetPsoDesc(psos[RenderMode::SsrDebug]->GetPsoDescription());
        ssrDebugPso->SetRootSignature(*secondGpuSsrRootSignature);
        ssrDebugPso->Initialize(secondDevice);
        secondGpuSsrPsos[RenderMode::SsrDebug] = std::move(ssrDebugPso);
#endif
    }

    void ReflectionRenderer::InitializeMgpuSsr()
    {
        mgpuSsrEnabled = false;

        auto& devices = GDeviceFactory::GetAllDevices(false);
        if (devices.size() <= GraphicAdapterSecond)
        {
            OutputDebugStringW(L"[MGPU-SSR] Second hardware adapter was not found. SSR stays on primary GPU.\n");
            return;
        }

        auto primaryDevice = GDeviceFactory::GetDevice(GraphicAdapterPrimary);
        secondDevice = GDeviceFactory::GetDevice(GraphicAdapterSecond);
        if (!primaryDevice->IsCrossAdapterTextureSupported() || !secondDevice->IsCrossAdapterTextureSupported())
        {
            OutputDebugStringW(L"[MGPU-SSR] Cross-adapter row-major texture support is reported as unavailable; trying shared resources like MGPU AO.\n");
        }

        secondGpuSsrRootSignature = BuildRootSignature(secondDevice);

        BuildSecondGpuSsrPSOs();
        BuildMgpuSsrResources();

        mgpuSsrEnabled = secondGpuSceneColor.IsValid() &&
            secondGpuSceneDepth.IsValid() &&
            secondGpuSceneNormal.IsValid() &&
            secondGpuSsrOutputColor.IsValid() &&
            sharedSceneColor != nullptr &&
            sharedSceneDepth != nullptr &&
            sharedSceneNormal != nullptr &&
            sharedSsrOutputColor != nullptr;

        if (mgpuSsrEnabled)
        {
            std::wstring message = L"[MGPU-SSR] SSR second GPU: ";
            message += secondDevice->GetName();
            message += L"\n";
            OutputDebugStringW(message.c_str());
        }
        else
        {
            OutputDebugStringW(L"[MGPU-SSR] Shared resources were not initialized. SSR stays on primary GPU.\n");
        }
    }

    void ReflectionRenderer::CreateSecondGpuTextureLike(GTexture& texture,
                                                        const GTexture& source,
                                                        const std::wstring& name,
                                                        const TextureUsage usage,
                                                        const D3D12_CLEAR_VALUE* clearValue) const
    {
        if (secondDevice == nullptr || !source.IsValid())
        {
            return;
        }

        const auto sourceDesc = source.GetD3D12ResourceDesc();
        if (!texture.IsValid())
        {
            texture = GTexture(secondDevice, sourceDesc, name, usage, clearValue);
            return;
        }

        const auto currentDesc = texture.GetD3D12ResourceDesc();
        if (currentDesc.Width != sourceDesc.Width ||
            currentDesc.Height != sourceDesc.Height ||
            currentDesc.DepthOrArraySize != sourceDesc.DepthOrArraySize)
        {
            GTexture::Resize(texture, static_cast<uint32_t>(sourceDesc.Width), sourceDesc.Height,
                             sourceDesc.DepthOrArraySize);
        }
    }

    void ReflectionRenderer::BuildMgpuSsrResources()
    {
        if (secondDevice == nullptr ||
            !composedSceneColor.IsValid() ||
            ssao == nullptr ||
            !ssao->NormalDepthMap().IsValid() ||
            !ssao->NormalMap().IsValid() ||
            !ssrOutputColor.IsValid())
        {
            return;
        }

        const auto primaryDevice = GDeviceFactory::GetDevice(GraphicAdapterPrimary);

        CreateSecondGpuTextureLike(secondGpuSceneColor, composedSceneColor, L"Second GPU SSR Scene Color",
                                   TextureUsage::RenderTarget);
        CreateSecondGpuTextureLike(secondGpuSceneDepth, ssao->NormalDepthMap(), L"Second GPU SSR Scene Depth",
                                   TextureUsage::Depth);
        CreateSecondGpuTextureLike(secondGpuSceneNormal, ssao->NormalMap(), L"Second GPU SSR Scene Normal",
                                   TextureUsage::Normalmap);

        const auto ssrFormat = ssrOutputColor.GetD3D12ResourceDesc().Format;
        const auto clearValue = CD3DX12_CLEAR_VALUE(ssrFormat, DirectX::Colors::Black);
        CreateSecondGpuTextureLike(secondGpuSsrOutputColor, ssrOutputColor, L"Second GPU SSR Output Color",
                                   TextureUsage::RenderTarget, &clearValue);

        auto sceneColorDesc = composedSceneColor.GetD3D12ResourceDesc();
        auto sceneDepthDesc = ssao->NormalDepthMap().GetD3D12ResourceDesc();
        auto sceneNormalDesc = ssao->NormalMap().GetD3D12ResourceDesc();
        auto ssrOutputDesc = ssrOutputColor.GetD3D12ResourceDesc();

        sharedSceneColor = std::make_unique<::GCrossAdapterResource>(sceneColorDesc, primaryDevice, secondDevice,
                                                                     L"MGPU SSR Shared Scene Color");
        sharedSceneDepth = std::make_unique<::GCrossAdapterResource>(sceneDepthDesc, primaryDevice, secondDevice,
                                                                     L"MGPU SSR Shared Scene Depth");
        sharedSceneNormal = std::make_unique<::GCrossAdapterResource>(sceneNormalDesc, primaryDevice, secondDevice,
                                                                      L"MGPU SSR Shared Scene Normal");
        sharedSsrOutputColor = std::make_unique<::GCrossAdapterResource>(ssrOutputDesc, primaryDevice, secondDevice,
                                                                         L"MGPU SSR Shared Output Color");

        BuildSecondGpuSsrDescriptors();
    }

    void ReflectionRenderer::BuildSecondGpuSsrDescriptors()
    {
        if (secondDevice == nullptr ||
            !secondGpuSceneColor.IsValid() ||
            !secondGpuSceneDepth.IsValid() ||
            !secondGpuSceneNormal.IsValid() ||
            !secondGpuSsrOutputColor.IsValid())
        {
            return;
        }

        if (secondGpuSsrInputSrv.IsNull())
        {
            secondGpuSsrInputSrv = secondDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);
        }

        if (secondGpuSsrOutputRtv.IsNull())
        {
            secondGpuSsrOutputRtv = secondDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        srvDesc.Format = secondGpuSceneColor.GetD3D12ResourceDesc().Format;
        secondGpuSceneColor.CreateShaderResourceView(&srvDesc, &secondGpuSsrInputSrv, 0);

        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        secondGpuSceneDepth.CreateShaderResourceView(&srvDesc, &secondGpuSsrInputSrv, 1);

        srvDesc.Format = secondGpuSceneNormal.GetD3D12ResourceDesc().Format;
        secondGpuSceneNormal.CreateShaderResourceView(&srvDesc, &secondGpuSsrInputSrv, 2);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = secondGpuSsrOutputColor.GetD3D12ResourceDesc().Format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.Texture2D.PlaneSlice = 0;
        secondGpuSsrOutputColor.CreateRenderTargetView(&rtvDesc, &secondGpuSsrOutputRtv);
    }

    void ReflectionRenderer::UpdateShadowTransform()
    {
        Vector3 lightDir = lightDirections[0];
        const auto sceneBounds = scene.GetBounds();
        Vector3 lightPos = -2.0f * sceneBounds.Radius * lightDir;
        Vector3 targetPos = sceneBounds.Center;
        Matrix currentLightView = XMMatrixLookAtLH(lightPos, targetPos, Vector3::Up);

        lightPosW = lightPos;
        Vector3 sphereCenterLS = Vector3::Transform(targetPos, currentLightView);

        float l = sphereCenterLS.x - sceneBounds.Radius;
        float b = sphereCenterLS.y - sceneBounds.Radius;
        float n = sphereCenterLS.z - sceneBounds.Radius;
        float r = sphereCenterLS.x + sceneBounds.Radius;
        float t = sphereCenterLS.y + sceneBounds.Radius;
        float f = sphereCenterLS.z + sceneBounds.Radius;

        lightNearZ = n;
        lightFarZ = f;
        Matrix currentLightProj = DirectX::XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

        Matrix T(
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, -0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f);

        lightView = currentLightView;
        lightProj = currentLightProj;
        shadowTransform = lightView * lightProj * T;
    }

    void ReflectionRenderer::UpdateShadowPassCB()
    {
        auto viewProj = lightView * lightProj;
        auto invView = lightView.Invert();
        auto invProj = lightProj.Invert();
        auto invViewProj = viewProj.Invert();

        UINT w = shadowMap->Width();
        UINT h = shadowMap->Height();

        shadowPassCB.View = lightView.Transpose();
        shadowPassCB.InvView = invView.Transpose();
        shadowPassCB.Proj = lightProj.Transpose();
        shadowPassCB.InvProj = invProj.Transpose();
        shadowPassCB.ViewProj = viewProj.Transpose();
        shadowPassCB.InvViewProj = invViewProj.Transpose();
        shadowPassCB.EyePosW = lightPosW;
        shadowPassCB.RenderTargetSize = Vector2(static_cast<float>(w), static_cast<float>(h));
        shadowPassCB.InvRenderTargetSize = Vector2(1.0f / w, 1.0f / h);
        shadowPassCB.NearZ = lightNearZ;
        shadowPassCB.FarZ = lightFarZ;

        currentFrameResource->ShadowPassConstantUploadBuffer->CopyData(0, shadowPassCB);
    }

    void ReflectionRenderer::UpdateMainPassCB(const GameTimer& gt)
    {
        auto view = camera->GetViewMatrix();
        auto proj = camera->GetProjectionMatrix();
        auto viewProj = view * proj;
        auto invView = view.Invert();
        auto invProj = proj.Invert();
        auto invViewProj = viewProj.Invert();

        Matrix T(
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, -0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f);
        Matrix viewProjTex = XMMatrixMultiply(viewProj, T);

#if defined(DEBUG) || defined(_DEBUG)
        mainPassCB.debugMap = debugMap;
#else
        mainPassCB.debugMap = 0.0f;
#endif
        mainPassCB.View = view.Transpose();
        mainPassCB.InvView = invView.Transpose();
        mainPassCB.Proj = proj.Transpose();
        mainPassCB.InvProj = invProj.Transpose();
        mainPassCB.ViewProj = viewProj.Transpose();
        mainPassCB.InvViewProj = invViewProj.Transpose();
        mainPassCB.ViewProjTex = viewProjTex.Transpose();
        mainPassCB.ShadowTransform = shadowTransform.Transpose();
        mainPassCB.EyePosW = camera->gameObject->GetTransform()->GetWorldPosition();

        const auto targetViewport = ssaa != nullptr ? ssaa->GetViewPort() : viewport;
        mainPassCB.RenderTargetSize = Vector2(targetViewport.Width, targetViewport.Height);
        mainPassCB.InvRenderTargetSize = Vector2(1.0f / mainPassCB.RenderTargetSize.x,
                                                 1.0f / mainPassCB.RenderTargetSize.y);
        mainPassCB.NearZ = camera->GetNearZ();
        mainPassCB.FarZ = camera->GetFarZ();
        mainPassCB.TotalTime = gt.TotalTime();
        mainPassCB.DeltaTime = gt.DeltaTime();
        mainPassCB.AmbientLight = Vector4(kSceneAmbientIntensity, kSceneAmbientIntensity, kSceneAmbientIntensity, 1.0f);
        mainPassCB.SsrRaySettings = Vector4(80.0f, 0.45f, 0.18f, 0.85f);
        mainPassCB.SsrResolveSettings = Vector4(128.0f, 8.0f, 16.0f, 0.0f);

        mainPassCB.DirectionalLight = MakeEmptyLightData();

        for (auto* light : scene.GetLights())
        {
            if (light->Type() == Directional)
            {
                mainPassCB.DirectionalLight = light->GetData();
                break;
            }
        }

        currentFrameResource->MainPassConstantUploadBuffer->CopyData(0, mainPassCB);
    }

    void ReflectionRenderer::UpdateReflectionProbeCB()
    {
        if (currentFrameResource == nullptr ||
            currentFrameResource->ReflectionProbeConstantUploadBuffer == nullptr)
        {
            return;
        }

        const auto& probeCenters = scene.GetReflectionProbeCenters();
        if (probeCenters.empty())
        {
            return;
        }

        ReflectionProbeConstants probeConstants;
        for (UINT probeIndex = 0; probeIndex < ReflectionProbeCount; ++probeIndex)
        {
            const Vector3& probePosition = probeCenters[probeIndex];
            auto& probeData = probeConstants.Probes[probeIndex];
            probeData.Position =
                Vector4(probePosition.x, probePosition.y, probePosition.z, 1.0f);
            probeData.ProxyBoxMin =
                Vector4(probePosition.x - kReflectionProbeProxyHalfExtentXZ,
                        kReflectionProbeProxyMinY,
                        probePosition.z - kReflectionProbeProxyHalfExtentXZ,
                        0.0f);
            probeData.ProxyBoxMax =
                Vector4(probePosition.x + kReflectionProbeProxyHalfExtentXZ,
                        kReflectionProbeProxyMaxY,
                        probePosition.z + kReflectionProbeProxyHalfExtentXZ,
                        0.0f);
        }
        currentFrameResource->ReflectionProbeConstantUploadBuffer->CopyData(0, probeConstants);
    }

    void ReflectionRenderer::UpdateReflectionProbePassCBs()
    {
        if (reflectionProbesBaked || currentFrameResource == nullptr ||
            currentFrameResource->ReflectionProbePassConstantUploadBuffer == nullptr)
        {
            return;
        }

        const auto& probeCenters = scene.GetReflectionProbeCenters();
        const std::array<Vector3, CubeMapRenderTarget::FaceCount> directions =
        {
            Vector3(1.0f, 0.0f, 0.0f), Vector3(-1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f), Vector3(0.0f, 0.0f, -1.0f)
        };
        const std::array<Vector3, CubeMapRenderTarget::FaceCount> upDirections =
        {
            Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f),
            Vector3(0.0f, 0.0f, -1.0f), Vector3(0.0f, 0.0f, 1.0f),
            Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f)
        };

        constexpr float nearZ = 0.1f;
        constexpr float farZ = 500.0f;
        const Matrix proj = DirectX::XMMatrixPerspectiveFovLH(0.5f * DirectX::XM_PI, 1.0f, nearZ, farZ);
        const Matrix textureTransform(
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, -0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f);

        for (UINT probeIndex = 0; probeIndex < ReflectionProbeCount; ++probeIndex)
        {
            const Vector3 center = probeCenters[probeIndex];
            for (UINT face = 0; face < CubeMapRenderTarget::FaceCount; ++face)
            {
                const Vector3 target = center + directions[face];
                const Matrix view = XMMatrixLookAtLH(center, target, upDirections[face]);
                const Matrix viewProj = view * proj;
                ReflectionPassConstants probePass = mainPassCB;
                probePass.View = view.Transpose();
                probePass.InvView = view.Invert().Transpose();
                probePass.Proj = proj.Transpose();
                probePass.InvProj = proj.Invert().Transpose();
                probePass.ViewProj = viewProj.Transpose();
                probePass.InvViewProj = viewProj.Invert().Transpose();
                probePass.ViewProjTex = (viewProj * textureTransform).Transpose();
                probePass.EyePosW = center;
                probePass.CameraForwardVector = directions[face];
                probePass.RenderTargetSize = Vector2(static_cast<float>(kReflectionProbeResolution),
                                                     static_cast<float>(kReflectionProbeResolution));
                probePass.InvRenderTargetSize = Vector2(1.0f / static_cast<float>(kReflectionProbeResolution),
                                                        1.0f / static_cast<float>(kReflectionProbeResolution));
                probePass.NearZ = nearZ;
                probePass.FarZ = farZ;
                const UINT passIndex = probeIndex * CubeMapRenderTarget::FaceCount + face;
                currentFrameResource->ReflectionProbePassConstantUploadBuffer->CopyData(passIndex, probePass);
            }
        }
    }
    void ReflectionRenderer::UpdateLightBuffers()
    {
        UINT pointLightIndex = 0;
        UINT spotLightIndex = 0;
        for (auto* light : scene.GetLights())
        {
            if (light->Type() == Point && pointLightIndex < currentFrameResource->PointLightCapacity)
            {
                currentFrameResource->PointLightBuffer->CopyData(pointLightIndex, light->GetData());
                ++pointLightIndex;
            }
            else if (light->Type() == Spot && spotLightIndex < currentFrameResource->SpotLightCapacity)
            {
                currentFrameResource->SpotLightBuffer->CopyData(spotLightIndex, light->GetData());
                ++spotLightIndex;
            }
        }

        mainPassCB.PointLightCount = pointLightIndex;
        mainPassCB.SpotLightCount = spotLightIndex;
    }

    void ReflectionRenderer::UpdateSsaoCB()
    {
        SsaoConstants ssaoCB;

        auto P = camera->GetProjectionMatrix();
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
        ssaoCB.OcclusionRadius = 0.5f;
        ssaoCB.OcclusionFadeStart = 0.2f;
        ssaoCB.OcclusionFadeEnd = 1.0f;
        ssaoCB.SurfaceEpsilon = 0.05f;

        currentFrameResource->SsaoConstantUploadBuffer->CopyData(0, ssaoCB);
    }

    void ReflectionRenderer::DrawSceneToShadowMap(const std::shared_ptr<GCommandList>& cmdList)
    {
        DrawSceneToRenderTarget(cmdList,
                                nullptr,
                                nullptr,
                                &shadowMap->GetTexture(),
                                shadowMap->GetDsv(),
                                *currentFrameResource->ShadowPassConstantUploadBuffer);
    }

    void ReflectionRenderer::DrawNormals(const std::shared_ptr<GCommandList>& cmdList)
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
        cmdList->ClearDepthStencil(normalMapDsv, 0, GetDepthClearFlags(normalDepthMap), 1.0f, 0);
        cmdList->SetRenderTargets(1, normalMapRtv, 0, normalMapDsv);

        cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                           *currentFrameResource->MainPassConstantUploadBuffer);

        cmdList->SetPipelineState(*psos[RenderMode::DrawNormalsOpaque]);
        scene.Draw(cmdList, RenderMode::Opaque);
        cmdList->SetPipelineState(*psos[RenderMode::DrawNormalsOpaqueDrop]);
        scene.Draw(cmdList, RenderMode::OpaqueAlphaDrop);

        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }

    void ReflectionRenderer::DrawReflectionProbes(const std::shared_ptr<GCommandList>& cmdList)
    {
        if (reflectionProbesBaked || currentFrameResource == nullptr ||
            currentFrameResource->ReflectionProbePassConstantUploadBuffer == nullptr)
        {
            return;
        }

        for (const auto& probe : reflectionProbes)
        {
            if (probe == nullptr)
            {
                return;
            }
        }

        cmdList->TransitionBarrier(shadowMap->GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(ssao->AmbientMap(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->FlushResourceBarriers();

        cmdList->SetRootSignature(*rootSignature);
        cmdList->SetDescriptorsHeap(scene.GetSrvHeap());
        cmdList->SetDescriptorsHeap(shadowMap->GetSrv());
        cmdList->SetDescriptorsHeap(ssao->AmbientMapSrv());
        cmdList->UpdateDescriptorHeaps();
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffer);
        cmdList->SetRootShaderResourceView(kPointLightsSlot, *currentFrameResource->PointLightBuffer);
        cmdList->SetRootShaderResourceView(kSpotLightsSlot, *currentFrameResource->SpotLightBuffer);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scene.GetSrvHeap());
        cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowMap->GetSrv());
        cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ssao->AmbientMapSrv());

        for (UINT probeIndex = 0; probeIndex < ReflectionProbeCount; ++probeIndex)
        {
            auto* probe = reflectionProbes[probeIndex].get();
            auto& cubeMap = probe->GetCubeMap();
            auto& depthMap = probe->GetDepthMap();
            const auto probeViewport = probe->GetViewport();
            const auto probeRect = probe->GetScissorRect();

            cmdList->TransitionBarrier(cubeMap, D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmdList->TransitionBarrier(depthMap, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            cmdList->FlushResourceBarriers();
            cmdList->SetViewports(&probeViewport, 1);
            cmdList->SetScissorRects(&probeRect, 1);

            for (UINT face = 0; face < CubeMapRenderTarget::FaceCount; ++face)
            {
                const UINT passIndex = probeIndex * CubeMapRenderTarget::FaceCount + face;
                auto faceRtv = probe->GetRTV(face);
                cmdList->SetRootConstantBufferView(
                    StandardShaderSlot::CameraData,
                    *currentFrameResource->ReflectionProbePassConstantUploadBuffer,
                    passIndex);
                cmdList->ClearRenderTarget(&faceRtv, 0, DirectX::Colors::Black);
                cmdList->ClearDepthStencil(probe->GetDSV(), 0, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0);
                cmdList->SetRenderTargets(1, &faceRtv, 0, probe->GetDSV());

                // Probe captures intentionally contain only the base scene. RenderMode::Reflection
                // and the fullscreen SSR pass are excluded to prevent recursive reflections and
                // screen-space history from being baked into the cubemaps.

                cmdList->SetPipelineState(*psos[RenderMode::SkyBox]);
                scene.Draw(cmdList, RenderMode::SkyBox);

                cmdList->SetPipelineState(*psos[RenderMode::Opaque]);
                scene.Draw(cmdList, RenderMode::Opaque);

                cmdList->SetPipelineState(*psos[RenderMode::OpaqueAlphaDrop]);
                scene.Draw(cmdList, RenderMode::OpaqueAlphaDrop);

                cmdList->SetPipelineState(*psos[RenderMode::Transparent]);
                scene.Draw(cmdList, RenderMode::Transparent);
            }

            cmdList->TransitionBarrier(cubeMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmdList->TransitionBarrier(depthMap, D3D12_RESOURCE_STATE_COMMON);
            cmdList->FlushResourceBarriers();
        }

        reflectionProbesBaked = true;
    }
    void ReflectionRenderer::DrawSceneToRenderTarget(const std::shared_ptr<GCommandList>& cmdList,
                                                     GTexture* renderTarget,
                                                     const GDescriptor* renderTargetView,
                                                     GTexture* depthStencil,
                                                     const GDescriptor* depthStencilView,
                                                     const ConstantUploadBuffer<ReflectionPassConstants>& passConstants)
    {
        assert((renderTarget != nullptr) == (renderTargetView != nullptr));
        assert((depthStencil != nullptr) == (depthStencilView != nullptr));

        const bool hasRenderTarget = renderTarget != nullptr && renderTargetView != nullptr;
        const bool hasDepthStencil = depthStencil != nullptr && depthStencilView != nullptr;
        const auto targetResource = hasRenderTarget ? renderTarget : depthStencil;
        assert(targetResource != nullptr && "DrawSceneToRenderTarget requires a render target or depth stencil.");
        const auto targetDesc = targetResource->GetD3D12ResourceDesc();
        const D3D12_VIEWPORT targetViewport = {
            0.0f, 0.0f,
            static_cast<float>(targetDesc.Width),
            static_cast<float>(targetDesc.Height),
            0.0f, 1.0f
        };
        const D3D12_RECT targetRect = {
            0, 0,
            static_cast<LONG>(targetDesc.Width),
            static_cast<LONG>(targetDesc.Height)
        };

        cmdList->SetViewports(&targetViewport, 1);
        cmdList->SetScissorRects(&targetRect, 1);

        if (hasRenderTarget)
        {
            cmdList->TransitionBarrier(*renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }

        if (hasDepthStencil)
        {
            cmdList->TransitionBarrier(*depthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        }

        cmdList->FlushResourceBarriers();

        if (hasRenderTarget)
        {
            cmdList->ClearRenderTarget(renderTargetView);
        }

        if (hasDepthStencil)
        {
            cmdList->ClearDepthStencil(depthStencilView, 0,
                                       GetDepthClearFlags(*depthStencil), 1.0f, 0);
        }

        cmdList->SetRenderTargets(hasRenderTarget ? 1 : 0,
                                  renderTargetView,
                                  0,
                                  depthStencilView);

        cmdList->SetRootSignature(*rootSignature.get());
        cmdList->SetDescriptorsHeap(scene.GetSrvHeap());
        if (hasRenderTarget)
        {
            cmdList->SetDescriptorsHeap(shadowMap->GetSrv());
            cmdList->SetDescriptorsHeap(ssao->AmbientMapSrv());
            cmdList->SetDescriptorsHeap(&reflectionProbeSrvTable);
        }
        cmdList->UpdateDescriptorHeaps();

        cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                           passConstants);
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData, *currentFrameResource->MaterialBuffer);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, scene.GetSrvHeap());

        if (!hasRenderTarget)
        {
            cmdList->SetPipelineState(*psos[RenderMode::ShadowMapOpaque]);
            scene.Draw(cmdList, RenderMode::Opaque);

            cmdList->SetPipelineState(*psos[RenderMode::ShadowMapOpaqueDrop]);
            scene.Draw(cmdList, RenderMode::OpaqueAlphaDrop);
        }

        if (hasRenderTarget)
        {
            cmdList->SetRootShaderResourceView(kPointLightsSlot, *currentFrameResource->PointLightBuffer);
            cmdList->SetRootShaderResourceView(kSpotLightsSlot, *currentFrameResource->SpotLightBuffer);
            cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowMap->GetSrv());
            cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ssao->AmbientMapSrv());

            cmdList->SetPipelineState(*psos[RenderMode::SkyBox]);
            scene.Draw(cmdList, RenderMode::SkyBox);

            cmdList->SetPipelineState(*psos[RenderMode::Opaque]);
            scene.Draw(cmdList, RenderMode::Opaque);

            cmdList->SetRootConstantBufferView(
                kReflectionProbeConstantsSlot,
                *currentFrameResource->ReflectionProbeConstantUploadBuffer);
            cmdList->SetRootDescriptorTable(kReflectionProbe0Slot, &reflectionProbeSrvTable);
            cmdList->SetPipelineState(*psos[RenderMode::Reflection]);
            scene.Draw(cmdList, RenderMode::Reflection);

            cmdList->SetPipelineState(*psos[RenderMode::OpaqueAlphaDrop]);
            scene.Draw(cmdList, RenderMode::OpaqueAlphaDrop);

            cmdList->SetPipelineState(*psos[RenderMode::Transparent]);
            scene.Draw(cmdList, RenderMode::Transparent);
        }

        if (hasRenderTarget)
        {
            cmdList->TransitionBarrier(*renderTarget, D3D12_RESOURCE_STATE_COMMON);
        }

        if (hasDepthStencil)
        {
            cmdList->TransitionBarrier(*depthStencil, D3D12_RESOURCE_STATE_COMMON);
        }

        cmdList->FlushResourceBarriers();
    }

    void ReflectionRenderer::DrawSceneToSsaaTarget(const std::shared_ptr<GCommandList>& cmdList)
    {
        cmdList->TransitionBarrier(shadowMap->GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(ssao->AmbientMap(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->FlushResourceBarriers();

        DrawSceneToRenderTarget(cmdList,
                                &ssaa->GetRenderTarget(),
                                ssaa->GetRTV(),
                                &ssaa->GetDepthMap(),
                                ssaa->GetDSV(),
                                *currentFrameResource->MainPassConstantUploadBuffer);
    }

    void ReflectionRenderer::DrawFullscreenTextureToRenderTarget(const std::shared_ptr<GCommandList>& cmdList,
                                                                 GTexture& source,
                                                                 const GDescriptor* sourceSrv,
                                                                 GTexture& renderTarget,
                                                                 const GDescriptor* renderTargetView)
    {
        const auto targetDesc = renderTarget.GetD3D12ResourceDesc();
        const D3D12_VIEWPORT targetViewport = {
            0.0f, 0.0f,
            static_cast<float>(targetDesc.Width),
            static_cast<float>(targetDesc.Height),
            0.0f, 1.0f
        };
        const D3D12_RECT targetRect = {
            0, 0,
            static_cast<LONG>(targetDesc.Width),
            static_cast<LONG>(targetDesc.Height)
        };

        cmdList->SetViewports(&targetViewport, 1);
        cmdList->SetScissorRects(&targetRect, 1);

        cmdList->TransitionBarrier(source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->FlushResourceBarriers();

        cmdList->ClearRenderTarget(renderTargetView);
        cmdList->SetRenderTargets(1, renderTargetView);

        cmdList->SetRootSignature(*rootSignature.get());
        cmdList->SetDescriptorsHeap(sourceSrv);
        cmdList->UpdateDescriptorHeaps();
        cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, sourceSrv);

        cmdList->SetPipelineState(*psos[RenderMode::Quad]);
        scene.Draw(cmdList, RenderMode::Quad);

        cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(source, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }

    void ReflectionRenderer::DrawSsaaTargetToComposedScene(const std::shared_ptr<GCommandList>& cmdList)
    {
        DrawFullscreenTextureToRenderTarget(cmdList,
                                            ssaa->GetRenderTarget(),
                                            ssaa->GetSRV(),
                                            composedSceneColor,
                                            &composedSceneColorRtv);
    }

    void ReflectionRenderer::DrawSsr(const std::shared_ptr<GCommandList>& cmdList)
    {
        const auto targetDesc = ssrOutputColor.GetD3D12ResourceDesc();
        const D3D12_VIEWPORT targetViewport = {
            0.0f, 0.0f,
            static_cast<float>(targetDesc.Width),
            static_cast<float>(targetDesc.Height),
            0.0f, 1.0f
        };
        const D3D12_RECT targetRect = {
            0, 0,
            static_cast<LONG>(targetDesc.Width),
            static_cast<LONG>(targetDesc.Height)
        };

        cmdList->SetViewports(&targetViewport, 1);
        cmdList->SetScissorRects(&targetRect, 1);

        cmdList->TransitionBarrier(composedSceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(ssao->NormalDepthMap(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(ssao->NormalMap(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(ssrOutputColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->FlushResourceBarriers();

        cmdList->ClearRenderTarget(&ssrOutputColorRtv);
        cmdList->SetRenderTargets(1, &ssrOutputColorRtv);

        cmdList->SetRootSignature(*rootSignature.get());
        cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                           *currentFrameResource->MainPassConstantUploadBuffer);

        cmdList->SetDescriptorsHeap(&composedSceneColorSrv);
        cmdList->SetDescriptorsHeap(ssao->NormalDepthMapSrv());
        cmdList->SetDescriptorsHeap(ssao->NormalMapSrv());

        cmdList->UpdateDescriptorHeaps();

        cmdList->SetRootDescriptorTable(kSsrSceneColorSlot, &composedSceneColorSrv);
        cmdList->SetRootDescriptorTable(kSsrSceneDepthSlot, ssao->NormalDepthMapSrv());
        cmdList->SetRootDescriptorTable(kSsrSceneNormalSlot, ssao->NormalMapSrv());

#if defined(DEBUG) || defined(_DEBUG)
        const auto ssrPipelineMode = debugMap == 1 ? RenderMode::SsrDebug : RenderMode::Ssr;
#else
        constexpr auto ssrPipelineMode = RenderMode::Ssr;
#endif

        cmdList->SetPipelineState(*psos[ssrPipelineMode]);
        cmdList->SetVBuffer(0, 0, nullptr);
        cmdList->SetIBuffer(nullptr);
        cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->Draw(3, 1, 0, 0);

        cmdList->TransitionBarrier(ssrOutputColor, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(ssao->NormalMap(), D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(ssao->NormalDepthMap(), D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(composedSceneColor, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }

    void ReflectionRenderer::DrawSsrOnSecondGpu(const std::shared_ptr<GCommandList>& cmdList,
                                                const ConstantUploadBuffer<ReflectionPassConstants>& passConstants)
    {
        const auto targetDesc = secondGpuSsrOutputColor.GetD3D12ResourceDesc();
        const D3D12_VIEWPORT targetViewport = {
            0.0f, 0.0f,
            static_cast<float>(targetDesc.Width),
            static_cast<float>(targetDesc.Height),
            0.0f, 1.0f
        };
        const D3D12_RECT targetRect = {
            0, 0,
            static_cast<LONG>(targetDesc.Width),
            static_cast<LONG>(targetDesc.Height)
        };

        cmdList->SetViewports(&targetViewport, 1);
        cmdList->SetScissorRects(&targetRect, 1);

        cmdList->TransitionBarrier(secondGpuSceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(secondGpuSceneDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(secondGpuSceneNormal, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(secondGpuSsrOutputColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->FlushResourceBarriers();

        cmdList->ClearRenderTarget(&secondGpuSsrOutputRtv);
        cmdList->SetRenderTargets(1, &secondGpuSsrOutputRtv);

        cmdList->SetRootSignature(*secondGpuSsrRootSignature);
        cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData, passConstants);

        cmdList->SetDescriptorsHeap(&secondGpuSsrInputSrv);

        cmdList->UpdateDescriptorHeaps();
        cmdList->SetRootDescriptorTable(kSsrSceneColorSlot, &secondGpuSsrInputSrv, 0);
        cmdList->SetRootDescriptorTable(kSsrSceneDepthSlot, &secondGpuSsrInputSrv, 1);
        cmdList->SetRootDescriptorTable(kSsrSceneNormalSlot, &secondGpuSsrInputSrv, 2);

#if defined(DEBUG) || defined(_DEBUG)
        const auto ssrPipelineMode = debugMap == 1 ? RenderMode::SsrDebug : RenderMode::Ssr;
#else
        constexpr auto ssrPipelineMode = RenderMode::Ssr;
#endif

        cmdList->SetPipelineState(*secondGpuSsrPsos[ssrPipelineMode]);
        cmdList->SetVBuffer(0, 0, nullptr);
        cmdList->SetIBuffer(nullptr);
        cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->Draw(3, 1, 0, 0);

        cmdList->TransitionBarrier(secondGpuSsrOutputColor, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(secondGpuSceneNormal, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(secondGpuSceneDepth, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(secondGpuSceneColor, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }

    void ReflectionRenderer::CopyPrimarySsrInputsToShared(const std::shared_ptr<GCommandList>& cmdList)
    {
        cmdList->CopyResource(sharedSceneColor->GetPrimeResource(), composedSceneColor);
        cmdList->CopyResource(sharedSceneDepth->GetPrimeResource(), ssao->NormalDepthMap());
        cmdList->CopyResource(sharedSceneNormal->GetPrimeResource(), ssao->NormalMap());
    }

    void ReflectionRenderer::CopySharedSsrInputsToSecond(const std::shared_ptr<GCommandList>& cmdList)
    {
        cmdList->CopyResource(secondGpuSceneColor, sharedSceneColor->GetSharedResource());
        cmdList->CopyResource(secondGpuSceneDepth, sharedSceneDepth->GetSharedResource());
        cmdList->CopyResource(secondGpuSceneNormal, sharedSceneNormal->GetSharedResource());
    }

    void ReflectionRenderer::CopySecondSsrOutputToShared(const std::shared_ptr<GCommandList>& cmdList)
    {
        cmdList->CopyResource(sharedSsrOutputColor->GetSharedResource(), secondGpuSsrOutputColor);
    }

    void ReflectionRenderer::CopySharedSsrOutputToPrimary(const std::shared_ptr<GCommandList>& cmdList)
    {
        cmdList->CopyResource(ssrOutputColor, sharedSsrOutputColor->GetPrimeResource());
    }

    void ReflectionRenderer::DrawPresentBackBuffer(const std::shared_ptr<GCommandList>& cmdList)
    {
        auto currentBackBufferRtv = renderTargetMemory.Offset(window->GetCurrentBackBufferIndex());
        DrawFullscreenTextureToRenderTarget(cmdList,
                                            ssrOutputColor,
                                            &ssrOutputColorSrv,
                                            window->GetCurrentBackBuffer(),
                                            &currentBackBufferRtv);

        cmdList->TransitionBarrier(window->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
        cmdList->FlushResourceBarriers();
    }

}
