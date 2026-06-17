#pragma once
#include "MathHelper.h"

using namespace DirectX::SimpleMath;

struct GrassData
{
    Vector3 Position;
    float Scale;
    float Rotation;
    float WindOffset;
    uint32_t TextureIndex;
    uint32_t Padding[3]; // ��� ������������ �� 16 ����
};

struct GrassRenderVertex
{
    Vector3 Position;
    float Padding0 = 0.0f;
    Vector2 TexCoord;
    Vector2 ExtraData = Vector2::Zero; // x = useTexture flag, y = blade height 0..1
    float WindStress01 = 0.0f;        // wind bend / field magnitude for PS darkening
    float ExtraPad0 = 0.0f;
};

// ��������� �������� �����
struct GrassEmitterData
{
    static constexpr uint32_t MaxWindOrigins = 4;
    // ������ ��, ��� ������� ������������
    uint32_t GrassCount;           // ���������� ��������
    uint32_t GridSize;             // ������ �����
    float WorldSize;                // ������ ����
    float QuadSize;                 // ������ �����
    float Time;                     // ����� ��� ��������
    float WindStrength;             // ���� �����
    float WindIntensity = 1.0f;
    float WindAmplitude = 1.0f;
    float Lod0BladeWidthScale = 1.0f;
    float Lod0BladeHeightScale = 1.0f;
    float Lod1BladeWidthScale = 1.0f;
    float Lod1BladeHeightScale = 1.0f;
    float Lod0SdofNaturalFreq = 2.5f;
    float Lod0SdofDampingRatio = 0.35f;
    float Lod0DistanceThreshold = 300.0f;
    float Lod1DistanceThreshold = 900.0f;
    uint32_t AtlasTextureCount;      // ���������� �������
    uint32_t GpuStressIterations = 0;
    uint32_t Lod0BladeCount = 3;
    uint32_t Lod1BladeCount = 1;
    Vector2 WindDirection = Vector2(1.0f, 0.0f);
    uint32_t WindOriginCount = 0;
    float WindMapFalloff = 1.5f;
    float FieldInfluenceScale = 1.0f;
    float DebugNearestOriginTint = 0.0f;
    float WindFluidEnable = 0.0f;   ///< 1: blend in GPU fluid wind (shared compute path); 0: analytic only
    float WindFluidBlend = 0.88f; ///< lerp analytic -> fluid when fluid enabled
    float WindFluidPad0 = 0.0f;
    float Lod0LeanGain = 3.0f; ///< Multi-GPU LOD0 field lean multiplier (ImGui: LOD0 lean gain)
    /** xz world center `.xy`, square half-extent `.z`, cell world size `.w` (for fluid->world scaling). */
    Vector4 WindFieldWorldParams =
        Vector4(0.0f, 0.0f, 500.0f, 1.0f);
    Vector4 WindOriginData[MaxWindOrigins] =
    {
        Vector4(0.0f, 0.0f, 0.0f, 1200.0f),
        Vector4(0.0f, 0.0f, 0.0f, 1200.0f),
        Vector4(0.0f, 0.0f, 0.0f, 1200.0f),
        Vector4(0.0f, 0.0f, 0.0f, 1200.0f)
    };
    Vector4 WindDirectionData[MaxWindOrigins] =
    {
        Vector4(1.0f, 0.0f, 0.0f, 1.0f),
        Vector4(1.0f, 0.0f, 0.0f, 1.0f),
        Vector4(1.0f, 0.0f, 0.0f, 1.0f),
        Vector4(1.0f, 0.0f, 0.0f, 1.0f)
    };
    /** Fluid obstacle in sim CB (expand reads A/B for wall wake metadata). */
    Vector4 WindFluidObstacleA = Vector4(0.0f, 0.50f, 0.50f, 0.0f);
    /** B.x/B.y/B.z: debug min/max/axis when B.w=1, else wall radius/wake/drag. B.w=1 enables LOD0 debug gradient. */
    Vector4 WindFluidObstacleB = Vector4(0.10f, 0.55f, 0.75f, 0.0f);
};

struct GrassCullData
{
    Matrix World = Matrix::Identity;
    Matrix ViewProj = Matrix::Identity;
    Vector3 EyePos = Vector3::Zero;
    float PadEye0 = 0.0f; // HLSL packs float3 EyePos to 16 bytes before MaxDistance
    float MaxDistance = 1500.0f;
    float Lod0Distance = 300.0f;
    float Lod1Distance = 900.0f;
    uint32_t Lod0BaseSegments = 4;
    float WindTessellationScale = 4.0f;
    float Padding0 = 0.0f;
};