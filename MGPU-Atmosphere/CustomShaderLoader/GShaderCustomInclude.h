#pragma once
#include "GShader.h"
#include <vector>
#include <string>

namespace PEPEngine::Graphics
{
    class GShaderCustomInclude :
        public GShader
    {
    public:
        GShaderCustomInclude(uint32_t numIncludeDirs, const std::vector<std::string>& dirs, const std::wstring& fileName, ShaderType type, const D3D_SHADER_MACRO* defines = nullptr,
            const std::string& entryPoint = "Main", const std::string& target = "ps_5_1");

        void LoadAndCompile() override;

    private:
        uint32_t mNumIncludeDirs = 0;
        ID3DInclude* mIncludeDirs = nullptr;
    };
}

