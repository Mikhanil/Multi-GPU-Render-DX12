#pragma once

#include "d3dUtil.h"
#include "GCrossAdapterResource.h"
#include "GraphicPSO.h"
#include "GDescriptor.h"
#include "GRenderTarger.h"
#include "GTexture.h"
#include "MathHelper.h"
#include "RenderModeFactory.h"
#include "ShaderBuffersData.h"

#include "ComputePSO.h"
#include "SharedSSAO.h"
#include "SSAA.h"

using namespace DirectX::SimpleMath;

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

class MBCrossResources;
class SharedMB;

class MBResources {
protected:
    static constexpr DXGI_FORMAT VelocityMapFormat = DXGI_FORMAT_R16G16_FLOAT; //DXGI_FORMAT_R16_UNORM;
    static constexpr DXGI_FORMAT TilemaxMapFormat = DXGI_FORMAT_R16G16_FLOAT;
    static constexpr DXGI_FORMAT NeighbourMaxMapFormat = DXGI_FORMAT_R16G16_FLOAT;
    static constexpr DXGI_FORMAT MbMapFormat = DXGI_FORMAT_R8G8B8A8_UNORM;//DXGI_FORMAT_R16G16_FLOAT;
    static constexpr DXGI_FORMAT DepthMapFormat = DXGI_FORMAT_R32_TYPELESS;

    std::shared_ptr<GDevice> device;
    std::shared_ptr<GRootSignature> velocityRootSignature;
    std::shared_ptr<GRootSignature> tilemaxRootSignature;
    std::shared_ptr<GRootSignature> neighbourmaxRootSignature;
    std::shared_ptr<GRootSignature> mbRootSignature;

    std::shared_ptr<ComputePSO> velocityPSO;
    std::shared_ptr<ComputePSO> tilemaxPSO;
    std::shared_ptr<ComputePSO> neighbourmaxPSO;
    std::shared_ptr<ComputePSO> mbPSO;

    GTexture depthMap;
    GDescriptor depthMapSRV;

    GTexture velocityMap;
    GDescriptor velocityMapSRV;
    GDescriptor velocityMapUAV;

    GTexture tilemaxMap;
    GDescriptor tilemaxMapSRV;
    GDescriptor tilemaxMapUAV;

    GTexture neighbourmaxMap;
    GDescriptor neighbourmaxMapSRV;
    GDescriptor neighbourmaxMapUAV;

    GTexture mbMap;
    GDescriptor mbMapSRV;
    GDescriptor mbMapUAV;

public:
    virtual ~MBResources() = default;

    const GTexture& GetDepthMap() const { return depthMap; }
    const GTexture& GetVelocityMap() const { return velocityMap; }
    const GTexture& GetTilemaxMap() const { return tilemaxMap; }
    const GTexture& GetNeighbourmaxMap() const { return neighbourmaxMap; }
    const GTexture& GetMbMap() const { return mbMap; }

    const GDescriptor* GetDepthMapSRV() const { return &depthMapSRV; }
    const GDescriptor* GetVelocityMapSRV() const { return &velocityMapSRV; }
    const GDescriptor* GetVelocityMapUAV() const { return &velocityMapUAV; }
    const GDescriptor* GetTilemaxMapSRV() const { return &tilemaxMapSRV; }
    const GDescriptor* GetTilemaxMapUAV() const { return &tilemaxMapUAV; }
    const GDescriptor* GetNeighbourmaxMapSRV() const { return &neighbourmaxMapSRV; }
    const GDescriptor* GetNeighbourmaxMapUAV() const { return &neighbourmaxMapUAV; }
    const GDescriptor* GetMbMapSRV() const { return &mbMapSRV; }
    const GDescriptor* GetMbMapUAV() const { return &mbMapUAV; }


    const GRootSignature& GetVelocityRootSignature() const { return *velocityRootSignature; }
    const GRootSignature& GetTilemaxRootSignature() const { return *tilemaxRootSignature; }
    const GRootSignature& GetNeighbourmaxRootSignature() const { return *neighbourmaxRootSignature; }
    const GRootSignature& GetMbRootSignature() const { return *mbRootSignature; }

