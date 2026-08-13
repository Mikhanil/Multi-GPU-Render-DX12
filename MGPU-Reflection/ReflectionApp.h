#pragma once

#include <array>

#include "BakedCubeMapRenderTarget.h"
#include "d3dApp.h"
#include "GDeviceFactory.h"
#include "GraphicPSO.h"
#include "Light.h"
#include "RenderModeFactory.h"
#include "ShadowMap.h"
#include "FrameResource.h"
#include "SSAA.h"
#include "SSAO.h"
#include "GCrossAdapterResource.h"
#include "CubeMapRenderTarget.h"
#include "Scene.h"

using namespace DirectX::SimpleMath;
using namespace PEPEngine;
using namespace Graphics;


class HybridCubeMapApp :
    public Common::D3DApp
{
public:
    HybridCubeMapApp(HINSTANCE hInstance);

    ~HybridCubeMapApp() override;;

    bool Initialize() override;


    int Run() override;

protected:
    void inline Update(const GameTimer& gt) override;
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
    void inline PopulateDrawQuadCommand(const std::shared_ptr<GCommandList>& cmdList, const GTexture& renderTarget,
                                        const GDescriptor* rtvMemory, UINT
                                        offsetRTV);
    void inline PopulateCopyResource(const std::shared_ptr<GCommandList>& cmdList, const GResource& srcResource,
                                     const GResource& dstResource);

    void Draw(const GameTimer& gt) override;

    void OnResize() override;

    bool InitMainWindow() override;;

private:
    void Flush() override;
    void inline InitDevices();
    void inline InitFrameResource();
    void inline InitRootSignature();
    void inline InitPipeLineResource();
    void inline InitRenderPaths();

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

    std::vector<std::shared_ptr<GDevice>> devices = std::vector<std::shared_ptr<GDevice>>();

    std::vector<std::unique_ptr<Common::Scene>> scenes;

    std::shared_ptr<GRootSignature> primeDeviceSignature;
    std::shared_ptr<GRootSignature> ssaoPrimeRootSignature;
    std::shared_ptr<GRootSignature> ssaoSecondRootSignature;
    std::shared_ptr<GRootSignature> secondDeviceSignature;

    RenderModeFactory primePipelineResources;
    RenderModeFactory secondPipelineResources;

    std::shared_ptr<GraphicPSO> shadowMapPSOSecondDevice;
    std::shared_ptr<GCrossAdapterResource> crossAdapterShadowMap;
    std::array<std::shared_ptr<GCrossAdapterResource>, 6> crossAdapterCubeMaps;

    std::shared_ptr<ShadowMap> shadowPathSecondDevice;
    std::shared_ptr<ShadowMap> cubeMapSecondDevice;

    std::shared_ptr<ShadowMap> shadowPathPrimeDevice;
    std::shared_ptr<ShadowMap> cubeMapPrimeDevice;
    GDescriptor primeCopyCubeMapSRV;


    std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout{};


    std::shared_ptr<SSAO> ambientPrimePath;
    std::shared_ptr<SSAA> antiAliasingPrimePath;


    PassConstants mainPassCB;
    PassConstants shadowPassCB;


    ComPtr<ID3D12Fence> primeFence;
    ComPtr<ID3D12Fence> secondFence;
    UINT64 sharedFenceValue = 0;

    std::vector<std::shared_ptr<FrameResource>> frameResources = std::vector<std::shared_ptr<
        FrameResource>>();
    std::shared_ptr<FrameResource> currentFrameResource = nullptr;
    std::atomic<UINT> currentFrameResourceIndex = 0;


    float mLightNearZ = 0.0f;
    float mLightFarZ = 0.0f;
    Vector3 mLightPosW;
    Matrix mLightView = Matrix::Identity;
    Matrix mLightProj = Matrix::Identity;
    Matrix mShadowTransform = Matrix::Identity;

    // new
    static constexpr UINT DynamicCubeMapFaceCount = 6;
    static constexpr UINT DynamicCubeMapFirstPassIndex = 2;
    static constexpr UINT DynamicCubeMapSize = 1024;

    std::shared_ptr<CubeMapRenderTarget> dynamicCubeMap = nullptr;
    std::shared_ptr<CubeMapRenderTarget> dynamicCubeMapSecond = nullptr;
    
    std::shared_ptr<BakedCubeMapRenderTarget> bakedCubeMapSecond = nullptr;
    std::atomic<bool> isBaked = false;
    static constexpr UINT BakedCubeMapFaceCount = 6;
    static constexpr UINT BakedCubeMapFirstPassIndex = 2;
    static constexpr UINT BakedCubeMapSize = 1024;
    
    GTexture dynamicCubeMapFaceColor; //= nullptr;
    GTexture dynamicCubeMapFaceDepth; //= nullptr;
    
    GDescriptor dynamicCubeMapFaceSrv;
    GDescriptor dynamicCubeMapFaceRtv;
    GDescriptor dynamicCubeMapFaceDsv;
    
    void CreateDynamicTextures(const GraphicsAdapter adapter);
    
    // end of new


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
    void inline PopulateDynamicCubeMapCommands(GraphicsAdapter adapter, const std::shared_ptr<GCommandList>& cmdList);
    std::array<PassConstants, DynamicCubeMapFaceCount> inline BuildCubeFacePassCBs(const Vector3& center) const;
    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;
};

using ReflectionApp = HybridCubeMapApp;
