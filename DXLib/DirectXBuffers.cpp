#include "DirectXBuffers.h"
#include "d3dApp.h"


ShaderBuffer::ShaderBuffer(UINT elementCount, UINT elementByteSize): elementByteSize(elementByteSize), elementCount(elementCount)
{
	auto& device = DXLib::D3DApp::GetApp().GetDevice();
	ThrowIfFailed(device.CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(elementByteSize * elementCount),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&buffer)));

	ThrowIfFailed(buffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData)));
}
