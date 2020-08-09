#pragma once
#include "GResource.h"

class GBuffer : public GResource
{
public:
    explicit GBuffer(const std::wstring& name = L"") : GResource(name)
    {}
    ;
    explicit GBuffer(const D3D12_RESOURCE_DESC& resDesc,
        size_t numElements, size_t elementSize,
        const std::wstring& name = L"") : GResource(resDesc, name)
    {};

    /**
     * Create the views for the buffer resource.
     * Used by the CommandList when setting the buffer contents.
     */
    virtual void CreateViews(size_t numElements, size_t elementSize) = 0;

protected:

private:

};

class GVertexBuffer : public GBuffer
{
public:
    GVertexBuffer(const std::wstring& name = L"");
    virtual ~GVertexBuffer();

    // Inherited from Buffer
    virtual void CreateViews(size_t elementCount, size_t elementSize) override;

    /**
     * Get the vertex buffer view for binding to the Input Assembler stage.
     */
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() const;

    size_t GetVertexCount() const;

    size_t GetVertexSize() const;

    /**
    * Get the SRV for a resource.
    */
    virtual DescriptorHandle GetShaderResourceView(
	    const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr) const override;

    /**
    * Get the UAV for a (sub)resource.
    */
    virtual DescriptorHandle GetUnorderedAccessView(
	    const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc = nullptr) const override;

protected:

private:
    size_t VertexCount;
    size_t VertexSize;

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
};


