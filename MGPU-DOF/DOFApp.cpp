#include "pch.h"
#include "DOFApp.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "GDeviceFactory.h"
#include "Window.h"
#include "d3dUtil.h"
#include "KeyboardDevice.h"
#include "ShaderBuffersData.h"
#include "Transform.h"
#include "GModel.h"
#include "imgui.h"
#include "Services/States/WaitState.h"

using namespace PEPEngine;
using namespace Graphics;
using namespace Utils;
using namespace DirectX::SimpleMath;

namespace
{
    constexpr DXGI_FORMAT kColorFormat = BackBufferFormat;
    constexpr DXGI_FORMAT kDepthResourceFormat = DXGI_FORMAT_R32_TYPELESS;
    constexpr DXGI_FORMAT kDepthSrvFormat = DXGI_FORMAT_R32_FLOAT;
    constexpr DXGI_FORMAT kDepthDsvFormat = DXGI_FORMAT_D32_FLOAT;
    // R16_UNORM = linearZ/FarZ. Halves PCIe traffic vs R32 NDC and gives
    // uniform precision over [0, FarZ] (NDC bunches near 1.0).
    constexpr DXGI_FORMAT kDepthCrossFormat = DXGI_FORMAT_R16_UNORM;
    constexpr DXGI_FORMAT kLinearDepthFormat = DXGI_FORMAT_R16_UNORM;
    constexpr DXGI_FORMAT kMaskFormat = DXGI_FORMAT_R8_UINT;
    constexpr DXGI_FORMAT kWeightFormat = DXGI_FORMAT_R16_FLOAT;
}

DOFApp::DOFApp(const HINSTANCE hInstance)
    : D3DApp(hInstance)
    , debugLogger(FileQueueWriter(std::filesystem::current_path() / "dof_log.txt"))
{
}

bool DOFApp::Initialize()
{
    InitDevices();

    if (!D3DApp::Initialize())
    {
        return false;
    }

    assets = std::make_shared<AssetsLoader>(primeDevice);
    typedRenderer.resize(static_cast<size_t>(RenderMode::Count));

    LoadTextures();
    LoadModels();
    CreateMaterials();
    InitDescriptorHeaps();

    InitSharedFence();
    InitSceneRootSignature();
    InitScenePSOs();
    InitRootSignatures();
    InitPSOs();
    InitFrameResources();
    CreateSceneObjects();
    SortScene();
    ApplyDofPreset(1);
    uiLayer = std::make_shared<UILayer>(primeDevice, MainWindow->GetWindowHandle());
    OnResize();

    Flush();

    int TestTime = 10;
#if !defined(DEBUG) && !defined(_DEBUG)
    TestTime = 120;
#endif

    {
        const std::wstring modeName = useMultiGpu ? L"MultiGPU DOF " : L"SingleGPU DOF ";
        const auto logPath = Benchmark::GetLogFile(modeName, *primeDevice, *secondDevice);

        // We bypass FileQueueWriter's "queue + drain on Exit" path: if the
        // window closes before the 120 s cycle finishes, OnExit never fires
        // and the queue is never drained. Writing each line directly in append
        // mode keeps partial runs usable.
        auto& benchState = benchmark.AddState<WaitState>(TestTime, FileQueueWriter(logPath));

        benchState.OnEnter = [this, logPath](FileQueueWriter&)
        {
            // BenchmarkService re-enters the same state after completion;
            // skip re-truncating the log so the previous results survive.
            if (benchmarkFinished) return;
            std::ofstream f(logPath, std::ios::out | std::ios::trunc);
            if (f.is_open())
            {
                f << "FPS;MSPF;MinFPS;MinMSPF;MaxFPS;MaxMSPF\n";
            }
        };
        benchState.OnStatChanged = [this, modeName, logPath](FileQueueWriter&,
                                                             const TimeStats& ts,
                                                             float progress)
        {
            std::ofstream f(logPath, std::ios::out | std::ios::app);
            if (f.is_open())
            {
                char line[128];
                std::snprintf(line, sizeof(line), "%.2f;%.2f;%.2f;%.2f;%.2f;%.2f\n",
                              ts.fps, ts.mspf, ts.minFps, ts.minMspf, ts.maxFps, ts.maxMspf);
                f << line;
            }
            MainWindow->SetWindowTitle(modeName +
                std::format(L"{:.0f}%", progress * 100) +
                L" FPS:" + std::to_wstring(static_cast<int>(ts.fps)));
        };
        benchState.OnExit = [this, modeName](FileQueueWriter&)
        {
            Flush();
            MainWindow->SetWindowTitle(modeName + L"[benchmark finished — close the window]");
            benchmarkFinished = true;
        };
    }

#if !defined(DEBUG) && !defined(_DEBUG)
    benchmark.Start();
#endif

    return true;
}

void DOFApp::Flush()
{
    primeDevice->Flush();
    if (secondDevice && secondDevice != primeDevice)
    {
        secondDevice->Flush();
    }
}

namespace
{
    // Discrete adapters always outrank integrated regardless of reported VRAM
    // (integrated GPUs may show tiny or zero DedicatedVideoMemory on Windows).
    long long AdapterRank(const std::shared_ptr<PEPEngine::Graphics::GDevice>& dev)
    {
        const auto& d = dev->GetDesc();
        const bool isDiscrete = d.DedicatedVideoMemory >= (256ull * 1024 * 1024);
        const long long discreteBonus = isDiscrete ? (1ll << 40) : 0;
        return discreteBonus + static_cast<long long>(d.DedicatedVideoMemory);
    }
}

void DOFApp::InitDevices()
{
    auto allDevices = GDeviceFactory::GetAllDevices(true);
    if (allDevices.empty())
    {
        MessageBoxW(nullptr, L"No Direct3D 12 adapters found.", L"MGPU-DOF", MB_OK | MB_ICONERROR);
        std::exit(1);
    }

    // DXGI enumeration order is not always discrete-first; sort explicitly.
    std::sort(allDevices.begin(), allDevices.end(),
              [](const auto& a, const auto& b) { return AdapterRank(a) > AdapterRank(b); });

    debugLogger.PushMessage(L"\n[InitDevices] Adapters ranked by capability:");
    for (size_t i = 0; i < allDevices.size(); ++i)
    {
        const auto& d = allDevices[i]->GetDesc();
        debugLogger.PushMessage(L"  [" + std::to_wstring(i) + L"] " + allDevices[i]->GetName() +
                                L"  VRAM=" + std::to_wstring(d.DedicatedVideoMemory / (1024 * 1024)) + L" MB");
    }

    primeDevice = allDevices[0];
    secondDevice = (allDevices.size() >= 2) ? allDevices[1] : allDevices[0];

    // --single-gpu collapses both pointers so the whole pipeline runs on one
    if (forceSingleGpu)
    {
        secondDevice = primeDevice;
        debugLogger.PushMessage(L"\n[CLI] forceSingleGpu — both queues bound to prime device.");
    }

    useMultiGpu = (primeDevice != secondDevice);

    debugLogger.PushMessage(L"\nPrime Device: " + primeDevice->GetName());
    debugLogger.PushMessage(L"\t\n Cross Adapter Texture Support: " + std::to_wstring(primeDevice->IsCrossAdapterTextureSupported()));
    debugLogger.PushMessage(L"\nSecond Device: " + secondDevice->GetName());
    debugLogger.PushMessage(L"\t\n Cross Adapter Texture Support: " + std::to_wstring(secondDevice->IsCrossAdapterTextureSupported()));
    debugLogger.PushMessage(L"\nMulti-GPU: " + std::to_wstring(useMultiGpu));

    dofDevice = useMultiGpu ? secondDevice : primeDevice;

    primeGraphicsQueue = primeDevice->GetCommandQueue(GQueueType::Graphics);
    primeCopyQueue = primeDevice->GetCommandQueue(GQueueType::Copy);
    secondGraphicsQueue = secondDevice->GetCommandQueue(GQueueType::Graphics);
    dofGraphicsQueue = useMultiGpu ? secondGraphicsQueue : primeGraphicsQueue;
}

void DOFApp::InitSharedFence()
{
    if (useMultiGpu)
    {
        primeDevice->SharedFence(primeFence, secondDevice, sharedFence, sharedFenceValue);
    }
}

void DOFApp::InitSceneRootSignature()
{
    auto rs = std::make_shared<GRootSignature>();
    CD3DX12_DESCRIPTOR_RANGE texParam[4];
    texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0);
    texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0);
    texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::AmbientMap - 3, 0);
    texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                     static_cast<UINT>(assets->GetLoadTexturesCount() > 0 ? assets->GetLoadTexturesCount() : 1),
                     StandardShaderSlot::TexturesMap - 3, 0);

    rs->AddConstantBufferParameter(0);
    rs->AddConstantBufferParameter(1);
    rs->AddShaderResourceView(0, 1);
    rs->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rs->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rs->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rs->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rs->Initialize(primeDevice);

    sceneRootSignature = rs;
}

void DOFApp::InitScenePSOs()
{
    std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    const D3D12_INPUT_LAYOUT_DESC desc = {defaultInputLayout.data(), static_cast<UINT>(defaultInputLayout.size())};

    pipelineFactory.LoadDefaultShaders();
    pipelineFactory.LoadDefaultPSO(primeDevice, sceneRootSignature, desc, kColorFormat, kDepthDsvFormat, nullptr,
                                   NormalMapFormat, AmbientMapFormat);
}

