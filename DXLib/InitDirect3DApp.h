#pragma once

#include <DirectXColors.h>
#include "d3dApp.h"

using namespace DirectX;

class InitDirect3DApp : public D3DApp
{
public:
	InitDirect3DApp(HINSTANCE hInstance);
	~InitDirect3DApp();

	bool Initialize() override;

private:
	void OnResize() override;
	void Update(const GameTimer& gt) override;
	void Draw(const GameTimer& gt) override;
};
