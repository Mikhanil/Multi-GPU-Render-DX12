#pragma once

#include <memory>
#include "GCrossAdapterResource.h"
#include "GTexture.h"
#include "GDescriptor.h"
#include "ShaderBuffersData.h"
#include "DirectXBuffers.h"

using namespace PEPEngine;
using namespace Graphics;

struct DOFFrameResource
{
    // Cross-adapter bridges. Forward = scene color + linearised depth from GPU0
    // to GPU1; backward = PatchMatch hole-fill + W-map from GPU1 to GPU0 (so
    // the heavy Gather pass can run on the prime device).
    std::shared_ptr<GCrossAdapterResource> CrossAdapterDepth;
    std::shared_ptr<GCrossAdapterResource> CrossAdapterColor;
    std::shared_ptr<GCrossAdapterResource> CrossAdapterColorFilled;
    std::shared_ptr<GCrossAdapterResource> CrossAdapterWFiltered;

    GTexture PrimeColor;
    GTexture PrimeDepth;
    GDescriptor ColorRTV;
    GDescriptor DepthDSV;

    GTexture SecondDepth;
    GTexture SecondColor;
    GTexture CoC;
    GTexture OcclusionMask;
    GTexture ColorFilled;
    GTexture ColorPyramid1;
    GTexture ColorPyramid2;
    GTexture WMax;
    GTexture WFiltered;

    GTexture PrimeCoC;
    GTexture PrimeDepthQuantized;  // R16_UNORM linearZ/FarZ; also the cross-adapter forward-copy source
    GTexture PrimeColorFilled;     // optimal-layout copy of CrossAdapterColorFilled
    GTexture PrimeWFiltered;       // optimal-layout copy of CrossAdapterWFiltered
    GTexture PrimeDOFResult;

    GDescriptor SrvUavHeap;       // GPU1 (DOF passes)
    GDescriptor PrimeGatherHeap;  // GPU0 (DepthQuantize/CoC/Gather)

    std::shared_ptr<ConstantUploadBuffer<PassConstants>> PassConstantUploadBuffer;
    std::shared_ptr<StructuredUploadBuffer<MaterialConstants>> MaterialBuffer;

    // Queue-local fence values gating slot reuse on each device.
    UINT64 PrimeRenderFenceValue = 0;
    UINT64 SecondRenderFenceValue = 0;

    // sharedFence value GPU1 signals when this frame's DOF is ready. The NEXT
    // frame's composite on GPU0 waits on this — that's the 1-frame latency of
    // the frame-pipelined design.
    UINT64 DOFSharedFenceValue = 0;
};