void DOFApp::InitRootSignatures()
{
    {
        CD3DX12_DESCRIPTOR_RANGE srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kSrvOffsetCount, 0, 0);
        CD3DX12_DESCRIPTOR_RANGE uavRange;
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, kUavOffsetCount, 0, 0);

        dofRootSignature.AddConstantParameter(6, 0); // resolution.xy, focusDistance, focusRange, nearZ, farZ
        dofRootSignature.AddConstantParameter(3, 1); // maxCoC, filterRadius, dofTaps
        dofRootSignature.AddConstantParameter(1, 2); // PatchMatch level / debug mode
        dofRootSignature.AddDescriptorParameter(&srvRange, 1, D3D12_SHADER_VISIBILITY_ALL);
        dofRootSignature.AddDescriptorParameter(&uavRange, 1, D3D12_SHADER_VISIBILITY_ALL);
        dofRootSignature.Initialize(dofDevice);
    }

    {
        CD3DX12_DESCRIPTOR_RANGE srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kSrvOffsetCount, 0, 0);
        CD3DX12_DESCRIPTOR_RANGE uavRange;
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, kUavOffsetCount, 0, 0);

        dofPrimeRootSignature.AddConstantParameter(6, 0);
        dofPrimeRootSignature.AddConstantParameter(3, 1);
        dofPrimeRootSignature.AddConstantParameter(1, 2);
        dofPrimeRootSignature.AddDescriptorParameter(&srvRange, 1, D3D12_SHADER_VISIBILITY_ALL);
        dofPrimeRootSignature.AddDescriptorParameter(&uavRange, 1, D3D12_SHADER_VISIBILITY_ALL);
        dofPrimeRootSignature.Initialize(primeDevice);
    }
}

void DOFApp::InitPSOs()
{
    csCoC = std::make_unique<GShader>(L"Shaders\\DOF\\DOF_CoC_CS.hlsl", ComputeShader, nullptr, "CS", "cs_5_1");
    csHoles = std::make_unique<GShader>(L"Shaders\\DOF\\DOF_Holes_CS.hlsl", ComputeShader, nullptr, "CS", "cs_5_1");
    csPatchPyramid = std::make_unique<GShader>(L"Shaders\\DOF\\DOF_Patch_Pyramid_CS.hlsl", ComputeShader, nullptr, "CS", "cs_5_1");
    csPatchMatch = std::make_unique<GShader>(L"Shaders\\DOF\\DOF_PatchMatch_CS.hlsl", ComputeShader, nullptr, "CS", "cs_5_1");
    csWmax = std::make_unique<GShader>(L"Shaders\\DOF\\DOF_Wmax_CS.hlsl", ComputeShader, nullptr, "CS", "cs_5_1");
    csWfilter = std::make_unique<GShader>(L"Shaders\\DOF\\DOF_WFilter_CS.hlsl", ComputeShader, nullptr, "CS", "cs_5_1");
    csGather = std::make_unique<GShader>(L"Shaders\\DOF\\DOF_Gather_CS.hlsl", ComputeShader, nullptr, "CS", "cs_5_1");
    csDebugCopy = std::make_unique<GShader>(L"Shaders\\DOF\\DOF_DebugCopy_CS.hlsl", ComputeShader, nullptr, "CS", "cs_5_1");
    csDepthQuantize = std::make_unique<GShader>(L"Shaders\\DOF\\DOF_DepthQuantize_CS.hlsl", ComputeShader, nullptr, "CS", "cs_5_1");

    csCoC->LoadAndCompile();
    csHoles->LoadAndCompile();
    csPatchPyramid->LoadAndCompile();
    csPatchMatch->LoadAndCompile();
    csWmax->LoadAndCompile();
    csWfilter->LoadAndCompile();
    csGather->LoadAndCompile();
    csDebugCopy->LoadAndCompile();
    csDepthQuantize->LoadAndCompile();

    auto makePso = [&](ComputePSO& pso, GShader* shader) {
        pso = ComputePSO(dofRootSignature);
        pso.SetShader(shader);
        pso.Initialize(dofDevice);
    };

    makePso(psoCoC, csCoC.get());
    makePso(psoHoles, csHoles.get());
    makePso(psoPatchPyramid, csPatchPyramid.get());
    makePso(psoPatchMatch, csPatchMatch.get());
    makePso(psoWmax, csWmax.get());
    makePso(psoWfilter, csWfilter.get());

    {
        auto makePrimePso = [&](ComputePSO& pso, GShader* shader) {
            pso = ComputePSO(dofPrimeRootSignature);
            pso.SetShader(shader);
            pso.Initialize(primeDevice);
        };
        makePrimePso(psoPrimeCoC, csCoC.get());
        makePrimePso(psoPrimeGather, csGather.get());
        makePrimePso(psoPrimeDebugCopy, csDebugCopy.get());
        makePrimePso(psoDepthQuantize, csDepthQuantize.get());
    }
}

void DOFApp::InitFrameResources()
{
    frameResources.clear();
    frameResources.reserve(globalCountFrameResources);

    const UINT materialCount = static_cast<UINT>(assets->GetMaterials().size());

    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        auto frame = std::make_unique<DOFFrameResource>();
        frame->PassConstantUploadBuffer = std::make_shared<ConstantUploadBuffer<PassConstants>>(primeDevice, 1, L"PassData");
        frame->MaterialBuffer = std::make_shared<StructuredUploadBuffer<MaterialConstants>>(primeDevice, materialCount, L"Materials");
        frameResources.emplace_back(std::move(frame));
    }
}

void DOFApp::ResizeFrameResources(const UINT width, const UINT height)
{
    dispatchWidth = (width + 7) / 8;
    dispatchHeight = (height + 7) / 8;

    if (useMultiGpu)
    {
        UINT64 rowSize = 0;
        UINT64 totalBytes = 0;

        D3D12_RESOURCE_DESC sharedDesc{};
        sharedDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        sharedDesc.Width = width;
        sharedDesc.Height = height;
        sharedDesc.DepthOrArraySize = 1;
        sharedDesc.MipLevels = 1;
        sharedDesc.SampleDesc.Count = 1;

        D3D12_RESOURCE_DESC colorRowDesc = sharedDesc;
        colorRowDesc.Format = kColorFormat;
        colorRowDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        colorRowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
        primeDevice->GetDXDevice()->GetCopyableFootprints(&colorRowDesc, 0, 1, 0, &colorFootprint, &colorNumRows,
                                                          &rowSize, &totalBytes);

        D3D12_RESOURCE_DESC depthRowDesc = sharedDesc;
        depthRowDesc.Format = kDepthCrossFormat;
        depthRowDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        depthRowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
        primeDevice->GetDXDevice()->GetCopyableFootprints(&depthRowDesc, 0, 1, 0, &depthFootprint, &depthNumRows,
                                                          &rowSize, &totalBytes);

        D3D12_RESOURCE_DESC colorFilledRowDesc = sharedDesc;
        colorFilledRowDesc.Format = kColorFormat;
        colorFilledRowDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        colorFilledRowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
        primeDevice->GetDXDevice()->GetCopyableFootprints(&colorFilledRowDesc, 0, 1, 0,
                                                          &colorFilledFootprint, &colorFilledNumRows,
                                                          &rowSize, &totalBytes);

        D3D12_RESOURCE_DESC wFilteredRowDesc = sharedDesc;
        wFilteredRowDesc.Format = kWeightFormat;
        wFilteredRowDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        wFilteredRowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
        primeDevice->GetDXDevice()->GetCopyableFootprints(&wFilteredRowDesc, 0, 1, 0,
                                                          &wFilteredFootprint, &wFilteredNumRows,
                                                          &rowSize, &totalBytes);
        (void)rowSize;
        (void)totalBytes;
    }

    for (auto& framePtr : frameResources)
    {
        auto& frame = *framePtr;

        frame.DOFSharedFenceValue = 0;

        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = width;
            desc.Height = height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

            desc.Format = kColorFormat;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_CLEAR_VALUE colorClear{};
            colorClear.Format = kColorFormat;
            colorClear.Color[3] = 1.0f;
            frame.PrimeColor = GTexture(primeDevice, desc, L"PrimeColor", TextureUsage::RenderTarget, &colorClear);
            frame.ColorRTV = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = kColorFormat;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            frame.PrimeColor.CreateRenderTargetView(&rtvDesc, &frame.ColorRTV);

            desc.Format = kDepthResourceFormat;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            D3D12_CLEAR_VALUE depthClear{};
            depthClear.Format = kDepthDsvFormat;
            depthClear.DepthStencil.Depth = 1.0f;
            depthClear.DepthStencil.Stencil = 0;
            frame.PrimeDepth = GTexture(primeDevice, desc, L"PrimeDepth", TextureUsage::Depth, &depthClear);
            frame.DepthDSV = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
            dsv.Format = kDepthDsvFormat;
            dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            frame.PrimeDepth.CreateDepthStencilView(&dsv, &frame.DepthDSV);
        }

        if (useMultiGpu)
        {
            D3D12_RESOURCE_DESC sharedDesc{};
            sharedDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            sharedDesc.Width = width;
            sharedDesc.Height = height;
            sharedDesc.DepthOrArraySize = 1;
            sharedDesc.MipLevels = 1;
            sharedDesc.SampleDesc.Count = 1;
            sharedDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

            sharedDesc.Format = kDepthCrossFormat;
            sharedDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
            frame.CrossAdapterDepth = std::make_shared<GCrossAdapterResource>(sharedDesc, primeDevice, secondDevice,
                                                                              L"CrossDepth");

            sharedDesc.Format = kColorFormat;
            frame.CrossAdapterColor = std::make_shared<GCrossAdapterResource>(sharedDesc, primeDevice, secondDevice,
                                                                              L"CrossColor");

            sharedDesc.Format = kColorFormat;
            frame.CrossAdapterColorFilled = std::make_shared<GCrossAdapterResource>(sharedDesc,
                                                                                    primeDevice, secondDevice,
                                                                                    L"CrossColorFilled");

            sharedDesc.Format = kWeightFormat;
            frame.CrossAdapterWFiltered = std::make_shared<GCrossAdapterResource>(sharedDesc,
                                                                                  primeDevice, secondDevice,
                                                                                  L"CrossWFiltered");
        }

        auto& dev = dofDevice;
        CreateLocalTexture(frame.SecondDepth, dev, kDepthCrossFormat, width, height, L"SecondDepth");
        CreateLocalTexture(frame.SecondColor, dev, kColorFormat, width, height, L"SecondColor");
        CreateLocalTexture(frame.CoC, dev, kWeightFormat, width, height, L"CoC");
        CreateLocalTexture(frame.OcclusionMask, dev, kMaskFormat, width, height, L"OcclusionMask",
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        CreateLocalTexture(frame.ColorFilled, dev, kColorFormat, width, height, L"ColorFilled");
        CreateLocalTexture(frame.ColorPyramid1, dev, kColorFormat, width / 2, height / 2, L"ColorPyramid1");
        CreateLocalTexture(frame.ColorPyramid2, dev, kColorFormat, std::max(1u, width / 4),
                           std::max(1u, height / 4), L"ColorPyramid2");
        CreateLocalTexture(frame.WMax, dev, kWeightFormat, width, height, L"WMax");
        CreateLocalTexture(frame.WFiltered, dev, kWeightFormat, width, height, L"WFiltered");

        CreateLocalTexture(frame.PrimeCoC, primeDevice, kWeightFormat, width, height, L"PrimeCoC");
        CreateLocalTexture(frame.PrimeDepthQuantized, primeDevice, kLinearDepthFormat, width, height,
                           L"PrimeDepthQuantized", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        CreateLocalTexture(frame.PrimeColorFilled, primeDevice, kColorFormat, width, height, L"PrimeColorFilled");
        CreateLocalTexture(frame.PrimeWFiltered, primeDevice, kWeightFormat, width, height, L"PrimeWFiltered");
        CreateLocalTexture(frame.PrimeDOFResult, primeDevice, kColorFormat, width, height, L"PrimeDOFResult");

        BuildDescriptors(frame, width, height);
        BuildPrimeDescriptors(frame, width, height);
    }
}

void DOFApp::BuildDescriptors(DOFFrameResource& frame, const UINT width, const UINT height)
{
    // SRV range padded to 16 slots so UAV base offset is fixed at 16.
    frame.SrvUavHeap = dofDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16 + kUavOffsetCount);

    const UINT uavBase = 16;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    srv.Format = kLinearDepthFormat;
    frame.SecondDepth.CreateShaderResourceView(&srv, &frame.SrvUavHeap, kSrvOffsetDepth);

    srv.Format = kColorFormat;
    frame.SecondColor.CreateShaderResourceView(&srv, &frame.SrvUavHeap, kSrvOffsetColor);

    srv.Format = kMaskFormat;
    frame.OcclusionMask.CreateShaderResourceView(&srv, &frame.SrvUavHeap, kSrvOffsetOcclusion);

    srv.Format = kColorFormat;
    frame.ColorFilled.CreateShaderResourceView(&srv, &frame.SrvUavHeap, kSrvOffsetColorFilled);
    frame.ColorPyramid1.CreateShaderResourceView(&srv, &frame.SrvUavHeap, kSrvOffsetPyramid1);
    frame.ColorPyramid2.CreateShaderResourceView(&srv, &frame.SrvUavHeap, kSrvOffsetPyramid2);

    srv.Format = kWeightFormat;
    frame.WMax.CreateShaderResourceView(&srv, &frame.SrvUavHeap, kSrvOffsetWmax);
    frame.WFiltered.CreateShaderResourceView(&srv, &frame.SrvUavHeap, kSrvOffsetWfiltered);
    frame.CoC.CreateShaderResourceView(&srv, &frame.SrvUavHeap, kSrvOffsetCoC);

    uav.Format = kWeightFormat;
    frame.CoC.CreateUnorderedAccessView(&uav, &frame.SrvUavHeap, uavBase + kUavOffsetCoC);

    uav.Format = kMaskFormat;
    frame.OcclusionMask.CreateUnorderedAccessView(&uav, &frame.SrvUavHeap, uavBase + kUavOffsetOcclusion);

    uav.Format = kColorFormat;
    frame.ColorFilled.CreateUnorderedAccessView(&uav, &frame.SrvUavHeap, uavBase + kUavOffsetColorFilled);

    uav.Format = kWeightFormat;
    frame.WMax.CreateUnorderedAccessView(&uav, &frame.SrvUavHeap, uavBase + kUavOffsetWmax);
    frame.WFiltered.CreateUnorderedAccessView(&uav, &frame.SrvUavHeap, uavBase + kUavOffsetWfiltered);

    uav.Format = kColorFormat;
    frame.ColorPyramid1.CreateUnorderedAccessView(&uav, &frame.SrvUavHeap, uavBase + kUavOffsetPyramid1);
    frame.ColorPyramid2.CreateUnorderedAccessView(&uav, &frame.SrvUavHeap, uavBase + kUavOffsetPyramid2);
    // kUavOffsetDOF is intentionally left unbound here — Gather (which writes
    // that slot) executes on the prime device, see PrimeGatherHeap.
}

