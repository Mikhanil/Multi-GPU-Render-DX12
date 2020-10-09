#include "pch.h"
#include "Counter.h"

#include <cassert>

#include "JobManager.h"
#include "TLS.h"
namespace DX
{
	namespace JobSystem
	{
		BaseCounter::BaseCounter(JobManager* mgr, uint8_t numWaitingFibers, WaitingFibers* waitingFibers,
			std::atomic_bool* freeWaitingSlots)
			: _numWaitingFibers(numWaitingFibers),
			_waitingFibers(waitingFibers),
			_freeWaitingSlots(freeWaitingSlots),
			_manager(mgr)
		{
		}

		void BaseCounter::InternalInit()
		{
			for (uint8_t i = 0; i < _numWaitingFibers; i++)
			{
				_freeWaitingSlots[i] = true;
			}
		}

		Counter::Counter(JobManager* mgr)
			: BaseCounter(mgr, MAX_WAITING, _impl_waitingFibers, _impl_freeWaitingSlots)
		{
			InternalInit();
		}

		TinyCounter::TinyCounter(JobManager* mgr)
			: BaseCounter(mgr, 1, &_waitingFiber, &_freeWaitingSlot)
		{
			InternalInit();
		}

		Counter::Unit_t BaseCounter::Increment(Unit_t by)
		{
			Unit_t prev = _counter.fetch_add(by);
			CheckWaitingFibers(prev + by);

			return prev;
		}

		Counter::Unit_t BaseCounter::Decrement(Unit_t by)
		{
			Unit_t prev = _counter.fetch_sub(by);
			CheckWaitingFibers(prev - by);

			return prev;
		}

		Counter::Unit_t BaseCounter::GetValue() const
		{
			return _counter.load(std::memory_order_seq_cst);
		}

		bool BaseCounter::AddWaitingFiber(uint16_t fiberIndex, Unit_t targetValue, std::atomic_bool* fiberStored)
		{
			for (uint8_t i = 0; i < _numWaitingFibers; i++)
			{
				// Acquire Free Waiting Slot
				bool expected = true;
				if (!std::atomic_compare_exchange_strong_explicit(&_freeWaitingSlots[i], &expected, false,
					std::memory_order_seq_cst, std::memory_order_relaxed))
				{
					continue;
				}

				// Setup Slot
				auto slot = &_waitingFibers[i];
				slot->_fiberIndex = fiberIndex;
				slot->_fiberStored = fiberStored;
				slot->_targetValue = targetValue;
				slot->_inUse.store(false);

				// Check if we are done already
				Unit_t counter = _counter.load(std::memory_order_relaxed);
				if (slot->_inUse.load(std::memory_order_acquire))
				{
					return false;
				}

				if (slot->_targetValue != counter)
				{
					return false;
				}

				expected = false;
				if (!std::atomic_compare_exchange_strong_explicit(&slot->_inUse, &expected, true, std::memory_order_seq_cst,
					std::memory_order_relaxed))
				{
					return false;
				}

				_freeWaitingSlots[i].store(true, std::memory_order_release);
				return true;
			}

			// Waiting Slots are full
			assert("EX_COUNTER_WAITINGS_SLOTS_FULL");
		}

		void BaseCounter::CheckWaitingFibers(Unit_t value)
		{
			for (size_t i = 0; i < _numWaitingFibers; i++)
			{
				if (_freeWaitingSlots[i].load(std::memory_order_acquire))
				{
					continue;
				}

				auto waitingSlot = &_waitingFibers[i];
				if (waitingSlot->_inUse.load(std::memory_order_acquire))
				{
					continue;
				}

				if (waitingSlot->_targetValue != value)
				{
					continue;
				}

				bool expected = false;
				if (!std::atomic_compare_exchange_strong_explicit(&waitingSlot->_inUse, &expected, true,
					std::memory_order_seq_cst, std::memory_order_relaxed))
				{
					continue;
				}

				_manager->GetCurrentTLS()->_readyFibers.emplace_back(waitingSlot->_fiberIndex, waitingSlot->_fiberStored);
				_freeWaitingSlots[i].store(true, std::memory_order_release);
			}
		}
	}
}