#pragma once

#include <array>
#include <memory>
#include <unordered_map>
#include <DirectXCollision.h>
#include "d3dApp.h"
#include "GCommandQueue.h"
#include "GRootSignature.h"
#include "ComputePSO.h"
#include "GShader.h"
#include "DOFFrameResource.h"
#include "GameTimer.h"
#include "AssetsLoader.h"
#include "RenderModeFactory.h"
#include "GameObject.h"
#include "Light.h"
#include "Camera.h"
#include "CameraController.h"
#include "SkyBox.h"
#include "ModelRenderer.h"
#include "UILayer.h"
#include "Services/FileQueueWriter.h"
#include "Services/BenchmarkService.h"

using namespace PEPEngine;
using namespace Graphics;
using namespace Utils;

class DOFApp final : public Common::D3DApp
{
public:
    explicit DOFApp(HINSTANCE hInstance);
    ~DOFApp() override = default;

    bool Initialize() override;
    void OnResize() override;
    void Update(const GameTimer& gt) override;
    void Draw(const GameTimer& gt) override;
    bool InitMainWindow() override;
    void Flush() override;
    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

    void SetForceSingleGpu(bool v) { forceSingleGpu = v; }

private:
    void InitDevices();
    void InitSharedFence();

    void InitRootSignatures();
    void InitPSOs();
    void InitSceneRootSignature();
    void InitScenePSOs();

    void InitFrameResources();
    void ResizeFrameResources(UINT width, UINT height);
    void BuildDescriptors(DOFFrameResource& frame, UINT width, UINT height);
    void BuildPrimeDescriptors(DOFFrameResource& frame, UINT width, UINT height);

    void LoadTextures();
    void LoadModels();
    void CreateMaterials();
    void InitDescriptorHeaps();
    void CreateSceneObjects();
    void SortScene();

    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateMaterials() const;

    void RecordScenePass(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame);
    void RecordPrimeCopyToShared(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame);
    void RecordSecondCopyFromShared(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame);
    void RecordSecondDOFPasses(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame);
    void RecordSecondCopyToShared(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame);

    void RecordPrimeDepthQuantize(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame);
    void RecordPrimeCoC(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame);
    void RecordPrimeCopyDOFFromShared(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& dofFrame);
    void RecordPrimeGather(const std::shared_ptr<GCommandList>& cmdList,
                           DOFFrameResource& sceneFrame,
                           DOFFrameResource& dofFrame);

    void RecordPrimeComposite(const std::shared_ptr<GCommandList>& cmdList,
                              DOFFrameResource& sceneFrame,
                              DOFFrameResource* dofResultSource);

    void CopyRowMajorTexture(const std::shared_ptr<GCommandList>& cmdList,
                             const GResource& dst, const GResource& src,
                             const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint,
                             UINT numRows);

