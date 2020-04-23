#pragma once

#include "d3dApp.h"
#include "FrameResource.h"
#include <memory>
#include <vector>



class ShapesApp : public D3DApp
{
public:
    ShapesApp(HINSTANCE hInstance);
    ShapesApp(const ShapesApp& rhs) = delete;
    ShapesApp& operator=(const ShapesApp& rhs) = delete;
    ~ShapesApp();

    virtual bool Initialize()override;

private:
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

   
    void BuildFrameResources();
   

private:
        	
    std::vector<std::unique_ptr<FrameResource>> frameResources;
    FrameResource* currentFrameResource = nullptr;
    int currentFrameResourceIndex = 0;

};
