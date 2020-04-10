
#include "BoxDraw3DApp.h"

BoxDraw3DApp::BoxDraw3DApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

BoxDraw3DApp::~BoxDraw3DApp()
{
}

bool BoxDraw3DApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    ResetCommandList();

    CreateConstantBufferDescriptorHeap();
    CreateConstantBuffer();
    CreateRootSignature();
    CreateShadersAndInputLayout();
    CreateBoxGeometry();
    CreatePSO();

    ExecuteCommandList();

    FlushCommandQueue();

    return true;
}

void BoxDraw3DApp::OnResize()
{
    D3DApp::OnResize();

    const XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void BoxDraw3DApp::Update(const GameTimer& gt)
{
	const float x = mRadius * sinf(mPhi) * cosf(mTheta);
	const float z = mRadius * sinf(mPhi) * sinf(mTheta);
	const float y = mRadius * cosf(mPhi);

	const XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
	const XMVECTOR target = XMVectorZero();
	const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	const XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);

    const XMMATRIX world = XMLoadFloat4x4(&mWorld);
	const XMMATRIX proj = XMLoadFloat4x4(&mProj);
	const XMMATRIX worldViewProj = world * view * proj;

    ObjectConstants objConstants;
    XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
    objConstants.time = gt.TotalTime();
    objConstants.pulseColor = XMFLOAT4(DirectX::Colors::Blue);	
    constantBuffer->CopyData(0, objConstants);
}

void BoxDraw3DApp::Draw(const GameTimer& gt)
{
    ThrowIfFailed(directCommandListAlloc->Reset());

    ResetCommandList(pipelineStateObject.Get());	

    commandList->RSSetViewports(1, &screenViewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    commandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    commandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    commandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap* descriptorHeaps[] = { constantBufferViewHeap.Get() };
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    commandList->SetGraphicsRootSignature(rootSignature.Get());

    commandList->IASetVertexBuffers(0, 1, &boxMesh->VertexBufferView());
    commandList->IASetIndexBuffer(&boxMesh->IndexBufferView());
    commandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    commandList->SetGraphicsRootDescriptorTable(0, constantBufferViewHeap->GetGPUDescriptorHandleForHeapStart());

    commandList->DrawIndexedInstanced(
        boxMesh->DrawArgs["box"].IndexCount,
        1, 0, 0, 0);

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(commandList->Close());

    ID3D12CommandList* cmdsLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(swapChain->Present(0, 0));
    currBackBufferIndex = (currBackBufferIndex + 1) % swapChainBufferCount;

    
    FlushCommandQueue();
}

void BoxDraw3DApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    lastMousePosition.x = x;
    lastMousePosition.y = y;

    SetCapture(mainWindow);
}

void BoxDraw3DApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void BoxDraw3DApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
	    const float dx = XMConvertToRadians(0.25f * static_cast<float>(x - lastMousePosition.x));
	    const float dy = XMConvertToRadians(0.25f * static_cast<float>(y - lastMousePosition.y));

        mTheta += dx;
        mPhi += dy;

        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((btnState & MK_RBUTTON) != 0)
    {
	    const float dx = 0.005f * static_cast<float>(x - lastMousePosition.x);
	    const float dy = 0.005f * static_cast<float>(y - lastMousePosition.y);

        mRadius += dx - dy;

        mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
    }

    lastMousePosition.x = x;
    lastMousePosition.y = y;
}

void BoxDraw3DApp::CreateConstantBufferDescriptorHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = 1;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(dxDevice->CreateDescriptorHeap(&cbvHeapDesc,
        IID_PPV_ARGS(&constantBufferViewHeap)));
}

void BoxDraw3DApp::CreateConstantBuffer()
{
    constantBuffer = std::make_unique<ConstantBuffer<ObjectConstants>>(dxDevice.Get(), 1);

    const UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = constantBuffer->Resource()->GetGPUVirtualAddress();
    // Offset to the ith object constant buffer in the buffer.
    const int boxCBufIndex = 0;
    cbAddress += boxCBufIndex * objCBByteSize;

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    dxDevice->CreateConstantBufferView(
        &cbvDesc,
        constantBufferViewHeap->GetCPUDescriptorHandleForHeapStart());
}

