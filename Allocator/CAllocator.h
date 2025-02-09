#pragma once

#include "pch.h"
#include "Allocator.h"

namespace PEPEngine::Allocator
{
    class CAllocator : public Allocator
    {
    public:
        CAllocator();

        ~CAllocator() override;

        void* Allocate(std::size_t size, std::size_t alignment = 0) override;

        void Free(void* ptr) override;

        void Init() override;
    };
}