    void CreateLocalTexture(GTexture& texture, const std::shared_ptr<GDevice>& device, DXGI_FORMAT format,
                            UINT width, UINT height, const std::wstring& name,
                            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    void ApplyDofPreset(int presetIndex);

private:
    std::shared_ptr<GDevice> primeDevice;
    std::shared_ptr<GDevice> secondDevice;
    std::shared_ptr<GDevice> dofDevice;

    std::shared_ptr<GCommandQueue> primeGraphicsQueue;
    std::shared_ptr<GCommandQueue> primeCopyQueue;
    std::shared_ptr<GCommandQueue> secondGraphicsQueue;
    std::shared_ptr<GCommandQueue> dofGraphicsQueue;

    std::vector<std::unique_ptr<DOFFrameResource>> frameResources;
    DOFFrameResource* currentFrame = nullptr;
    int currentFrameIndex = 0;

    std::shared_ptr<AssetsLoader> assets;
    RenderModeFactory pipelineFactory;
    std::shared_ptr<GRootSignature> sceneRootSignature;
    GDescriptor srvTexturesMemory;
    GDescriptor backBufferRTVs;
    std::unordered_map<std::wstring, std::shared_ptr<GModel>> models;
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    std::vector<std::vector<std::shared_ptr<Renderer>>> typedRenderer;
    std::vector<Light*> lights;
    std::shared_ptr<Camera> camera;
    PassConstants mainPassCB;
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissorRect{};
    DirectX::BoundingSphere sceneBounds{DirectX::SimpleMath::Vector3::Zero, 200.0f};
    float lightRotationAngle = 0.0f;
    DirectX::SimpleMath::Vector3 baseLightDirections[3] = {
        {0.57735f, -0.57735f, 0.57735f},
        {-0.57735f, -0.57735f, 0.57735f},
        {0.0f, -0.707f, -0.707f}
    };
    DirectX::SimpleMath::Vector3 rotatedLightDirections[3];

    std::shared_ptr<UILayer> uiLayer;

    GRootSignature dofRootSignature;
    GRootSignature dofPrimeRootSignature;

    std::unique_ptr<GShader> csCoC;
    std::unique_ptr<GShader> csHoles;
    std::unique_ptr<GShader> csPatchPyramid;
    std::unique_ptr<GShader> csPatchMatch;
    std::unique_ptr<GShader> csWmax;
    std::unique_ptr<GShader> csWfilter;
    std::unique_ptr<GShader> csGather;
    std::unique_ptr<GShader> csDebugCopy;
    std::unique_ptr<GShader> csDepthQuantize;

    ComputePSO psoCoC;
    ComputePSO psoHoles;
    ComputePSO psoPatchPyramid;
    ComputePSO psoPatchMatch;
    ComputePSO psoWmax;
    ComputePSO psoWfilter;

    ComputePSO psoPrimeCoC;
    ComputePSO psoPrimeGather;
    ComputePSO psoPrimeDebugCopy;
    ComputePSO psoDepthQuantize;

    static constexpr UINT kSrvOffsetDepth = 0;
    static constexpr UINT kSrvOffsetColor = 1;
    static constexpr UINT kSrvOffsetOcclusion = 2;
    static constexpr UINT kSrvOffsetColorFilled = 3;
    static constexpr UINT kSrvOffsetWmax = 4;
    static constexpr UINT kSrvOffsetWfiltered = 5;
    static constexpr UINT kSrvOffsetCoC = 6;
    static constexpr UINT kSrvOffsetPyramid1 = 7;
    static constexpr UINT kSrvOffsetNdcDepth = kSrvOffsetPyramid1;
    static constexpr UINT kSrvOffsetPyramid2 = 8;
    static constexpr UINT kSrvOffsetCount = 9;

    static constexpr UINT kUavOffsetCoC = 0;
    static constexpr UINT kUavOffsetOcclusion = 1;
    static constexpr UINT kUavOffsetPrimeDepth = kUavOffsetOcclusion;
    static constexpr UINT kUavOffsetColorFilled = 2;
    static constexpr UINT kUavOffsetWmax = 3;
    static constexpr UINT kUavOffsetWfiltered = 4;
    static constexpr UINT kUavOffsetDOF = 5;
    static constexpr UINT kUavOffsetPyramid1 = 6;
    static constexpr UINT kUavOffsetPyramid2 = 7;
    static constexpr UINT kUavOffsetCount = 8;

    ComPtr<ID3D12Fence> primeFence;
    ComPtr<ID3D12Fence> sharedFence;
    UINT64 sharedFenceValue = 0;

    UINT dispatchWidth = 0;
    UINT dispatchHeight = 0;
    UINT backBufferIndex = 0;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT colorFootprint{};
    UINT colorNumRows = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT depthFootprint{};
    UINT depthNumRows = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT colorFilledFootprint{};
    UINT colorFilledNumRows = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT wFilteredFootprint{};
    UINT wFilteredNumRows = 0;

    float focusDistance = 25.0f;
    float focusRange = 6.0f;
    float maxCoC = 3.4f;
    float filterRadius = 3.0f;
    int   dofTaps = 32;
    bool useMultiGpu = true;
    bool forceSingleGpu = false;

    UINT64 primeGPURenderingTime = 0;
    UINT64 secondGPURenderingTime = 0;
    FileQueueWriter debugLogger;
    BenchmarkService benchmark;
    bool benchmarkFinished = false;

    static constexpr int kFrameHistorySize = 128;
    float frameTimeHistory[kFrameHistorySize] = {};
    int frameTimeOffset = 0;

    enum class DebugViewMode : UINT
    {
        Normal = 0,
        WFiltered = 1,
        WMax = 2,
        NoEffect = 3,
    };
    DebugViewMode debugView = DebugViewMode::Normal;
};
