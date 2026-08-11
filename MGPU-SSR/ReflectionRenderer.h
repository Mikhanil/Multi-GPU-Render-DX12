#pragma once

#include "FrameResource.h"
#include "CubeMapRenderTarget.h"
#include "GDescriptor.h"
#include "GCrossAdapterResource.h"
#include "GRootSignature.h"
#include "GShader.h"
#include "GTexture.h"
#include "GraphicPSO.h"
#include "Scene.h"
#include "ShadowMap.h"
#include "SSAA.h"
#include "SSAO.h"
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "GameTimer.h"

class Camera;

namespace Common
{
    class Window;

    class ReflectionRenderer
    {
    public:
        ReflectionRenderer(std::shared_ptr<Window> window, Scene& scene, std::shared_ptr<Camera> camera,
                           DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthStencilFormat, bool is4xMsaa,
                           UINT msaaQuality);

        void Initialize(const std::shared_ptr<GCommandList>& cmdList);
        void OnResize();
        void SetDebugMap(UINT debugMap);
        void SetFrameResource(FrameResource* frameResource);
        void SetSsaaMultiplier(UINT multiplier);
        void SetUseSecondGpuForSsr(bool enabled);
        void SetUseSecondGpuForReflectionProbes(bool enabled);
        void SetUseDynamicReflectionProbes(bool enabled);
        void Update(const GameTimer& gt);
        void Render(const std::shared_ptr<GCommandList>& cmdList);
        bool IsMgpuSsrEnabled() const;
        bool IsMgpuProbeEnabled() const;
        void RenderMgpuPrimary(const std::shared_ptr<GCommandList>& cmdList);
        void RenderMgpuPrimaryPresent(const std::shared_ptr<GCommandList>& cmdList);
        void RenderPrimaryBeforeSsr(const std::shared_ptr<GCommandList>& cmdList);
        void RenderSsrOnSecondGpu();
        void RenderProbesOnSecondGpu();
        void RenderPrimaryWithImportedProbes(const std::shared_ptr<GCommandList>& cmdList);
        void SignalPrimaryProbeConsumption();

    private:
        static constexpr UINT ReflectionProbeCount = Scene::ReflectionProbeCount;
        std::unique_ptr<GRootSignature> BuildRootSignature(const std::shared_ptr<GDevice>& device) const;
        void BuildRootSignature();
        void BuildSsaoRootSignature();
        void BuildShadersAndInputLayout();
        void BuildPSOs();
        void BuildSecondGpuSsrPSOs();
        void BuildSecondGpuProbePSOs();
        void BuildScreenRenderTargets();
        void BuildColorRenderTarget(PEPEngine::Graphics::GTexture& texture, GDescriptor& rtv, GDescriptor& srv,
                                    const std::wstring& name) const;
        void InitializeMgpu();
        void BuildMgpuSsrResources();
        void BuildSecondGpuSsrDescriptors();
        void BuildMgpuProbeResources();
        void CreateSecondGpuTextureLike(PEPEngine::Graphics::GTexture& texture,
                                        const PEPEngine::Graphics::GTexture& source,
                                        const std::wstring& name,
                                        PEPEngine::Graphics::TextureUsage usage,
                                        const D3D12_CLEAR_VALUE* clearValue = nullptr) const;

        void UpdateShadowTransform();
        void UpdateShadowPassCB();
        void UpdateMainPassCB(const GameTimer& gt);
        void UpdateReflectionProbeCB();
        void UpdateReflectionProbePassCBs();
        void UpdateSsaoCB();
        void UpdateLightBuffers();

        void DrawSceneToShadowMap(const std::shared_ptr<GCommandList>& cmdList);
        void DrawNormals(const std::shared_ptr<GCommandList>& cmdList);
        void DrawReflectionProbes(const std::shared_ptr<GCommandList>& cmdList);
        void DrawSecondGpuShadowMap(const std::shared_ptr<GCommandList>& cmdList);
        void DrawReflectionProbesOnSecondGpu(const std::shared_ptr<GCommandList>& cmdList);
        void CopySecondProbeOutputsToShared(const std::shared_ptr<GCommandList>& cmdList);
        void CopySharedProbeOutputsToPrimary(const std::shared_ptr<GCommandList>& cmdList);

        void DrawSceneToRenderTarget(const std::shared_ptr<GCommandList>& cmdList,
                                     PEPEngine::Graphics::GTexture* renderTarget,
                                     const GDescriptor* renderTargetView,
                                     PEPEngine::Graphics::GTexture* depthStencil,
                                     const GDescriptor* depthStencilView,
                                     const ConstantUploadBuffer<ReflectionPassConstants>& passConstants);
        void DrawSceneToSsaaTarget(const std::shared_ptr<GCommandList>& cmdList);
        void DrawFullscreenTextureToRenderTarget(const std::shared_ptr<GCommandList>& cmdList,
                                                 PEPEngine::Graphics::GTexture& source,
                                                 const GDescriptor* sourceSrv,
                                                 PEPEngine::Graphics::GTexture& renderTarget,
                                                 const GDescriptor* renderTargetView);
        void DrawSsaaTargetToComposedScene(const std::shared_ptr<GCommandList>& cmdList);
        void DrawSsr(const std::shared_ptr<GCommandList>& cmdList);
        void DrawSsrOnSecondGpu(const std::shared_ptr<GCommandList>& cmdList,
                                const ConstantUploadBuffer<ReflectionPassConstants>& passConstants);
        void DrawPresentBackBuffer(const std::shared_ptr<GCommandList>& cmdList);
        void CopyPrimarySsrInputsToShared(const std::shared_ptr<GCommandList>& cmdList);
        void CopySharedSsrInputsToSecond(const std::shared_ptr<GCommandList>& cmdList);
        void CopySecondSsrOutputToShared(const std::shared_ptr<GCommandList>& cmdList);
        void CopySharedSsrOutputToPrimary(const std::shared_ptr<GCommandList>& cmdList);