void BoxDraw3DApp::CreateRootSignature()
{
    // Shader programs typically require resources as input (constant buffers,
    // textures, samplers).  The root signature defines the resources the shader
    // programs expect.  If we think of the shader programs as a function, and
    // the input resources as function parameters, then the root signature can be
    // thought of as defining the function signature.  

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[1];

    // Create a single descriptor table of CBVs.
    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

    // A root signature is an array of root parameters.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(dxDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature)));
}

void BoxDraw3DApp::CreateShadersAndInputLayout()
{
    HRESULT hr = S_OK;

    vertexShaderBlob = d3dUtil::CompileShader(L"Shaders\\VertexColorShader.hlsl", nullptr, "main", "vs_5_1");
    pixelShaderBlob = d3dUtil::CompileShader(L"Shaders\\PixelColorShader.hlsl", nullptr, "main", "ps_5_1");

    inputLayout =
    {
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
}

void BoxDraw3DApp::CreateBoxGeometry()
{
    std::array<Vertex, 8> vertices =
    {
        Vertex({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White) }),
        Vertex({ XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black) }),
        Vertex({ XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red) }),
        Vertex({ XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green) }),
        Vertex({ XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow) }),
        Vertex({ XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan) }),
        Vertex({ XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta) })
    };

   /* std::array<Vertex, 5> vertices =
    {
        Vertex{XMFLOAT3(0.0f, 1.5f, 0.0f), XMFLOAT4(Colors::White)},
        Vertex{XMFLOAT3(-1.0f, 0.0f, -1.0f), XMFLOAT4(Colors::Black)},
        Vertex{XMFLOAT3(1.0f, 0.0f, -1.0f), XMFLOAT4(Colors::Red)},
        Vertex{XMFLOAT3(-1.0f, 0.0f, 1.0f), XMFLOAT4(Colors::Green)},
        Vertex{XMFLOAT3(1.0f, 0.0f, 1.0f), XMFLOAT4(Colors::Yellow)}
    };*/

	
    std::array<std::uint16_t, 36> indices =
    {
        // front face
        0, 1, 2,
        0, 2, 3,

        // back face
        4, 6, 5,
        4, 7, 6,

        // left face
        4, 5, 1,
        4, 1, 0,

        // right face
        3, 2, 6,
        3, 6, 7,

        // top face
        1, 5, 6,
        1, 6, 2,

        // bottom face
        4, 0, 3,
        4, 3, 7
    };

 /*   std::array<std::uint16_t, 18> indices =
    {
       0, 2, 1, 
        0, 3, 4, 
        0, 1, 3, 
        0, 4, 2,
        1, 2, 3,
        2, 4, 3,
    };*/

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    boxMesh = std::make_unique<MeshGeometry>();
    boxMesh->Name = "boxGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &boxMesh->VertexBufferCPU));
    CopyMemory(boxMesh->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &boxMesh->IndexBufferCPU));
    CopyMemory(boxMesh->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    boxMesh->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
        commandList.Get(), vertices.data(), vbByteSize, boxMesh->VertexBufferUploader);

    boxMesh->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
        commandList.Get(), indices.data(), ibByteSize, boxMesh->IndexBufferUploader);

    boxMesh->VertexByteStride = sizeof(Vertex);
    boxMesh->VertexBufferByteSize = vbByteSize;
    boxMesh->IndexFormat = DXGI_FORMAT_R16_UINT;
    boxMesh->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    boxMesh->DrawArgs["box"] = submesh;
}

void BoxDraw3DApp::CreatePSO()
{
   
	
    auto rastState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    //rastState.FillMode =  D3D12_FILL_MODE_WIREFRAME;
	
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    psoDesc.InputLayout = { inputLayout.data(), (UINT)inputLayout.size() };
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS =
    {
        reinterpret_cast<BYTE*>(vertexShaderBlob->GetBufferPointer()),
        vertexShaderBlob->GetBufferSize()
    };
    psoDesc.PS =
    {
        reinterpret_cast<BYTE*>(pixelShaderBlob->GetBufferPointer()),
        pixelShaderBlob->GetBufferSize()
    };
    psoDesc.RasterizerState = rastState;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = backBufferFormat;
    psoDesc.SampleDesc.Count = isM4xMsaa ? 4 : 1;
    psoDesc.SampleDesc.Quality = isM4xMsaa ? (m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = depthStencilFormat;
    ThrowIfFailed(dxDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));
}