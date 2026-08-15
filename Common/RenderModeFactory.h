#pragma once

#include "GraphicPSO.h"
#include "GRootSignature.h"
#include "GShader.h"

#include <unordered_map>

using namespace PEPEngine;
using namespace Graphics;

class RenderModeFactory
{
    static std::unordered_map<std::string, std::shared_ptr<GShader>> shaders;

    std::unordered_map<RenderMode, std::shared_ptr<GraphicPSO>> PSO;

public:
    void LoadDefaultPSO(std::shared_ptr<GDevice> device, std::shared_ptr<GRootSignature> rootSignature,
                        D3D12_INPUT_LAYOUT_DESC defautlInputDesc, DXGI_FORMAT backBufferFormat,
                        DXGI_FORMAT depthStencilFormat, std::shared_ptr<GRootSignature> ssaoRootSignature,
                        DXGI_FORMAT normalMapFormat, DXGI_FORMAT
                        ambientMapFormat, bool includeSsr = false);

    static void LoadDefaultShaders(bool includeSsr = false);

    static const std::shared_ptr<GShader>& GetShader(const std::string& name);

    std::shared_ptr<GraphicPSO> GetPSO(const RenderMode type)
    {
        return PSO[type];
    }
};
