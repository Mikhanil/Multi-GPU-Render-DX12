#pragma once

#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <fstream>

namespace PEPEngine::Graphics
{
    class SimpleInclude : public ID3DInclude
    {
    public:
        std::string RootDir;

        SimpleInclude(const std::string& root)
            : RootDir(root) {
        }

        STDMETHOD(Open)(
            D3D_INCLUDE_TYPE IncludeType,
            LPCSTR pFileName,
            LPCVOID pParentData,
            LPCVOID* ppData,
            UINT* pBytes) override
        {
            std::string fullPath = RootDir + "/" + pFileName;

            std::ifstream file(fullPath, std::ios::binary);
            if (!file)
                return E_FAIL;

            file.seekg(0, std::ios::end);
            size_t size = file.tellg();
            file.seekg(0, std::ios::beg);

            char* data = new char[size];
            file.read(data, size);

            *ppData = data;
            *pBytes = (UINT)size;

            return S_OK;
        }

        STDMETHOD(Close)(LPCVOID pData) override
        {
            delete[] reinterpret_cast<const char*>(pData);
            return S_OK;
        }
    };

    class MultiInclude : public ID3DInclude
    {
    public:
        std::vector<std::string> IncludeDirs;

        MultiInclude(const std::vector<std::string>& dirs)
            : IncludeDirs(dirs) {
        }

        STDMETHOD(Open)(
            D3D_INCLUDE_TYPE IncludeType,
            LPCSTR pFileName,
            LPCVOID pParentData,
            LPCVOID* ppData,
            UINT* pBytes) override
        {
            for (const auto& dir : IncludeDirs)
            {
                std::string fullPath = dir + "/" + pFileName;

                std::ifstream file(fullPath, std::ios::binary);
                if (!file)
                    continue;

                file.seekg(0, std::ios::end);
                size_t size = file.tellg();
                file.seekg(0, std::ios::beg);

                char* data = new char[size];
                file.read(data, size);

                *ppData = data;
                *pBytes = (UINT)size;

                return S_OK;
            }

            return E_FAIL;
        }

        STDMETHOD(Close)(LPCVOID pData) override
        {
            delete[] reinterpret_cast<const char*>(pData);
            return S_OK;
        }
    };
}