    const ComputePSO& GetVelocityPSO() const { return *velocityPSO; }
    const ComputePSO& GetTilemaxPSO() const { return *tilemaxPSO; }
    const ComputePSO& GetNeighbourmaxPSO() const { return *neighbourmaxPSO; }
    const ComputePSO& GetMbPSO() const { return *mbPSO; }

    void virtual InitializeRS();
    void virtual Initialize(const std::shared_ptr<GDevice>& Device, const D3D12_INPUT_LAYOUT_DESC& layout);

    void virtual OnResize(uint32_t width, uint32_t height);

protected:
    void virtual RebuildDescriptors() const;

    void virtual BuildPSO(const D3D12_INPUT_LAYOUT_DESC& layout);

    void virtual InitializeVelocityRS();
    void virtual InitializeTilemaxRS();
    void virtual InitializeNeighbourmaxRS();
    void virtual InitializeMbRS();
};

class MBCrossResources final {

    // здесь должны быть те штуки, которые € отдаю наружу в итоге?
    // а промежуточные карты?
    // фрейм наверное нет т к он скорее в не-параллельном пассе
    
    std::shared_ptr<GCrossAdapterResource> sharedDepthMap;
    //std::shared_ptr<GCrossAdapterResource> sharedVelocityMap;
    std::shared_ptr<GCrossAdapterResource> sharedNeighbourmaxMap;

public:
    void Initialize(const MBResources& Resources, const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice);

    void OnResize(uint32_t width, uint32_t height) const;

    const GCrossAdapterResource& GetDepthMap() const { return *sharedDepthMap; }
    //const GCrossAdapterResource& GetVelocityMap() const { return *sharedVelocityMap; }
    const GCrossAdapterResource& GetNeighbourmaxMap() const { return *sharedNeighbourmaxMap; }
};

class SharedMB final 
{
public:
	SharedMB();
	~SharedMB();

    static const int tileSize = 8;
    static const float maxVelocity; // объ€влена в .h

    const MBResources& GetPrimeResources() { return primeResources; }
    const MBResources& GetSecondResources() { return secondResources; }
    const MBCrossResources& GetCrossResources() { return crossResources; }

    void Initialize(const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice,
        const D3D12_INPUT_LAYOUT_DESC& layout, UINT width, UINT height);

    void OnResize(UINT newWidth, UINT newHeight);


    void ComputeMbTextures(
        const std::shared_ptr<GCommandList>& cmdList,
        const std::shared_ptr<ConstantUploadBuffer<MBConstants>>& currFrame,
        const MBResources& Resources//,/* int blurCount,*/// const std::shared_ptr<SharedSSAO> ssaoPass
    );

    void ComputeVelocityBuffer(
        const std::shared_ptr<GCommandList>& cmdList,
        const std::shared_ptr<ConstantUploadBuffer<MBConstants>>& currFrame,
        const MBResources& Resources//,
       // const std::shared_ptr<SharedSSAO> ssaoPass
    );

    void ComputeTileMax(
        const std::shared_ptr<GCommandList>& cmdList,
        const std::shared_ptr<ConstantUploadBuffer<MBConstants>>& currFrame,
        const MBResources& Resources
    );

    void ComputeNeighbourMax(
        const std::shared_ptr<GCommandList>& cmdList,
        const std::shared_ptr<ConstantUploadBuffer<MBConstants>>& currFrame,
        const MBResources& Resources
    );
    
    void ComputeMotionBlur(
        const std::shared_ptr<GCommandList>& cmdList,
        const std::shared_ptr<ConstantUploadBuffer<MBConstants>>& currFrame,
        const MBResources& Resources,
        const std::shared_ptr<SharedSSAO> ssaoPass,
        std::shared_ptr<SSAA> antiAliasingPrimePath
    );

private:
    MBResources primeResources;
    MBResources secondResources;
    MBCrossResources crossResources;

    UINT RenderTargetWidth;
    UINT RenderTargetHeight;

    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissorRect;
};