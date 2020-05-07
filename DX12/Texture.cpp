#include "Texture.h"
#include "DirectXTex.h"
#include "filesystem"

using namespace std::filesystem;

Texture::Texture(std::string name, std::wstring filename):
	Filename(std::move(filename)), Name(std::move(name))
{
}

void Texture::LoadTexture(ID3D12Device* device, ID3D12CommandQueue* queue)
{
	if (isLoaded) return;

	path filePath(Filename);
	if (!exists(filePath))
	{
		assert("File not found.");
	}


	
	//DirectX::ResourceUploadBatch upload(device);
	//upload.Begin();

	//ThrowIfFailed(DirectX::CreateDDSTextureFromFile(device,
	//	upload, Filename.c_str(), Resource.GetAddressOf()));


	//// Upload the resources to the GPU.
	//upload.End(queue).wait();

	isLoaded = true;
}

ID3D12Resource* Texture::GetResource() const
{
	if (isLoaded)return Resource.Get();

	return nullptr;
}

std::string& Texture::GetName()
{
	return Name;
}

std::wstring& Texture::GetFileName()
{
	return Filename;
}
