#include "GRenderTarger.h"

#include "GDeviceFactory.h"

void PEPEngine::Graphics::GRenderTexture::CreateRTV() const
{
    if (renderTexture != nullptr && renderTexture->IsValid())
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.Texture2D.PlaneSlice = 0;
        rtvDesc.Format = renderTexture->GetD3D12ResourceDesc().Format;

        renderTexture->CreateRenderTargetView(&rtvDesc, rtv, offset);
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE PEPEngine::Graphics::GRenderTexture::GetRTVCpu() const
{
    return rtv->GetCPUHandle(offset);
}

D3D12_GPU_DESCRIPTOR_HANDLE PEPEngine::Graphics::GRenderTexture::GetRTVGpu() const
{
    return rtv->GetGPUHandle(offset);
}

const PEPEngine::Graphics::GTexture* PEPEngine::Graphics::GRenderTexture::GetRenderTexture() const
{
    return renderTexture.get();
}

void PEPEngine::Graphics::GRenderTexture::Resize(const UINT newHeight, const UINT newWeight) const
{
    renderTexture->Resize(*renderTexture, newHeight, newWeight, 1);
    CreateRTV();
}

void PEPEngine::Graphics::GRenderTexture::ChangeTexture(const ComPtr<ID3D12Resource>& texture, std::shared_ptr<GDevice>& device)
{
    if (renderTexture != nullptr && renderTexture->IsValid())
    {
        renderTexture->Reset();
        renderTexture->SetD3D12Resource(device, texture);
    }
    else
    {
        if (renderTexture == nullptr)
        {
            renderTexture = std::make_shared<GTexture>(L"RenderTarget");
        }

        renderTexture->SetD3D12Resource(device, texture);
    }

    CreateRTV();
}

void PEPEngine::Graphics::GRenderTexture::ChangeTexture(const std::shared_ptr<GTexture>& texture)
{
    renderTexture = texture;
    CreateRTV();
}

void PEPEngine::Graphics::GRenderTexture::ChangeTexture(const std::shared_ptr<GTexture>& texture, GDescriptor* rtv)
{
    renderTexture = texture;
    this->rtv = std::move(rtv);

    CreateRTV();
}

PEPEngine::Graphics::GRenderTexture::GRenderTexture(const std::shared_ptr<GTexture>& texture): renderTexture(texture),
    offset(0)
{
    rtv = std::move(&renderTexture->GetDevice()->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1));
}

PEPEngine::Graphics::GRenderTexture::GRenderTexture(const std::shared_ptr<GTexture>& texture, GDescriptor* rtv,
                                                    const UINT offset): renderTexture(texture), rtv(rtv), offset(offset)
{
    CreateRTV();
}
