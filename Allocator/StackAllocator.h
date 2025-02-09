#pragma once

#include "pch.h"
#include "Allocator.h"

namespace PEPEngine::Allocator
{
    class StackAllocator : public Allocator
    {
    protected:
        void* m_start_ptr = nullptr;
        std::size_t m_offset;

    public:
        StackAllocator(std::size_t totalSize);

        ~StackAllocator() override;

        void* Allocate(std::size_t size, std::size_t alignment = 0) override;

        void Free(void* ptr) override;

        void Init() override;

        virtual void Reset();

    private:
        StackAllocator(StackAllocator& stackAllocator);

        struct AllocationHeader
        {
            char padding;
        };
    };
}
