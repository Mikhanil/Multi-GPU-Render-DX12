#pragma once
#include <d3d12.h>
#include <string>

#include "GMeshBuffer.h"
#include "Lazy.h"


using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

class NativeMesh;
class GModel;


class GMesh
{
    friend GModel;

    std::shared_ptr<NativeMesh> mesh;


    std::shared_ptr<GMeshBuffer> vertexBuffer = nullptr;
    std::shared_ptr<GMeshBuffer> indexBuffer = nullptr;

public:
    std::shared_ptr<NativeMesh> GetMeshData() const;

    D3D12_PRIMITIVE_TOPOLOGY GetPrimitiveType() const;

    D3D12_VERTEX_BUFFER_VIEW* GetVertexView() const;

    D3D12_INDEX_BUFFER_VIEW* GetIndexView() const;


    GMesh(const std::shared_ptr<NativeMesh>& data, std::shared_ptr<GCommandList>& cmdList);


    void Draw(const std::shared_ptr<GCommandList>& cmdList) const;

    std::wstring GetName() const;
};
