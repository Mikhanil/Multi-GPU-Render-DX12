#pragma once

#include "AssetsLoader.h"
#include "d3dApp.h"
#include "GDeviceFactory.h"
#include "GModel.h"
#include "GraphicPSO.h"
#include "Light.h"
#include "Renderer.h"
#include "RenderModeFactory.h"
#include "ShadowMap.h"
#include "FrameResource.h"
#include "SSAA.h"
#include "SSAO.h"
#include "GCrossAdapterResource.h"

using namespace DirectX::SimpleMath;
using namespace PEPEngine;
using namespace Graphics;


class HybridShadowApp :
    public Common::D3DApp
{
public:
    HybridShadowApp(HINSTANCE hInstance);

    ~HybridShadowApp() override
    {
        HybridShadowApp::Flush();

        for (auto&& device : devices)
        {
            device->ResetAllocators(frameCount);
            device->TerminatedQueuesWorker();
            device.reset();
        }

        devices.clear();

        logThreadIsAlive = false;
    };

    bool Initialize() override;


    int Run() override;

protected:
    void inline Update(const GameTimer& gt) override;
    void inline UpdateMaterials();
    void inline UpdateShadowTransform(const GameTimer& gt);
    void inline UpdateShadowPassCB(const GameTimer& gt);
    void inline UpdateMainPassCB(const GameTimer& gt);
    void inline UpdateSsaoCB(const GameTimer& gt);
    void inline PopulateShadowMapCommands(GraphicsAdapter adapter, std::shared_ptr<GCommandList> cmdList);
    void inline PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList);
    void inline PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList);
    void inline PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList);
    void inline PopulateDrawCommands(GraphicsAdapter adapterIndex, const std::shared_ptr<GCommandList>& cmdList,
                                     RenderMode type);
    void inline PopulateDrawQuadCommand(const std::shared_ptr<GCommandList>& cmdList, GTexture& renderTarget,
                                        GDescriptor* rtvMemory, UINT
                                        offsetRTV);
    void inline PopulateCopyResource(const std::shared_ptr<GCommandList>& cmdList, const GResource& srcResource,
                                     const GResource& dstResource);

    void Draw(const GameTimer& gt) override;

    void OnResize() override;

    bool InitMainWindow() override;;

private:
    void Flush() override;;
    void inline InitDevices();
    void inline InitFrameResource();
    void inline InitRootSignature();
    void inline InitPipeLineResource();
    void inline CreateMaterials();
    void inline InitSRVMemoryAndMaterials();
    void inline InitRenderPaths();
    void inline LoadStudyTexture();
    void inline LoadModels();
    void inline MipMasGenerate();
    void inline DublicateResource();
    void inline SortGO();
    std::shared_ptr<Renderer> inline CreateRenderer(UINT deviceIndex, std::shared_ptr<GModel> model);
    void inline AddMultiDeviceOpaqueRenderComponent(GameObject* object, const std::wstring& modelName,
                                                    RenderMode psoType = RenderMode::Opaque);
    void inline CreateGO();

    void CalculateFrameStats() override;
    void LogWriting();

    UINT64 gpuTimes[GraphicAdapterCount];
    std::atomic<bool> logThreadIsAlive = true;
    LockThreadQueue<std::wstring> logQueue{};
    bool finishTest = false;

    std::atomic<bool> UseOnlyPrime = true;
    UINT multi = 1;

    D3D12_VIEWPORT fullViewport{};
    D3D12_RECT fullRect;

    custom_vector<std::shared_ptr<GDevice>> devices = MemoryAllocator::CreateVector<std::shared_ptr<GDevice>>();

    custom_vector<GDescriptor> srvTexturesMemory = MemoryAllocator::CreateVector<GDescriptor>();

    custom_vector<AssetsLoader> assets = MemoryAllocator::CreateVector<AssetsLoader>();

    custom_vector<custom_unordered_map<std::wstring, std::shared_ptr<GModel>>> models = MemoryAllocator::CreateVector<
        custom_unordered_map<std::wstring, std::shared_ptr<GModel>>>();

    std::shared_ptr<GRootSignature> primeDeviceSignature;
    std::shared_ptr<GRootSignature> ssaoPrimeRootSignature;
    std::shared_ptr<GRootSignature> secondDeviceShadowMapSignature;

    RenderModeFactory defaultPrimePipelineResources;

    std::shared_ptr<GraphicPSO> shadowMapPSOSecondDevice;
    std::shared_ptr<GCrossAdapterResource> crossAdapterShadowMap;
    GTexture primeCopyShadowMap;
    GDescriptor primeCopyShadowMapSRV;
    std::shared_ptr<ShadowMap> shadowPathSecondDevice;

    std::shared_ptr<ShadowMap> shadowPathPrimeDevice;


    std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout{};


    std::shared_ptr<SSAO> ambientPrimePath;
    std::shared_ptr<SSAA> antiAliasingPrimePath;

    custom_vector<std::shared_ptr<GameObject>> gameObjects = MemoryAllocator::CreateVector<std::shared_ptr<
        GameObject>>();

    custom_vector<custom_vector<custom_vector<std::shared_ptr<Renderer>>>> typedRenderer = MemoryAllocator::CreateVector
        <custom_vector<custom_vector<std::shared_ptr<Renderer>>>>();

    PassConstants mainPassCB;
    PassConstants shadowPassCB;


    ComPtr<ID3D12Fence> primeFence;
    ComPtr<ID3D12Fence> secondFence;
    UINT64 sharedFenceValue = 0;

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


    UINT pathMapShow = 0;
    //off, shadowMap, ssaoMap
    const UINT maxPathMap = 3;
    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;
};
