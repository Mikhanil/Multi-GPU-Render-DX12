#pragma once
#include "GDescriptor.h"
#include "Renderer.h"
#include "GrassData.h"

class GrassEmitter : public Renderer
{
public:
    GrassEmitter(std::shared_ptr<GDevice> device, uint32_t grassCount = 10000, float worldSize = 100.0f,
                 uint32_t lod0BladeCount = 3, uint32_t lod1BladeCount = 1);
    virtual ~GrassEmitter();

    // Renderer interface
    void Update() override;
    void Draw(const std::shared_ptr<GCommandList>& cmdList) override;
    void Dispatch(const std::shared_ptr<GCommandList>& cmdList); // ��� ��������� �� GPU

    // ����������
    void SetWindStrength(float strength) { emitterData.WindStrength = strength; }
    void SetWindIntensity(float intensity) { emitterData.WindIntensity = intensity; }
    void SetWindAmplitude(float amplitude) { emitterData.WindAmplitude = amplitude; }
    void SetGpuWindFluid(float enable, float blend)
    {
        emitterData.WindFluidEnable = enable;
        emitterData.WindFluidBlend = blend;
    }
    void SetLodBladeSize(float lod0WidthScale, float lod0HeightScale, float lod1WidthScale, float lod1HeightScale)
    {
        emitterData.Lod0BladeWidthScale = lod0WidthScale < 0.05f ? 0.05f : lod0WidthScale;
        emitterData.Lod0BladeHeightScale = lod0HeightScale < 0.05f ? 0.05f : lod0HeightScale;
        emitterData.Lod1BladeWidthScale = lod1WidthScale < 0.05f ? 0.05f : lod1WidthScale;
        emitterData.Lod1BladeHeightScale = lod1HeightScale < 0.05f ? 0.05f : lod1HeightScale;
    }
    void SetLod0Sdof(float naturalFreq, float dampingRatio)
    {
        emitterData.Lod0SdofNaturalFreq = naturalFreq < 0.05f ? 0.05f : naturalFreq;
        emitterData.Lod0SdofDampingRatio = dampingRatio < 0.01f ? 0.01f : dampingRatio;
    }
    void SetWindGradient(uint32_t originCount, float falloff,
                         const Vector4* originData, const Vector4* directionData);
    void SetFieldInfluenceScale(float scale)
    {
        emitterData.FieldInfluenceScale = scale < 0.0f ? 0.0f : scale;
    }
    void SetLod0LeanGain(float gain)
    {
        emitterData.Lod0LeanGain = gain < 0.1f ? 0.1f : gain;
    }
    void SetDebugNearestOriginTint(bool enabled)
    {
        emitterData.DebugNearestOriginTint = enabled ? 1.0f : 0.0f;
    }
    void SetWindDirection(const Vector2& direction);
    void SetWorldSize(float size) { emitterData.WorldSize = size; needRegenerate = true; }
    void SetGrassCount(uint32_t count);
    void SetLodBladeCounts(uint32_t lod0BladeCount, uint32_t lod1BladeCount);
    void Regenerate();

    std::shared_ptr<GBuffer> GetGrassBuffer() const { return grassBuffer; }
    void UpdateConstants(const GrassEmitterData& data) { emitterData = data; }
    void SetWorldConstantsBuffer(const GBuffer* worldConstants) { worldConstantsBuffer = worldConstants; }
    const GBuffer* GetWorldConstantsBuffer() const { return worldConstantsBuffer; }
    const GDescriptor* GetGrassDescriptors() const { return &grassDescriptors; }
    GTexture* GetGrassAtlasTexture() const { return Atlas.empty() ? nullptr : Atlas[0].get(); }
    const GBuffer* GetObjectPositionBuffer() const { return objectPositionBuffer.get(); }

private:
    void Initialize();
    void CreateBuffers();
    void CreateRootSignatures();
    void CreatePipelineState();
    void CreateComputeShaders();
    void DescriptorInitialize();
    void GenerateGrassDataCPU(); 

    std::shared_ptr<GDevice> device;

    // ������
    std::shared_ptr<ConstantUploadBuffer<ObjectConstants>> objectPositionBuffer;
    std::shared_ptr<GBuffer> grassBuffer;                 // RWStructuredBuffer<GrassData>
    std::shared_ptr<GBuffer> constantBuffer;              // ConstantBuffer<GrassEmitterData>
    const GBuffer* worldConstantsBuffer = nullptr;        // b1 WorldConstants for grass draw

    // �����������
    GDescriptor grassDescriptors;                          // ��� ������� (SRV)
    GDescriptor computeDescriptors;                        // ��� compute (UAV) - �����������

    // ������
    std::vector<GrassData> grassDataCPU;                   // CPU �����
    GrassEmitterData emitterData = {};
    ObjectConstants objectWorldData{};

    // ��������
    std::vector<std::shared_ptr<GTexture>> Atlas;         // �������� �����

    // ������� � PSO
    std::shared_ptr<GRootSignature> renderSignature;
    std::shared_ptr<GRootSignature> computeSignature;     // ��� ��������� �� GPU
    std::shared_ptr<GraphicPSO> renderPSO;
    std::shared_ptr<ComputePSO> generatePSO;              // ��� ��������� �� GPU
    std::shared_ptr<GShader> generateShader;              // Compute ������ ��� ���������

    // ���������
    bool needRegenerate = true;
    bool useGPUGeneration = false;                          // true ���� ���������� compute shader
};