        std::shared_ptr<Window> window;
        Scene& scene;
        std::shared_ptr<Camera> camera;
        FrameResource* currentFrameResource = nullptr;
        UINT debugMap = 0;
        DXGI_FORMAT backBufferFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_UNKNOWN;
        bool is4xMsaa = false;
        UINT msaaQuality = 0;
        bool useSecondGpuForSsr = false;
        bool useSecondGpuForReflectionProbes = false;

        std::unique_ptr<GRootSignature> rootSignature;
        std::unique_ptr<GRootSignature> secondGpuRootSignature;
        std::unique_ptr<GRootSignature> ssaoRootSignature;
        std::unique_ptr<ShadowMap> shadowMap;
        std::unique_ptr<SSAO> ssao;
        std::unique_ptr<SSAA> ssaa;
        std::array<std::unique_ptr<CubeMapRenderTarget>, ReflectionProbeCount> reflectionProbes;
        GDescriptor reflectionProbeSrvTable;
        bool reflectionProbeContentsValid = false;
        bool useDynamicReflectionProbes = false;

        std::unordered_map<std::string, std::unique_ptr<GShader>> shaders;
        std::unordered_map<PEPEngine::Graphics::RenderMode, std::unique_ptr<GraphicPSO>> psos;
        std::unordered_map<PEPEngine::Graphics::RenderMode, std::unique_ptr<GraphicPSO>> secondGpuSsrPsos;
        std::unordered_map<PEPEngine::Graphics::RenderMode, std::unique_ptr<GraphicPSO>> secondGpuProbePsos;

        std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout;
        std::vector<D3D12_INPUT_ELEMENT_DESC> treeSpriteInputLayout;

        D3D12_VIEWPORT viewport{};
        D3D12_RECT rect{};
        GDescriptor renderTargetMemory;
        PEPEngine::Graphics::GTexture composedSceneColor;
        GDescriptor composedSceneColorRtv;
        GDescriptor composedSceneColorSrv;
        PEPEngine::Graphics::GTexture ssrOutputColor;
        GDescriptor ssrOutputColorRtv;
        GDescriptor ssrOutputColorSrv;

        bool mgpuSsrEnabled = false;
        Microsoft::WRL::ComPtr<ID3D12Fence> primarySsrSharedFence;
        Microsoft::WRL::ComPtr<ID3D12Fence> secondSsrSharedFence;
        UINT64 ssrSharedFenceValue = 0;
        std::shared_ptr<GDevice> secondDevice;
        PEPEngine::Graphics::GTexture secondGpuSceneColor;
        PEPEngine::Graphics::GTexture secondGpuSceneDepth;
        PEPEngine::Graphics::GTexture secondGpuSceneNormal;
        PEPEngine::Graphics::GTexture secondGpuSsrOutputColor;
        GDescriptor secondGpuSsrInputSrv;
        GDescriptor secondGpuSsrOutputRtv;
        std::unique_ptr<::GCrossAdapterResource> sharedSceneColor;
        std::unique_ptr<::GCrossAdapterResource> sharedSceneDepth;
        std::unique_ptr<::GCrossAdapterResource> sharedSceneNormal;
        std::unique_ptr<::GCrossAdapterResource> sharedSsrOutputColor;

        bool mgpuProbeEnabled = false;
        bool secondProbeContentsValid = false;
        Microsoft::WRL::ComPtr<ID3D12Fence> primaryProbeSharedFence;
        Microsoft::WRL::ComPtr<ID3D12Fence> secondProbeSharedFence;
        UINT64 probeSharedFenceValue = 0;
        UINT64 primaryProbeConsumedFenceValue = 0;
        std::unique_ptr<Scene> secondProbeScene;
        std::unique_ptr<ShadowMap> secondGpuShadowMap;
        std::array<std::unique_ptr<CubeMapRenderTarget>, ReflectionProbeCount> secondGpuReflectionProbes;
        std::array<std::array<std::unique_ptr<::GCrossAdapterResource>, CubeMapRenderTarget::FaceCount>,
                   ReflectionProbeCount> sharedReflectionProbeFaces;

        ReflectionPassConstants mainPassCB;
        ReflectionPassConstants shadowPassCB;

        float lightNearZ = 0.0f;
        float lightFarZ = 0.0f;
        DirectX::SimpleMath::Vector3 lightPosW;
        DirectX::SimpleMath::Matrix lightView = DirectX::SimpleMath::Matrix::Identity;
        DirectX::SimpleMath::Matrix lightProj = DirectX::SimpleMath::Matrix::Identity;
        DirectX::SimpleMath::Matrix shadowTransform = DirectX::SimpleMath::Matrix::Identity;
        DirectX::SimpleMath::Vector3 lightDirections[3] = {
            DirectX::SimpleMath::Vector3(0.457f, -0.457f, -0.762f),
            DirectX::SimpleMath::Vector3(-0.57735f, -0.57735f, 0.57735f),
            DirectX::SimpleMath::Vector3(0.0f, -0.707f, -0.707f)
        };
    };
}
