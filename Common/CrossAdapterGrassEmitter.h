#pragma once
#include "WindFluidSimulator.h"
#include "Renderer.h"
#include "GCrossAdapterResource.h"
#include "GDescriptor.h"
#include "GResource.h"
#include "GrassEmitter.h"

class CrossAdapterGrassEmitter : public Renderer
{
public:
    CrossAdapterGrassEmitter(std::shared_ptr<GDevice> primeDevice,
        const std::shared_ptr<GDevice>& secondDevice,
        uint32_t grassCount = 10000,
        float worldSize = 100.0f,
        uint32_t lod0BladeCount = 3,
        uint32_t lod1BladeCount = 1);
    virtual ~CrossAdapterGrassEmitter();

    void Update() override;
    void Draw(const std::shared_ptr<GCommandList>& cmdList) override;
    void Dispatch(const std::shared_ptr<GCommandList>& cmdList);

    void SetWindStrength(float strength);
    void SetWindIntensity(float intensity);
    void SetWindAmplitude(float amplitude);
    void SetLodBladeSize(float lod0WidthScale, float lod0HeightScale, float lod1WidthScale, float lod1HeightScale);
    void SetLod0Sdof(float naturalFreq, float dampingRatio);
    void SetWindGradient(uint32_t originCount, float falloff,
                         const Vector4* originData, const Vector4* directionData);
    void SetFieldInfluenceScale(float scale);
    void SetLod0LeanGain(float gain);
    void SetGpuWindFluid(float enable, float blend, uint32_t jacobiIterations = 22u);
    void SetWindFluidSimulationTuning(float injectStrength, float dissipation, float dt,
                                      float vorticityEps, uint32_t gridResolution);
    // Wall obstacle in fluid UV space (0..1). angle is radians.
    // halfLengthNorm / halfWidthNorm are normalized by field half-extent.
    void SetWindFluidWall(bool enabled, float posU, float posV, float angleRad,
                          float halfLengthNorm, float halfWidthNorm,
                          float drag, float wakeStrength);
    /// LOD0 expand debug: linear |v| ramp min->max across field (bypasses fluid texture when enabled).
    void SetLod0DebugGradientWind(bool enabled, float minWorldSpeed, float maxWorldSpeed, float axis01);
    void SetDebugNearestOriginTint(bool enabled);
    void SetWindDirection(const Vector2& direction);
    void AdvanceTime(float deltaSeconds);
    void SetClickWindBoost(float boost);
    void SetWindFluidClickImpulse(float u, float v, float strength, float radiusSq);
    void SetWorldSize(float size);
    void SetGrassCount(uint32_t count);
    void SetLodBladeCounts(uint32_t lod0BladeCount, uint32_t lod1BladeCount);
    void Regenerate();

    void EnableShared();
    void DisableShared();
    /// Navier-Stokes wind textures / PSOs created successfully on the compute adapter.
    bool IsWindFluidGpuReady() const { return windFluid.IsInitialized(); }
    bool IsCrossAdapterSharedComputeActive() const { return useSharedCompute; }

    Vector4 GetWindFieldWorldParams() const { return emitterData.WindFieldWorldParams; }
    float GetEmitterWorldSize() const { return emitterData.WorldSize; }

    const PEPEngine::Graphics::WindFluidSimulator& GetWindFluid() const { return windFluid; }
    /// Stable post-sim velocity copy used by expand + debug readback (same device as wind sim).
    Microsoft::WRL::ComPtr<ID3D12Resource> GetExpandWindVelocityResource() const;
    void SetWorldConstantsBuffer(const GBuffer* worldConstants);
    void SetFrustumCullingData(const Matrix& viewProj, const Vector3& eyePos, float maxDistance = 1500.0f,
                               float lod0Distance = 300.0f, float lod1Distance = 900.0f,
                               uint32_t lod0BaseSegments = 4,
                               float windTessellationScale = 4.0f);


private:
    void InitPSO(const std::shared_ptr<GDevice>& otherDevice);
    void CreateBuffers();
    void DescriptorInitialize();
    void DescriptorInitializeExpandedDraw();
    /// Retries simulator init after ctor (e.g. if first attempt failed before shaders were reachable).
    void EnsureSharedComputeResourcesInitialized();
    void EnsureWindFluidGpuInitialized();
    void EnsureExpandWindVelocitySnapshot();
    void ApplyLod0DebugGradientToEmitterData();
    void GenerateGrassDataCPU();

