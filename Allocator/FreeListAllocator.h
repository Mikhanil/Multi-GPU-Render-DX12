#pragma once

#include "pch.h"
#include "Allocator.h"
#include "SinglyLinkedList.h"

namespace PEPEngine::Allocator
{
    class FreeListAllocator : public Allocator
    {
    public:
        enum PlacementPolicy
        {
            FIND_FIRST,
            FIND_BEST
        };

    private:
        struct FreeHeader
        {
            std::size_t blockSize;
        };

        struct AllocationHeader
        {
            std::size_t blockSize;
            char padding;
        };

        using Node = SinglyLinkedList<FreeHeader>::Node;


        void* m_start_ptr = nullptr;
        PlacementPolicy m_pPolicy;
        SinglyLinkedList<FreeHeader> m_freeList;

    public:
        FreeListAllocator(std::size_t totalSize, PlacementPolicy pPolicy);

        ~FreeListAllocator() override;

        void* Allocate(std::size_t size, std::size_t alignment = 0) override;

        void Free(void* ptr) override;

        void Init() override;

        virtual void Reset();

    private:
        FreeListAllocator(FreeListAllocator& freeListAllocator) = delete;

        void Coalescence(Node* prevBlock, Node* freeBlock);

        void Find(std::size_t size, std::size_t alignment, std::size_t& padding, Node*& previousNode,
                  Node*& foundNode);
        void FindBest(std::size_t size, std::size_t alignment, std::size_t& padding, Node*& previousNode,
                      Node*& foundNode);
        void FindFirst(std::size_t size, std::size_t alignment, std::size_t& padding, Node*& previousNode,
                       Node*& foundNode);
    };
}
