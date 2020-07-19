
#include "pch.h"
#include <iostream>
#include <cstddef>
#include <vector>

#include "Allocator.h"
#include "StackAllocator.h"
#include "CAllocator.h"
#include "LinearAllocator.h"
#include "PoolAllocator.h"
#include "FreeListAllocator.h"

int main() {
    const std::vector<std::size_t> ALLOCATION_SIZES {32, 64, 256, 512, 1024, 2048, 4096};
    const std::vector<std::size_t> ALIGNMENTS {8, 8, 8, 8, 8, 8, 8};

    Allocator * cAllocator = new CAllocator();
    Allocator * linearAllocator = new LinearAllocator(1e9);
    Allocator * stackAllocator = new StackAllocator(1e9);
    Allocator * poolAllocator = new PoolAllocator(16777216, 4096);
    Allocator * freeListAllocator = new FreeListAllocator(1e8, FreeListAllocator::PlacementPolicy::FIND_FIRST);
       
    delete cAllocator;
    delete linearAllocator;
    delete stackAllocator;
    delete poolAllocator;
    
    return 1;
}




