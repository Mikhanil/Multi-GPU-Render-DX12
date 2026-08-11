#include "UILayer.h"

#include "ComputePSO.h"
#include <utility>
#include "Window.h"
#include "GResourceStateTracker.h"
#include "dxgiformat.h"
#include <iostream>
#include <clocale>
#include <random>
#include "GCrossAdapterResource.h"
#include "GCrossAdapterResource.h"
#include "d2d1effects.h"
#include "GRootSignature.h"

#include "pch.h"
#include "RenderModeFactory.h"
#include "DirectXTex.h"
#include "GRootSignature.h"
#include "GCommandList.h"
#include "GCommandQueue.h"
#include "GDescriptorHeap.h"
#include "d2d1_1.h"
#include <string>
#include "UIPictruresLoader.h"
#include "imgui_internal.h"
#include "d3dcompiler.h"
#include <chrono>
#include <fstream>
#include "Data.h"

uint16_t skillCounter = 0;
static int mod = 1;
static bool isShared = false;
static float currentTime = 0.0f;
static float borderSize = 1.0f;
static int uiDescriptorCounter = 0;
static double timee = 0.016f;
static double deltatime = 0;
static std::vector<ImVec4> baseColors;
static std::vector<ImVec4> endColors;
static float totalTime = 0;
static int currentColorIndex = 0;
static int maxColors = 100;
static float glow_phase = 0.0f;
static bool initialized = false;
static float glow_speed = 0.05f;
static std::wstring currentDeviceName;
static int postDeviceChangeFpsCounter = 0;
static int fpsToInitBlur = 10;
static float gradAnimSpeed = 100.6f;
static auto currentDeltaTime = std::chrono::high_resolution_clock::now();
static auto lastDeltaTime = currentDeltaTime;
std::vector<float> glows = std::vector<float>(20);

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    ImTextureRef TextureRefFromDescriptor(const GDescriptor& descriptor)
    {
        return ImTextureRef(static_cast<ImTextureID>(descriptor.GetGPUHandle().ptr));
    }

    void ImageFromDescriptor(const GDescriptor& descriptor, const ImVec2& size,
                             const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1))
    {
        ImGui::Image(TextureRefFromDescriptor(descriptor), size, uv0, uv1);
    }

    bool ImageButtonFromDescriptor(const char* id, const GDescriptor& descriptor, const ImVec2& size,
                                   const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        const bool pressed = ImGui::ImageButton(id, TextureRefFromDescriptor(descriptor), size, uv0, uv1);
        ImGui::PopStyleVar();
        return pressed;
    }
}

void UILayer_ImGuiSrvAllocFn(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
                             D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
{
    auto* layer = static_cast<UILayer*>(info->UserData);
    IM_ASSERT(layer != nullptr);

    const auto descriptorCount = static_cast<uint32_t>(layer->imguiSrvDescriptorUsage.size());
    for (uint32_t index = 0; index < descriptorCount; ++index)
    {
        if (layer->imguiSrvDescriptorUsage[index])
            continue;

        layer->imguiSrvDescriptorUsage[index] = true;
        *out_cpu_handle = layer->srvMemory.GetCPUHandle(index);
        *out_gpu_handle = layer->srvMemory.GetGPUHandle(index);
        return;
    }

    IM_ASSERT(false && "ImGui SRV descriptor heap exhausted");
    *out_cpu_handle = {};
    *out_gpu_handle = {};
}

void UILayer_ImGuiSrvFreeFn(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                            D3D12_GPU_DESCRIPTOR_HANDLE)
{
    auto* layer = static_cast<UILayer*>(info->UserData);
    IM_ASSERT(layer != nullptr);

    const auto base = layer->srvMemory.GetCPUHandle(0).ptr;
    const auto descriptorSize = static_cast<SIZE_T>(
        layer->srvMemory.GetDescriptorHeap()->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
    IM_ASSERT(descriptorSize > 0);
    IM_ASSERT(cpu_handle.ptr >= base);

    const auto offset = cpu_handle.ptr - base;
    IM_ASSERT(offset % descriptorSize == 0);
    const auto index = static_cast<size_t>(offset / descriptorSize);
    IM_ASSERT(index < layer->imguiSrvDescriptorUsage.size() && layer->imguiSrvDescriptorUsage[index]);

    layer->imguiSrvDescriptorUsage[index] = false;
}

const char* name = "ddd";


LRESULT UILayer::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    return 0;
}

void UILayer::SetupRenderBackends()
{
    srvMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ImGuiSrvDescriptorCount);
    imguiSrvDescriptorUsage.assign(ImGuiSrvDescriptorCount, false);

    if (!loader)
        loader = std::make_shared<UIPictruresLoader>();

    displaySize = ImGui::GetIO().DisplaySize;
    ImGui_ImplWin32_Init(this->hwnd);

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = device->GetDXDevice().Get();
    initInfo.CommandQueue = device->GetCommandQueue(GQueueType::Graphics)->GetD3D12CommandQueue().Get();
    initInfo.NumFramesInFlight = globalCountFrameResources;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.UserData = this;
    initInfo.SrvDescriptorHeap = srvMemory.GetDescriptorHeap()->GetDirectxHeap();
    initInfo.SrvDescriptorAllocFn = UILayer_ImGuiSrvAllocFn;
    initInfo.SrvDescriptorFreeFn = UILayer_ImGuiSrvFreeFn;
    ImGui_ImplDX12_Init(&initInfo);
}

