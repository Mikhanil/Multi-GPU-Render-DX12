#include "pch.h"
#include "RenderModeFactory.h"
#include "d3dUtil.h"
#include "d3dx12.h"
#include "GRootSignature.h"

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

custom_unordered_map<std::string, std::shared_ptr<GShader>> RenderModeFactory::shaders =
    MemoryAllocator::CreateUnorderedMap<std::string, std::shared_ptr<GShader>>();

void RenderModeFactory::LoadDefaultPSO(std::shared_ptr<GDevice> device, std::shared_ptr<GRootSignature> rootSignature,
                                       D3D12_INPUT_LAYOUT_DESC defautlInputDesc, DXGI_FORMAT backBufferFormat,
                                       DXGI_FORMAT depthStencilFormat,
                                       std::shared_ptr<GRootSignature> ssaoRootSignature, DXGI_FORMAT normalMapFormat,
                                       DXGI_FORMAT ambientMapFormat)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;

    ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    basePsoDesc.InputLayout = defautlInputDesc;
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
    basePsoDesc.SampleDesc.Count = 1;
    basePsoDesc.SampleDesc.Quality = 0;
    basePsoDesc.DSVFormat = depthStencilFormat;

    auto depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    auto rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);


    auto opaquePSO = std::make_shared<GraphicPSO>(RenderMode::Opaque);
    opaquePSO->SetPsoDesc(basePsoDesc);
    depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePSO->SetDepthStencilState(depthStencilDesc);

    auto alphaDropPso = std::make_shared<GraphicPSO>(RenderMode::OpaqueAlphaDrop);
    alphaDropPso->SetPsoDesc(opaquePSO->GetPsoDescription());
    alphaDropPso->SetShader(shaders["AlphaDrop"].get());
    rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
    alphaDropPso->SetRasterizationState(rasterizedDesc);


    auto shadowMapPSO = std::make_shared<GraphicPSO>(RenderMode::ShadowMapOpaque);
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
    shadowMapPSO->SetRasterizationState(rasterizedDesc);


    auto shadowMapDropPSO = std::make_shared<GraphicPSO>(RenderMode::ShadowMapOpaqueDrop);
    shadowMapDropPSO->SetPsoDesc(shadowMapPSO->GetPsoDescription());
    shadowMapDropPSO->SetShader(shaders["shadowOpaqueDropPS"].get());


    auto drawNormalsPso = std::make_shared<GraphicPSO>(RenderMode::DrawNormalsOpaque);
    basePsoDesc.DSVFormat = depthStencilFormat;
    drawNormalsPso->SetPsoDesc(basePsoDesc);
    drawNormalsPso->SetShader(shaders["drawNormalsVS"].get());
    drawNormalsPso->SetShader(shaders["drawNormalsPS"].get());
    drawNormalsPso->SetRTVFormat(0, normalMapFormat);
    drawNormalsPso->SetSampleCount(1);
    drawNormalsPso->SetSampleQuality(0);
    drawNormalsPso->SetDSVFormat(depthStencilFormat);

    auto drawNormalsDropPso = std::make_shared<GraphicPSO>(*rootSignature.get(), RenderMode::DrawNormalsOpaqueDrop);
    drawNormalsDropPso->SetPsoDesc(drawNormalsPso->GetPsoDescription());
    drawNormalsDropPso->SetShader(shaders["drawNormalsAlphaDropPS"].get());
    rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
    drawNormalsDropPso->SetRasterizationState(rasterizedDesc);

    auto skyBoxPSO = std::make_shared<GraphicPSO>(RenderMode::SkyBox);
    skyBoxPSO->SetPsoDesc(basePsoDesc);
    skyBoxPSO->SetShader(shaders["SkyBoxVertex"].get());
    skyBoxPSO->SetShader(shaders["SkyBoxPixel"].get());

    depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    skyBoxPSO->SetDepthStencilState(depthStencilDesc);
    rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
    skyBoxPSO->SetRasterizationState(rasterizedDesc);


    auto transperentPSO = std::make_shared<GraphicPSO>(RenderMode::Transparent);
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

    auto debugPso = std::make_shared<GraphicPSO>( RenderMode::Debug);
    debugPso->SetPsoDesc(basePsoDesc);
    debugPso->SetShader(shaders["quadVS"].get());
    debugPso->SetShader(shaders["quadPS"].get());

    auto quadPso = std::make_shared<GraphicPSO>( RenderMode::Quad);
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


    auto noisePSO = std::make_shared<GraphicPSO>( RenderMode::Debug);
    noisePSO->SetPsoDesc(basePsoDesc);
    noisePSO->SetShader(shaders["noiseVS"].get());
    noisePSO->SetShader(shaders["noisePS"].get());
    noisePSO->SetSampleCount(1);
    noisePSO->SetSampleQuality(0);
    noisePSO->SetDSVFormat(DXGI_FORMAT_UNKNOWN);
    depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = false;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    noisePSO->SetDepthStencilState(depthStencilDesc);


    auto uiPSO = std::make_shared<GraphicPSO>( RenderMode::UI);
    uiPSO->SetPsoDesc(quadPso->GetPsoDescription());

    // Create the blending setup
    {
        D3D12_RENDER_TARGET_BLEND_DESC desc = {};
        desc.BlendEnable = true;
        desc.LogicOpEnable = false;
        desc.SrcBlend = D3D12_BLEND_ONE;
        desc.DestBlend = D3D12_BLEND_SRC_ALPHA;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        desc.LogicOp = D3D12_LOGIC_OP_NOOP;
        desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        uiPSO->SetRenderTargetBlendState(0, desc);
    }

    if (ssaoRootSignature != nullptr)
    {
        auto ssaoPSO = std::make_shared<GraphicPSO>(RenderMode::Ssao);
        ssaoPSO->SetPsoDesc(basePsoDesc);
        ssaoPSO->SetRootSignature(*ssaoRootSignature.get());
        ssaoPSO->SetInputLayout({nullptr, 0});
        ssaoPSO->SetShader(shaders["ssaoVS"].get());
        ssaoPSO->SetShader(shaders["ssaoPS"].get());
        ssaoPSO->SetRTVFormat(0, ambientMapFormat);
        ssaoPSO->SetSampleCount(1);
        ssaoPSO->SetSampleQuality(0);
        ssaoPSO->SetDSVFormat(DXGI_FORMAT_UNKNOWN);
        depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        depthStencilDesc.DepthEnable = false;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        ssaoPSO->SetDepthStencilState(depthStencilDesc);


        auto ssaoBlurPSO = std::make_shared<GraphicPSO>(RenderMode::SsaoBlur);
        ssaoBlurPSO->SetPsoDesc(ssaoPSO->GetPsoDescription());
        ssaoBlurPSO->SetShader(shaders["ssaoBlurVS"].get());
        ssaoBlurPSO->SetShader(shaders["ssaoBlurPS"].get());

        
        PSO[ssaoPSO->GetRenderMode()] = std::move(ssaoPSO);
        PSO[ssaoBlurPSO->GetRenderMode()] = std::move(ssaoBlurPSO);
    }

    PSO[opaquePSO->GetRenderMode()] = std::move(opaquePSO);
    PSO[transperentPSO->GetRenderMode()] = std::move(transperentPSO);
    PSO[alphaDropPso->GetRenderMode()] = std::move(alphaDropPso);
    PSO[skyBoxPSO->GetRenderMode()] = std::move(skyBoxPSO);
    PSO[shadowMapPSO->GetRenderMode()] = std::move(shadowMapPSO);
    PSO[shadowMapDropPSO->GetRenderMode()] = std::move(shadowMapDropPSO);
    PSO[drawNormalsPso->GetRenderMode()] = std::move(drawNormalsPso);
    PSO[drawNormalsDropPso->GetRenderMode()] = std::move(drawNormalsDropPso);
    PSO[debugPso->GetRenderMode()] = std::move(debugPso);
    PSO[quadPso->GetRenderMode()] = std::move(quadPso);
    PSO[noisePSO->GetRenderMode()] = std::move(noisePSO);
    PSO[uiPSO->GetRenderMode()] = std::move(uiPSO);

    for (auto& pso : PSO)
    {
        pso.second->Initialize(device);
    }
}

