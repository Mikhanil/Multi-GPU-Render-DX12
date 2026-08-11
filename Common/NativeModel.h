#pragma once
#include <d3d12.h>
#include <iterator>
#include <string>
#include "MemoryAllocator.h"
#include "ShaderBuffersData.h"

using namespace PEPEngine;
using namespace Allocator;

class NativeMesh
{
    std::wstring meshName;
    std::vector<Vertex> vertices = std::vector<Vertex>();
    std::vector<DWORD> indexes = std::vector<DWORD>();

public:
    NativeMesh(const Vertex* vertices, const size_t vertexesCount, const DWORD* indices, const size_t indexesCount,
               const D3D12_PRIMITIVE_TOPOLOGY topology = D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED,
               std::wstring name = L"") : meshName(
                                              std::move(name)), topology(topology)
    {
        std::copy(vertices, vertices + vertexesCount, std::back_inserter(this->vertices));
        std::copy(indices, indices + indexesCount, std::back_inserter(this->indexes));
    }

    std::vector<Vertex>& GetVertexes()
    {
        return vertices;
    }

    std::vector<DWORD>& GetIndexes()
    {
        return indexes;
    }

    UINT GetIndexCount() const
    {
        return static_cast<UINT>(indexes.size());
    }

    std::wstring& GetName()
    {
        return meshName;
    }

    D3D12_PRIMITIVE_TOPOLOGY topology;


    static UINT GetVertexSize()
    {
        return sizeof(Vertex);
    }

    static UINT GetIndexSize()
    {
        return sizeof(DWORD);
    }
};


class NativeModel
{
    std::vector<std::shared_ptr<NativeMesh>> meshes = std::vector<std::shared_ptr<NativeMesh>>();

    std::wstring name;

public:
    NativeModel(const std::wstring& name) : name(name)
    {
    }

    void AddMesh(const std::shared_ptr<NativeMesh>& mesh)
    {
        meshes.push_back(mesh);
    }

    UINT GetMeshesCount() const { return static_cast<UINT>(meshes.size()); };

    std::wstring GetName() const { return name; };

    std::shared_ptr<NativeMesh> GetMesh(const UINT index)
    {
        assert(index <= meshes.size());

        return meshes[index];
    }
};
