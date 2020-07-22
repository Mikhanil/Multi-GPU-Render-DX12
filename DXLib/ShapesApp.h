#pragma once

#include "d3dApp.h"
#include "MathHelper.h"
#include "DirectXBuffers.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
#include "ModelRenderer.h"
#include "Camera.h"
#include "Light.h"
#include "Shader.h"
#include "PSO.h"
#include "RootSignature.h"
#include "ShadowMap.h"
#include "Ssao.h"


using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace PackedVector;

namespace DXLib
{
	class ShapesApp : public D3DApp
	{
	public:
		ShapesApp(HINSTANCE hInstance);
		ShapesApp(const ShapesApp& rhs) = delete;
		ShapesApp& operator=(const ShapesApp& rhs) = delete;
		~ShapesApp();

		Keyboard* GetKeyboard();

		Mouse* GetMouse();

		Camera* GetMainCamera() const;

		void GeneratedMipMap();
		void BuildSsaoRootSignature();
		bool Initialize() override;


		bool ShowAmbiantMap = false;
		bool computeSsao = false;


	private:
		void OnResize() override;
		void AnimatedMaterial(const GameTimer& gt);
		void UpdateShadowTransform(const GameTimer& gt);
		void UpdateShadowPassCB(const GameTimer& gt);
		void UpdateSsaoCB(const GameTimer& gt);
		void LoadDoomSlayerTexture();
		void LoadStudyTexture();
		void LoadNanosuitTexture();
		void LoadAtlasTexture();
		void LoadPBodyTexture();
		void LoadGolemTexture();
		void BuildTexturesHeap();
		void Update(const GameTimer& gt) override;
		void UpdateMaterial(const GameTimer& gt);
		void DrawSceneToShadowMap(ID3D12GraphicsCommandList* cmdList);
		void DrawNormalsAndDepth(ID3D12GraphicsCommandList* cmdList);
		void Draw(const GameTimer& gt) override;

		void UpdateGameObjects(const GameTimer& gt);
		void UpdateMainPassCB(const GameTimer& gt);


		void LoadTextures(ID3D12CommandQueue* queue, ID3D12GraphicsCommandList2* cmdList);
		void BuildRootSignature();
		void BuildShadersAndInputLayout();
		void BuildShapeGeometry();
		void BuildRoomGeometry();
		void BuildPSOs();
		void BuildFrameResources();
		void BuildLandGeometry();
		void BuildTreesGeometry();
		void BuildMaterials();
		void BuildGameObjects();
		static void DrawGameObjects(ID3D12GraphicsCommandList* cmdList, const custom_vector<GameObject*>& ritems);
		static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();
		void SortGO();

		CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv(int index) const;

		CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrv(int index) const;

		CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsv(int index) const;

		CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtv(int index) const;


	private:


		custom_vector<std::unique_ptr<FrameResource>> frameResources = DXAllocator::CreateVector<std::unique_ptr<FrameResource>>();
		
		FrameResource* currentFrameResource = nullptr;
		int currentFrameResourceIndex = 0;


		std::unique_ptr<RootSignature> rootSignature = nullptr;
		ComPtr<ID3D12RootSignature> ssaoRootSignature = nullptr;
		std::unique_ptr<Ssao> mSsao;

		D2D1_RECT_F fpsRect = D2D1::RectF(0.0f, 0, 800, 300);

		ComPtr<ID3D12DescriptorHeap> rtvHeap;
		ComPtr<ID3D12DescriptorHeap> dsvHeap;
		ComPtr<ID3D12DescriptorHeap> srvHeap;

		custom_unordered_map<std::string, std::unique_ptr<MeshGeometry>> meshes = DXAllocator::CreateUnorderedMap<std::string, std::unique_ptr<MeshGeometry>>();
		custom_unordered_map<std::string, std::unique_ptr<Material>> materials = DXAllocator::CreateUnorderedMap<std::string, std::unique_ptr<Material>>();
		custom_unordered_map<std::string, std::unique_ptr<Shader>> shaders = DXAllocator::CreateUnorderedMap<std::string, std::unique_ptr<Shader>>();
		custom_unordered_map<std::string, std::unique_ptr<Texture>> textures = DXAllocator::CreateUnorderedMap<std::string, std::unique_ptr<Texture>>();
		custom_unordered_map<std::string, std::unique_ptr<ModelMesh>> modelMeshes = DXAllocator::CreateUnorderedMap<std::string, std::unique_ptr<ModelMesh>>();
		custom_unordered_map<PsoType::Type, std::unique_ptr<PSO>> psos = DXAllocator::CreateUnorderedMap<PsoType::Type, std::unique_ptr<PSO>>();
		custom_vector<Light*> lights = DXAllocator::CreateVector<Light*>();
		std::unique_ptr<Camera> camera = nullptr;

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

		UINT mShadowMapHeapIndex = 0;


		float mLightRotationAngle = 0.0f;
		Vector3 mBaseLightDirections[3] = {
			Vector3(0.57735f, -0.57735f, 0.57735f),
			Vector3(-0.57735f, -0.57735f, 0.57735f),
			Vector3(0.0f, -0.707f, -0.707f)
		};
		Vector3 mRotatedLightDirections[3];


		std::unique_ptr<ShadowMap> mShadowMap;

		BoundingSphere mSceneBounds;

		UINT passCbvOffset = 0;

		bool isWireframe = false;
		UINT mSsaoHeapIndexStart;
		UINT mSsaoAmbientMapIndex;
	};
}