    std::shared_ptr<GDevice> primeDevice;
    std::shared_ptr<GDevice> secondDevice;

    std::shared_ptr<GrassEmitter> primeGrassEmitter;

    std::shared_ptr<GBuffer> grassBuffer;
    std::shared_ptr<GCrossAdapterResource> crossAdapterGrassBuffer;
    std::shared_ptr<GBuffer> expandedVertexBuffer;
    std::shared_ptr<GCrossAdapterResource> crossAdapterExpandedVertexBuffer;
    std::shared_ptr<GBuffer> primeExpandedVertexBuffer;
    std::shared_ptr<GBuffer> visibleVertexCountBuffer;
    std::shared_ptr<GCrossAdapterResource> crossAdapterVisibleVertexCountBuffer;
    std::shared_ptr<GBuffer> primeVisibleVertexCountBuffer;
    std::shared_ptr<GBuffer> sharedGrassEmitterCb_;
    std::shared_ptr<GBuffer> sharedGrassCullCb_;

    GDescriptor computeDescriptors;
    GDescriptor expandDescriptors;
    GDescriptor expandedDrawDescriptors;

    std::shared_ptr<ComputePSO> generatePSO;
    std::shared_ptr<ComputePSO> expandPSO;
    std::shared_ptr<GRootSignature> computeRS;
    std::shared_ptr<GRootSignature> expandRS;
    std::shared_ptr<GRootSignature> drawRS;
    std::shared_ptr<GraphicPSO> expandedDrawPSO;

    PEPEngine::Graphics::WindFluidSimulator windFluid{};
    std::unique_ptr<GResource> expandWindVelSnapshot_;
    std::unique_ptr<GResource> expandWindVelFallback_;
    uint32_t expandWindVelGrid_{0};

    GrassEmitterData emitterData = {};
    GrassCullData cullData = {};
    std::vector<GrassData> grassDataCPU;

    bool useSharedCompute = false;
    bool sharedComputeResourcesInitialized_ = false;
    bool needRegenerate = true;

    enum Status : short
    {
        None = -1,
        Enable = 0,
        Disable = 1
    };

    Status dirtyActivated = None;

    uint32_t windFluidJacobiIterations_{22u};
    uint32_t windFluidGridResolution_{128u};
    float windFluidInjectStrength_{0.35f};
    float windFluidDissipation_{0.972f};
    float windFluidDt_{0.016f};
    float windFluidVorticityEps_{0.0f};
    Vector4 windFluidWallA_{0.0f, 0.50f, 0.50f, 0.0f};
    Vector4 windFluidWallB_{0.28f, 0.018f, 0.75f, 0.55f};
    float grassObstacleWakeLean_{110.0f};
    float windFluidClickU_{0.0f};
    float windFluidClickV_{0.0f};
    float windFluidClickStrength_{0.0f};
    float windFluidClickRadiusSq_{0.0f};

    bool windFluidInitGiveUp_{false};

    bool lod0DebugGradientEnable_{false};
    float lod0DebugGradientMin_{0.0f};
    float lod0DebugGradientMax_{80.0f};
    float lod0DebugGradientAxis_{0.0f};

    static constexpr uint32_t kMaxLod0Segments = 6;
    static constexpr uint32_t kMaxBladeCount = 4;
    static constexpr uint32_t kMaxVerticesPerBlade = kMaxBladeCount * kMaxLod0Segments * 6;
};