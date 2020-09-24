#pragma once
#include "AssetsLoader.h"
#include "Camera.h"
#include "d3dApp.h"
#include "GameObject.h"
#include "SplitFrameResource.h"
#include "GCrossAdapterResource.h"
#include "GMemory.h"
#include "GraphicPSO.h"
#include "GTexture.h"
#include "KeyboardDevice.h"
#include "Light.h"
#include "Mousepad.h"
#include "ShaderFactory.h"
#include "ShadowMap.h"
#include "SSAA.h"
#include "Ssao.h"

class MultiSplitGPU :
    public DXLib::D3DApp
{
public:
	MultiSplitGPU(HINSTANCE hInstance);
	

	bool Initialize() override;
	
protected:
	void Update(const GameTimer& gt) override;
	void UpdateShadowTransform(const GameTimer& gt);
	void UpdateShadowPassCB(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateSsaoCB(const GameTimer& gt);
	void Draw(const GameTimer& gt) override;

	void OnResize() override;

	bool InitMainWindow() override;;
	
private:
	void InitDevices();
	
	
	
	void InitFrameResource();
	void InitRootSignature();
	void InitPipeLineResource();
	void BuildMaterials();
	void InitSRVMemoryAndMaterials();
	void InitRenderPaths();
	void LoadStudyTexture();
	void LoadModels();

	UINT backBufferIndex = 0;
	DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	static const DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;
	static const DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	static const DXGI_FORMAT DepthMapFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	
	D3D12_VIEWPORT primeViewport{};
	D3D12_RECT primeRect{};
	D3D12_RECT secondRect{};
	D3D12_BOX copyRegionBox;

	
	custom_vector<std::shared_ptr<GDevice>> devices = MemoryAllocator::CreateVector<std::shared_ptr<GDevice>>();
	custom_vector<GMemory> srvTexturesMemory = MemoryAllocator::CreateVector<GMemory>();
	
	custom_vector<AssetsLoader> assets = MemoryAllocator::CreateVector<AssetsLoader>();
	
	custom_vector<std::shared_ptr<GRootSignature>> rootSignatures = MemoryAllocator::CreateVector<std::shared_ptr<GRootSignature>>();

	custom_vector<std::shared_ptr<GRootSignature>> ssaoRootSignatures = MemoryAllocator::CreateVector<std::shared_ptr<GRootSignature>>();

	custom_vector< ShaderFactory> defaultPipelineResources = MemoryAllocator::CreateVector<ShaderFactory>();	
	

	custom_vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout = MemoryAllocator::CreateVector<D3D12_INPUT_ELEMENT_DESC>();

	custom_vector<std::shared_ptr<ShadowMap>> shadowPaths = MemoryAllocator::CreateVector< std::shared_ptr<ShadowMap>>();
	custom_vector<std::shared_ptr<Ssao>> ambientPaths = MemoryAllocator::CreateVector< std::shared_ptr<Ssao>>();
	custom_vector<std::shared_ptr<SSAA>> antiAliasingPaths = MemoryAllocator::CreateVector< std::shared_ptr<SSAA>>();
	
	custom_vector<std::shared_ptr<GameObject>> gameObjects = MemoryAllocator::CreateVector<std::shared_ptr<GameObject>>();

	custom_vector<custom_vector<std::shared_ptr<GameObject>>> typedGameObjects = MemoryAllocator::CreateVector<custom_vector<std::shared_ptr<GameObject>>>();

	PassConstants mainPassCB;
	PassConstants shadowPassCB;

	
	ComPtr<ID3D12Fence> primeFence;
	ComPtr<ID3D12Fence> sharedFence;
	UINT64 sharedFenceValue = 0;
	
	custom_vector<std::shared_ptr<SplitFrameResource>> frameResources = MemoryAllocator::CreateVector<std::shared_ptr<SplitFrameResource>>();

	std::shared_ptr<SplitFrameResource> currentFrameResource = nullptr;
	
	int currentFrameResourceIndex = 0;

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

