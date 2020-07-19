#pragma once

#include "pch.h"
#include "Allocator.h"

class LinearAllocator : public Allocator {
protected:
	void* startPtr = nullptr;
	std::size_t offset;
public:
	LinearAllocator(const std::size_t totalSize);

	virtual ~LinearAllocator();

	virtual void* Allocate(const std::size_t size, const std::size_t alignment = 0) override;
	
	virtual void Free(void* ptr) override;

	virtual void Init() override;

	virtual void Reset();

};

