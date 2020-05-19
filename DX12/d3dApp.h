#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "d3dUtil.h"
#include "GameTimer.h"
#include "Keyboard.h"
#include "Mouse.h"

// Link necessary d3d12 libraries.
#pragma comment(lib,"d3dcompiler.lib")

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib,"dwrite.lib")
#pragma comment(lib,"d3d11.lib")

#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "RuntimeObject.lib")


#include "d2d1_3.h"
#include "dwrite.h"

#include "d3d11on12.h"
#include "d3d11.h"

using Microsoft::WRL::ComPtr;
class D3DApp
{
protected:

    D3DApp(HINSTANCE hInstance);
    D3DApp(const D3DApp& rhs) = delete;
    D3DApp& operator=(const D3DApp& rhs) = delete;
    virtual ~D3DApp();

public:

    GameTimer* GetTimer()
    {
        return &timer;
    }
	
    static D3DApp* GetApp();

    HINSTANCE AppInst()const;
    HWND      MainWnd()const;
    float     AspectRatio()const;

    bool Get4xMsaaState()const;
    void Set4xMsaaState(bool value);

    int Run();

    virtual bool Initialize();
    virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:
    virtual void CreateRtvAndDsvDescriptorHeaps();
    virtual void OnResize();
    virtual void Update(const GameTimer& gt) = 0;
    virtual void Draw(const GameTimer& gt) = 0;

protected:

    std::wstring fpsStr;
    std::wstring mainWindowCaption = L"d3d App";
    D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_HARDWARE;
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    int clientWidth = 800;
    int clientHeight = 600;
	
    static D3DApp* instance;
    HINSTANCE appInstance = nullptr;
    HWND      mainWindow = nullptr;
    bool      isAppPaused = false;
    bool      isMinimized = false;
    bool      isMaximized = false;
    bool      isResizing = false;
    bool      isFullscreen = false;
	
    // Set true to use 4X MSAA (§4.1.8).  The default is false.
    bool      isM4xMsaa = false;    // 4X MSAA enabled
    UINT      m4xMsaaQuality = 0;      // quality level of 4X MSAA

    GameTimer timer;

    Keyboard keyboard;

    Mouse mouse;

  
	
	
    bool InitMainWindow();

    Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
    Microsoft::WRL::ComPtr<ID3D12Device> dxDevice;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    UINT64 currentFence = 0;
    bool InitDirect3D();

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueueDirect;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueueCompute;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> directCommandListAlloc;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> computeCommandListAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandListDirect;   
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandListCompute;   
    void CreateCommandObjects();

    static const int swapChainBufferCount = 2;
    int currBackBufferIndex = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> swapChainBuffers[swapChainBufferCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilBuffer;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> renderTargetViewHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> depthStencilViewHeap;
    D3D12_VIEWPORT screenViewport;
    D3D12_RECT scissorRect;
    UINT rtvDescriptorSize = 0;
    UINT dsvDescriptorSize = 0;
    UINT cbvSrvUavDescriptorSize = 0;

    ComPtr<ID3D11Device> d3d11Device;
    ComPtr<ID3D11On12Device> d3d11On12Device;
    ComPtr<ID3D11DeviceContext> d3d11DeviceContext;
    ComPtr<IDXGIDevice3> dxgiDevice;
    ComPtr<IDWriteFactory> d2dWriteFactory;
    ComPtr<IDWriteTextFormat> d2dTextFormat;
    ComPtr<IDWriteTextFormat> d2dTextFormatSans;
    ComPtr<ID2D1Factory3> d2dFactory;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<ID2D1DeviceContext> d2dContext;
    ComPtr<ID2D1SolidColorBrush> d2dBrush;
    ComPtr<IDWriteTextLayout> d2dTextLayout;
    ComPtr<ID3D11Resource> wrappedBackBuffers[swapChainBufferCount];
    ComPtr<IDXGISurface> d2dSurfaces[swapChainBufferCount];
    ComPtr<ID2D1Bitmap1> d2dRenderTargets[swapChainBufferCount];

    void InitializeD2D();

    void CreateSwapChain();

    void FlushCommandQueue();

    ID3D12Resource* GetCurrentBackBuffer()const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferView()const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView()const;

    void CalculateFrameStats();

    void LogAdapters();
    void LogAdapterOutputs(IDXGIAdapter* adapter);
    void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);
    void ExecuteCommandList() const;
    void ExecuteComputeCommandList() const;
    void FlushComputeCommandQueue();
    void ResetCommandList(ID3D12PipelineState* pipelinestate = nullptr) const;   
};

