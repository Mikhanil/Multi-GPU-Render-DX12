#include "GShaderCustomInclude.h"
#include "CustomShaderUtils.h"
#include "CustomInclude.h"

namespace PEPEngine::Graphics
{
    GShaderCustomInclude::GShaderCustomInclude(uint32_t numIncludeDirs, const std::vector<std::string>& dirs,
        const std::wstring& fileName, ShaderType type, const D3D_SHADER_MACRO* defines,
        const std::string& entryPoint, const std::string& target)
        : GShader(fileName, type, defines, entryPoint, target)
    {
        mNumIncludeDirs = numIncludeDirs;
        mIncludeDirs = new MultiInclude(dirs);
    }

    void GShaderCustomInclude::LoadAndCompile()
    {
        if (IsInited) return;
        shaderBlob = CompileShaderCustomInclude(FileName, defines, entryPoint, target, mNumIncludeDirs, mIncludeDirs);
        IsInited = true;
    }
}