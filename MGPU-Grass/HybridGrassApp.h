#pragma once
#include "AssetsLoader.h"
#include "CrossAdapterParticleEmitter.h"
#include "ParticleEmitter.h"
#include "CrossAdapterGrassEmitter.h"
#include "GrassEmitter.h"
#include "d3dApp.h"
#include "Renderer.h"
#include "RenderModeFactory.h"
#include "ShadowMap.h"
#include "SSAA.h"
#include "SSAO.h"
#include "FrameResource.h"
#include "GCrossAdapterResource.h"
#include "GDeviceFactory.h"
#include "GDescriptor.h"
#include "Light.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <string>
#include <limits>
#include <vector>

struct ImGui_ImplDX12_InitInfo;
class Transform;

class HybridGrassApp :
    public Common::D3DApp
{
public:
    HybridGrassApp(HINSTANCE hInstance);
    ~HybridGrassApp() override;

    bool Initialize() override;;

    int Run() override;
    void EnablePerformanceTestMode(int warmupSeconds = 5, int sampleSeconds = 20);
    void EnablePerformanceSweepMode(int warmupSeconds = 5, int sampleSeconds = 15);

protected:
    void Update(const GameTimer& gt) override;
    void PopulateShadowMapCommands(std::shared_ptr<GCommandList> cmdList);;
    void PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList);
    void PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList);
    void PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList);
   
    void PopulateDrawCommands(std::shared_ptr<GCommandList> cmdList,
                              RenderMode type);
    void PopulateInitRenderTarget(const std::shared_ptr<GCommandList>& cmdList, GTexture& renderTarget, GDescriptor* rtvMemory,
                                  UINT offsetRTV);
    void PopulateDrawFullQuadTexture(const std::shared_ptr<GCommandList>& cmdList,
                                     GDescriptor* renderTextureSRVMemory, UINT renderTextureMemoryOffset,
                                     GraphicPSO& pso);
    void Draw(const GameTimer& gt) override;

    void InitDevices();
    bool TryCreateCrossAdapterFences();
    void InitFrameResource();
    void InitRootSignature();
    void InitPipeLineResource();
    void CreateMaterials();
    void InitSRVMemoryAndMaterials();
    void InitRenderPaths();
    void LoadStudyTexture();
    void LoadModels();
    void MipMasGenerate();
    void SortGO();
    void CreateGO();
    void CalculateFrameStats() override;
    void LogWriting();
    void WritePerformanceTestResults();
    void WritePerformanceSweepResults();
    void UpdateMaterials();
    void UpdateShadowTransform(const GameTimer& gt);
    void UpdateShadowPassCB(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateSsaoCB(const GameTimer& gt);
    bool InitMainWindow() override;
    void OnResize() override;
    void Flush() override;
    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

    void InitImGui();
    void ShutdownImGui();
    void InitWindGradientPreviewTexture();
    void EnsureWindGradientPreviewTexture();
    void ReleaseWindGradientPreviewTexture();
    void RefreshWindGradientPreviewTexture(const std::shared_ptr<GCommandList>& cmdList);
    void AppendWindFluidPreviewReadbackIfDue(const std::shared_ptr<GCommandList>& cmdList);
    bool TryRebuildWindGradientPreviewFromSecondGpu(UINT8* uploadMappedBase);
    void EnsureWindFluidReadbackMatchesVelocity(ID3D12Resource* velocityTex);

    void GetGrassWindFieldExtents(float& outCenterX, float& outCenterZ, float& outHalfExtent) const;
    bool TryPickGrassGroundFromMouse(int clientX, int clientY, Vector3& outHitWorld) const;
    void DrawImGui(const std::shared_ptr<GCommandList>& cmdList);

    std::shared_ptr<GDevice> primeDevice;
    std::shared_ptr<GDevice> secondDevice;

    LockThreadQueue<std::wstring> logQueue{};
    UINT64 primeGPURenderingTime = 0;
    UINT64 secondGPURenderingTime = 0;

    UINT64 primeGPUComputingTime = 0;
    UINT64 secondGPUComputingTime = 0;

    D3D12_VIEWPORT fullViewport{};
    D3D12_RECT fullRect;

    std::shared_ptr<AssetsLoader> assets;

    std::unordered_map<std::wstring, std::shared_ptr<GModel>> models = std::unordered_map<
        std::wstring, std::shared_ptr<GModel>>();
    std::shared_ptr<GRootSignature> primeDeviceSignature;
    std::shared_ptr<GRootSignature> ssaoPrimeRootSignature;
    std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout{};
    GDescriptor srvTexturesMemory;
    RenderModeFactory defaultPrimePipelineResources;


    bool IsStop = false;
    bool performanceTestMode = false;
    bool performanceSweepMode = false;
    int perfWarmupSeconds = 5;
    int perfSampleSeconds = 20;
    int perfCurrentStage = 0; // 0 = single GPU, 1 = multi GPU
    double perfStageStartTime = -1.0;
    bool perfStageInitialized = false;
    std::wstring perfResultPath;

    struct PerfAggregate
    {
        int samples = 0;
        double fpsSum = 0.0;
        double primeRenderSum = 0.0;
        double secondRenderSum = 0.0;
        double primeComputeSum = 0.0;
        double secondComputeSum = 0.0;
        double minFps = std::numeric_limits<double>::max();
        double maxFps = std::numeric_limits<double>::lowest();
    };
    std::array<PerfAggregate, 2> perfAggregates{};
    struct PerfScenario
    {
        std::wstring name;
        int grassCount = 5000;
        float lod0Distance = 350.0f;
        float lod1Distance = 1000.0f;
        int lod0BladeCount = 3;
        int lod1BladeCount = 1;
        float fieldInfluenceScale = 1.0f;
    };
    std::vector<PerfScenario> perfScenarios{};
    std::vector<std::array<PerfAggregate, 2>> perfScenarioAggregates{};

    const UINT StatisticStepSecondsCount = 120;


    std::shared_ptr<ShadowMap> shadowPath;
    std::shared_ptr<SSAO> ambientPrimePath;
    std::shared_ptr<SSAA> antiAliasingPrimePath;

    std::vector<std::shared_ptr<GameObject>> gameObjects = std::vector<std::shared_ptr<
        GameObject>>();

    std::vector<std::vector<std::shared_ptr<Renderer>>> typedRenderer = std::vector<std::vector<
        std::shared_ptr<Renderer>>>();

    bool UseCrossAdapter = false;
    /// At least two DXGI hardware adapters (excludes WARP).
    bool HaveTwoHardwareAdapters = false;
    /// Shared cross-adapter fences were created successfully (runtime; not tied to CrossAdapterRowMajorTexture).
    bool CrossAdapterFencesCreated = false;
    /// Informational: both adapters report CrossAdapterRowMajorTexture (optional feature).
    bool CrossAdapterSharingCapable = false;
    /// Two hardware GPUs present (for perf / statistics).
    bool HaveCrossAdapterHardware = false;


    std::vector<CrossAdapterParticleEmitter*> crossEmitter = std::vector<CrossAdapterParticleEmitter*>();
    std::vector<CrossAdapterGrassEmitter*> crossGrassEmitters = std::vector<CrossAdapterGrassEmitter*>();
    ComPtr<ID3D12Fence> primeComputeFence;
    ComPtr<ID3D12Fence> secondComputeFence;
    UINT64 sharedComputeFenceValue = 0;

    ComPtr<ID3D12Fence> primeRenderFence;
    ComPtr<ID3D12Fence> secondRenderFence;
    UINT64 sharedRenderFenceValue = 0;

    PassConstants mainPassCB;
    PassConstants shadowPassCB;
   // PassConstants shadowPassCB;

    std::vector<std::shared_ptr<FrameResource>> frameResources = std::vector<std::shared_ptr<
        FrameResource>>();
    std::shared_ptr<FrameResource> currentFrameResource = nullptr;
    std::atomic<UINT> currentFrameResourceIndex = 0;

    std::vector<Light*> lights = std::vector<Light*>();

    float mLightNearZ = 0.0f;
    float mLightFarZ = 0.0f;
    Vector3 mLightPosW;
    Matrix mLightView = Matrix::Identity;
    Matrix mLightProj = Matrix::Identity;
    Matrix mShadowTransform = Matrix::Identity;

    float mLightRotationAngle = 0.0f;
    Vector3 mBaseLightDirections[3] = {
        Vector3(0.57735f, -0.57735f, 0.57735f),
        Vector3(-0.57735f, -0.57735f, 0.57735f),
        Vector3(0.0f, -0.707f, -0.707f)
    };
    Vector3 mRotatedLightDirections[3];

    DirectX::BoundingSphere mSceneBounds;

    GDescriptor imguiSrvDescriptors;
    bool imguiInitialized = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> windGradientPreviewTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> windGradientPreviewUpload;
    D3D12_GPU_DESCRIPTOR_HANDLE windGradientPreviewSrvGpu{};
    UINT windGradientPreviewSrvIndex = 1;
    UINT windGradientPreviewW = 128;
    UINT windGradientPreviewH = 128;
    UINT windGradientPreviewRowPitch = 0;
    bool windGradientPreviewReady = false;
    bool windGradientPreviewShowsGpuFluid_{false};
    bool windPreviewLiveGpuReadback_{true};

    Microsoft::WRL::ComPtr<ID3D12Resource> windFluidReadbackSecond_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT windFluidRbLayout_{};
    UINT64 windFluidRbTotalBytes_{0};
    UINT windFluidReadbackGrid_{0};
    UINT64 windFluidReadbackFenceValue_{0};
    bool windFluidReadbackQueued_{false};
    std::vector<UINT8> windFluidReadbackCpu_;
    uint32_t windFluidGpuPreviewFrameCounter_{};
    bool windFluidGpuPreviewCacheValid_{false};
    std::vector<unsigned char> windFluidGpuPreviewCache_;
    std::vector<float> windPreviewDye_;
    std::vector<float> windPreviewDyeTmp_;
    bool windPreviewDyeValid_{false};
    float windPreviewDyeExposure_{4.0f};
    int windPreviewMode_ = 0; // 0=abs velocity, 1=signed velocity, 2=dye
    DXGI_ADAPTER_DESC3 primeAdapterDesc{};
    DXGI_ADAPTER_DESC3 secondAdapterDesc{};
    bool primeAdapterDescValid = false;
    bool secondAdapterDescValid = false;

    bool imguiFontDescriptorInUse = false;
    std::string imguiIniFilePath;
    float grassCullMaxDistance = 18000.0f;
    float grassLod0Distance = 9000.0f;
    float grassLod1Distance = 12000.0f;
    int grassLod0BaseSegments = 4;
    int grassLod0BladeCount = 3;
    int grassLod1BladeCount = 1;
    float grassWindTessellationScale = 4.0f;
    float grassWindIntensity = 1.0f;
    float grassWindAmplitude = 1.0f;
    float grassLod0SdofNaturalFreq = 2.5f;
    float grassLod0SdofDampingRatio = 0.35f;
    float grassLod0BladeWidthScale = 1.0f;
    float grassLod0BladeHeightScale = 1.0f;
    float grassLod1BladeWidthScale = 1.0f;
    float grassLod1BladeHeightScale = 1.0f;
    /// 0 while LMB up; 1 with a valid ground hit under the cursor while LMB is held (outside ImGui).
    int grassWindOriginCount = 0;
    float grassWindCursorRadius = 900.0f;
    float grassWindCursorStrength = 3.0f;
    float grassWindBaseStrength = 0.35f;
    float grassWindBaseAngleDeg = 0.0f;
    float grassWindBaseCoverage = 1.2f;
    float grassWindMapFalloff = 1.5f;
    float grassFieldInfluenceScale = 2.0f;
    float grassLod0LeanGain = 5.0f;
    std::array<Vector4, 4> grassWindOrigins =
    {
        Vector4(0.0f, 0.0f, 0.0f, 900.0f),
        Vector4(0.0f, 0.0f, 0.0f, 900.0f),
        Vector4(0.0f, 0.0f, 0.0f, 900.0f),
        Vector4(0.0f, 0.0f, 0.0f, 900.0f)
    };
    std::array<Vector4, 4> grassWindDirections =
    {
        Vector4(0.0f, 0.0f, 0.0f, 1.0f),
        Vector4(0.0f, 0.0f, 0.0f, 1.0f),
        Vector4(0.0f, 0.0f, 0.0f, 1.0f),
        Vector4(1.0f, 0.0f, 0.0f, 1.0f)
    };
    bool showWindFieldDebug = false;
    bool debugNearestOriginTint = false;
    int windFieldGridResolution = 12;
    bool grassGpuWindFluid = true;
    float grassGpuWindFluidBlend = 1.0f;
    int grassGpuWindJacobiIterations = 20;
    int grassGpuWindGridResolution = 128;
    float grassGpuWindInjectStrength = 0.35f;
    float grassGpuWindDissipation = 0.972f;
    float grassGpuWindDt = 0.016f;
    float grassGpuWindVorticityEps = 0.0f;
    bool grassGpuWindWallEnable = true;
    float grassGpuWindWallPosU = 0.20f;
    float grassGpuWindWallPosV = 0.50f;
    float grassGpuWindWallAngleDeg = 90.0f;
    float grassGpuWindWallHalfLength = 0.10f;
    float grassGpuWindWallHalfWidth = 0.018f;
    float grassGpuWindWallDrag = 0.75f;
    float grassGpuWindWallWake = 0.55f;
    bool grassLod0DebugGradient = false;
    float grassLod0DebugGradMin = 0.0f;
    float grassLod0DebugGradMax = 80.0f;
    int grassLod0DebugGradAxis = 0;
    int grassBladeCount = 5000;
    float grassWorldSize = 2000.0f;
    float grassFieldScaleXZ = 15.0f;
    float grassFieldScaleY = 11.0f;
    std::shared_ptr<Transform> grassFieldTransform = nullptr;
    std::shared_ptr<Transform> platformTransform = nullptr;
    int pendingGrassBladeCount = -1;
    float pendingGrassWorldSize = -1.0f;
    bool fpsLimitEnabled = true;
    int fpsLimitTarget = 60;

    friend void HybridGrassApp_ImGuiSrvAllocFn(ImGui_ImplDX12_InitInfo* info,
                                               D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
                                               D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle);
    friend void HybridGrassApp_ImGuiSrvFreeFn(ImGui_ImplDX12_InitInfo* info,
                                              D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                                              D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle);
};
