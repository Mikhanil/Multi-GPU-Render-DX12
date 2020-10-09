#pragma once
#include <stdint.h>
#include <vector>
#include <atomic>
#include "Fiber.h"

namespace DX
{
	namespace JobSystem
	{
		enum class FiberDestination : uint8_t
		{
			None,
			Waiting,
			Pool
		};

		struct TLS
		{
			TLS() = default;
			~TLS() = default;

			// Thread Index
			uint8_t _threadIndex = UINT8_MAX;
			bool _hasAffinity = false;
			bool _isIO = false;

			// Thread Fiber
			Fiber _threadFiber;

			// Current Fiber
			uint16_t _currentFiberIndex = UINT16_MAX;

			// Previous Fiber
			uint16_t _previousFiberIndex = UINT16_MAX;
			std::atomic_bool* _previousFiberStored = nullptr;
			FiberDestination _previousFiberDestination = FiberDestination::None;

			// Ready Fibers
			std::vector<std::pair<uint16_t, std::atomic_bool*>> _readyFibers;

			void Cleanup()
			{
				_previousFiberIndex = UINT16_MAX;
				_previousFiberDestination = FiberDestination::None;
				_previousFiberStored = nullptr;
			}
		};
	}
}