#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#include "LinearAllocator.h"

constexpr std::size_t INIT_LINEAR_SIZE = 1024u * 1024u * 256u;

//по 256 мб 
template<std::size_t INIT_LINEAR_SIZE = INIT_LINEAR_SIZE>
class LinearAllocationStrategy
{
    static_assert(INIT_LINEAR_SIZE != 0u, "Linear size must be more, than zero");
    static_assert(INIT_LINEAR_SIZE <= std::numeric_limits<std::uint32_t>::max(),
        "Linear size must be less or equal max value of the uint32_t");
public:

	void* Allocate(std::size_t size)
	{
		if(size > INIT_LINEAR_SIZE)
		{
			assert(true && "Incorrect size for allocate");
		}		

		if (size == 0u)
		{
			return nullptr;
		}

		for (auto& allocator : pages)
		{
			const auto memory = allocator.Allocate(size);
			if (memory)
			{
				return memory;
			}
		}

		pages.push_back(LinearAllocator(INIT_LINEAR_SIZE));

		auto& allocator = pages.back();
		allocator.Init();
		return allocator.Allocate(size);
	}

	
	void Deallocate(void* memory_ptr, std::size_t size)
	{
		if ((!memory_ptr) || (size == 0u))
		{
			return;
		}

		/*А что нам делать? Линейный же аллокатор, сидим ждем когда вызовут деструктор у класса чтобы все почистить за собой*/
	}

	void ClearAll()
	{
		for (auto& allocator : pages)
		{
			allocator.Reset();
		}
		pages.clear();
	}
	
	
	~LinearAllocationStrategy()
	{
		ClearAll();
	}
private:
    std::vector<LinearAllocator> pages;
};







