#pragma once

#include "pch.h"
#include <cstddef> // size_t

class Allocator {
protected:
    std::size_t totalSize;
    std::size_t used;   
    std::size_t peak;
public:
    Allocator(const std::size_t totalSize);

    virtual ~Allocator();

    virtual void* Allocate(const std::size_t size, const std::size_t alignment = 0) = 0;

    virtual void Free(void* ptr) = 0;

    virtual void Init() = 0;

};


