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
#if defined(DEBUG) || defined(_DEBUG)
class UILayer;
#endif

// This sample has one primary path and, when available, one compatible hardware secondary.
constexpr UINT ReflectionAdapterCount = 2;

enum class ReflectionProbeCaptureMode : uint8_t
{
    BakedDynamicOverlay,
    FullDynamic
};

enum class ReflectionProbeUpdateMode : uint8_t
{
    AllProbesPerFrame,
    OneProbePerFrame,
    OneFacePerFrame
};

struct ReflectionProbeConfiguration
{
    UINT PrimaryProbeCount = Common::Scene::ReflectionProbeCount;
    ReflectionProbeCaptureMode CaptureMode = ReflectionProbeCaptureMode::FullDynamic;
    ReflectionProbeUpdateMode UpdateMode = ReflectionProbeUpdateMode::AllProbesPerFrame;
};

class ReflectionRenderer
{
public:
    // Ownership invariant: every render resource and synchronization primitive is
    // renderer-owned; scenes remain adapter-local and are never shared between GPUs.
    ReflectionRenderer(std::shared_ptr<Common::Window> window,
                       std::vector<std::shared_ptr<GDevice>>& devices,
                       std::vector<std::unique_ptr<Common::Scene>>& scenes,
                       DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthStencilFormat);
    ~ReflectionRenderer();

    void Initialize();
    void Update(const GameTimer& gt);
    void Draw(const GameTimer& gt);
    void OnResize(float aspectRatio);
    void Flush() const;
    void SetUseOnlyPrime(bool value);
    bool GetUseOnlyPrime() const { return probeConfiguration.PrimaryProbeCount == ReflectionProbeCount; }
    void SetReflectionProbeConfiguration(ReflectionProbeConfiguration configuration);
    void ResetBenchmarkAnimation();
    void SetSsaaMultiplier(UINT value);
    UINT GetSsaaMultiplier() const { return multi; }
    void SetDebugMap(UINT value) { pathMapShow = value; }
#if defined(DEBUG) || defined(_DEBUG)
    void ForwardUiMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) const;
    bool UiWantsMouseCapture() const;
    bool UiWantsKeyboardCapture() const;
