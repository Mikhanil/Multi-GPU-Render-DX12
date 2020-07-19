#include "pch.h"
#include "LinearAllocator.h"
#include "Utils.h"  /* CalculatePadding */
#include <stdlib.h>     /* malloc, free */
#include <cassert>   /*assert		*/
#include <algorithm>    // max
#ifdef _DEBUG
#include <iostream>
#endif

LinearAllocator::LinearAllocator(const std::size_t totalSize)
: Allocator(totalSize) {
}

void LinearAllocator::Init() {
    if (startPtr != nullptr) {
        free(startPtr);
    }

	/*В идиале вот тут лучше не выделять так память.
	 *А прокидывать сюда указатель на страницы памяти из VirtualAlloc (или чего-то друго что общается с системой)*/
    startPtr = malloc(totalSize);
    offset = 0;
}

LinearAllocator::~LinearAllocator() {
    free(startPtr);
    startPtr = nullptr;
}

void* LinearAllocator::Allocate(const std::size_t size, const std::size_t alignment) {

	std::size_t padding = 0;    
    const std::size_t currentAddress = (std::size_t)startPtr + offset;

    if (alignment != 0 && offset % alignment != 0) {
        // Alignment is required. Find the next aligned memory address and update offset
        padding = Utils::CalculatePadding(currentAddress, alignment);
    }

    if (offset + padding + size > totalSize) {
        return nullptr;
    }

    offset += padding;
    const std::size_t nextAddress = currentAddress + padding;
    offset += size;

#ifdef _DEBUG
    std::cout << "A" << "\t@C " << reinterpret_cast<void*>(currentAddress) << "\t@R " << reinterpret_cast<void*>(nextAddress) << "\tO " << offset << "\tP " << padding << std::endl;
#endif

    used = offset;
    peak = std::max(peak, used);

    return reinterpret_cast<void*>(nextAddress);
}

void LinearAllocator::Free(void* ptr)
{
    assert(false && "Use Reset() method");
}

void LinearAllocator::Reset()
{
    offset = 0;
    used = 0;
    peak = 0;
}
