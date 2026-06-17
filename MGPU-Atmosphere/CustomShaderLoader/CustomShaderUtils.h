#pragma once

#include <d3dcompiler.h>
#include <fstream>
#include "d3dUtil.h"

namespace PEPEngine::Graphics
{
    using namespace Utils;

    ComPtr<ID3DBlob> CompileShaderCustomInclude(
        const std::wstring& filename,
        const D3D_SHADER_MACRO* defines,
        const std::string& entrypoint,
        const std::string& target,
        uint32_t numIncludeDirs,
        ID3DInclude* includeDir)
    {
        UINT compileFlags = D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES | D3DCOMPILE_ALL_RESOURCES_BOUND;

#if defined(DEBUG) || defined(_DEBUG)
        compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        HRESULT hr = S_OK;

        ComPtr<ID3DBlob> byteCode = nullptr;
        ComPtr<ID3DBlob> errors;

        if (numIncludeDirs == 0)
        {
            hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);
        }
        else
        {
            hr = D3DCompileFromFile(filename.c_str(), defines, includeDir,
                entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);
        }

        if (errors != nullptr)
            OutputDebugStringA(static_cast<char*>(errors->GetBufferPointer()));

        ThrowIfFailed(hr);

        return byteCode;
    }
}