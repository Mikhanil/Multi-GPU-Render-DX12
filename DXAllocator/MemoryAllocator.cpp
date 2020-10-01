#include "pch.h"
#include "MemoryAllocator.h"


std::shared_ptr<LinearAllocationStrategy<>> MemoryAllocator::allocatorStrategy = std::make_shared<LinearAllocationStrategy<INIT_LINEAR_SIZE>>();