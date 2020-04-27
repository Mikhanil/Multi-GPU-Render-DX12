#pragma once

#include "d3dApp.h"
#include "MathHelper.h"
#include "DirectXBuffers.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
#include <map>
#include "ModelRenderer.h"
#include "Camera.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;



class ShapesApp : public D3DApp
{
public:
    ShapesApp(HINSTANCE hInstance);
    ShapesApp(const ShapesApp& rhs) = delete;
    ShapesApp& operator=(const ShapesApp& rhs) = delete;
    ~ShapesApp();

    Keyboard* GetKeyboard() 
    {
        return &keyboard;
    }

    Mouse* GetMouse() 
    {
        return  &mouse;
    }

    Camera* GetMainCamera()
    {
        return camera.get();
    }
	
    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    void UpdateGameObjects(const GameTimer& gt);
    void UpdateGlobalCB(const GameTimer& gt);

    void LoadTextures();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildGameObjects();
    static void DrawGameObjects(ID3D12GraphicsCommandList* cmdList, const std::vector<GameObject*>& ritems);

    void SortGOByMaterial();

private:
        	
    std::vector<std::unique_ptr<FrameResource>> frameResources;
    FrameResource* currentFrameResource = nullptr;
    int currentFrameResourceIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandListAllocator;
	
    ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	
    ComPtr<ID3D12DescriptorHeap> shaderTextureViewDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> meshes;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials;
    std::unordered_map<std::string, std::unique_ptr<Shader>> shaders;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
    std::unordered_map<std::string, std::unique_ptr<ModelMesh>> modelMeshes;
    std::vector<Light*> lights;
    std::unique_ptr<Camera> camera = nullptr;
	
    ComPtr<ID3D12PipelineState> opaquePSO = nullptr;
	
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

    // List of all the render items.
    std::vector<std::unique_ptr<GameObject>> gameObjects;


    // Render items divided by PSO.
    std::map<MaterialType, std::vector<GameObject*>> typedGameObjects;
	

    PassConstants mainPassCB;

    UINT passCbvOffset = 0;

    bool isWireframe = false;

   
};