void RenderModeFactory::LoadDefaultShaders()
{
    if (!shaders.empty()) return;

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
        std::make_shared<GShader>(L"Shaders\\Default.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
    shaders["AlphaDrop"] = std::move(
        std::make_shared<GShader>(L"Shaders\\Default.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));
    shaders["shadowVS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\Shadows.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
    shaders["shadowOpaquePS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\Shadows.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));
    shaders["shadowOpaqueDropPS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\Shadows.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));
    shaders["OpaquePixel"] = std::move(
        std::make_shared<GShader>(L"Shaders\\Default.hlsl", PixelShader, defines, "PS", "ps_5_1"));
    shaders["SkyBoxVertex"] = std::move(
        std::make_shared<GShader>(L"Shaders\\SkyBoxShader.hlsl", VertexShader, defines, "SKYMAP_VS", "vs_5_1"));
    shaders["SkyBoxPixel"] = std::move(
        std::make_shared<GShader>(L"Shaders\\SkyBoxShader.hlsl", PixelShader, defines, "SKYMAP_PS", "ps_5_1"));

    shaders["treeSpriteVS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\TreeSprite.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
    shaders["treeSpriteGS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\TreeSprite.hlsl", GeometryShader, nullptr, "GS", "gs_5_1"));
    shaders["treeSpritePS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\TreeSprite.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));


    shaders["drawNormalsVS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\DrawNormals.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
    shaders["drawNormalsPS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\DrawNormals.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));
    shaders["drawNormalsAlphaDropPS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\DrawNormals.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));


    shaders["ssaoVS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\Ssao.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
    shaders["ssaoPS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\Ssao.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

    shaders["ssaoBlurVS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\SsaoBlur.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
    shaders["ssaoBlurPS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\SsaoBlur.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

    shaders["quadVS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\Quad.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
    shaders["quadPS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\Quad.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

    shaders["noiseVS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\NoiseDraw.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
    shaders["noisePS"] = std::move(
        std::make_shared<GShader>(L"Shaders\\NoiseDraw.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

    for (auto&& pair : shaders)
    {
        pair.second->LoadAndCompile();
    }
}

const std::shared_ptr<GShader>& RenderModeFactory::GetShader(const std::string& name)
{
    return shaders[name];
}
