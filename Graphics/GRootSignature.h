#pragma once


#include "MemoryAllocator.h"
#include <d3d12.h>
#include <wrl/client.h>

#include "d3dx12.h"

using namespace Microsoft::WRL;

namespace PEPEngine::Graphics
{
    using namespace Allocator;

    class GDevice;

    class GRootSignature
    {
        ComPtr<ID3D12RootSignature> signature;
        D3D12_ROOT_SIGNATURE_DESC rootSigDesc;


        custom_vector<CD3DX12_ROOT_PARAMETER> slotRootParameters = MemoryAllocator::CreateVector<
            CD3DX12_ROOT_PARAMETER>();

        void AddParameter(const CD3DX12_ROOT_PARAMETER& parameter);

        custom_vector<D3D12_STATIC_SAMPLER_DESC> staticSampler = MemoryAllocator::CreateVector<
            D3D12_STATIC_SAMPLER_DESC>();

        custom_vector<uint32_t> descriptorPerTableCount = MemoryAllocator::CreateVector<uint32_t>();

        uint32_t samplerTableBitMask;

        uint32_t descriptorTableBitMask;

        bool IsInitialize = false;

    public:
        GRootSignature() = default;
        GRootSignature(ID3D12RootSignature* rootSignature);
        virtual ~GRootSignature();

        const D3D12_ROOT_SIGNATURE_DESC& GetRootSignatureDesc() const;

        void AddDescriptorParameter(const CD3DX12_DESCRIPTOR_RANGE* rangeParameters, UINT size,
                                    D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        void AddConstantBufferParameter(UINT shaderRegister, UINT registerSpace = 0,
                                        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        void AddConstantParameter(UINT valueCount, UINT shaderRegister, UINT registerSpace = 0,
                                  D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        void AddShaderResourceView(UINT shaderRegister, UINT registerSpace = 0,
                                   D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        void AddUnorderedAccessView(UINT shaderRegister, UINT registerSpace = 0,
                                    D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        void AddStaticSampler(const CD3DX12_STATIC_SAMPLER_DESC& sampler);
        void AddStaticSampler(const D3D12_STATIC_SAMPLER_DESC& sampler);;

        void SetDesc(const D3D12_ROOT_SIGNATURE_DESC& desc);

        void Initialize(const std::shared_ptr<GDevice>& device, bool force = false);

        ComPtr<ID3D12RootSignature> GetNativeSignature() const;
    };
}
