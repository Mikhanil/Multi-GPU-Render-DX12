#pragma once

#include "AssetsLoader.h"
#include "d3dApp.h"
#include "GDeviceFactory.h"
#include "GModel.h"
#include "GraphicPSO.h"
#include "Light.h"
#include "LockThreadQueue.h"
#include "Renderer.h"
#include "RenderModeFactory.h"
#include "ShadowMap.h"
#include "SplitFrameResource.h"
#include "SSAA.h"
#include "SSAO.h"


using namespace PEPEngine;
using namespace Graphics;

class SFRApp :
    public Common::D3DApp
{
public:
    SFRApp(HINSTANCE hInstance);

    ~SFRApp() override
    {
        SFRApp::Flush();

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
    void inline PopulateShadowMapCommands(GraphicsAdapter adapterIndex, std::shared_ptr<GCommandList> cmdList);
    void inline PopulateNormalMapCommands(GraphicsAdapter adapterIndex, const std::shared_ptr<GCommandList>& cmdList);
    void inline PopulateAmbientMapCommands(GraphicsAdapter adapterIndex, const std::shared_ptr<GCommandList>& cmdList);
    void inline PopulateForwardPathCommands(GraphicsAdapter adapterIndex, const std::shared_ptr<GCommandList>& cmdList);
    void inline PopulateDrawCommands(GraphicsAdapter adapterIndex, std::shared_ptr<GCommandList> cmdList,
                                     RenderMode type);
    void inline PopulateDrawQuadCommand(GraphicsAdapter adapterIndex, const std::shared_ptr<GCommandList>& cmdList,
                                        GTexture& renderTarget, GDescriptor* rtvMemory, UINT
                                        offsetRTV);
    void PopulateCopyResource(const std::shared_ptr<GCommandList>& cmdList, const GResource& srcResource,
                              const GResource& dstResource);
    void inline PopulateCopyResourceRegion(const std::shared_ptr<GCommandList>& cmdList, const GResource& srcResource,
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
    std::thread logThread;
    LockThreadQueue<std::wstring> logQueue;
    std::atomic<UINT> statisticCount = 0;

    int percentOfUsePrimeDevice = 100;


    D3D12_VIEWPORT fullViewport{};
    D3D12_RECT fullRect;
    std::vector<D3D12_RECT> adapterRects = std::vector<D3D12_RECT>();
    D3D12_BOX copyRegionBox;


    std::vector<std::shared_ptr<GDevice>> devices = std::vector<std::shared_ptr<GDevice>>();
    std::vector<GDescriptor> srvTexturesMemory = std::vector<GDescriptor>();

    std::vector<AssetsLoader> assets = std::vector<AssetsLoader>();

    std::vector<std::unordered_map<std::wstring, std::shared_ptr<GModel>>> models = std::vector<
        std::unordered_map<std::wstring, std::shared_ptr<GModel>>>();

    std::vector<std::shared_ptr<GRootSignature>> rootSignatures = std::vector<std::shared_ptr<
        GRootSignature>>();

    std::vector<std::shared_ptr<GRootSignature>> ssaoRootSignatures = std::vector<std::shared_ptr<
        GRootSignature>>();

    std::vector<RenderModeFactory> defaultPipelineResources = std::vector<RenderModeFactory>();

    std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout = std::vector<
        D3D12_INPUT_ELEMENT_DESC>();

    std::vector<std::shared_ptr<ShadowMap>> shadowPaths = std::vector<std::shared_ptr<ShadowMap>>();
    std::vector<std::shared_ptr<SSAO>> ambientPaths = std::vector<std::shared_ptr<SSAO>>();
    std::vector<std::shared_ptr<SSAA>> antiAliasingPaths = std::vector<std::shared_ptr<SSAA>>();

    std::vector<std::shared_ptr<GameObject>> gameObjects = std::vector<std::shared_ptr<
        GameObject>>();

    std::vector<std::vector<std::vector<std::shared_ptr<Renderer>>>> typedRenderer = std::vector
        <std::vector<std::vector<std::shared_ptr<Renderer>>>>();

    PassConstants mainPassCB;
    PassConstants shadowPassCB;


    ComPtr<ID3D12Fence> primeFence;
    ComPtr<ID3D12Fence> sharedFence;
    UINT64 sharedFenceValue = 0;

    std::vector<std::shared_ptr<SplitFrameResource>> frameResources = std::vector<std::shared_ptr<
        SplitFrameResource>>();
    std::shared_ptr<SplitFrameResource> currentFrameResource = nullptr;
    UINT currentFrameResourceIndex = 0;

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


    UINT pathMapShow = 0;
    //off, shadowMap, ssaoMap
    const UINT maxPathMap = 3;
    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;
};
