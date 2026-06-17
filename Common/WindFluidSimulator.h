#pragma once

#include "GBuffer.h"
#include "GDescriptor.h"
#include "GrassData.h"
#include "GRootSignature.h"

#include <wrl/client.h>

#include <memory>

namespace PEPEngine::Graphics
{
    class GCommandList;
    class GDevice;
    class ComputePSO;
    class GShader;
    class GResource;

    struct WindFluidGpuCB
    {
        uint32_t GridW{};
        uint32_t GridH{};
        uint32_t JacobiIterations{};
        uint32_t WindOriginCount{};

        float InvGridW{};
        float InvGridH{};
        float Dt{};
        float Dissipation{};

        float InjectStrength{};
        float VorticityEps{};
        float WindMapFalloff{};
        float CellWorldSize{};

        DirectX::SimpleMath::Vector4 FieldCenterHalf{};
        DirectX::SimpleMath::Vector4 ObstacleWallA{};
        DirectX::SimpleMath::Vector4 ObstacleWallB{};

        DirectX::SimpleMath::Vector4 WindOriginData[GrassEmitterData::MaxWindOrigins]{};
        DirectX::SimpleMath::Vector4 WindDirectionData[GrassEmitterData::MaxWindOrigins]{};

        float ClickImpulseU{};
        float ClickImpulseV{};
        float ClickImpulseStrength{};
        float ClickImpulseRadiusSq{};

        uint32_t _cbPadding[4]{};
    };

    class WindFluidSimulator
    {
    public:
        void Initialize(std::weak_ptr<GDevice> deviceWeak, UINT gridResolution = 256);
        bool IsInitialized() const { return initialized_; }

        void Simulate(const std::shared_ptr<GCommandList>& cmdList, const WindFluidGpuCB& params);

        Microsoft::WRL::ComPtr<ID3D12Resource> GetVelocityResource() const;
        Microsoft::WRL::ComPtr<ID3D12Resource> GetDyeResource() const;

        UINT ReadableVelocitySrvHeapIndex() const { return readableSrvHeapIdx_; }

        bool HasReadableVelocitySrv() const
        {
            return initialized_ && readableSrvHeapIdx_ != kInvalidSrvIndex;
        }

        UINT GetGridResolution() const { return grid_; }

        const GDescriptor& GpuDescriptors() const { return descriptors_; }

        /// Copy the current readable wind-velocity view into another CBV/SRV/UAV heap (same device).
        void PublishReadableSrvTo(GDescriptor* destHeap, UINT destSrvOffset) const;

        static constexpr UINT kInvalidSrvIndex = 0xffffffffu;

    private:
        void ReleaseWindFluidGpuState();

        bool CreateTexturesAndViews(const std::shared_ptr<GDevice>& device);
        bool CreateRootsAndPSOs(const std::shared_ptr<GDevice>& device);

        std::weak_ptr<GDevice> device_;
        UINT grid_{256};
        bool initialized_{false};
        bool needsReset_{true};

        bool velHistoryReadsA_{true};

        std::unique_ptr<GResource> velA_;
        std::unique_ptr<GResource> velB_;
        std::unique_ptr<GResource> div_;
        std::unique_ptr<GResource> pressA_;
        std::unique_ptr<GResource> pressB_;
        std::unique_ptr<GResource> dyeA_;
        std::unique_ptr<GResource> dyeB_;

        GDescriptor descriptors_{};
        UINT readableSrvHeapIdx_{kInvalidSrvIndex};
        UINT readableDyeSrvHeapIdx_{kInvalidSrvIndex};

        std::unique_ptr<GBuffer> gpuCB_;

        std::shared_ptr<GRootSignature> rsInject_;
        std::shared_ptr<GRootSignature> rsAdvect_;
        std::shared_ptr<GRootSignature> rsDiv_;
        std::shared_ptr<GRootSignature> rsJacobi_;
        std::shared_ptr<GRootSignature> rsProject_;

        std::shared_ptr<ComputePSO> psoInject_;
        std::shared_ptr<ComputePSO> psoAdvect_;
        std::shared_ptr<ComputePSO> psoDiv_;
        std::shared_ptr<ComputePSO> psoJacobi_;
        std::shared_ptr<ComputePSO> psoProject_;
        std::shared_ptr<ComputePSO> psoDyeAdvect_;
    };
}
