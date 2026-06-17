#pragma once
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include "GDescriptor.h"
#include <cstdint>
#include <vector>

#include "Data.h"
#include "UIPictruresLoader.h"
#include "AssetsLoader.h"
#include "GTexture.h"
#include "MemoryAllocator.h"
#include "GCrossAdapterResource.h"
#include "GCrossAdapterResource.h"
#include "Window.h"
using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace DirectX;
using namespace Common;

class UILayer
{
    struct GradientNoiseCB
    {
        XMFLOAT4 ColorStart = { 1.0f, 0.0f, 0.0f, 1.0f }; // Красный
        XMFLOAT4 ColorEnd = { 0.0f, 0.0f, 1.0f, 1.0f };   // Синий
        XMFLOAT2 TextureSize;
        float Time;
        float ColorShiftSpeed;
        float NoiseIntensity;
        float Smoothness;

    };

    struct HPBarCB
    {
        XMFLOAT2 TextureSize;
        float Time;
        float Health;
        XMFLOAT4 ColorStart;
        XMFLOAT4 ColorEnd;
        float SparkleSpeed;
        float SparkleIntensity;
        XMFLOAT2 StarPosMin;
        XMFLOAT2 StarPosMax;

        float DistortionPower;
        float GlowIntensity;
        float FractalScale;
        float FillThreshold;

        float CrackRoughness; // Шероховатость трещин (0.1–0.5)
        float CrackThickness; //
    };

    //HP Bar
    GTexture hbBarTex;
    GDescriptor hbBarSRV;
    GDescriptor hbBarUAV;
    GShader hbBarShader;
    ComputePSO hbBarPso;

    const DXGI_FORMAT rtvFormat = GetSRGBFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
    GDescriptor srvUavMemory;
    static constexpr uint32_t ImGuiSrvDescriptorCount = 64;

    GDescriptor srvMemory;
    std::vector<bool> imguiSrvDescriptorUsage;

    std::shared_ptr<GTexture> gradientTexture;

    GDescriptor skillPicturesDescriptors[SKILL_PICTURES_COUNT];
    GDescriptor mapPicture;
    GDescriptor uiNavigationPicturesDescriptors[80];
    GDescriptor uiSRVs[80];
    GDescriptor uiInputTexSRVs[80];
    GDescriptor uiUAVs[80];
    GTexture uiTexs[80];
    GTexture uiInputTexs[80];
    ComputePSO uiPSOs[80];
    GShader uiShaders[80];
    GShader gradientShader;

    GShader blurShader;
    ComputePSO blurPSO;

    // Текстуры и дескрипторы для теста блюра (например, для одной иконки)
    GTexture baseMapTex;
    GTexture blurredIconTex;
    GDescriptor blurredIconUAV;
    GDescriptor blurredIconSRV;

    GTexture blurredIconTex2;
    GDescriptor blurredIconUAV2;
    GDescriptor blurredIconSRV2;

    UINT groupCountWidth{};
    UINT groupCountHeight{};

    std::shared_ptr<AssetsLoader> assets;

    HWND hwnd;
    std::shared_ptr<GDevice> device;

    std::shared_ptr<ComputePSO> gradientStarPSO; // Compute PSO for the shader
    std::shared_ptr<GResource> constantBuffer; // Constant buffer for shader
    GDescriptor constantBufferDesc;
    // CBV descriptor
    struct GradientStarConstants {
        XMFLOAT4 ColorStart;
        XMFLOAT4 ColorEnd;
        float NoiseScale;
        float NoiseStrength;
        XMFLOAT2 TextureSize;
        float Time;           // Новое поле
        float AnimationSpeed; // Новое поле
    };

    struct BlurSettings {
        uint32_t width;
        uint32_t height;
        uint32_t blurRadius;
        uint32_t padding;
    };
    // GradientStarConstants shaderConstants;

    std::shared_ptr<UIPictruresLoader> loader;

    void SetupRenderBackends();
    void Initialize();
    void SetStyle();
    void DrawGlowingPicture() const;

    friend void UILayer_ImGuiSrvAllocFn(ImGui_ImplDX12_InitInfo* info,
                                        D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
                                        D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle);
    friend void UILayer_ImGuiSrvFreeFn(ImGui_ImplDX12_InitInfo* info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle);

    int pictureSizeX = 128;
    int pictureSizeY = 128;

    int mapPicSizeX = 256;
    int mapPicSizeY = 256;

    ID3D12Resource* my_textures[200];
    ID3D12Resource* map;
    ID3D12Resource* uiInputTextures[200];
    ID3D12Resource* bBackText;
    ID3D12RootSignature* rootSignature;

    std::map<std::string, ImVec4> elementsPoses;

    void InitializeBlurResources();
    void InitCircleBar(float value, float radius, const ImVec2& centrePos, const ImVec4& fillColor, const ImVec4& emptyColor, float thickness);

    void DrawTime(uint16_t mins, uint16_t hours);

    void DrawExpLine(float_t val);

    void DrawBlurShit();
    void DrawLeftBar();
    void DrawLeftBottomPanel();
    void DrawRightBar();
    void DrawRightTopBar();
    void DrawRightMiddlePanel();
    void DrawBottomPanel();
    void AddColorToButton(float glow);
    void BlurIcon(ImVec2 iconPos, ImVec2 size, const std::shared_ptr<GCommandList>& cmdLis);
    void ApplyGradientAndStarsEffect(
        const ImVec2& size,
        const ImVec4& gradientColor1 = ImVec4(0.0f, 0.0f, 1.0f, 0.3f),
        const ImVec4& gradientColor2 = ImVec4(1.0f, 0.0f, 0.0f, 0.3f),
        float glowSpeed = 0.05f,
        int starCount = 25,
        float starSize = 0.6f,
        const ImVec4& starColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
    );

    void Update(const std::shared_ptr<GCommandList>& cmdList);
    //   void CreateGradientResources();

    float expBarValue = 0.0f;
    float exbBarModifier = 1.0f;
    ImVec2 displaySize;
    ImVec2 blurPos;
    ImVec2 blurSize;
    uint16_t blursToPush = 1;
    float paddingX = 10.0f;
    float paddingY = 10.0f;
    GShader* starEffectShader;
    void InitializeGradientResources();
    void InitializeHPBar();
    void UpdateHBBar(const std::shared_ptr<GCommandList>& cmdList, float value);
    GTexture gradientTex;

    float buttonSize = 40.0f;
    float iconSize = 40.0f;
    int intIconSize = 35;
    float barHeight = 60.0f;
    float sizeOffset = 50.0f;
    float bottomPosOffset = -20.0f;
public:
    UILayer(const std::shared_ptr<GDevice>& device, HWND hwnd);

    ~UILayer();
    void RenderEffects(const std::shared_ptr<GCommandQueue>& queue) noexcept;
    void SetTexture();
    void CreateDeviceObject();
    void Invalidate();
    void SetFPS(float FPS);

    void Render(const std::shared_ptr<GCommandList>& cmdList);

    void ChangeDevice(const std::shared_ptr<GDevice>& device);

    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
