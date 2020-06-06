#pragma once

#include "d3dApp.h"
#include "MathHelper.h"
#include "DirectXBuffers.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
#include <map>
#include "ModelRenderer.h"
#include "Camera.h"
#include "Light.h"
#include "Shader.h"
#include "PSO.h"
#include "RootSignature.h"


using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace PackedVector;



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
    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    void AnimatedMaterial(const GameTimer& gt);
    virtual void Update(const GameTimer& gt)override;
    void UpdateMaterial(const GameTimer& gt);
    void RenderUI();
    virtual void Draw(const GameTimer& gt)override;

    void UpdateGameObjects(const GameTimer& gt);
    void UpdateGlobalCB(const GameTimer& gt);

    void LoadTextures();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildRoomGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildLandGeometry();
    void BuildTreesGeometry();
    void BuildMaterials();
    void BuildGameObjects();
    static void DrawGameObjects(ID3D12GraphicsCommandList* cmdList, const std::vector<GameObject*>& ritems);

    void SortGO();

private:

    static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();
	
    std::unique_ptr<RootSignature> rootSignature = nullptr;
	
    std::vector<std::unique_ptr<FrameResource>> frameResources;
    FrameResource* currentFrameResource = nullptr;
    int currentFrameResourceIndex = 0;

    
    D2D1_RECT_F fpsRect = D2D1::RectF(0.0f, 0, 800, 300);


    ComPtr<ID3D12DescriptorHeap> textureSRVHeap = nullptr;
	
    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> meshes;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials;
    std::unordered_map<std::string, std::unique_ptr<Shader>> shaders;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
    std::unordered_map<std::string, std::unique_ptr<ModelMesh>> modelMeshes;
    std::unordered_map<PsoType::Type, std::unique_ptr<PSO>> psos;
    std::vector<Light*> lights;
    std::unique_ptr<Camera> camera = nullptr;
	
    ComPtr<ID3D12PipelineState> opaquePSO = nullptr;
	
    std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout;

    std::vector<D3D12_INPUT_ELEMENT_DESC> treeSpriteInputLayout;
	
    std::vector<std::unique_ptr<GameObject>> gameObjects;

    std::vector<GameObject*> typedGameObjects[ PsoType::Count ];
	
    GameObject* player;
	
    PassConstants mainPassCB;
    PassConstants reflectedPassCB;

    UINT passCbvOffset = 0;

    bool isWireframe = false;   
};
