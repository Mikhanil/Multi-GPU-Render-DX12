#pragma once
#include "AssetsLoader.h"
#include "d3dApp.h"
#include "Renderer.h"
#include "RenderModeFactory.h"
#include "ShadowMap.h"
#include "SSAA.h"
#include "SSAO.h"
#include "FrameResource.h"
#include "GDeviceFactory.h"
#include "Light.h"

class MultiGPUUIApp :
    public Common::D3DApp
{
public:
	MultiGPUUIApp(HINSTANCE hInstance);
	~MultiGPUUIApp();
	
	bool Initialize() override;;
	


protected:
	void Update(const GameTimer& gt) override;
	void PopulateShadowMapCommands(std::shared_ptr<GCommandList> cmdList);;
	void PopulateNormalMapCommands(std::shared_ptr<GCommandList> cmdList);
	void PopulateAmbientMapCommands(std::shared_ptr<GCommandList> cmdList);
	void PopulateForwardPathCommands(std::shared_ptr<GCommandList> cmdList);
	void PopulateDrawCommands(std::shared_ptr<GCommandList> cmdList,
	                          RenderMode::Mode type);
	void PopulateDrawQuadCommand(std::shared_ptr<GCommandList> cmdList, GTexture& renderTarget, GDescriptor* rtvMemory,
	                             UINT offsetRTV);
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
	void CalculateFrameStats();
	void LogWriting();
	void UpdateMaterials();
	void UpdateShadowTransform(const GameTimer& gt);
	void UpdateShadowPassCB(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateSsaoCB(const GameTimer& gt);
	bool InitMainWindow() override;
	void OnResize() override;
	void Flush() override;
	LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

	std::shared_ptr<GDevice> primeDevice;
	std::shared_ptr<GDevice> secondDevice;

	LockThreadQueue<std::wstring> logQueue{};
	UINT64 primeGPURenderingTime = 0;
	UINT64 secondGPURenderingTime = 0;

	D3D12_VIEWPORT fullViewport{};
	D3D12_RECT fullRect;

	std::shared_ptr<AssetsLoader> assets;

	custom_unordered_map < std::wstring, std::shared_ptr<GModel>> models = MemoryAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<GModel>>();
	std::shared_ptr<GRootSignature> primeDeviceSignature;
	std::shared_ptr<GRootSignature> ssaoPrimeRootSignature;
	std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout{};
	GDescriptor srvTexturesMemory;
	GDescriptor imguiSRVMemory;
	RenderModeFactory defaultPrimePipelineResources;

	std::shared_ptr<ShadowMap> shadowPath;
	std::shared_ptr<SSAO> ambientPrimePath;
	std::shared_ptr<SSAA> antiAliasingPrimePath;

	custom_vector<std::shared_ptr<GameObject>> gameObjects = MemoryAllocator::CreateVector<std::shared_ptr<GameObject>>();

	custom_vector<custom_vector<std::shared_ptr<Renderer>>> typedRenderer = MemoryAllocator::CreateVector<custom_vector<std::shared_ptr<Renderer>>>();

	PassConstants mainPassCB;
	PassConstants shadowPassCB;

	custom_vector<std::shared_ptr<FrameResource>> frameResources = MemoryAllocator::CreateVector<std::shared_ptr<FrameResource>>();
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
	
};

