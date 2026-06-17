#include "pch.h"
#include "RenderModeFactory.h"
#include "d3dUtil.h"
#include "d3dx12.h"
#include "GRootSignature.h"

#include <filesystem>
#include <initializer_list>
#include <system_error>

using namespace PEPEngine;
using namespace Graphics;
using namespace Utils;

std::unordered_map<std::string, std::shared_ptr<GShader>> RenderModeFactory::shaders;

namespace
{
    bool ShaderFileExists(const std::wstring& fileName)
    {
        std::error_code error;
        return std::filesystem::exists(std::filesystem::path(fileName), error);
    }

    void LogMissingShaderFile(const std::wstring& fileName)
    {
        const auto message = L"[RenderModeFactory] Shader file not found: " + fileName + L"\n";
        OutputDebugStringW(message.c_str());
    }

    void LogMissingPsoShader(const std::string& shaderName, const RenderMode renderMode)
    {
        const auto message = "[RenderModeFactory] Shader \"" + shaderName +
            "\" is not loaded. Skip PSO " + std::to_string(static_cast<int>(renderMode)) + ".\n";
        OutputDebugStringA(message.c_str());
    }
}

void RenderModeFactory::LoadDefaultPSO(std::shared_ptr<GDevice> device, std::shared_ptr<GRootSignature> rootSignature,
                                       D3D12_INPUT_LAYOUT_DESC defautlInputDesc, DXGI_FORMAT backBufferFormat,
                                       DXGI_FORMAT depthStencilFormat,
                                       std::shared_ptr<GRootSignature> ssaoRootSignature, DXGI_FORMAT normalMapFormat,
                                       DXGI_FORMAT ambientMapFormat)
{
    this->PSO.clear();

    const auto findShader = [&](const std::string& shaderName, const RenderMode renderMode) -> const GShader*
    {
        const auto shader = shaders.find(shaderName);
        if (shader == shaders.end() || shader->second == nullptr)
        {
            LogMissingPsoShader(shaderName, renderMode);
            return nullptr;
        }

        return shader->second.get();
    };

    const auto setShaders = [&](GraphicPSO& pipelineState, const std::initializer_list<const char*> shaderNames)
    {
        bool result = true;
        for (const auto shaderName : shaderNames)
        {
            const auto shader = findShader(shaderName, pipelineState.GetRenderMode());
            if (shader == nullptr)
            {
                result = false;
                continue;
            }

            pipelineState.SetShader(shader);
        }

        return result;
    };

    const auto addPso = [&](std::shared_ptr<GraphicPSO>& pipelineState, const bool canCompile)
    {
        if (!canCompile)
        {
            return;
        }

        const auto renderMode = pipelineState->GetRenderMode();
        this->PSO[renderMode] = std::move(pipelineState);
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;

    ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    basePsoDesc.InputLayout = defautlInputDesc;
    basePsoDesc.pRootSignature = rootSignature->GetNativeSignature().Get();
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
    const auto opaquePsoShadersLoaded = setShaders(*opaquePSO, {"StandardVertex", "OpaquePixel"});
    depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePSO->SetDepthStencilState(depthStencilDesc);

    auto alphaDropPso = std::make_shared<GraphicPSO>(RenderMode::OpaqueAlphaDrop);
    alphaDropPso->SetPsoDesc(opaquePSO->GetPsoDescription());
    const auto alphaDropPsoShadersLoaded = setShaders(*alphaDropPso, {"StandardVertex", "AlphaDrop"});
    rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
    alphaDropPso->SetRasterizationState(rasterizedDesc);

    auto reflectionsPSO = std::make_shared<GraphicPSO>(RenderMode::Reflection);
    reflectionsPSO->SetPsoDesc(opaquePSO->GetPsoDescription());
    const auto reflectionsPsoShadersLoaded = setShaders(*reflectionsPSO, {"ReflectionsVertex", "ReflectionsPixel"});


    auto shadowMapPSO = std::make_shared<GraphicPSO>(RenderMode::ShadowMapOpaque);
    basePsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowMapPSO->SetPsoDesc(basePsoDesc);
    const auto shadowMapPsoShadersLoaded = setShaders(*shadowMapPSO, {"shadowVS", "shadowOpaquePS"});
    shadowMapPSO->SetRTVFormat(0, DXGI_FORMAT_UNKNOWN);
    shadowMapPSO->SetRenderTargetsCount(0);

    rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizedDesc.DepthBias = 100000;
    rasterizedDesc.DepthBiasClamp = 0.0f;
    rasterizedDesc.SlopeScaledDepthBias = 1.0f;
    shadowMapPSO->SetRasterizationState(rasterizedDesc);


    auto shadowMapDropPSO = std::make_shared<GraphicPSO>(RenderMode::ShadowMapOpaqueDrop);
    shadowMapDropPSO->SetPsoDesc(shadowMapPSO->GetPsoDescription());
    const auto shadowMapDropPsoShadersLoaded = setShaders(*shadowMapDropPSO, {"shadowVS", "shadowOpaqueDropPS"});


    auto drawNormalsPso = std::make_shared<GraphicPSO>(RenderMode::DrawNormalsOpaque);
    basePsoDesc.DSVFormat = depthStencilFormat;
    drawNormalsPso->SetPsoDesc(basePsoDesc);
    const auto drawNormalsPsoShadersLoaded = setShaders(*drawNormalsPso, {"drawNormalsVS", "drawNormalsPS"});
    drawNormalsPso->SetRTVFormat(0, normalMapFormat);
    drawNormalsPso->SetSampleCount(1);
    drawNormalsPso->SetSampleQuality(0);
    drawNormalsPso->SetDSVFormat(depthStencilFormat);

    auto drawNormalsDropPso = std::make_shared<GraphicPSO>(*rootSignature.get(), RenderMode::DrawNormalsOpaqueDrop);
    drawNormalsDropPso->SetPsoDesc(drawNormalsPso->GetPsoDescription());
    const auto drawNormalsDropPsoShadersLoaded = setShaders(*drawNormalsDropPso, {"drawNormalsVS", "drawNormalsAlphaDropPS"});
    rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
    drawNormalsDropPso->SetRasterizationState(rasterizedDesc);

    auto skyBoxPSO = std::make_shared<GraphicPSO>(RenderMode::SkyBox);
    skyBoxPSO->SetPsoDesc(basePsoDesc);
    const auto skyBoxPsoShadersLoaded = setShaders(*skyBoxPSO, {"SkyBoxVertex", "SkyBoxPixel"});

    depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    skyBoxPSO->SetDepthStencilState(depthStencilDesc);
    rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
    skyBoxPSO->SetRasterizationState(rasterizedDesc);


    auto transperentPSO = std::make_shared<GraphicPSO>(RenderMode::Transparent);
    transperentPSO->SetPsoDesc(basePsoDesc);
    const auto transparentPsoShadersLoaded = setShaders(*transperentPSO, {"StandardVertex", "OpaquePixel"});
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

    auto debugPso = std::make_shared<GraphicPSO>(RenderMode::Debug);
    debugPso->SetPsoDesc(basePsoDesc);
    const auto debugPsoShadersLoaded = setShaders(*debugPso, {"quadVS", "quadPS"});

    auto quadPso = std::make_shared<GraphicPSO>(RenderMode::Quad);
    quadPso->SetPsoDesc(basePsoDesc);
    const auto quadPsoShadersLoaded = setShaders(*quadPso, {"quadVS", "quadPS"});
    quadPso->SetSampleCount(1);
    quadPso->SetSampleQuality(0);
    quadPso->SetDSVFormat(DXGI_FORMAT_UNKNOWN);
    depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = false;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    quadPso->SetDepthStencilState(depthStencilDesc);


    auto noisePSO = std::make_shared<GraphicPSO>(RenderMode::Debug);
    noisePSO->SetPsoDesc(basePsoDesc);
    const auto noisePsoShadersLoaded = setShaders(*noisePSO, {"noiseVS", "noisePS"});
    noisePSO->SetSampleCount(1);
    noisePSO->SetSampleQuality(0);
    noisePSO->SetDSVFormat(DXGI_FORMAT_UNKNOWN);
    depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = false;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    noisePSO->SetDepthStencilState(depthStencilDesc);


    auto uiPSO = std::make_shared<GraphicPSO>(RenderMode::UI);
    uiPSO->SetPsoDesc(quadPso->GetPsoDescription());
    const auto uiPsoShadersLoaded = setShaders(*uiPSO, {"quadVS", "quadPS"});

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
        const auto ssaoPsoShadersLoaded = setShaders(*ssaoPSO, {"ssaoVS", "ssaoPS"});
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
        const auto ssaoBlurPsoShadersLoaded = setShaders(*ssaoBlurPSO, {"ssaoBlurVS", "ssaoBlurPS"});


        addPso(ssaoPSO, ssaoPsoShadersLoaded);
        addPso(ssaoBlurPSO, ssaoBlurPsoShadersLoaded);
    }

    addPso(opaquePSO, opaquePsoShadersLoaded);
    addPso(transperentPSO, transparentPsoShadersLoaded);
    addPso(reflectionsPSO, reflectionsPsoShadersLoaded);
    addPso(alphaDropPso, alphaDropPsoShadersLoaded);
    addPso(skyBoxPSO, skyBoxPsoShadersLoaded);
    addPso(shadowMapPSO, shadowMapPsoShadersLoaded);
    addPso(shadowMapDropPSO, shadowMapDropPsoShadersLoaded);
    addPso(drawNormalsPso, drawNormalsPsoShadersLoaded);
    addPso(drawNormalsDropPso, drawNormalsDropPsoShadersLoaded);
    addPso(debugPso, debugPsoShadersLoaded);
    addPso(quadPso, quadPsoShadersLoaded);
    addPso(noisePSO, noisePsoShadersLoaded);
    addPso(uiPSO, uiPsoShadersLoaded);

    for (auto& pipelineState : PSO)
    {
        pipelineState.second->Initialize(device);
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

    const auto loadShader = [](const std::string& name, const std::wstring& fileName, const ShaderType type,
                               const D3D_SHADER_MACRO* shaderDefines, const std::string& entryPoint,
                               const std::string& target)
    {
        if (!ShaderFileExists(fileName))
        {
            LogMissingShaderFile(fileName);
            return;
        }

        auto shader = std::make_shared<GShader>(fileName, type, shaderDefines, entryPoint, target);
        shader->LoadAndCompile();
        shaders[name] = std::move(shader);
    };

    loadShader("StandardVertex", L"Shaders\\Default.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
    loadShader("AlphaDrop", L"Shaders\\Default.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1");
    loadShader("shadowVS", L"Shaders\\Shadows.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
    loadShader("shadowOpaquePS", L"Shaders\\Shadows.hlsl", PixelShader, nullptr, "PS", "ps_5_1");
    loadShader("shadowOpaqueDropPS", L"Shaders\\Shadows.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1");
    loadShader("OpaquePixel", L"Shaders\\Default.hlsl", PixelShader, defines, "PS", "ps_5_1");
    loadShader("SkyBoxVertex", L"Shaders\\SkyBoxShader.hlsl", VertexShader, defines, "SKYMAP_VS", "vs_5_1");
    loadShader("SkyBoxPixel", L"Shaders\\SkyBoxShader.hlsl", PixelShader, defines, "SKYMAP_PS", "ps_5_1");

    loadShader("ReflectionsVertex", L"Shaders\\Reflections.hlsl", VertexShader, defines, "REFLECTIONS_VS", "vs_5_1");
    loadShader("ReflectionsPixel", L"Shaders\\Reflections.hlsl", PixelShader, defines, "REFLECTIONS_PS", "ps_5_1");

    loadShader("treeSpriteVS", L"Shaders\\TreeSprite.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
    loadShader("treeSpriteGS", L"Shaders\\TreeSprite.hlsl", GeometryShader, nullptr, "GS", "gs_5_1");
    loadShader("treeSpritePS", L"Shaders\\TreeSprite.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1");

    loadShader("drawNormalsVS", L"Shaders\\DrawNormals.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
    loadShader("drawNormalsPS", L"Shaders\\DrawNormals.hlsl", PixelShader, nullptr, "PS", "ps_5_1");
    loadShader("drawNormalsAlphaDropPS", L"Shaders\\DrawNormals.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1");

    loadShader("ssaoVS", L"Shaders\\Ssao.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
    loadShader("ssaoPS", L"Shaders\\Ssao.hlsl", PixelShader, nullptr, "PS", "ps_5_1");

    loadShader("ssaoBlurVS", L"Shaders\\SsaoBlur.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
    loadShader("ssaoBlurPS", L"Shaders\\SsaoBlur.hlsl", PixelShader, nullptr, "PS", "ps_5_1");

    loadShader("quadVS", L"Shaders\\Quad.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
    loadShader("quadPS", L"Shaders\\Quad.hlsl", PixelShader, nullptr, "PS", "ps_5_1");

    loadShader("noiseVS", L"Shaders\\NoiseDraw.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
    loadShader("noisePS", L"Shaders\\NoiseDraw.hlsl", PixelShader, nullptr, "PS", "ps_5_1");

    loadShader("velocityCS", L"Shaders\\MotionBlur.hlsl", ComputeShader, nullptr, "velocityCS", "cs_5_1");
    loadShader("tilemaxCS", L"Shaders\\MotionBlur.hlsl", ComputeShader, nullptr, "tilemaxCS", "cs_5_1");
    loadShader("neighbourmaxCS", L"Shaders\\MotionBlur.hlsl", ComputeShader, nullptr, "neighbourmaxCS", "cs_5_1");
    loadShader("mbCS", L"Shaders\\MotionBlur.hlsl", ComputeShader, nullptr, "mbCS", "cs_5_1");
}

const std::shared_ptr<GShader>& RenderModeFactory::GetShader(const std::string& name)
{
    return shaders[name];
}