void DOFApp::BuildPrimeDescriptors(DOFFrameResource& frame, const UINT width, const UINT height)
{
    frame.PrimeGatherHeap = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                             16 + kUavOffsetCount);
    const UINT uavBase = 16;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    srv.Format = kLinearDepthFormat;
    frame.PrimeDepthQuantized.CreateShaderResourceView(&srv, &frame.PrimeGatherHeap, kSrvOffsetDepth);

    srv.Format = kColorFormat;
    frame.PrimeColor.CreateShaderResourceView(&srv, &frame.PrimeGatherHeap, kSrvOffsetColor);
    frame.PrimeColorFilled.CreateShaderResourceView(&srv, &frame.PrimeGatherHeap, kSrvOffsetColorFilled);

    srv.Format = kWeightFormat;
    frame.PrimeWFiltered.CreateShaderResourceView(&srv, &frame.PrimeGatherHeap, kSrvOffsetWfiltered);
    frame.PrimeCoC.CreateShaderResourceView(&srv, &frame.PrimeGatherHeap, kSrvOffsetCoC);

    srv.Format = kDepthSrvFormat;
    frame.PrimeDepth.CreateShaderResourceView(&srv, &frame.PrimeGatherHeap, kSrvOffsetNdcDepth);

    uav.Format = kWeightFormat;
    frame.PrimeCoC.CreateUnorderedAccessView(&uav, &frame.PrimeGatherHeap, uavBase + kUavOffsetCoC);

    uav.Format = kLinearDepthFormat;
    frame.PrimeDepthQuantized.CreateUnorderedAccessView(&uav, &frame.PrimeGatherHeap,
                                                        uavBase + kUavOffsetPrimeDepth);

    uav.Format = kColorFormat;
    frame.PrimeDOFResult.CreateUnorderedAccessView(&uav, &frame.PrimeGatherHeap, uavBase + kUavOffsetDOF);
}

void DOFApp::OnResize()
{
    if (!primeDevice) return;

    Flush();

    D3DApp::OnResize();

    viewport = {0.0f, 0.0f, static_cast<float>(MainWindow->GetClientWidth()),
                static_cast<float>(MainWindow->GetClientHeight()), 0.0f, 1.0f};
    scissorRect = {0, 0, MainWindow->GetClientWidth(), MainWindow->GetClientHeight()};

    if (camera)
    {
        camera->SetAspectRatio(AspectRatio());
    }

    backBufferRTVs = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, globalCountFrameResources);
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = GetSRGBFormat(BackBufferFormat);
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        MainWindow->GetBackBuffer(i).CreateRenderTargetView(&rtvDesc, &backBufferRTVs, i);
    }

    if (uiLayer)
    {
        uiLayer->Invalidate();
        uiLayer->CreateDeviceObject();
    }

    ResizeFrameResources(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
}

bool DOFApp::InitMainWindow()
{
    MainWindow = CreateRenderWindow(primeDevice, mainWindowCaption, 1920, 1080, false);
    return true;
}

