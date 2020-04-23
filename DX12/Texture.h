#pragma once
#include "d3dUtil.h"


class Texture
{
	std::wstring Filename;
	std::string Name;
	Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
	bool isLoaded = false;
public:

	Texture(std::string name, std::wstring filename);

	void LoadTexture(ID3D12Device* device, ID3D12CommandQueue* queue);

	ID3D12Resource* GetResource() const;

	std::string& GetName();

	std::wstring& GetFileName();
};


