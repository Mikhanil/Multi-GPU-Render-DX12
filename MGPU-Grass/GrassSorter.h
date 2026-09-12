#pragma once
#include "GBuffer.h"
#include "ComputePSO.h"
#include "GDescriptor.h"

// Sorts small (distance, instance index) pairs; geometry stays in place.
class GrassSorter
{
public:
    void Sort(const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commands,
              const std::shared_ptr<PEPEngine::Graphics::GDevice>& device,
              const PEPEngine::Graphics::GBuffer& source,
              const PEPEngine::Graphics::GBuffer& objectConstants,
              const PEPEngine::Graphics::GBuffer& passConstants,
              uint32_t count, uint32_t verticesPerInstance = 1);
    const PEPEngine::Graphics::GBuffer& GetIndices() const { return *indices_; }

private:
    std::shared_ptr<PEPEngine::Graphics::GRootSignature> signature_;
    std::shared_ptr<PEPEngine::Graphics::GRootSignature> bitonicSignature_;
    std::shared_ptr<PEPEngine::Graphics::ComputePSO> initializePSO_;
    std::shared_ptr<PEPEngine::Graphics::ComputePSO> preSortPSO_;
    std::shared_ptr<PEPEngine::Graphics::ComputePSO> outerSortPSO_;
    std::shared_ptr<PEPEngine::Graphics::ComputePSO> innerSortPSO_;
    std::shared_ptr<PEPEngine::Graphics::GBuffer> indices_;
    std::shared_ptr<PEPEngine::Graphics::GBuffer> counter_;
    PEPEngine::Graphics::GDescriptor descriptors_;
    uint32_t capacity_ = 0;
};
