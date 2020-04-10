#include "d3dApp.h"
#include "MathHelper.h"
#include "DirectXBuffers.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT4 Color;    
};

struct ObjectConstants
{
    XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
    XMFLOAT4 pulseColor = XMFLOAT4(DirectX::Colors::Blue);
    UINT time;
};

class BoxDraw3DApp : public D3DApp
{
public:
    BoxDraw3DApp(HINSTANCE hInstance);
    BoxDraw3DApp(const BoxDraw3DApp& rhs) = delete;
    BoxDraw3DApp& operator=(const BoxDraw3DApp& rhs) = delete;
    ~BoxDraw3DApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;


    XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f * XM_PI;
    float mPhi = XM_PIDIV4;
    float mRadius = 5.0f;
    POINT lastMousePosition;
    virtual void Update(const GameTimer& gt)override;
	
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    ComPtr<ID3D12DescriptorHeap> constantBufferViewHeap = nullptr;
    void CreateConstantBufferDescriptorHeap();

    std::unique_ptr<ConstantBuffer<ObjectConstants>> constantBuffer = nullptr;
    void CreateConstantBuffer();

    ComPtr<ID3D12RootSignature> rootSignature = nullptr;
    void CreateRootSignature();

    ComPtr<ID3DBlob> vertexShaderBlob = nullptr;
    ComPtr<ID3DBlob> pixelShaderBlob = nullptr;
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
    void CreateShadersAndInputLayout();

    std::unique_ptr<MeshGeometry> boxMesh = nullptr;
    void CreateBoxGeometry();


    ComPtr<ID3D12PipelineState> pipelineStateObject = nullptr;
    void CreatePSO();
};
