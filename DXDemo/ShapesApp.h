#pragma once

#include "d3dApp.h"
#include "MathHelper.h"
#include "DirectXBuffers.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
#include "ModelRenderer.h"
#include "Camera.h"
#include "GMemory.h"
#include "Light.h"
#include "Shader.h"
#include "GraphicPSO.h"
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

	

		void GeneratedMipMap();
		void BuildSsaoRootSignature();
		bool Initialize() override;




	private:
		void OnResize() override;
		void AnimatedMaterial(const GameTimer& gt);
		void UpdateShadowTransform(const GameTimer& gt);
		void UpdateShadowPassCB(const GameTimer& gt);
		void UpdateSsaoCB(const GameTimer& gt);
		void LoadDoomSlayerTexture(std::shared_ptr<GCommandList> cmdList);
		void LoadStudyTexture(std::shared_ptr<GCommandList> cmdList);
		void LoadNanosuitTexture(std::shared_ptr<GCommandList> cmdList);
		void LoadAtlasTexture(std::shared_ptr<GCommandList> cmdList);
		void LoadPBodyTexture(std::shared_ptr<GCommandList> cmdList);
		void LoadGolemTexture(std::shared_ptr<GCommandList> cmdList);
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
		void BuildRoomGeometry();
		void BuildPSOs();
		void BuildFrameResources();
		void BuildLandGeometry();
		void BuildTreesGeometry();
		void BuildMaterials();
		void BuildGameObjects();
		static void DrawGameObjects(std::shared_ptr<GCommandList> cmdList, const custom_vector<GameObject*>& ritems);
		static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();
		void SortGO();


	private:


		custom_vector<std::unique_ptr<FrameResource>> frameResources = DXAllocator::CreateVector<std::unique_ptr<FrameResource>>();
		
		FrameResource* currentFrameResource = nullptr;
		int currentFrameResourceIndex = 0;


		std::unique_ptr<RootSignature> rootSignature = nullptr;
		std::unique_ptr<RootSignature> ssaoRootSignature = nullptr;
		std::unique_ptr<Ssao> mSsao;

		D2D1_RECT_F fpsRect = D2D1::RectF(0.0f, 0, 800, 300);

		GMemory srvHeap;

		custom_unordered_map<std::string, std::unique_ptr<MeshGeometry>> meshes = DXAllocator::CreateUnorderedMap<std::string, std::unique_ptr<MeshGeometry>>();
		custom_unordered_map<std::string, std::unique_ptr<Material>> materials = DXAllocator::CreateUnorderedMap<std::string, std::unique_ptr<Material>>();
		custom_unordered_map<std::string, std::unique_ptr<Shader>> shaders = DXAllocator::CreateUnorderedMap<std::string, std::unique_ptr<Shader>>();
		custom_unordered_map<std::wstring, std::shared_ptr<Texture>> textures = DXAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<Texture>>();
		custom_unordered_map<std::string, std::unique_ptr<ModelMesh>> modelMeshes = DXAllocator::CreateUnorderedMap<std::string, std::unique_ptr<ModelMesh>>();
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
