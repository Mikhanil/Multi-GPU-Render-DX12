#pragma once
#include "AssetsLoader.h"
#include "d3dApp.h"
#include "Renderer.h"
#include "RenderModeFactory.h"
#include "ShadowMap.h"
#include "SSAA.h"
#include "SharedSSAO.h"
#include "FrameResource.h"
#include "GCrossAdapterResource.h"
#include "GDeviceFactory.h"
#include "Light.h"
#include "ParticleEmitter.h"
#include "SharedHBAO.h"
#include "Transform.h"
#include "UILayer.h"
#include "Services/FileQueueWriter.h"
#include "Services/BenchmarkService.h"

class HybridSSAOApp final :
    public Common::D3DApp
{
public:
    HybridSSAOApp(HINSTANCE hInstance);
    ~HybridSSAOApp() override;

    bool Initialize() override;

    int Run() override;

    USHORT GetBlurCount() const { return blurCount; }
    void SetBlurPassCount(const USHORT blurCount) { this->blurCount = blurCount; }
    void SwitchDevice();
    void ChangeAOMethod();
    void ResetCamera() const;
protected:
    void Update(const GameTimer& gt) override;
    void PopulateShadowMapCommands(const std::shared_ptr<GCommandList>& cmdList);;
    void PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList);
    void PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList) const;
    void PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList);
    void PopulateDrawCommands(const std::shared_ptr<GCommandList>& cmdList,
                              RenderMode type) const;
    void PopulateInitRenderTarget(const std::shared_ptr<GCommandList>& cmdList, const GTexture& renderTarget, const GDescriptor* rtvMemory,
                                  UINT offsetRTV) const;
    void PopulateDrawFullQuadTexture(const std::shared_ptr<GCommandList>& cmdList,
                                     const GDescriptor* renderTextureSRVMemory, UINT renderTextureMemoryOffset,
                                     const GraphicPSO& pso) const;
    void PopulateDebugCommands(const std::shared_ptr<GCommandList>& cmdList);
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
    void OnApplicationExit();
    void UpdateMaterials() const;
    void UpdateShadowTransform(const GameTimer& gt);
    void UpdateShadowPassCB(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateSsaoCB(const GameTimer& gt) const;
    bool InitMainWindow() override;
    void OnResize() override;
    void Flush() override;
    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

    
    FileQueueWriter debugLogger;
    BenchmarkService benchmark;
    std::shared_ptr<GDevice> primeDevice;
    std::shared_ptr<GDevice> secondDevice;

    UINT64 primeGPURenderingTime = 0;
    UINT64 secondGPURenderingTime = 0;

    D3D12_VIEWPORT fullViewport{};
    D3D12_RECT fullRect;

    std::shared_ptr<AssetsLoader> assets;

    custom_unordered_map<std::wstring, std::shared_ptr<GModel>> models = MemoryAllocator::CreateUnorderedMap<
        std::wstring, std::shared_ptr<GModel>>();
    std::shared_ptr<GRootSignature> primeDeviceSignature;
    std::shared_ptr<GRootSignature> secondDeviceSignature;
    std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout{};
    GDescriptor srvTexturesMemory;
    RenderModeFactory defaultPrimePipelineResources;

    bool IsStop = false;

    bool IsUsingSharedSSAO = false;
    bool IsUseHBAO = false;

    UINT pathMapShow = 0;
    //off, shadowMap, ssaoMap
    const UINT maxPathMap = 3;


    std::shared_ptr<UILayer> UIPath;
    std::shared_ptr<ShadowMap> shadowPath;
    std::shared_ptr<SharedSSAO> ssaoPass;
    std::shared_ptr<SharedHBAO> hbaoPass;
    std::shared_ptr<SSAA> antiAliasingPrimePath;

    custom_vector<std::shared_ptr<GameObject>> gameObjects = MemoryAllocator::CreateVector<std::shared_ptr<
        GameObject>>();

    custom_vector<custom_vector<std::shared_ptr<Renderer>>> typedRenderer = MemoryAllocator::CreateVector<custom_vector<
        std::shared_ptr<Renderer>>>();

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
    int blurCount = 3;
    Matrix RotaterSaveMatrix;
    Matrix CameraSaveMatrix;
    std::vector<ParticleEmitter*> emitters;
};
