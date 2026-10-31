#pragma once
#include "AssetsLoader.h"
#include "CrossAdapterParticleEmitter.h"
#include "d3dApp.h"
#include "SharedFluidParticleEmitter.h"
#include "Renderer.h"
#include "RenderModeFactory.h"
#include "ShadowMap.h"
#include "SSAA.h"
#include "SSAO.h"
#include "FrameResource.h"
#include "GCrossAdapterResource.h"
#include "GDeviceFactory.h"
#include "Light.h"
#include <cstdint>

class GameObject;

class HybridParticleApp :
    public Common::D3DApp
{
public:
    HybridParticleApp(HINSTANCE hInstance);
    ~HybridParticleApp() override;

    bool Initialize() override;;

    int Run() override;

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
    void UpdateMaterials();
    void UpdateShadowTransform(const GameTimer& gt);
    void UpdateShadowPassCB(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateSsaoCB(const GameTimer& gt);
    bool InitMainWindow() override;
    void OnResize() override;
    void Flush() override;
    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

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

    custom_unordered_map<std::wstring, std::shared_ptr<GModel>> models = MemoryAllocator::CreateUnorderedMap<
        std::wstring, std::shared_ptr<GModel>>();
    std::shared_ptr<GRootSignature> primeDeviceSignature;
    std::shared_ptr<GRootSignature> ssaoPrimeRootSignature;
    std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout{};
    GDescriptor srvTexturesMemory;
    RenderModeFactory defaultPrimePipelineResources;


    bool IsStop = false;

    const int StatisticStepSecondsCount = 30;
    const float CalibrationSampleSeconds = 5.0f;


    std::shared_ptr<ShadowMap> shadowPath;
    std::shared_ptr<SSAO> ambientPrimePath;
    std::shared_ptr<SSAA> antiAliasingPrimePath;

    custom_vector<std::shared_ptr<GameObject>> gameObjects = MemoryAllocator::CreateVector<std::shared_ptr<
        GameObject>>();

    custom_vector<custom_vector<std::shared_ptr<Renderer>>> typedRenderer = MemoryAllocator::CreateVector<custom_vector<
        std::shared_ptr<Renderer>>>();

    bool UseCrossAdapter = false;
    bool UseCrossSync = false;
    custom_vector<CrossAdapterParticleEmitter*> crossEmitter = MemoryAllocator::CreateVector<CrossAdapterParticleEmitter
        *>();


    ComPtr<ID3D12Fence> primeComputeFence;
    ComPtr<ID3D12Fence> secondComputeFence;
    UINT64 sharedComputeFenceValue = 0;

    ComPtr<ID3D12Fence> primeRenderFence;
    ComPtr<ID3D12Fence> secondRenderFence;
    UINT64 sharedRenderFenceValue = 0;

    PassConstants mainPassCB;
    PassConstants shadowPassCB;

    custom_vector<std::shared_ptr<FrameResource>> frameResources = MemoryAllocator::CreateVector<std::shared_ptr<
        FrameResource>>();
    std::shared_ptr<FrameResource> currentFrameResource = nullptr;
    std::atomic<UINT> currentFrameResourceIndex = 0;

    custom_vector<Light*> lights = MemoryAllocator::CreateVector<Light*>();

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

public:
    void SwitchDevice();
private:
    struct RenderPathTuningPreset
    {
        UINT ShadowMapSize = 512;
        UINT SsaaMultiplier = 1;
        UINT SsaoDivisor = 4;
    };

    enum class StartupCalibrationStage : uint8_t
    {
        TuneFluidParticles,
        TuneMainGpuRenderPath,
        Completed
    };

    enum class RenderPathCalibrationAxis : uint8_t
    {
        ShadowMap,
        Ssaa,
        Ssao,
        Completed
    };

    void ApplyRenderPathTuningPreset(const RenderPathTuningPreset& preset);
    void ResetPerformanceSamplingWindow();
    Vector3 CalculateFluidBoundsScale(uint32_t targetParticleCount) const;
    Vector3 CalculateFluidSpawnRegionSize(uint32_t spawnedParticleCount, const Vector3& boundsScale) const;
    void RecreateFluidEmitter(uint32_t targetParticleCount);
    bool HandleStartupCalibration(float fps);

    bool m_bUseSharedFluidSim = false;
    std::shared_ptr<SharedFluidParticleEmitter> fluidParticleEmitter;
    std::shared_ptr<GameObject> fluidSimulationObject;

    RenderPathTuningPreset currentRenderPathPreset{1024, 2, 1};
    StartupCalibrationStage startupCalibrationStage = StartupCalibrationStage::TuneFluidParticles;
    Vector3 fluidSimulationPosition = Vector3::Up * 5.f;
    Vector3 fluidSimulationScale = Vector3(10.f, 10.f, 10.f);
    ParticleSpawner::SpawnRegion fluidSpawnRegion{};
    FluidSimulationData fluidSimulationTemplateData{};
    uint32_t fluidCalibrationTargetParticleCount = 50000;
    uint32_t fluidCalibrationActualParticleCount = 0;
    uint32_t fluidCalibrationIterations = 0;
    double secondaryGpuCalibrationTimeMicroseconds = 0.0;
    uint32_t secondaryGpuCalibrationSampleCount = 0;
    RenderPathCalibrationAxis renderPathCalibrationAxis = RenderPathCalibrationAxis::ShadowMap;
    bool renderPathCalibrationLastStepWasIncrease = false;
    float secondaryDispatchAccumulatedDt = 0.0f;
};