#endif

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
    void PopulateSecondaryStaticShadowMapCommands(const std::shared_ptr<GCommandList>& cmdList);
    void PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList);
    void PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList);
    void PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList);
    void PopulateDrawCommands(GraphicsAdapter adapterIndex, const std::shared_ptr<GCommandList>& cmdList, RenderMode type);
    void PopulateDrawQuadCommand(const std::shared_ptr<GCommandList>& cmdList, const GTexture& renderTarget,
                                 const GDescriptor* rtvMemory, UINT offsetRTV);
    void PopulateBakedProbeCommands(GraphicsAdapter adapter, UINT probeIndex,
                                    const std::shared_ptr<GCommandList>& cmdList);
    void PopulatePrimaryProbeCommands(const std::shared_ptr<GCommandList>& cmdList);
    void PopulateSecondaryProbeCommands(const std::shared_ptr<GCommandList>& cmdList);
    void CopySharedProbeOutputsToPrimary(const std::shared_ptr<GCommandList>& cmdList);
    void ResetProbeScheduling();
    void InitCubeFacePasses();
    ReflectionPassConstants GetCubeFacePass(UINT probeIndex, UINT face,
                                             bool useBakedSecondaryLighting) const;
    void CreateDynamicTextures(GraphicsAdapter adapter);

    std::shared_ptr<Common::Window> window;
    std::vector<std::shared_ptr<GDevice>>& devices;
    std::vector<std::unique_ptr<Common::Scene>>& scenes;
    DXGI_FORMAT backBufferFormat;
    DXGI_FORMAT depthStencilFormat;
    bool hasSecondaryAdapter = false;
    static constexpr UINT ReflectionProbeCount = Common::Scene::ReflectionProbeCount;
    static constexpr UINT InvalidProbeIndex = ReflectionProbeCount;
    std::array<std::array<ReflectionPassConstants, CubeMapRenderTarget::FaceCount>, ReflectionProbeCount>
        cubeFaceCameraPasses{};
    ReflectionProbeConfiguration probeConfiguration;
    UINT nextPrimaryProbeIndex = 0;
    UINT nextPrimaryProbeFace = 0;
    UINT nextSecondaryProbeIndex = InvalidProbeIndex;
    UINT nextSecondaryProbeFace = 0;
    UINT nextSharedProbeIndex = InvalidProbeIndex;
    UINT nextSharedProbeFace = 0;
    UINT64 secondaryProbeFenceValue = 0;
    bool secondaryProbeSubmissionReady = false;
    UINT multi = 1;
    D3D12_VIEWPORT fullViewport{};
    D3D12_RECT fullRect{};
    std::shared_ptr<GRootSignature> primeDeviceSignature, ssaoPrimeRootSignature, ssaoSecondRootSignature, secondDeviceSignature;
    RenderModeFactory primePipelineResources, secondPipelineResources;
    std::array<std::array<std::shared_ptr<GCrossAdapterResource>, CubeMapRenderTarget::FaceCount>,
               ReflectionProbeCount> crossAdapterCubeMaps;
    std::shared_ptr<ShadowMap> shadowPathPrimeDevice, shadowPathSecondDevice;
    std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout{};
    std::shared_ptr<SSAO> ambientPrimePath;
    std::shared_ptr<SSAA> antiAliasingPrimePath;
    ReflectionPassConstants mainPassCB{}, shadowPassCB{};
    bool hasBakedSecondaryLighting = false;
    LightData bakedSecondaryDirectionalLight{};
    DirectX::SimpleMath::Matrix bakedSecondaryShadowTransform = DirectX::SimpleMath::Matrix::Identity;
    std::vector<std::shared_ptr<FrameResource>> frameResources{};
    std::shared_ptr<FrameResource> currentFrameResource;
    std::atomic<UINT> currentFrameResourceIndex = 0;
    float mLightNearZ = 0.0f, mLightFarZ = 0.0f;
    DirectX::SimpleMath::Vector3 mLightPosW;
    DirectX::SimpleMath::Matrix mLightView = DirectX::SimpleMath::Matrix::Identity, mLightProj = DirectX::SimpleMath::Matrix::Identity, mShadowTransform = DirectX::SimpleMath::Matrix::Identity;
    static constexpr UINT DynamicCubeMapFirstPassIndex = 2, DynamicCubeMapSize = 1024;
    std::array<std::shared_ptr<CubeMapRenderTarget>, ReflectionProbeCount> dynamicCubeMaps;
    std::array<std::shared_ptr<BakedCubeMapRenderTarget>, ReflectionProbeCount> bakedCubeMapsPrime;
    std::array<std::shared_ptr<BakedCubeMapRenderTarget>, ReflectionProbeCount> bakedCubeMapsSecond;
    std::array<bool, ReflectionProbeCount> primeProbeBaked{};
    std::array<bool, ReflectionProbeCount> secondProbeBaked{};
    static constexpr UINT BakedCubeMapFirstPassIndex = 2, BakedCubeMapSize = 1024;
    GTexture dynamicCubeMapFaceColor, dynamicCubeMapFaceDepth;
    GDescriptor dynamicCubeMapFaceRtv, dynamicCubeMapFaceDsv;
    float mLightRotationAngle = 0.0f;
    DirectX::SimpleMath::Vector3 mBaseLightDirections[3] = {
        {0.57735f, -0.57735f, 0.57735f}, {-0.57735f, -0.57735f, 0.57735f}, {0.0f, -0.707f, -0.707f}};
    DirectX::SimpleMath::Vector3 mRotatedLightDirections[3];
    DirectX::BoundingSphere mSceneBounds{};
    UINT pathMapShow = 0;
#if defined(DEBUG) || defined(_DEBUG)
    std::unique_ptr<UILayer> uiLayer;
#endif
};