void UILayer::Initialize()
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    loader = std::make_shared<UIPictruresLoader>();
    SetTexture();
    SetStyle();
    assets = std::make_shared<AssetsLoader>(device);
    SetupRenderBackends();
}

void UILayer::SetStyle()
{
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> color(0, 255);

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.3f;
    style.FrameRounding = 2.3f;
    style.ScrollbarRounding = 0;
    style.FrameBorderSize = borderSize;
    style.WindowBorderSize = 4.0f;

    ImGui::StyleColorsDark();
    style.WindowRounding = 5.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.4f; // Полупрозрачный фон
    style.Colors[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f); // Синяя граница

    for (uint16_t i = 0; i < maxColors - 1; i++)
    {
        float r = static_cast<float>(color(rng)) / 255.0f;
        float g = static_cast<float>(color(rng)) / 255.0f;
        float b = static_cast<float>(color(rng)) / 255.0f;
        baseColors.push_back(ImVec4(r, g, b, 0.0f));

        r = static_cast<float>(color(rng)) / 255.0f;
        g = static_cast<float>(color(rng)) / 255.0f;
        b = static_cast<float>(color(rng)) / 255.0f;
        endColors.push_back(ImVec4(r, g, b, 0.5f));
    }

    ImGuiIO& io = ImGui::GetIO();
    //  io.DisplaySize = ImVec2(7680, 4320); // 8K разрешение
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

void UILayer::DrawGlowingPicture() const
{
}

void UILayer::CreateDeviceObject()
{
    ImGui_ImplDX12_CreateDeviceObjects();
}

void UILayer::Invalidate()
{
    ImGui_ImplDX12_InvalidateDeviceObjects();
}

void UILayer::SetFPS(float FPS)
{

}

void UILayer::InitializeBlurResources()
{
    GRootSignature blurSignature;

    // Параметр 0: Константы (BlurSettings - 4 штуки uint32_t)
    blurSignature.AddConstantParameter(sizeof(BlurSettings) / sizeof(uint32_t), 0);

    // Параметр 1: SRV (Текстура для чтения - t0)
    CD3DX12_DESCRIPTOR_RANGE srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    blurSignature.AddDescriptorParameter(&srvRange, 1);

    // Параметр 2: UAV (Текстура для записи - u0)
    CD3DX12_DESCRIPTOR_RANGE uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    blurSignature.AddDescriptorParameter(&uavRange, 1);

    blurSignature.Initialize(device);

    // Загружаем наш новый объединенный шейдер
    blurShader = GShader(L"Shaders\\BlurFX.hlsl", ComputeShader, nullptr, "CSMain", "cs_5_1");
    blurShader.LoadAndCompile();

    // Создаем PSO
    blurPSO = ComputePSO();
    blurPSO.SetRootSignature(blurSignature);
    blurPSO.SetShader(&blurShader);
    blurPSO.Initialize(device);

    D3D12_RESOURCE_DESC texDesc;
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = mapPicSizeX; // Замените на pictureSizeX
    texDesc.Height = mapPicSizeY; // Замените на pictureSizeY
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; // Важно для UAV!

    blurredIconTex = GTexture(device, texDesc, L"Blurred Icon Tex");

    // Выделяем дескрипторы (у вас это делается через AllocateDescriptors)
    blurredIconUAV = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    blurredIconSRV = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    // Создаем UAV (для записи из шейдера)
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    blurredIconTex.CreateUnorderedAccessView(&uavDesc, &blurredIconUAV);

    // Создаем SRV (для чтения в ImGui)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.Texture2D.MipLevels = 1;
    blurredIconTex.CreateShaderResourceView(&srvDesc, &blurredIconSRV);

    blurredIconTex2 = GTexture(device, texDesc, L"Blurred Icon Tex 2");

    blurredIconUAV2 = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    blurredIconSRV2 = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    blurredIconTex2.CreateUnorderedAccessView(&uavDesc, &blurredIconUAV2);
    blurredIconTex2.CreateShaderResourceView(&srvDesc, &blurredIconSRV2);
}

void UILayer::InitCircleBar(float value, float radius = 40.0f, const ImVec2& centrePos = ImVec2(0, 0), const ImVec4& fillColor = ImVec4(0, 0, 1, 1), const ImVec4& emptyColor = ImVec4(0, 0, 0, 0.5f), float thickness = 6.0f)
{
}

void UILayer::DrawTime(uint16_t mins, uint16_t hours)
{
    std::string strMins = std::to_string(mins);
    const char* cStrMinsValue = strMins.c_str();

    std::string strHrs = std::to_string(hours);
    const char* cStrHrsValue = strHrs.c_str();

    const auto len = strlen(cStrMinsValue) + strlen(cStrHrsValue) + 2;

    char* finalStr = new char[len];

    snprintf(finalStr, len, "%s:%s", cStrMinsValue, cStrHrsValue);

    ImGui::Text(finalStr);

    delete[] finalStr;
}

void UILayer::DrawExpLine(float_t val)
{

}

void UILayer::DrawBlurShit()
{

}

void UILayer::DrawLeftBar()
{
    ImGui::SetNextWindowPos(ImVec2(paddingX, paddingY));
    ImGui::SetNextWindowSize(ImVec2(500.0f, 250.0f));
    ImGui::Begin("LeftTopPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    // 1.1 Картинка с иконкой персонажа

    ImageButtonFromDescriptor("character_icon", skillPicturesDescriptors[36], ImVec2(buttonSize, buttonSize)); // Замени на реальную текстуру
    //ApplyGradientAndStarsEffect(ImVec2(iconSize, iconSize), baseColors[currentColorIndex], endColors[currentColorIndex]);
    currentColorIndex++;
    ImGui::SameLine();
    // 1.2 Бар ХП
    ImGui::ProgressBar(400, ImVec2(140.0f, 60), "HP");
    // 1.5 Никнейм
    ImGui::Text("%s", L"Zakgar");
    // 1.3 Бар Маны
    ImGui::ProgressBar(400, ImVec2(140.0f, 60), "Mana");
    // 1.4 Уровень
    ImGui::Text("Уровень: %d", 52);
    // 1.6 Список состояний
    for (uint16_t i = 0; i < 5; i++) {
        ImGui::PushID(static_cast<int>(i));
        ImageButtonFromDescriptor("status_icon", skillPicturesDescriptors[36], ImVec2(buttonSize, buttonSize)); // Замени на иконки
        ApplyGradientAndStarsEffect(ImVec2(iconSize, iconSize), baseColors[currentColorIndex], endColors[currentColorIndex]);
        currentColorIndex++;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L"Goida");
        ImGui::SameLine();
        ImGui::PopID();
    }
    ImGui::End();
}

void UILayer::DrawLeftBottomPanel()
{
    ImGui::SetNextWindowPos(ImVec2(paddingX, displaySize.y - paddingY - buttonSize + bottomPosOffset));
    ImGui::SetNextWindowSize(ImVec2(buttonSize * 10 + 100, buttonSize + 20));
    ImGui::Begin("HotbarLeft", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    for (size_t i = 0; i < 10; ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImageButtonFromDescriptor("hotbar_left_icon", uiNavigationPicturesDescriptors[0], ImVec2(buttonSize, buttonSize)); // Замени на иконки
        ApplyGradientAndStarsEffect(ImVec2(iconSize, iconSize), baseColors[currentColorIndex], endColors[currentColorIndex]);
        currentColorIndex++;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Слот %zu", i + 1);
        if (i < 9) ImGui::SameLine();
        ImGui::PopID();
    }
    ImGui::End();
}

void UILayer::DrawRightBar()
{
    ImGui::SetNextWindowPos(ImVec2(displaySize.x - paddingX - iconSize * 10 - 100, displaySize.y - paddingY - iconSize + bottomPosOffset));
    ImGui::SetNextWindowSize(ImVec2(iconSize * 10 + 100, iconSize + 20));
    ImGui::Begin("HotbarRight", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    for (size_t i = 0; i < 10; ++i) {
        ImGui::PushID(static_cast<int>(i + 22));
        ImageButtonFromDescriptor("hotbar_right_icon", uiNavigationPicturesDescriptors[0], ImVec2(iconSize, iconSize)); // Замени на иконки
        ApplyGradientAndStarsEffect(ImVec2(iconSize, iconSize), baseColors[currentColorIndex], endColors[currentColorIndex]);
        currentColorIndex++;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Слот %zu", i + 1);
        if (i < 9) ImGui::SameLine();
        ImGui::PopID();
    }
    ImGui::End();
}

void UILayer::DrawRightTopBar()
{
    const ImVec2 panelSize(300.0f, 450.0f);
    float panelX = displaySize.x - paddingX - panelSize.x;
    if (panelX < paddingX)
        panelX = paddingX;

    ImGui::SetNextWindowPos(ImVec2(panelX, paddingY));
    ImGui::SetNextWindowSize(panelSize);
    ImGui::Begin("RightTopPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // 5.1 Миникарта
    ImageButtonFromDescriptor("minimap", blurredIconSRV, ImVec2(256.0f, 256.0f)); // Замени на текстуру миникарты
  //  ApplyGradientAndStarsEffect(ImVec2(iconSize, iconSize), baseColors[currentColorIndex], endColors[currentColorIndex]);
   // currentColorIndex++;
    // 5.2 Текущее время
    ImGui::Text("Время: %s", L"22:34");
    ImGui::End();
}

void UILayer::DrawRightMiddlePanel()
{
    ImGui::SetNextWindowPos(ImVec2(displaySize.x - iconSize - 100, displaySize.y - iconSize * 15));
    ImGui::SetNextWindowSize(ImVec2((iconSize + 10) * 2, iconSize * 10 + 100));

    ImGui::Begin("HotbarRightMiddle", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    for (size_t i = 0; i < 20; ++i) {
        ImGui::PushID(static_cast<int>(i + 22));
        ImageButtonFromDescriptor("hotbar_right_middle_icon", uiNavigationPicturesDescriptors[0], ImVec2(iconSize, iconSize)); // Замени на иконки
        ApplyGradientAndStarsEffect(ImVec2(iconSize, iconSize), baseColors[currentColorIndex], endColors[currentColorIndex]);
        currentColorIndex++;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slot %zu", i + 1);
        if (i % 2 == 0) ImGui::SameLine();
        ImGui::PopID();
    }

    ImGui::End();
}

void UILayer::DrawBottomPanel()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float hpBarWidth = 750.0f;
    const float hpBarHeight = 100.0f;
    const float rowWidth = 15.0f * iconSize + 14.0f * style.ItemSpacing.x;
    float contentWidth = hpBarWidth > rowWidth ? hpBarWidth : rowWidth;
    float panelWidth = contentWidth + style.WindowPadding.x * 2.0f;
    const float maxPanelWidth = displaySize.x - paddingX * 2.0f;
    if (panelWidth > maxPanelWidth)
        panelWidth = maxPanelWidth;

    const float panelHeight = 2.0f * iconSize + hpBarHeight + style.ItemSpacing.y * 3.0f + style.WindowPadding.y * 2.0f;
    float panelX = (displaySize.x - panelWidth) * 0.5f;
    if (panelX < paddingX)
        panelX = paddingX;
    float panelY = displaySize.y - paddingY - panelHeight + bottomPosOffset;
    if (panelY < paddingY)
        panelY = paddingY;

    ImGui::SetNextWindowPos(ImVec2(panelX, panelY));
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight));
    ImGui::Begin("CenterPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // 3.2 Полоска опыта

    // 3.3 Ряд иконок-способностей (верхний)
    for (size_t i = 0; i < 15; ++i) {
        ImGui::PushID(static_cast<int>(i + 12));
        ImGui::BeginGroup();
        {
            // Фоновое изображение (под кнопкой)
            ImageFromDescriptor(skillPicturesDescriptors[skillCounter],
                ImVec2(iconSize, iconSize),
                ImVec2(0, 0));

            // Кнопка поверх изображения (с отступом или без)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - iconSize - 4.0f); // Смещаем назад на высоту изображения
            ImageButtonFromDescriptor("top_skill_overlay", uiSRVs[skillCounter],
                ImVec2(iconSize, iconSize));
        }
        ImGui::EndGroup();
        //ApplyGradientAndStarsEffect(ImVec2(iconSize, iconSize), baseColors[currentColorIndex], endColors[currentColorIndex]);
        currentColorIndex++;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Способность %zu", i + 1);
        if (i < 14) ImGui::SameLine();
        ImGui::PopID();

        skillCounter++;
    }

    ImageFromDescriptor(hbBarSRV, ImVec2(hpBarWidth, hpBarHeight), ImVec2(0, 0), ImVec2(1, 1));
    //ApplyGradientAndStarsEffect(ImVec2(750, 200), baseColors[currentColorIndex], endColors[currentColorIndex], 0.05f, 100, 1);

    // 3.1 Ряд иконок-способностей (нижний)
    for (size_t i = 0; i < 15; ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginGroup();
        {
            // Фоновое изображение (под кнопкой)
            ImageFromDescriptor(skillPicturesDescriptors[skillCounter],
                ImVec2(iconSize, iconSize),
                ImVec2(0, 0),
                ImVec2(1, 1));

            // Кнопка поверх изображения (с отступом или без)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - iconSize - 4.0f); // Смещаем назад на высоту изображения
            ImageButtonFromDescriptor("bottom_skill_overlay", uiSRVs[skillCounter],
                ImVec2(iconSize, iconSize));
        }
        ImGui::EndGroup();
        //ApplyGradientAndStarsEffect(ImVec2(iconSize, iconSize), baseColors[currentColorIndex], endColors[currentColorIndex]);
        currentColorIndex++;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Способность %zu", i + 1);
        if (i < 14) ImGui::SameLine();
        ImGui::PopID();

        skillCounter++;
    }
    ImGui::End();
}

void UILayer::AddColorToButton(float glow)
{

}

void UILayer::BlurIcon(ImVec2 iconPos, ImVec2 size, const std::shared_ptr<GCommandList>& cmdList)
{
    int passes = 1;
    if (!map || size.x <= 0 || size.y <= 0 || passes <= 0) return;

    BlurSettings settings = {};
    settings.width = static_cast<uint32_t>(size.x);
    settings.height = static_cast<uint32_t>(size.y);
    settings.blurRadius = 5;

    uint32_t dispatchX = (settings.width + 7) / 8;
    uint32_t dispatchY = (settings.height + 7) / 8;

    cmdList->SetDescriptorsHeap(&mapPicture);
    cmdList->SetPipelineState(blurPSO);
    cmdList->SetRoot32BitConstants(0, sizeof(BlurSettings) / sizeof(uint32_t), &settings, 0);

    // Умный выбор: если проходов четное количество (2, 4), начинаем писать во вторую текстуру,
    // чтобы последний (финальный) проход ВСЕГДА попадал в оригинальную blurredIconTex.
    bool writeToTex1 = (passes % 2 != 0);

    // === ПЕРВЫЙ ПРОХОД (Всегда читаем из оригинальной map) ===
    ID3D12Resource* firstWriteTex = writeToTex1 ? blurredIconTex.GetD3D12Resource().Get() : blurredIconTex2.GetD3D12Resource().Get();
    GDescriptor* firstWriteUAV = writeToTex1 ? &blurredIconUAV : &blurredIconUAV2;

    cmdList->TransitionBarrier(firstWriteTex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    cmdList->SetRootDescriptorTable(1, &mapPicture);
    cmdList->SetRootDescriptorTable(2, firstWriteUAV);
    cmdList->Dispatch(dispatchX, dispatchY, 1);

    // === ПОСЛЕДУЮЩИЕ ПРОХОДЫ (Пинг-Понг) ===
    for (int i = 1; i < passes; ++i)
    {
        writeToTex1 = !writeToTex1; // Переключаем буфер

        ID3D12Resource* readTex = writeToTex1 ? blurredIconTex2.GetD3D12Resource().Get() : blurredIconTex.GetD3D12Resource().Get();
        ID3D12Resource* writeTex = writeToTex1 ? blurredIconTex.GetD3D12Resource().Get() : blurredIconTex2.GetD3D12Resource().Get();

        GDescriptor* readSRV = writeToTex1 ? &blurredIconSRV2 : &blurredIconSRV;
        GDescriptor* writeUAV = writeToTex1 ? &blurredIconUAV : &blurredIconUAV2;

        // Ставим барьеры для текущей итерации
        cmdList->TransitionBarrier(readTex, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(writeTex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->FlushResourceBarriers();

        cmdList->SetRootDescriptorTable(1, readSRV);
        cmdList->SetRootDescriptorTable(2, writeUAV);
        cmdList->Dispatch(dispatchX, dispatchY, 1);
    }

    // === ФИНАЛИЗАЦИЯ ===
    // Возвращаем обе текстуры в базовое состояние для отрисовки интерфейсом
    cmdList->TransitionBarrier(blurredIconTex.GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
    if (passes > 1) {
        cmdList->TransitionBarrier(blurredIconTex2.GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
    }
    cmdList->FlushResourceBarriers();
}

ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, UINT64 size)
{
    ComPtr<ID3D12Resource> resource;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource));

    return resource;
}

void UILayer::Update(const std::shared_ptr<GCommandList>& cmdList)
{
    GradientNoiseCB par{};
    const auto baseCol = baseColors[uiDescriptorCounter];
    const auto ednCol = endColors[uiDescriptorCounter];
    par.ColorStart = XMFLOAT4(baseCol.x, baseCol.y, baseCol.z, baseCol.w);
    par.ColorEnd = XMFLOAT4(ednCol.x, ednCol.y, ednCol.z, ednCol.w);
    par.Smoothness = 2.0f;
    par.TextureSize = XMFLOAT2(128, 128);
    par.Time = totalTime;

    par.ColorShiftSpeed = 1.95f; // Скорость перехода
    par.NoiseIntensity = 0.1f; // Слабая текстура

    cmdList->TransitionBarrier(uiTexs[uiDescriptorCounter].GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    cmdList->SetDescriptorsHeap(&uiUAVs[uiDescriptorCounter]);
    cmdList->SetPipelineState(uiPSOs[uiDescriptorCounter]);

   // cmdList->SetRootConstantBufferView(0, params);
    cmdList->SetRoot32BitConstants(0, sizeof(GradientNoiseCB) / sizeof(float), &par, 0);
    cmdList->SetRootDescriptorTable(1, &uiUAVs[uiDescriptorCounter]);

    uint32_t dispatchX = (128 + 8 - 1) / 8; // 32 groups (256 / 8)
    uint32_t dispatchY = (128 + 8 - 1) / 8; // 32 groups (256 / 8)
    cmdList->Dispatch(dispatchX, dispatchY, 1);

    cmdList->TransitionBarrier(uiTexs[uiDescriptorCounter].GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();

    // device->GetCommandQueue(GQueueType::Compute)->ExecuteCommandList(cmd);

    uiDescriptorCounter++;
}


void UILayer::InitializeGradientResources()
{
    uiSRVs[uiDescriptorCounter] = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    uiUAVs[uiDescriptorCounter] = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    D3D12_RESOURCE_DESC gradientText;
    gradientText.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    gradientText.Alignment = 0;
    gradientText.Width = 128;
    gradientText.Height = 128;
    gradientText.DepthOrArraySize = 1;
    gradientText.MipLevels = 1;
    gradientText.Format = rtvFormat;
    gradientText.SampleDesc.Count = 1;
    gradientText.SampleDesc.Quality = 0;
    gradientText.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    gradientText.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    uiTexs[uiDescriptorCounter] = GTexture(device, gradientText, L"Gradied tex");

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
    uavDesc.Format = rtvFormat;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.PlaneSlice = 0;
    uavDesc.Texture2D.MipSlice = 0;

    uiTexs[uiDescriptorCounter].CreateUnorderedAccessView(&uavDesc, &uiUAVs[uiDescriptorCounter]);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    srvDesc.Texture2D.MipLevels = 1;

    uiTexs[uiDescriptorCounter].CreateShaderResourceView(&srvDesc, &uiSRVs[uiDescriptorCounter]);

    GRootSignature gradientSignature;

    // 1. Используем 32-bit константы вместо CBV
    gradientSignature.AddConstantParameter(sizeof(GradientNoiseCB) / sizeof(float), 0);  // 12 значений float

    // 2. Создаем диапазон для UAV
    CD3DX12_DESCRIPTOR_RANGE uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    // 3. Добавляем UAV таблицу
    gradientSignature.AddDescriptorParameter(&uavRange, 1);

    // 4. Инициализируем
    gradientSignature.Initialize(device);

    uiShaders[uiDescriptorCounter] = GShader(L"Shaders\\GradientCompute.hlsl", ComputeShader, nullptr, "CSMain", "cs_5_1");
    uiShaders[uiDescriptorCounter].LoadAndCompile();

    uiPSOs[uiDescriptorCounter] = ComputePSO();
    uiPSOs[uiDescriptorCounter].SetRootSignature(gradientSignature);
    uiPSOs[uiDescriptorCounter].SetShader(&uiShaders[uiDescriptorCounter]);
    uiPSOs[uiDescriptorCounter].Initialize(device);

    uiDescriptorCounter++;
}

void UILayer::InitializeHPBar()
{
    hbBarSRV = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    hbBarUAV = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    D3D12_RESOURCE_DESC gradientText;
    gradientText.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    gradientText.Alignment = 0;
    gradientText.Width = 2048;
    gradientText.Height = 2048;
    gradientText.DepthOrArraySize = 1;
    gradientText.MipLevels = 1;
    gradientText.Format = rtvFormat;
    gradientText.SampleDesc.Count = 1;
    gradientText.SampleDesc.Quality = 0;
    gradientText.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    gradientText.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hbBarTex = GTexture(device, gradientText, L"HP bar");

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
    uavDesc.Format = rtvFormat;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.PlaneSlice = 0;
    uavDesc.Texture2D.MipSlice = 0;

    hbBarTex.CreateUnorderedAccessView(&uavDesc, &hbBarUAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    srvDesc.Texture2D.MipLevels = 1;

    hbBarTex.CreateShaderResourceView(&srvDesc, &hbBarSRV);

    GRootSignature gradientSignature;

    // 1. Используем 32-bit константы вместо CBV
    gradientSignature.AddConstantParameter(sizeof(HPBarCB) / sizeof(float), 0);  // 12 значений float

    // 2. Создаем диапазон для UAV
    CD3DX12_DESCRIPTOR_RANGE uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    // 3. Добавляем UAV таблицу
    gradientSignature.AddDescriptorParameter(&uavRange, 1);

    // 4. Инициализируем
    gradientSignature.Initialize(device);

    hbBarShader = GShader(L"Shaders\\HPBarComputeShader.hlsl", ComputeShader, nullptr, "CSMain", "cs_5_1");
    hbBarShader.LoadAndCompile();

    hbBarPso = ComputePSO();
    hbBarPso.SetRootSignature(gradientSignature);
    hbBarPso.SetShader(&hbBarShader);
    hbBarPso.Initialize(device);
}

void UILayer::UpdateHBBar(const std::shared_ptr<GCommandList>& cmdList, float value = 0.2f)
{
    HPBarCB par;
    const auto baseCol = baseColors[uiDescriptorCounter];
    const auto ednCol = endColors[uiDescriptorCounter];
    par.ColorStart = XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f); // Зелёный

    const auto endR = 1.0f - 1.0f * value * 11;
    const auto endG = 0.0f + 1.0f * value * 11;

    par.ColorEnd = XMFLOAT4(endR, endG, 0.0f, 1.0f);   // Красный

    par.Health = value * 11;
    par.TextureSize = XMFLOAT2(2048, 2048);
    par.Time = totalTime;
    par.SparkleIntensity = 0.0009f;
    par.SparkleSpeed = 0.0008f;
    // Область спавна - по всему HP бару
    par.StarPosMin = XMFLOAT2(0.0f, 0.0f);
    par.StarPosMax = XMFLOAT2(value * 10, 1.0f);
    par.DistortionPower = 0.5f;
    par.GlowIntensity = 1.8f;
    par.FractalScale = 3.8f;
    par.FillThreshold = 1.7f;
    par.CrackThickness = 0.005f;
    par.CrackRoughness = 0.3f;

    cmdList->TransitionBarrier(hbBarTex.GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    cmdList->SetDescriptorsHeap(&hbBarUAV);
    cmdList->SetPipelineState(hbBarPso);

    // cmdList->SetRootConstantBufferView(0, params);
    cmdList->SetRoot32BitConstants(0, sizeof(HPBarCB) / sizeof(float), &par, 0);
    cmdList->SetRootDescriptorTable(1, &hbBarUAV);

    uint32_t dispatchX = (2048 + 7) / 8;  // Округляем вверх (1012 групп)
    uint32_t dispatchY = (2048 + 7) / 8;   // Округляем вверх (13 групп)
    cmdList->Dispatch(dispatchX, dispatchY, 1);

    cmdList->TransitionBarrier(hbBarTex.GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();

    // device->GetCommandQueue(GQueueType::Compute)->ExecuteCommandList(cmd);
}

UILayer::UILayer(const std::shared_ptr<GDevice>& device, const HWND hwnd) : hwnd(hwnd), device((device))
{
    Initialize();
    CreateDeviceObject();
}



UILayer::~UILayer()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void UILayer::RenderEffects(const std::shared_ptr<GCommandQueue>& queue) noexcept
{
    uiDescriptorCounter = 0;
    currentDeltaTime = std::chrono::high_resolution_clock::now();
    const double deltaTime = std::chrono::duration<double>(currentDeltaTime - lastDeltaTime).count();
    lastDeltaTime = currentDeltaTime;

    const auto cmdList = queue->GetCommandList();

    for (uint16_t i = 0; i < 30; i++)
    {
        Update(cmdList);
    }

    if (totalTime * (740.0f / 8096.0f) > (740.0f / 8096.0f))
    {
        mod = -1;
    }
    else if (totalTime * (740.0f / 8096.0f) < 0)
    {
        mod = 1;
    }

    totalTime += static_cast<float>(deltaTime) * static_cast<float>(mod);

    const auto barVal = totalTime * (740.0f / 8096.0f);
    BlurIcon(
        ImVec2(0, 0),       // Позиция здесь не нужна для Compute Shader, если мы блюрим всю текстуру
        ImVec2(static_cast<float>(mapPicSizeX), static_cast<float>(mapPicSizeY)),   // Размеры текстуры
        cmdList
    );

    UpdateHBBar(cmdList, barVal);

    queue->ExecuteCommandList(cmdList);
    //  queue->Close();
}

void UILayer::SetTexture()
{
    uiDescriptorCounter = 0;
    skillCounter = 0;

    for (uint16_t i = 0; i < 30; i++)
    {
        uiTexs[i].Reset();
        InitializeGradientResources();
    }

    for (uint16_t i = 0; i < 40; i++)
    {
        skillPicturesDescriptors[i] = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

        loader.get()->LoadTextureFromFile(SKILL_PICURES_PLACEMENTS[i], device->GetDXDevice().Get(), skillPicturesDescriptors[i].GetCPUHandle(), &my_textures[i], &pictureSizeX, &pictureSizeY);
    }

    for (uint16_t i = 0; i < 1; i++)
    {
        uiNavigationPicturesDescriptors[i] = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

        loader.get()->LoadTextureFromFile(NAVIGATION_ICONS_PLACEMENTS[i], device->GetDXDevice().Get(), uiNavigationPicturesDescriptors[i].GetCPUHandle(), &my_textures[i], &pictureSizeX, &pictureSizeY);
    }

    mapPicture = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    loader.get()->LoadTextureFromFile(MAP_PLACEMENT, device->GetDXDevice().Get(), mapPicture.GetCPUHandle(), &map, &mapPicSizeX, &mapPicSizeY);
    InitializeHPBar();

    InitializeBlurResources();
}

void UILayer::ChangeDevice(const std::shared_ptr<GDevice>& device)
{
    uiDescriptorCounter = 0;
    currentColorIndex = 0;

    if (this->device == device)
    {
        return;
    }
    this->device = device;

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    isShared = !isShared;
    SetupRenderBackends();
    CreateDeviceObject();
    SetTexture();
}

void UILayer::ApplyGradientAndStarsEffect(
    const ImVec2& size,
    const ImVec4& gradientColor1,
    const ImVec4& gradientColor2,
    float glowSpeed,
    int starCount,
    float starSize,
    const ImVec4& starColor
)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Получение координат элемента
    ImVec2 item_min = ImGui::GetItemRectMin();
    ImVec2 item_max = ImGui::GetItemRectMax();

    // Обновление фазы свечения
    glow_phase += ImGui::GetIO().DeltaTime * glowSpeed;
    if (glow_phase > 2.0f * IM_PI) glow_phase -= 2.0f * IM_PI;

    // Вычисление градиентного цвета
    float t = 0.5f + 0.5f * sinf(glow_phase); // Анимация градиента

    // Выполняем интерполяцию компонентов цвета вручную
    ImVec4 color1 = gradientColor1;
    ImVec4 color2 = gradientColor2;
    ImVec4 interpolatedColor(
        color1.x * (1.0f - t) + color2.x * t,
        color1.y * (1.0f - t) + color2.y * t,
        color1.z * (1.0f - t) + color2.z * t,
       color1.w * (1.0f - t) + color2.w * t
    );

   ImU32 gradient_color = ImGui::ColorConvertFloat4ToU32(interpolatedColor);

    // Наложение градиента
    draw_list->AddRectFilledMultiColor(
        item_min, item_max,
        gradient_color, gradient_color, // Левый верхний и правый верхний
        gradient_color, gradient_color  // Левый нижний и правый нижний
    );

    // Добавление звездочек
   for (int i = 0; i < starCount; ++i)
    {
        float x = item_min.x + (item_max.x - item_min.x) * (0.2f + i * 0.3f / starCount) + sinf(glow_phase + i) * 5.0f;
        float y = item_min.y + (item_max.y - item_min.y) * 0.5f + cosf(glow_phase + i) * 5.0f;
        ImVec2 center(x, y);
        ImVec2 p1(center.x, center.y - starSize);
        ImVec2 p2(center.x - starSize * 0.5f, center.y + starSize * 0.5f);
        ImVec2 p3(center.x + starSize * 0.5f, center.y + starSize * 0.5f);
        draw_list->AddTriangleFilled(p1, p2, p3, ImGui::ColorConvertFloat4ToU32(starColor));
    }
}

void UILayer::Render(const std::shared_ptr<GCommandList>& cmdList)
{
    skillCounter = 0;

    cmdList->SetDescriptorsHeap(&srvMemory);

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    displaySize = ImGui::GetIO().DisplaySize;

    for (uint16_t i = 0; i < 1; i++)
    {
        currentColorIndex = 0;
    //    DrawLeftBar();
     //   DrawLeftBottomPanel();
      //  DrawRightBar();
        DrawRightTopBar();
      //  DrawRightMiddlePanel();

        DrawBottomPanel();
    }

    ImGui::Begin("sec", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    ImGui::Text(std::to_string(deltatime).c_str());
    ImGui::End();

    ImGui::ShowDemoWindow();
    ImGui::Render();

    // Рендерим ImGui
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList->GetGraphicsCommandList().Get());

    // lastTime = std::chrono::high_resolution_clock::now();
     //std::chrono::duration<double> duration = lastTime - currentTime;
   //  timee = duration.count();
    // totalTime += timee * mod;
    // deltatime = timee;
}
