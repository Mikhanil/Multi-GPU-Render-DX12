#pragma once

#include "d3dApp.h"
#include "MathHelper.h"
#include "DirectXBuffers.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
#include <map>
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

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    virtual void OnKeyboardKeyUp(WPARAM key) override;
    void UpdateCamera(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);

    void LoadTextures();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems) const;

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
    //std::unordered_map<PSOType, ComPtr<ID3D12PipelineState>> psos;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;


	
    ComPtr<ID3D12PipelineState> opaquePSO = nullptr;
	
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

    // List of all the render items.
    std::vector<std::unique_ptr<RenderItem>> renderItems;


    // Render items divided by PSO.
    std::map<MaterialType, std::vector<RenderItem*>> typedRenderItems;
	

    PassConstants mainPassCB;

    UINT passCbvOffset = 0;

    bool isWireframe = false;

    XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f * XM_PI;
    float mPhi = 0.2f * XM_PI;
    float mRadius = 15.0f;


    float mSunTheta = 1.25f * XM_PI;
    float mSunPhi = XM_PIDIV4;
	
    POINT mLastMousePos;
};
