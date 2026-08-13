#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "BakedCubeMapRenderTarget.h"
#include "CubeMapRenderTarget.h"
#include "FrameResource.h"
#include "GCrossAdapterResource.h"
#include "GraphicPSO.h"
#include "RenderModeFactory.h"
#include "ShadowMap.h"
#include "SSAA.h"
#include "SSAO.h"
#include "Scene.h"
#include "GDeviceFactory.h"
#include "GameTimer.h"

using namespace PEPEngine;
using namespace PEPEngine::Graphics;
using namespace PEPEngine::Utils;

namespace Common { class Window; }

// This sample has one primary path and, when available, one compatible hardware secondary.
constexpr UINT ReflectionAdapterCount = 2;

class ReflectionRenderer
{
public:
    // Ownership invariant: every render resource and synchronization primitive is
    // renderer-owned; scenes remain adapter-local and are never shared between GPUs.
    ReflectionRenderer(std::shared_ptr<Common::Window> window,
                       std::vector<std::shared_ptr<GDevice>>& devices,
                       std::vector<std::unique_ptr<Common::Scene>>& scenes,
                       DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthStencilFormat);

    void Initialize();
    void Update(const GameTimer& gt);
    void Draw(const GameTimer& gt);
    void OnResize(float aspectRatio);
    void Flush();
    void SetUseOnlyPrime(bool value) { UseOnlyPrime = value || !hasSecondaryAdapter; }
    bool GetUseOnlyPrime() const { return UseOnlyPrime; }
    void ResetBenchmarkAnimation();
    void SetSsaaMultiplier(UINT value);
    UINT GetSsaaMultiplier() const { return multi; }
    void SetDebugMap(UINT value) { pathMapShow = value; }
    UINT64 GetGpuTime(GraphicsAdapter adapter) const { return gpuTimes[adapter]; }

private:
    void InitFrameResource();
    void InitRootSignature();
    void InitPipeLineResource();
    void InitRenderPaths();
    void UpdateShadowTransform();
    void UpdateShadowPassCB();
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateLightBuffers();
    void UpdateSsaoCB();
    void PopulateShadowMapCommands(std::shared_ptr<GCommandList> cmdList);
    void PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList);
    void PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList);
    void PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList);
    void PopulateDrawCommands(GraphicsAdapter adapterIndex, const std::shared_ptr<GCommandList>& cmdList, RenderMode type);
    void PopulateDrawQuadCommand(const std::shared_ptr<GCommandList>& cmdList, const GTexture& renderTarget,
                                 const GDescriptor* rtvMemory, UINT offsetRTV);
    void PopulateDynamicCubeMapCommands(GraphicsAdapter adapter, const std::shared_ptr<GCommandList>& cmdList);
    std::array<ReflectionPassConstants, CubeMapRenderTarget::FaceCount> BuildCubeFacePassCBs(const DirectX::SimpleMath::Vector3& center) const;
    void CreateDynamicTextures(GraphicsAdapter adapter);

    std::shared_ptr<Common::Window> window;
    std::vector<std::shared_ptr<GDevice>>& devices;
    std::vector<std::unique_ptr<Common::Scene>>& scenes;
    DXGI_FORMAT backBufferFormat;
    DXGI_FORMAT depthStencilFormat;
    UINT64 gpuTimes[ReflectionAdapterCount]{};
    bool hasSecondaryAdapter = false;
    bool UseOnlyPrime = true;
    UINT multi = 1;
    D3D12_VIEWPORT fullViewport{};
    D3D12_RECT fullRect{};
    std::shared_ptr<GRootSignature> primeDeviceSignature, ssaoPrimeRootSignature, ssaoSecondRootSignature, secondDeviceSignature;
    RenderModeFactory primePipelineResources, secondPipelineResources;
    std::array<std::shared_ptr<GCrossAdapterResource>, CubeMapRenderTarget::FaceCount> crossAdapterCubeMaps;
    std::shared_ptr<ShadowMap> cubeMapSecondDevice, shadowPathPrimeDevice, cubeMapPrimeDevice;
    std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout{};
    std::shared_ptr<SSAO> ambientPrimePath;
    std::shared_ptr<SSAA> antiAliasingPrimePath;
    ReflectionPassConstants mainPassCB{}, shadowPassCB{};
    ComPtr<ID3D12Fence> primeFence, secondFence;
    UINT64 sharedFenceValue = 0;
    std::vector<std::shared_ptr<FrameResource>> frameResources{};
    std::shared_ptr<FrameResource> currentFrameResource;
    std::atomic<UINT> currentFrameResourceIndex = 0;
    float mLightNearZ = 0.0f, mLightFarZ = 0.0f;
    DirectX::SimpleMath::Vector3 mLightPosW;
    DirectX::SimpleMath::Matrix mLightView = DirectX::SimpleMath::Matrix::Identity, mLightProj = DirectX::SimpleMath::Matrix::Identity, mShadowTransform = DirectX::SimpleMath::Matrix::Identity;
    static constexpr UINT DynamicCubeMapFirstPassIndex = 2, DynamicCubeMapSize = 1024;
    std::shared_ptr<CubeMapRenderTarget> dynamicCubeMap;
    std::shared_ptr<BakedCubeMapRenderTarget> bakedCubeMapSecond;
    std::atomic<bool> isBaked = false;
    static constexpr UINT BakedCubeMapFirstPassIndex = 2, BakedCubeMapSize = 1024;
    GTexture dynamicCubeMapFaceColor, dynamicCubeMapFaceDepth;
    GDescriptor dynamicCubeMapFaceRtv, dynamicCubeMapFaceDsv;
    float mLightRotationAngle = 0.0f;
    DirectX::SimpleMath::Vector3 mBaseLightDirections[3] = {
        {0.57735f, -0.57735f, 0.57735f}, {-0.57735f, -0.57735f, 0.57735f}, {0.0f, -0.707f, -0.707f}};
    DirectX::SimpleMath::Vector3 mRotatedLightDirections[3];
    DirectX::BoundingSphere mSceneBounds{};
    UINT pathMapShow = 0;
};