LRESULT DOFApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (uiLayer && uiLayer->MsgProc(hwnd, msg, wParam, lParam))
    {
        return true;
    }

    switch (msg)
    {
    case WM_INPUT:
        {
            if (ImGui::GetIO().WantCaptureMouse) return true;
            UINT dataSize;
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dataSize, sizeof(RAWINPUTHEADER));
            if (dataSize > 0)
            {
                auto rawdata = std::make_unique<BYTE[]>(dataSize);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawdata.get(), &dataSize,
                                    sizeof(RAWINPUTHEADER)) == dataSize)
                {
                    auto raw = reinterpret_cast<RAWINPUT*>(rawdata.get());
                    if (raw->header.dwType == RIM_TYPEMOUSE)
                    {
                        GetMouse()->OnMouseMoveRaw(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
                    }
                }
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    case WM_MOUSEMOVE:
        if (ImGui::GetIO().WantCaptureMouse) return true;
        GetMouse()->OnMouseMove(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_LBUTTONDOWN:
        if (ImGui::GetIO().WantCaptureMouse) return true;
        GetMouse()->OnLeftPressed(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_RBUTTONDOWN:
        if (ImGui::GetIO().WantCaptureMouse) return true;
        GetMouse()->OnRightPressed(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MBUTTONDOWN:
        if (ImGui::GetIO().WantCaptureMouse) return true;
        GetMouse()->OnMiddlePressed(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (ImGui::GetIO().WantCaptureMouse) return true;
        GetMouse()->OnLeftReleased(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_RBUTTONUP:
        if (ImGui::GetIO().WantCaptureMouse) return true;
        GetMouse()->OnRightReleased(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MBUTTONUP:
        if (ImGui::GetIO().WantCaptureMouse) return true;
        GetMouse()->OnMiddleReleased(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MOUSEWHEEL:
        if (ImGui::GetIO().WantCaptureMouse) return true;
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) GetMouse()->OnWheelUp(x, y);
            else if (GET_WHEEL_DELTA_WPARAM(wParam) < 0) GetMouse()->OnWheelDown(x, y);
            return 0;
        }
    case WM_KEYDOWN:
        {
            const auto keycode = static_cast<unsigned char>(wParam);
            if (ImGui::GetIO().WantCaptureKeyboard) return true;
            if (GetKeyboard()->IsKeysAutoRepeat()) GetKeyboard()->OnKeyPressed(keycode);
            else
            {
                const bool wasPressed = lParam & 0x40000000;
                if (!wasPressed) GetKeyboard()->OnKeyPressed(keycode);
            }
            return 0;
        }
    case WM_KEYUP:
        if (ImGui::GetIO().WantCaptureKeyboard) return true;
        GetKeyboard()->OnKeyReleased(static_cast<unsigned char>(wParam));
        return 0;
    }

    return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}

void DOFApp::Update(const GameTimer& gt)
{
    const UINT olderIndex = (currentFrameIndex + globalCountFrameResources - 1) % globalCountFrameResources;
    primeGPURenderingTime = primeDevice->GetCommandQueue()->GetTimestamp(olderIndex);
    if (useMultiGpu)
    {
        secondGPURenderingTime = secondDevice->GetCommandQueue()->GetTimestamp(olderIndex);
    }
    else
    {
        secondGPURenderingTime = 0;
    }

    currentFrameIndex = (currentFrameIndex + 1) % globalCountFrameResources;
    currentFrame = frameResources[currentFrameIndex].get();

    if (currentFrame->PrimeRenderFenceValue != 0 && !primeGraphicsQueue->IsFinish(currentFrame->PrimeRenderFenceValue))
    {
        primeGraphicsQueue->WaitForFenceValue(currentFrame->PrimeRenderFenceValue);
    }
    else
    {
        primeDevice->ReleaseSlateDescriptors(currentFrame->PrimeRenderFenceValue);
    }

    if (useMultiGpu)
    {
        if (currentFrame->SecondRenderFenceValue != 0 && !secondGraphicsQueue->IsFinish(currentFrame->SecondRenderFenceValue))
        {
            secondGraphicsQueue->WaitForFenceValue(currentFrame->SecondRenderFenceValue);
        }
        else
        {
            secondDevice->ReleaseSlateDescriptors(currentFrame->SecondRenderFenceValue);
        }
    }

    auto* keyboard = GetKeyboard();
    const auto& io = ImGui::GetIO();
    const float stepFocus = 1.0f;
    const float stepRange = 0.5f;
    const float stepCoC   = 0.5f;
    if (keyboard && !io.WantCaptureKeyboard)
    {
        if (keyboard->KeyIsPressed('F')) focusDistance = std::max(0.5f, focusDistance - stepFocus);
        if (keyboard->KeyIsPressed('G')) focusDistance = std::min(200.0f, focusDistance + stepFocus);
        if (keyboard->KeyIsPressed('R')) focusRange = std::max(0.1f, focusRange - stepRange);
        if (keyboard->KeyIsPressed('T')) focusRange = std::min(50.0f, focusRange + stepRange);
        if (keyboard->KeyIsPressed('C')) maxCoC = std::max(0.5f, maxCoC - stepCoC);
        if (keyboard->KeyIsPressed('V')) maxCoC += stepCoC;
        if (keyboard->KeyIsPressed('B')) filterRadius = std::max(1.0f, filterRadius - 1.0f);
        if (keyboard->KeyIsPressed('N')) filterRadius = std::min(8.0f, filterRadius + 1.0f);
        if (keyboard->KeyIsPressed(VK_F1)) debugView = DebugViewMode::Normal;
        if (keyboard->KeyIsPressed(VK_F2)) debugView = DebugViewMode::WFiltered;
        if (keyboard->KeyIsPressed(VK_F3)) debugView = DebugViewMode::WMax;
        if (keyboard->KeyIsPressed(VK_F4)) debugView = DebugViewMode::NoEffect;
        if (keyboard->KeyIsPressed(VK_F5)) ApplyDofPreset(0);
        if (keyboard->KeyIsPressed(VK_F6)) ApplyDofPreset(1);
        if (keyboard->KeyIsPressed(VK_F7)) ApplyDofPreset(2);
    }

    lightRotationAngle += 0.1f * gt.DeltaTime();
    Matrix R = Matrix::CreateRotationY(lightRotationAngle);
    for (int i = 0; i < 3; ++i)
    {
        auto lightDir = baseLightDirections[i];
        lightDir = Vector3::TransformNormal(lightDir, R);
        rotatedLightDirections[i] = lightDir;
    }

    for (const auto& go : gameObjects)
    {
        go->Update();
    }

    UpdateMaterials();
    UpdateMainPassCB(gt);

    if (uiLayer)
    {
        uiLayer->Update();

        const float frameTimeMs = gt.DeltaTime() * 1000.0f;
        frameTimeHistory[frameTimeOffset] = frameTimeMs;
        frameTimeOffset = (frameTimeOffset + 1) % kFrameHistorySize;

        ImGui::Begin("DOF Controls (Multi-GPU)");

        ImGui::Text("FPS: %.1f  (%.2f ms/frame)", 1.0f / gt.DeltaTime(), frameTimeMs);
        ImGui::Text("GPU0 (Scene): %llu us", primeGPURenderingTime);
        if (useMultiGpu)
        {
            ImGui::Text("GPU1 (DOF):   %llu us", secondGPURenderingTime);
        }
        else
        {
            ImGui::Text("GPU0 (DOF):   on same device");
        }

        {
            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%.2f ms", frameTimeMs);
            ImGui::PlotLines("Frame Time", frameTimeHistory, kFrameHistorySize,
                             frameTimeOffset, overlay, 0.0f, 80.0f, ImVec2(0, 60));
        }
        ImGui::Separator();

        ImGui::Text("Mode: %s", useMultiGpu ? "Multi-GPU" : "Single-GPU");
        ImGui::Text("Presets: F5 Front, F6 Mid, F7 Far");
        ImGui::Separator();

        ImGui::SliderFloat("Focus Distance (m)", &focusDistance, 0.5f, 200.0f, "%.2f");
        ImGui::SliderFloat("Focus Range (m)",    &focusRange,    0.1f,  50.0f, "%.2f");
        ImGui::SliderFloat("Max CoC (px)",       &maxCoC,        1.0f,  40.0f, "%.1f");
        ImGui::SliderFloat("Filter Radius",      &filterRadius,  1.0f,  10.0f, "%.1f");
        ImGui::SliderInt  ("DOF Taps",           &dofTaps,       4,     64);

        // WMax is GPU1-only and never shipped across the adapter, so its
        // debug view is disabled in the combo.
        int debugIdx = static_cast<int>(debugView);
        if (ImGui::Combo("Debug View", &debugIdx, "Normal\0WFiltered\0(WMax n/a)\0NoEffect\0"))
        {
            debugView = static_cast<DebugViewMode>(debugIdx);
        }

        if (ImGui::Button("Front focus")) ApplyDofPreset(0);
        ImGui::SameLine();
        if (ImGui::Button("Mid focus")) ApplyDofPreset(1);
        ImGui::SameLine();
        if (ImGui::Button("Far focus")) ApplyDofPreset(2);
        ImGui::End();
    }

    if (!benchmarkFinished)
    {
        benchmark.Tick(gt.DeltaTime());
    }
}

void DOFApp::Draw(const GameTimer& gt)
{
    if (isResizing) return;

    auto& frame = *currentFrame;

    if (useMultiGpu)
    {
        // Three-stage frame-pipelined design:
        //   1. GPU0: scene + DepthQuantize + PrimeCoC + forward cross-adapter copy.
        //   2. GPU1: DOF passes on this frame's data, ship ColorFilled+WFiltered back.
        //   3. GPU0: gather using frame N-1's DOF result, composite, present.
        // Stage 3 reads N-1 to keep GPU0 from blocking on GPU1, the cost is 1 frame of DOF latency.
        const UINT64 fenceValue = ++sharedFenceValue;

        const UINT prevIndex = (currentFrameIndex + globalCountFrameResources - 1)
                               % globalCountFrameResources;
        DOFFrameResource* const prevFrame = frameResources[prevIndex].get();

        {
            auto cmd = primeGraphicsQueue->GetCommandList();
            RecordScenePass(cmd, frame);

            cmd->TransitionBarrier(frame.PrimeDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            cmd->FlushResourceBarriers();
            RecordPrimeDepthQuantize(cmd, frame);
            RecordPrimeCoC(cmd, frame);
            cmd->TransitionBarrier(frame.PrimeDepth, D3D12_RESOURCE_STATE_COMMON);
            cmd->FlushResourceBarriers();

            RecordPrimeCopyToShared(cmd, frame);
            frame.PrimeRenderFenceValue = primeGraphicsQueue->ExecuteCommandList(cmd);
            primeGraphicsQueue->Signal(primeFence, fenceValue);
        }

        {
            secondGraphicsQueue->Wait(sharedFence, fenceValue);
            auto cmd = secondGraphicsQueue->GetCommandList();
            RecordSecondCopyFromShared(cmd, frame);
            RecordSecondDOFPasses(cmd, frame);
            RecordSecondCopyToShared(cmd, frame);
            frame.SecondRenderFenceValue = secondGraphicsQueue->ExecuteCommandList(cmd);
            secondGraphicsQueue->Signal(sharedFence, fenceValue);
            frame.DOFSharedFenceValue = fenceValue;
        }

        {
            const bool havePrevDOF = prevFrame->DOFSharedFenceValue != 0;
            if (havePrevDOF)
            {
                primeGraphicsQueue->Wait(sharedFence, prevFrame->DOFSharedFenceValue);
            }

            auto cmd = primeGraphicsQueue->GetCommandList();
            if (havePrevDOF && debugView != DebugViewMode::NoEffect)
            {
                RecordPrimeCopyDOFFromShared(cmd, *prevFrame);
                RecordPrimeGather(cmd, frame, *prevFrame);
            }
            RecordPrimeComposite(cmd, frame, havePrevDOF ? prevFrame : nullptr);

            cmd->SetViewports(&viewport, 1);
            cmd->SetScissorRects(&scissorRect, 1);
            cmd->SetRenderTargets(1, &backBufferRTVs, MainWindow->GetCurrentBackBufferIndex());
            if (uiLayer) uiLayer->Render(cmd);

            auto& backBuffer = MainWindow->GetCurrentBackBuffer();
            cmd->TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT);
            cmd->FlushResourceBarriers();

            frame.PrimeRenderFenceValue = primeGraphicsQueue->ExecuteCommandList(cmd);
        }
    }
    else
    {
        // Single-GPU path
        auto cmd = primeGraphicsQueue->GetCommandList();
        RecordScenePass(cmd, frame);

        if (debugView != DebugViewMode::NoEffect)
        {
            cmd->TransitionBarrier(frame.PrimeDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            cmd->FlushResourceBarriers();
            RecordPrimeDepthQuantize(cmd, frame);
            cmd->TransitionBarrier(frame.PrimeDepth, D3D12_RESOURCE_STATE_COMMON);
            cmd->FlushResourceBarriers();

            cmd->TransitionBarrier(frame.PrimeColor, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmd->TransitionBarrier(frame.PrimeDepthQuantized, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmd->TransitionBarrier(frame.SecondColor, D3D12_RESOURCE_STATE_COPY_DEST);
            cmd->TransitionBarrier(frame.SecondDepth, D3D12_RESOURCE_STATE_COPY_DEST);
            cmd->FlushResourceBarriers();

            cmd->GetGraphicsCommandList()->CopyResource(frame.SecondColor.GetD3D12Resource().Get(),
                                                         frame.PrimeColor.GetD3D12Resource().Get());
            cmd->GetGraphicsCommandList()->CopyResource(frame.SecondDepth.GetD3D12Resource().Get(),
                                                         frame.PrimeDepthQuantized.GetD3D12Resource().Get());

            cmd->TransitionBarrier(frame.SecondColor, D3D12_RESOURCE_STATE_COMMON);
            cmd->TransitionBarrier(frame.SecondDepth, D3D12_RESOURCE_STATE_COMMON);
            cmd->TransitionBarrier(frame.PrimeColor, D3D12_RESOURCE_STATE_COMMON);
            cmd->TransitionBarrier(frame.PrimeDepthQuantized, D3D12_RESOURCE_STATE_COMMON);
            cmd->FlushResourceBarriers();

            RecordSecondDOFPasses(cmd, frame);

            cmd->TransitionBarrier(frame.ColorFilled, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmd->TransitionBarrier(frame.WFiltered,   D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmd->TransitionBarrier(frame.PrimeColorFilled, D3D12_RESOURCE_STATE_COPY_DEST);
            cmd->TransitionBarrier(frame.PrimeWFiltered,   D3D12_RESOURCE_STATE_COPY_DEST);
            cmd->FlushResourceBarriers();
            cmd->GetGraphicsCommandList()->CopyResource(frame.PrimeColorFilled.GetD3D12Resource().Get(),
                                                         frame.ColorFilled.GetD3D12Resource().Get());
            cmd->GetGraphicsCommandList()->CopyResource(frame.PrimeWFiltered.GetD3D12Resource().Get(),
                                                         frame.WFiltered.GetD3D12Resource().Get());
            cmd->TransitionBarrier(frame.PrimeColorFilled, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            cmd->TransitionBarrier(frame.PrimeWFiltered,   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            cmd->TransitionBarrier(frame.ColorFilled, D3D12_RESOURCE_STATE_COMMON);
            cmd->TransitionBarrier(frame.WFiltered,   D3D12_RESOURCE_STATE_COMMON);
            cmd->FlushResourceBarriers();

            cmd->TransitionBarrier(frame.PrimeDepthQuantized, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            cmd->FlushResourceBarriers();
            RecordPrimeCoC(cmd, frame);

            frame.DOFSharedFenceValue = ++sharedFenceValue;
            RecordPrimeGather(cmd, frame, frame);
        }

        RecordPrimeComposite(cmd, frame, (debugView != DebugViewMode::NoEffect) ? &frame : nullptr);

        auto& backBuffer = MainWindow->GetCurrentBackBuffer();
        cmd->SetViewports(&viewport, 1);
        cmd->SetScissorRects(&scissorRect, 1);
        cmd->SetRenderTargets(1, &backBufferRTVs, MainWindow->GetCurrentBackBufferIndex());
        if (uiLayer) uiLayer->Render(cmd);

        cmd->TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT);
        cmd->FlushResourceBarriers();

        frame.PrimeRenderFenceValue = primeGraphicsQueue->ExecuteCommandList(cmd);
    }

    backBufferIndex = MainWindow->Present();
}

void DOFApp::RecordScenePass(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame)
{
    cmdList->SetGraphicsRootSignature(*sceneRootSignature);
    cmdList->SetDescriptorsHeap(&srvTexturesMemory);
    cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData, *frame.MaterialBuffer);
    cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory);
    const UINT whiteTexIndex = assets->GetTextureIndex(L"white1x1");
    cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, &srvTexturesMemory, whiteTexIndex);
    cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, &srvTexturesMemory, whiteTexIndex);
    cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData, *frame.PassConstantUploadBuffer);

    cmdList->SetViewports(&viewport, 1);
    cmdList->SetScissorRects(&scissorRect, 1);

    cmdList->TransitionBarrier(frame.PrimeColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->TransitionBarrier(frame.PrimeDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    cmdList->FlushResourceBarriers();

    float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
    cmdList->ClearRenderTarget(&frame.ColorRTV, 0, clearColor);
    cmdList->ClearDepthStencil(&frame.DepthDSV, 0, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

    cmdList->SetRenderTargets(1, &frame.ColorRTV, 0, &frame.DepthDSV);

    auto drawGroup = [&](RenderMode mode)
    {
        const auto* pso = pipelineFactory.GetPSO(mode).get();
        if (!pso) return;
        cmdList->SetPipelineState(*pso);
        for (auto& renderer : typedRenderer[static_cast<size_t>(mode)])
        {
            renderer->Draw(cmdList);
        }
    };

    drawGroup(RenderMode::SkyBox);
    drawGroup(RenderMode::Opaque);
    drawGroup(RenderMode::OpaqueAlphaDrop);
}

void DOFApp::RecordPrimeCopyToShared(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame)
{
    cmdList->TransitionBarrier(frame.PrimeColor, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->TransitionBarrier(frame.PrimeDepthQuantized, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->TransitionBarrier(frame.CrossAdapterColor->GetPrimeResource(), D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->TransitionBarrier(frame.CrossAdapterDepth->GetPrimeResource(), D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->FlushResourceBarriers();

    CopyRowMajorTexture(cmdList, frame.CrossAdapterColor->GetPrimeResource(), frame.PrimeColor, colorFootprint,
                        colorNumRows);
    CopyRowMajorTexture(cmdList, frame.CrossAdapterDepth->GetPrimeResource(), frame.PrimeDepthQuantized,
                        depthFootprint, depthNumRows);

    cmdList->TransitionBarrier(frame.CrossAdapterColor->GetPrimeResource(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(frame.CrossAdapterDepth->GetPrimeResource(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(frame.PrimeColor, D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(frame.PrimeDepthQuantized, D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();
}

void DOFApp::RecordSecondCopyFromShared(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame)
{
    cmdList->TransitionBarrier(frame.CrossAdapterColor->GetSharedResource(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->TransitionBarrier(frame.CrossAdapterDepth->GetSharedResource(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->TransitionBarrier(frame.SecondColor, D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->TransitionBarrier(frame.SecondDepth, D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->FlushResourceBarriers();

    CopyRowMajorTexture(cmdList, frame.SecondColor, frame.CrossAdapterColor->GetSharedResource(), colorFootprint,
                        colorNumRows);
    CopyRowMajorTexture(cmdList, frame.SecondDepth, frame.CrossAdapterDepth->GetSharedResource(), depthFootprint,
                        depthNumRows);

    cmdList->TransitionBarrier(frame.SecondColor, D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(frame.SecondDepth, D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(frame.CrossAdapterColor->GetSharedResource(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(frame.CrossAdapterDepth->GetSharedResource(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();
}

void DOFApp::RecordSecondDOFPasses(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame)
{
    cmdList->SetPipelineState(psoCoC);
    cmdList->SetRootSignature(dofRootSignature);
    cmdList->SetDescriptorsHeap(&frame.SrvUavHeap);

    cmdList->TransitionBarrier(frame.SecondDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(frame.SecondColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(frame.CoC, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->TransitionBarrier(frame.OcclusionMask, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->TransitionBarrier(frame.ColorFilled, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->TransitionBarrier(frame.WMax, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->TransitionBarrier(frame.WFiltered, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->TransitionBarrier(frame.ColorPyramid1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->TransitionBarrier(frame.ColorPyramid2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    const float nearZ = camera ? camera->GetNearZ() : 0.1f;
    const float farZ  = camera ? camera->GetFarZ()  : 10000.0f;
    const float constantsA[6] = {
        static_cast<float>(MainWindow->GetClientWidth()),
        static_cast<float>(MainWindow->GetClientHeight()),
        focusDistance,
        focusRange,
        nearZ,
        farZ
    };
    const float constantsB[3] = {maxCoC, filterRadius, static_cast<float>(dofTaps)};
    cmdList->SetComputeRoot32BitConstants(0, 6, constantsA, 0);
    cmdList->SetComputeRoot32BitConstants(1, 3, constantsB, 0);

    cmdList->SetComputeRootDescriptorTable(3, &frame.SrvUavHeap, 0);
    cmdList->SetComputeRootDescriptorTable(4, &frame.SrvUavHeap, 16);

    // 1) CoC.
    cmdList->Dispatch(dispatchWidth, dispatchHeight, 1);
    cmdList->UAVBarrier(frame.CoC.GetD3D12Resource());

    // 2) Holes — 3-band depth classification (Zhang 2019 replaces SLIC).
    cmdList->SetPipelineState(psoHoles);
    cmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    cmdList->TransitionBarrier(frame.OcclusionMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();

    // 3) Patch Pyramid.
    {
        const UINT fullW = MainWindow->GetClientWidth();
        const UINT fullH = MainWindow->GetClientHeight();

        UINT pyramidLevel = 0;
        cmdList->SetPipelineState(psoPatchPyramid);
        cmdList->SetComputeRoot32BitConstants(2, 1, &pyramidLevel, 0);

        const UINT halfW = std::max(1u, fullW / 2);
        const UINT halfH = std::max(1u, fullH / 2);
        cmdList->Dispatch((halfW + 7) / 8, (halfH + 7) / 8, 1);

        cmdList->TransitionBarrier(frame.ColorPyramid1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->FlushResourceBarriers();

        pyramidLevel = 1;
        cmdList->SetComputeRoot32BitConstants(2, 1, &pyramidLevel, 0);

        const UINT qtrW = std::max(1u, fullW / 4);
        const UINT qtrH = std::max(1u, fullH / 4);
        cmdList->Dispatch((qtrW + 7) / 8, (qtrH + 7) / 8, 1);
        cmdList->UAVBarrier(frame.ColorPyramid2.GetD3D12Resource());
    }

    // 4) PatchMatch hole-fill. Each level reads the previous one as SRV and writes the next as UAV.
    {
        const UINT fullW = MainWindow->GetClientWidth();
        const UINT fullH = MainWindow->GetClientHeight();

        cmdList->SetPipelineState(psoPatchMatch);

        UINT levelConst = 2;
        cmdList->SetComputeRoot32BitConstants(2, 1, &levelConst, 0);
        const UINT qtrW = std::max(1u, fullW / 4);
        const UINT qtrH = std::max(1u, fullH / 4);
        cmdList->Dispatch((qtrW + 7) / 8, (qtrH + 7) / 8, 1);

        cmdList->TransitionBarrier(frame.ColorPyramid2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(frame.ColorPyramid1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->FlushResourceBarriers();

        levelConst = 1;
        cmdList->SetComputeRoot32BitConstants(2, 1, &levelConst, 0);
        const UINT halfW = std::max(1u, fullW / 2);
        const UINT halfH = std::max(1u, fullH / 2);
        cmdList->Dispatch((halfW + 7) / 8, (halfH + 7) / 8, 1);

        cmdList->TransitionBarrier(frame.ColorPyramid1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->FlushResourceBarriers();

        levelConst = 0;
        cmdList->SetComputeRoot32BitConstants(2, 1, &levelConst, 0);
        cmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

        cmdList->TransitionBarrier(frame.ColorFilled, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(frame.CoC, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->FlushResourceBarriers();
    }

    // 5) Wmax
    cmdList->SetPipelineState(psoWmax);
    cmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    cmdList->TransitionBarrier(frame.WMax, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();

    // 6) WFilter
    cmdList->SetPipelineState(psoWfilter);
    cmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    cmdList->TransitionBarrier(frame.WFiltered, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

void DOFApp::RecordSecondCopyToShared(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame)
{
    cmdList->TransitionBarrier(frame.ColorFilled, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->TransitionBarrier(frame.WFiltered,   D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->TransitionBarrier(frame.CrossAdapterColorFilled->GetSharedResource(),
                               D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->TransitionBarrier(frame.CrossAdapterWFiltered->GetSharedResource(),
                               D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->FlushResourceBarriers();

    CopyRowMajorTexture(cmdList, frame.CrossAdapterColorFilled->GetSharedResource(),
                        frame.ColorFilled, colorFilledFootprint, colorFilledNumRows);
    CopyRowMajorTexture(cmdList, frame.CrossAdapterWFiltered->GetSharedResource(),
                        frame.WFiltered, wFilteredFootprint, wFilteredNumRows);

    cmdList->TransitionBarrier(frame.CrossAdapterColorFilled->GetSharedResource(),
                               D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(frame.CrossAdapterWFiltered->GetSharedResource(),
                               D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(frame.ColorFilled, D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(frame.WFiltered,   D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();
}

void DOFApp::RecordPrimeDepthQuantize(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame)
{
    cmdList->SetPipelineState(psoDepthQuantize);
    cmdList->SetRootSignature(dofPrimeRootSignature);
    cmdList->SetDescriptorsHeap(&frame.PrimeGatherHeap);

    cmdList->TransitionBarrier(frame.PrimeDepthQuantized, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    const float nearZ = camera ? camera->GetNearZ() : 0.1f;
    const float farZ  = camera ? camera->GetFarZ()  : 10000.0f;
    const float constantsA[6] = {
        static_cast<float>(MainWindow->GetClientWidth()),
        static_cast<float>(MainWindow->GetClientHeight()),
        focusDistance, focusRange, nearZ, farZ
    };
    const float constantsB[3] = {maxCoC, filterRadius, static_cast<float>(dofTaps)};
    cmdList->SetComputeRoot32BitConstants(0, 6, constantsA, 0);
    cmdList->SetComputeRoot32BitConstants(1, 3, constantsB, 0);
    cmdList->SetComputeRootDescriptorTable(3, &frame.PrimeGatherHeap, 0);
    cmdList->SetComputeRootDescriptorTable(4, &frame.PrimeGatherHeap, 16);

    cmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    cmdList->TransitionBarrier(frame.PrimeDepthQuantized, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

void DOFApp::RecordPrimeCoC(const std::shared_ptr<GCommandList>& cmdList, DOFFrameResource& frame)
{
    cmdList->SetPipelineState(psoPrimeCoC);
    cmdList->SetRootSignature(dofPrimeRootSignature);
    cmdList->SetDescriptorsHeap(&frame.PrimeGatherHeap);

    cmdList->TransitionBarrier(frame.PrimeCoC, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    const float nearZ = camera ? camera->GetNearZ() : 0.1f;
    const float farZ  = camera ? camera->GetFarZ()  : 10000.0f;
    const float constantsA[6] = {
        static_cast<float>(MainWindow->GetClientWidth()),
        static_cast<float>(MainWindow->GetClientHeight()),
        focusDistance, focusRange, nearZ, farZ
    };
    const float constantsB[3] = {maxCoC, filterRadius, static_cast<float>(dofTaps)};
    cmdList->SetComputeRoot32BitConstants(0, 6, constantsA, 0);
    cmdList->SetComputeRoot32BitConstants(1, 3, constantsB, 0);
    cmdList->SetComputeRootDescriptorTable(3, &frame.PrimeGatherHeap, 0);
    cmdList->SetComputeRootDescriptorTable(4, &frame.PrimeGatherHeap, 16);

    cmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    cmdList->TransitionBarrier(frame.PrimeCoC, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

void DOFApp::RecordPrimeCopyDOFFromShared(const std::shared_ptr<GCommandList>& cmdList,
                                          DOFFrameResource& dofFrame)
{
    cmdList->TransitionBarrier(dofFrame.CrossAdapterColorFilled->GetPrimeResource(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->TransitionBarrier(dofFrame.CrossAdapterWFiltered->GetPrimeResource(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->TransitionBarrier(dofFrame.PrimeColorFilled, D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->TransitionBarrier(dofFrame.PrimeWFiltered,   D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->FlushResourceBarriers();

    CopyRowMajorTexture(cmdList, dofFrame.PrimeColorFilled,
                        dofFrame.CrossAdapterColorFilled->GetPrimeResource(),
                        colorFilledFootprint, colorFilledNumRows);
    CopyRowMajorTexture(cmdList, dofFrame.PrimeWFiltered,
                        dofFrame.CrossAdapterWFiltered->GetPrimeResource(),
                        wFilteredFootprint, wFilteredNumRows);

    cmdList->TransitionBarrier(dofFrame.PrimeColorFilled, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(dofFrame.PrimeWFiltered,   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(dofFrame.CrossAdapterColorFilled->GetPrimeResource(),
                               D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(dofFrame.CrossAdapterWFiltered->GetPrimeResource(),
                               D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();
}

void DOFApp::RecordPrimeGather(const std::shared_ptr<GCommandList>& cmdList,
                               DOFFrameResource& sceneFrame,
                               DOFFrameResource& dofFrame)
{
    cmdList->SetPipelineState(psoPrimeGather);
    cmdList->SetRootSignature(dofPrimeRootSignature);
    cmdList->SetDescriptorsHeap(&dofFrame.PrimeGatherHeap);

    cmdList->TransitionBarrier(dofFrame.PrimeColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(dofFrame.PrimeDOFResult, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    const float nearZ = camera ? camera->GetNearZ() : 0.1f;
    const float farZ  = camera ? camera->GetFarZ()  : 10000.0f;
    const float constantsA[6] = {
        static_cast<float>(MainWindow->GetClientWidth()),
        static_cast<float>(MainWindow->GetClientHeight()),
        focusDistance, focusRange, nearZ, farZ
    };
    const float constantsB[3] = {maxCoC, filterRadius, static_cast<float>(dofTaps)};
    cmdList->SetComputeRoot32BitConstants(0, 6, constantsA, 0);
    cmdList->SetComputeRoot32BitConstants(1, 3, constantsB, 0);
    cmdList->SetComputeRootDescriptorTable(3, &dofFrame.PrimeGatherHeap, 0);
    cmdList->SetComputeRootDescriptorTable(4, &dofFrame.PrimeGatherHeap, 16);

    cmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    if (debugView == DebugViewMode::WFiltered)
    {
        cmdList->UAVBarrier(dofFrame.PrimeDOFResult.GetD3D12Resource());
        cmdList->SetPipelineState(psoPrimeDebugCopy);
        const UINT debugMode = static_cast<UINT>(debugView);
        cmdList->SetComputeRoot32BitConstants(2, 1, &debugMode, 0);
        cmdList->Dispatch(dispatchWidth, dispatchHeight, 1);
    }

    cmdList->UAVBarrier(dofFrame.PrimeDOFResult.GetD3D12Resource());
    (void)sceneFrame;
}

void DOFApp::RecordPrimeComposite(const std::shared_ptr<GCommandList>& cmdList,
                                  DOFFrameResource& sceneFrame,
                                  DOFFrameResource* dofResultSource)
{
    auto& backBuffer = MainWindow->GetCurrentBackBuffer();

    const bool useDOF = (debugView != DebugViewMode::NoEffect)
                     && (dofResultSource != nullptr)
                     && (dofResultSource->DOFSharedFenceValue != 0);

    if (!useDOF)
    {
        cmdList->TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->TransitionBarrier(sceneFrame.PrimeColor, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->FlushResourceBarriers();

        cmdList->GetGraphicsCommandList()->CopyResource(backBuffer.GetD3D12Resource().Get(),
                                                         sceneFrame.PrimeColor.GetD3D12Resource().Get());

        cmdList->TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(sceneFrame.PrimeColor, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }
    else
    {
        cmdList->TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->TransitionBarrier(dofResultSource->PrimeDOFResult, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->FlushResourceBarriers();

        cmdList->GetGraphicsCommandList()->CopyResource(
            backBuffer.GetD3D12Resource().Get(),
            dofResultSource->PrimeDOFResult.GetD3D12Resource().Get());

        cmdList->TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(dofResultSource->PrimeDOFResult, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }
}

void DOFApp::CreateLocalTexture(GTexture& texture, const std::shared_ptr<GDevice>& device, const DXGI_FORMAT format,
                                const UINT width, const UINT height, const std::wstring& name,
                                const D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = flags;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    texture = GTexture(device, desc, name, TextureUsage::RenderTarget);
}

void DOFApp::CopyRowMajorTexture(const std::shared_ptr<GCommandList>& cmdList,
                                 const GResource& dst, const GResource& src,
                                 const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint,
                                 const UINT numRows)
{
    const auto dstRes = dst.GetD3D12Resource();
    const auto srcRes = src.GetD3D12Resource();
    const auto dstDesc = dst.GetD3D12ResourceDesc();
    const auto srcDesc = src.GetD3D12ResourceDesc();

    CD3DX12_TEXTURE_COPY_LOCATION dstLoc(dstRes.Get(), 0);
    CD3DX12_TEXTURE_COPY_LOCATION srcLoc(srcRes.Get(), 0);

    D3D12_BOX srcBox{};
    const UINT copyWidth = static_cast<UINT>(std::min(dstDesc.Width, srcDesc.Width));
    const UINT copyHeight = static_cast<UINT>(std::min(dstDesc.Height, srcDesc.Height));
    srcBox.left = 0;
    srcBox.top = 0;
    srcBox.front = 0;
    srcBox.right = copyWidth;
    srcBox.bottom = copyHeight;
    srcBox.back = 1;

    cmdList->GetGraphicsCommandList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);
}

void DOFApp::ApplyDofPreset(const int presetIndex)
{
    switch (presetIndex)
    {
    case 0:
        focusDistance = 5.0f;
        focusRange    = 2.0f;
        maxCoC        = 14.0f;
        filterRadius  = 3.0f;
        break;
    case 1:
        focusDistance = 25.0f;
        focusRange    = 6.0f;
        maxCoC        = 14.0f;
        filterRadius  = 3.0f;
        break;
    case 2:
        focusDistance = 80.0f;
        focusRange    = 20.0f;
        maxCoC        = 14.0f;
        filterRadius  = 3.0f;
        break;
    default:
        break;
    }
}

void DOFApp::LoadTextures()
{
    auto queue = primeDevice->GetCommandQueue(GQueueType::Compute);
    const auto cmdList = queue->GetCommandList();

    auto bricksTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\bricks2.dds", cmdList);
    bricksTex->SetName(L"bricksTex"); assets->AddTexture(bricksTex);

    auto stoneTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\stone.dds", cmdList);
    stoneTex->SetName(L"stoneTex"); assets->AddTexture(stoneTex);

    auto tileTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\tile.dds", cmdList);
    tileTex->SetName(L"tileTex"); assets->AddTexture(tileTex);

    auto fenceTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\WireFence.dds", cmdList);
    fenceTex->SetName(L"fenceTex"); assets->AddTexture(fenceTex);

    auto waterTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\water1.dds", cmdList);
    waterTex->SetName(L"waterTex"); assets->AddTexture(waterTex);

    auto skyTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\skymap.dds", cmdList);
    skyTex->SetName(L"skyTex"); assets->AddTexture(skyTex);

    auto grassTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\grass.dds", cmdList);
    grassTex->SetName(L"grassTex"); assets->AddTexture(grassTex);

    auto treeArrayTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\treeArray2.dds", cmdList);
    treeArrayTex->SetName(L"treeArrayTex"); assets->AddTexture(treeArrayTex);

    auto seamless = GTexture::LoadTextureFromFile(L"Data\\Textures\\seamless_grass.jpg", cmdList);
    seamless->SetName(L"seamless"); assets->AddTexture(seamless);

    auto whiteTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\white1x1.dds", cmdList);
    whiteTex->SetName(L"white1x1"); assets->AddTexture(whiteTex);

    std::vector<std::wstring> texNormalNames = {L"bricksNormalMap", L"tileNormalMap", L"defaultNormalMap"};
    std::vector<std::wstring> texNormalFilenames = {
        L"Data\\Textures\\bricks2_nmap.dds",
        L"Data\\Textures\\tile_nmap.dds",
        L"Data\\Textures\\default_nmap.dds"
    };

    for (size_t j = 0; j < texNormalNames.size(); ++j)
    {
        auto texture = GTexture::LoadTextureFromFile(texNormalFilenames[j], cmdList, TextureUsage::Normalmap);
        texture->SetName(texNormalNames[j]);
        assets->AddTexture(texture);
    }

    queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
    primeDevice->Flush();
}

void DOFApp::LoadModels()
{
    auto queue = primeDevice->GetCommandQueue(GQueueType::Compute);
    auto cmdList = queue->GetCommandList();

    auto nano = assets->CreateModelFromFile(cmdList, "Data\\Objects\\Nanosuit\\Nanosuit.obj");
    models[L"nano"] = std::move(nano);

    auto atlas = assets->CreateModelFromFile(cmdList, "Data\\Objects\\Atlas\\Atlas.obj");
    models[L"atlas"] = std::move(atlas);

    auto pbody = assets->CreateModelFromFile(cmdList, "Data\\Objects\\P-Body\\P-Body.obj");
    models[L"pbody"] = std::move(pbody);

    auto griffon = assets->CreateModelFromFile(cmdList, "Data\\Objects\\Griffon\\Griffon.FBX");
    griffon->scaleMatrix = Matrix::CreateScale(0.1f);
    models[L"griffon"] = std::move(griffon);

    auto mountDragon = assets->CreateModelFromFile(cmdList, "Data\\Objects\\MOUNTAIN_DRAGON\\MOUNTAIN_DRAGON.FBX");
    mountDragon->scaleMatrix = Matrix::CreateScale(0.1f);
    models[L"mountDragon"] = std::move(mountDragon);

    auto desertDragon = assets->CreateModelFromFile(cmdList, "Data\\Objects\\DesertDragon\\DesertDragon.FBX");
    desertDragon->scaleMatrix = Matrix::CreateScale(0.1f);
    models[L"desertDragon"] = std::move(desertDragon);

    auto sphere = assets->GenerateSphere(cmdList);
    models[L"sphere"] = std::move(sphere);

    auto quad = assets->GenerateQuad(cmdList);
    models[L"quad"] = std::move(quad);

    auto stair = assets->CreateModelFromFile(cmdList, "Data\\Objects\\Temple\\SM_AsianCastle_A.FBX");
    models[L"stair"] = std::move(stair);

    auto columns = assets->CreateModelFromFile(cmdList, "Data\\Objects\\Temple\\SM_AsianCastle_E.FBX");
    models[L"columns"] = std::move(columns);

    auto fountain = assets->CreateModelFromFile(cmdList, "Data\\Objects\\Temple\\SM_Fountain.FBX");
    models[L"fountain"] = std::move(fountain);

    auto platform = assets->CreateModelFromFile(cmdList, "Data\\Objects\\Temple\\SM_PlatformSquare.FBX");
    models[L"platform"] = std::move(platform);

    auto doom = assets->CreateModelFromFile(cmdList, "Data\\Objects\\DoomSlayer\\doommarine.obj");
    models[L"doom"] = std::move(doom);

    queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
    primeDevice->Flush();
}

void DOFApp::CreateMaterials()
{
    auto seamlessMat = std::make_shared<Material>(L"seamless", RenderMode::Opaque);
    seamlessMat->FresnelR0 = Vector3(0.02f, 0.02f, 0.02f);
    seamlessMat->Roughness = 0.1f;

    auto diffuseIdx = assets->GetTextureIndex(L"seamless");
    seamlessMat->SetDiffuseTexture(assets->GetTexture(diffuseIdx), diffuseIdx);

    auto normalIdx = assets->GetTextureIndex(L"defaultNormalMap");
    seamlessMat->SetNormalMap(assets->GetTexture(normalIdx), normalIdx);
    assets->AddMaterial(seamlessMat);

    models[L"quad"]->SetMeshMaterial(0, assets->GetMaterial(assets->GetMaterialIndex(L"seamless")));
}

void DOFApp::InitDescriptorHeaps()
{
    srvTexturesMemory =
        primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, assets->GetTextures().size());

    auto materials = assets->GetMaterials();
    for (size_t j = 0; j < materials.size(); ++j)
    {
        materials[j]->InitMaterial(&srvTexturesMemory);
    }

    const UINT whiteIdx = assets->GetTextureIndex(L"white1x1");
    if (whiteIdx < assets->GetTextures().size())
    {
        auto whiteTex = assets->GetTexture(whiteIdx);
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        auto desc = whiteTex->GetD3D12Resource()->GetDesc();
        srvDesc.Format = GetSRGBFormat(desc.Format);
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        whiteTex->CreateShaderResourceView(&srvDesc, &srvTexturesMemory, whiteIdx);
    }
}

void DOFApp::CreateSceneObjects()
{
    auto skySphere = std::make_shared<GameObject>("Sky");
    skySphere->GetTransform()->SetScale({500, 500, 500});
    {
        auto renderer = std::make_shared<SkyBox>(primeDevice,
                                                 models[L"sphere"],
                                                 *assets->GetTexture(assets->GetTextureIndex(L"skyTex")).get(),
                                                 &srvTexturesMemory,
                                                 assets->GetTextureIndex(L"skyTex"));
        skySphere->AddComponent(renderer);
        typedRenderer[static_cast<size_t>(RenderMode::SkyBox)].push_back(renderer);
    }
    gameObjects.push_back(skySphere);

    auto quadRitem = std::make_shared<GameObject>("Quad");
    {
        auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"quad"]);
        renderer->SetModel(models[L"quad"]);
        quadRitem->AddComponent(renderer);
        typedRenderer[static_cast<size_t>(RenderMode::Debug)].push_back(renderer);
        typedRenderer[static_cast<size_t>(RenderMode::Quad)].push_back(renderer);
    }
    gameObjects.push_back(quadRitem);

    auto sun1 = std::make_shared<GameObject>("Directional Light");
    auto light = std::make_shared<Light>(Directional);
    light->Direction({0.57735f, -0.57735f, 0.57735f});
    light->Strength({0.8f, 0.8f, 0.8f});
    sun1->AddComponent(light);
    gameObjects.push_back(sun1);

    for (int i = 0; i < 11; ++i)
    {
        auto nano = std::make_shared<GameObject>();
        nano->GetTransform()->SetPosition(Vector3::Right * -15.0f + Vector3::Forward * 12.0f * static_cast<float>(i));
        nano->GetTransform()->SetEulerRotate(Vector3(0, -90, 0));
        auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"nano"]);
        nano->AddComponent(renderer);
        typedRenderer[static_cast<size_t>(RenderMode::Opaque)].push_back(renderer);
        gameObjects.push_back(nano);

        auto doom = std::make_shared<GameObject>();
        doom->SetScale(0.08f);
        doom->GetTransform()->SetPosition(Vector3::Right * 15.0f + Vector3::Forward * 12.0f * static_cast<float>(i));
        doom->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
        renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"doom"]);
        doom->AddComponent(renderer);
        typedRenderer[static_cast<size_t>(RenderMode::Opaque)].push_back(renderer);
        gameObjects.push_back(doom);
    }

    for (int i = 0; i < 12; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            auto atlas = std::make_shared<GameObject>();
            atlas->GetTransform()->SetPosition(
                Vector3::Right * -60.0f + Vector3::Right * -30.0f * static_cast<float>(j) + Vector3::Up * 11.0f + Vector3::Forward * 10.0f * static_cast<float>(i));
            auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"atlas"]);
            atlas->AddComponent(renderer);
            typedRenderer[static_cast<size_t>(RenderMode::Opaque)].push_back(renderer);
            gameObjects.push_back(atlas);

            auto pbody = std::make_shared<GameObject>();
            pbody->GetTransform()->SetPosition(
                Vector3::Right * 130.0f + Vector3::Right * -30.0f * static_cast<float>(j) + Vector3::Up * 11.0f + Vector3::Forward * 10.0f * static_cast<float>(i));
            renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"pbody"]);
            pbody->AddComponent(renderer);
            typedRenderer[static_cast<size_t>(RenderMode::Opaque)].push_back(renderer);
            gameObjects.push_back(pbody);
        }
    }

    auto platform = std::make_shared<GameObject>();
    platform->SetScale(0.2f);
    platform->GetTransform()->SetEulerRotate(Vector3(90, 90, 0));
    platform->GetTransform()->SetPosition(Vector3::Backward * -130);
    auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"platform"]);
    platform->AddComponent(renderer);
    typedRenderer[static_cast<size_t>(RenderMode::Opaque)].push_back(renderer);

    auto rotater = std::make_shared<GameObject>();
    rotater->GetTransform()->SetParent(platform->GetTransform().get());
    rotater->GetTransform()->SetPosition(Vector3::Forward * 325 + Vector3::Left * 625);
    rotater->GetTransform()->SetEulerRotate(Vector3(0, -90, 90));

    auto camGo = std::make_shared<GameObject>("MainCamera");
    camGo->GetTransform()->SetParent(rotater->GetTransform().get());
    camGo->GetTransform()->SetPosition(Vector3(-1000, 190, -32));
    camGo->GetTransform()->SetEulerRotate(Vector3(-30, 270, 0));
    camGo->AddComponent(std::make_shared<Camera>(AspectRatio()));
    camGo->AddComponent(std::make_shared<CameraController>());

    gameObjects.push_back(camGo);
    gameObjects.push_back(rotater);

    auto stair = std::make_shared<GameObject>();
    stair->GetTransform()->SetParent(platform->GetTransform().get());
    stair->SetScale(0.2f);
    stair->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
    stair->GetTransform()->SetPosition(Vector3::Left * 700);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"stair"]);
    stair->AddComponent(renderer);
    typedRenderer[static_cast<size_t>(RenderMode::Opaque)].push_back(renderer);

    auto columns = std::make_shared<GameObject>();
    columns->GetTransform()->SetParent(stair->GetTransform().get());
    columns->SetScale(0.8f);
    columns->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
    columns->GetTransform()->SetPosition(Vector3::Up * 2000 + Vector3::Forward * 900);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"columns"]);
    columns->AddComponent(renderer);
    typedRenderer[static_cast<size_t>(RenderMode::Opaque)].push_back(renderer);

    auto fountain = std::make_shared<GameObject>();
    fountain->SetScale(0.005f);
    fountain->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    fountain->GetTransform()->SetPosition(Vector3::Up * 35 + Vector3::Backward * 77);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"fountain"]);
    fountain->AddComponent(renderer);
    typedRenderer[static_cast<size_t>(RenderMode::Opaque)].push_back(renderer);

    gameObjects.push_back(platform);
    gameObjects.push_back(stair);
    gameObjects.push_back(columns);
    gameObjects.push_back(fountain);

    auto mountDragon = std::make_shared<GameObject>();
    mountDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    mountDragon->GetTransform()->SetPosition(Vector3::Right * -960 + Vector3::Up * 45 + Vector3::Backward * 775);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"mountDragon"]);
    mountDragon->AddComponent(renderer);
    typedRenderer[static_cast<size_t>(RenderMode::Opaque)].push_back(renderer);
    gameObjects.push_back(mountDragon);

    auto desertDragon = std::make_shared<GameObject>();
    desertDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    desertDragon->GetTransform()->SetPosition(Vector3::Right * 960 + Vector3::Up * -5 + Vector3::Backward * 775);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"desertDragon"]);
    desertDragon->AddComponent(renderer);
    typedRenderer[static_cast<size_t>(RenderMode::Opaque)].push_back(renderer);
    gameObjects.push_back(desertDragon);

    auto griffon = std::make_shared<GameObject>();
    griffon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    griffon->SetScale(0.8f);
    griffon->GetTransform()->SetPosition(Vector3::Right * -355 + Vector3::Up * -7 + Vector3::Backward * 17);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"griffon"]);
    griffon->AddComponent(renderer);
    typedRenderer[static_cast<size_t>(RenderMode::OpaqueAlphaDrop)].push_back(renderer);
    gameObjects.push_back(griffon);

    griffon = std::make_shared<GameObject>();
    griffon->SetScale(0.8f);
    griffon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    griffon->GetTransform()->SetPosition(Vector3::Right * 355 + Vector3::Up * -7 + Vector3::Backward * 17);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"griffon"]);
    griffon->AddComponent(renderer);
    typedRenderer[static_cast<size_t>(RenderMode::OpaqueAlphaDrop)].push_back(renderer);
    gameObjects.push_back(griffon);
}

void DOFApp::SortScene()
{
    lights.clear();
    camera.reset();

    for (auto&& item : gameObjects)
    {
        auto lightComp = item->GetComponent<Light>();
        if (lightComp != nullptr)
        {
            lights.push_back(lightComp.get());
        }

        auto camComp = item->GetComponent<Camera>();
        if (camComp != nullptr)
        {
            camera = camComp;
        }
    }
}

void DOFApp::UpdateMainPassCB(const GameTimer& gt)
{
    auto view = camera->GetViewMatrix();
    auto proj = camera->GetProjectionMatrix();

    auto viewProj = (view * proj);
    auto invView = view.Invert();
    auto invProj = proj.Invert();
    auto invViewProj = viewProj.Invert();

    Matrix T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);
    Matrix viewProjTex = XMMatrixMultiply(viewProj, T);
    mainPassCB.debugMap = 0;
    mainPassCB.View = view.Transpose();
    mainPassCB.InvView = invView.Transpose();
    mainPassCB.Proj = proj.Transpose();
    mainPassCB.InvProj = invProj.Transpose();
    mainPassCB.ViewProj = viewProj.Transpose();
    mainPassCB.InvViewProj = invViewProj.Transpose();
    mainPassCB.ViewProjTex = viewProjTex.Transpose();
    mainPassCB.ShadowTransform = Matrix::Identity;
    mainPassCB.EyePosW = camera->gameObject->GetTransform()->GetWorldPosition();
    mainPassCB.RenderTargetSize = Vector2(static_cast<float>(MainWindow->GetClientWidth()),
                                          static_cast<float>(MainWindow->GetClientHeight()));
    mainPassCB.InvRenderTargetSize = Vector2(1.0f / mainPassCB.RenderTargetSize.x,
                                             1.0f / mainPassCB.RenderTargetSize.y);
    mainPassCB.NearZ = 1.0f;
    mainPassCB.FarZ = 1000.0f;
    mainPassCB.TotalTime = gt.TotalTime();
    mainPassCB.DeltaTime = gt.DeltaTime();
    mainPassCB.AmbientLight = Vector4{0.25f, 0.25f, 0.35f, 1.0f};

    for (int i = 0; i < MaxLights; ++i)
    {
        if (i < static_cast<int>(lights.size()))
        {
            mainPassCB.Lights[i] = lights[i]->GetData();
        }
        else
        {
            break;
        }
    }

    if (!lights.empty())
    {
        mainPassCB.Lights[0].Direction = rotatedLightDirections[0];
        mainPassCB.Lights[0].Strength = Vector3{0.9f, 0.8f, 0.7f};
    }
    if (lights.size() > 1)
    {
        mainPassCB.Lights[1].Direction = rotatedLightDirections[1];
        mainPassCB.Lights[1].Strength = Vector3{0.4f, 0.4f, 0.4f};
    }
    if (lights.size() > 2)
    {
        mainPassCB.Lights[2].Direction = rotatedLightDirections[2];
        mainPassCB.Lights[2].Strength = Vector3{0.2f, 0.2f, 0.2f};
    }

    auto currentPassCB = currentFrame->PassConstantUploadBuffer;
    currentPassCB->CopyData(0, mainPassCB);
}

void DOFApp::UpdateMaterials() const
{
    auto currentMaterialBuffer = currentFrame->MaterialBuffer;

    for (auto&& material : assets->GetMaterials())
    {
        material->Update();
        auto constantData = material->GetMaterialConstantData();
        currentMaterialBuffer->CopyData(material->GetIndex(), constantData);
    }
}
