#include "pch.h"
#include "MemoryAllocator.h"


LinearAllocationStrategy<> MemoryAllocator::allocatorStrategy = LinearAllocationStrategy<INIT_LINEAR_SIZE>();
