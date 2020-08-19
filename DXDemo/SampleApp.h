#pragma once

#include "Camera.h"
#include "d3dApp.h"
#include "FrameResource.h"
#include "GMemory.h"
#include "GraphicPSO.h"
#include "Light.h"
#include "GRootSignature.h"
#include "GShader.h"
#include "ShadowMap.h"
#include "Ssao.h"
#include "GameTimer.h"
#include "Keyboard.h"
#include "Model.h"
#include "Mouse.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace PackedVector;

namespace DXLib
{
	class SampleApp : public D3DApp
	{
	public:
		SampleApp(HINSTANCE hInstance);
		SampleApp(const SampleApp& rhs) = delete;
		SampleApp& operator=(const SampleApp& rhs) = delete;
		~SampleApp();	

		void GeneratedMipMap();
		void BuildSsaoRootSignature();
		
		bool Initialize() override;


		Keyboard* GetKeyboard();

		Mouse* GetMouse();

		Camera* GetMainCamera() const;



		bool isDebug = false;
		UINT showPathMap = 0;
		//off, shadowMap, ssaoMap
		const UINT maxPathMap = 3;

	private:

		LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;
		
		void OnResize() override;
		void UpdateShadowTransform(const GameTimer& gt);
		void UpdateShadowPassCB(const GameTimer& gt);
		void UpdateSsaoCB(const GameTimer& gt);
		void LoadDoomSlayerTexture(std::shared_ptr<GCommandList> cmdList);
		void LoadStudyTexture(std::shared_ptr<GCommandList> cmdList);
		void LoadNanosuitTexture(std::shared_ptr<GCommandList> cmdList);
		void LoadAtlasTexture(std::shared_ptr<GCommandList> cmdList);
		void LoadPBodyTexture(std::shared_ptr<GCommandList> cmdList);
		void LoadGolemTexture(std::shared_ptr<GCommandList> cmdList);
		void LoadBinTextures(std::shared_ptr<GCommandList> cmdList, const UINT8* assetData);
		void LoadSquidModels(std::shared_ptr<GCommandList> cmdLit);
		void BuildTexturesHeap();
		void Update(const GameTimer& gt) override;
		void UpdateMaterial(const GameTimer& gt);
		void DrawSceneToShadowMap(std::shared_ptr<GCommandList> cmdList);
		void DrawNormalsAndDepth(std::shared_ptr<GCommandList> cmdList);
		void Draw(const GameTimer& gt) override;

		void UpdateGameObjects(const GameTimer& gt);
		void UpdateMainPassCB(const GameTimer& gt);


		void LoadTextures( std::shared_ptr<GCommandList> cmdList);
		void BuildRootSignature();
		void BuildShadersAndInputLayout();
		void BuildShapeGeometry();
		void BuildPSOs();
		void BuildFrameResources();
		void BuildMaterials();
		void BuildGameObjects();
		static void DrawGameObjects(std::shared_ptr<GCommandList> cmdList, const custom_vector<GameObject*>& ritems);
		static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();
		void SortGO();
		
	private:

		Keyboard keyboard;
		Mouse mouse;
		std::unique_ptr<Camera> camera = nullptr;

		custom_vector<std::unique_ptr<FrameResource>> frameResources = DXAllocator::CreateVector<std::unique_ptr<FrameResource>>();
		
		FrameResource* currentFrameResource = nullptr;
		int currentFrameResourceIndex = 0;


		std::shared_ptr<GBuffer> squidVertexBuffer;
		std::shared_ptr<GBuffer> squidIndexBuffer;
		
		std::unique_ptr<GRootSignature> rootSignature = nullptr;
		std::unique_ptr<GRootSignature> ssaoRootSignature = nullptr;
		std::unique_ptr<Ssao> mSsao;

		D2D1_RECT_F fpsRect = D2D1::RectF(0.0f, 0, 800, 300);

		GMemory srvHeap;

		custom_unordered_map<std::string, std::unique_ptr<MeshGeometry>> meshes = DXAllocator::CreateUnorderedMap<std::string, std::unique_ptr<MeshGeometry>>();
		custom_unordered_map<std::wstring, std::unique_ptr<Material>> materials = DXAllocator::CreateUnorderedMap<std::wstring, std::unique_ptr<Material>>();
		custom_unordered_map<std::string, std::unique_ptr<GShader>> shaders = DXAllocator::CreateUnorderedMap<std::string, std::unique_ptr<GShader>>();
		custom_unordered_map<std::wstring, std::shared_ptr<GTexture>> textures = DXAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<GTexture>>();
		custom_unordered_map<std::wstring, std::shared_ptr<Model>> models = DXAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<Model>>();
		custom_vector<std::shared_ptr<Mesh>> meshesModel = DXAllocator::CreateVector<std::shared_ptr<Mesh>>();
		custom_unordered_map<PsoType::Type, std::unique_ptr<GraphicPSO>> psos = DXAllocator::CreateUnorderedMap<PsoType::Type, std::unique_ptr<GraphicPSO>>();
		custom_vector<Light*> lights = DXAllocator::CreateVector<Light*>();
		

		custom_vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout = DXAllocator::CreateVector<D3D12_INPUT_ELEMENT_DESC>();
		custom_vector<D3D12_INPUT_ELEMENT_DESC> treeSpriteInputLayout = DXAllocator::CreateVector<D3D12_INPUT_ELEMENT_DESC>();

		custom_vector<std::unique_ptr<GameObject>> gameObjects = DXAllocator::CreateVector<std::unique_ptr<GameObject>>();
		
		custom_vector<custom_vector<GameObject*>> typedGameObjects = DXAllocator::CreateVector<custom_vector<GameObject*>>();

		GameObject* player;

		PassConstants mainPassCB;
		PassConstants mShadowPassCB;

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


		std::unique_ptr<ShadowMap> mShadowMap;

		BoundingSphere mSceneBounds;
	};

	
}
