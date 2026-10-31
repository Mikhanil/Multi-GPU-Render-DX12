#include "pch.h"
#include "Allocator.h"

namespace PEPEngine::Allocator
{
    Allocator::Allocator(const std::size_t totalSize)
        : totalSize(totalSize), used(0)
    {
    }

    Allocator::~Allocator()
    {
        totalSize = 0;
    }
}
