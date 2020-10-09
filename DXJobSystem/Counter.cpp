#include "pch.h"
#include "Counter.h"

#include "d3dUtil.h"
#include "JobManager.h"
#include "TLS.h"

DX::DXJobSystem::BaseCounter::BaseCounter(JobManager* mgr, uint8_t numWaitingFibers, WaitingFibers* waitingFibers,
                                          std::atomic_bool* freeWaitingSlots) :
	waitingFibersCount(numWaitingFibers),
	waitingFibers(waitingFibers),
	freeWaitingSlots(freeWaitingSlots),
	manager(mgr)
{
	for (uint8_t i = 0; i < numWaitingFibers; i++)
	{
		freeWaitingSlots[i].store(true);
	}
}

DX::DXJobSystem::Counter::Counter(JobManager* mgr) :
	BaseCounter(mgr, MAX_WAITING, impl_waitingFibers, impl_freeWaitingSlots)
{
}

DX::DXJobSystem::TinyCounter::TinyCounter(JobManager* mgr) :
	BaseCounter(mgr, 1, &waitingFiber, &freeWaitingSlot)
{
}

DX::DXJobSystem::Counter::Unit_t DX::DXJobSystem::BaseCounter::Increment(Unit_t by)
{
	const Unit_t prev = count.fetch_add(by);
	CheckWaitingFibers(prev + by);

	return prev;
}

DX::DXJobSystem::Counter::Unit_t DX::DXJobSystem::BaseCounter::Decrement(Unit_t by)
{
	const Unit_t prev = count.fetch_sub(by);
	CheckWaitingFibers(prev - by);

	return prev;
}

DX::DXJobSystem::Counter::Unit_t DX::DXJobSystem::BaseCounter::GetValue() const
{
	return count.load(std::memory_order_seq_cst);
}

bool DX::DXJobSystem::BaseCounter::AddWaitingFiber(uint16_t fiberIndex, Unit_t targetValue,
                                                   std::atomic_bool* fiberStored)
{
	for (uint8_t i = 0; i < waitingFibersCount; i++)
	{
		// Acquire Free Waiting Slot
		bool expected = true;
		if (!std::atomic_compare_exchange_strong_explicit(&freeWaitingSlots[i], &expected, false,
		                                                  std::memory_order_seq_cst, std::memory_order_relaxed))
		{
			continue;
		}

		// Setup Slot
		auto slot = &waitingFibers[i];
		slot->FiberIndex = fiberIndex;
		slot->FiberStored = fiberStored;
		slot->TargetValue = targetValue;

		slot->InUse.store(false);

		// Check if we are done already
		const auto counter = count.load(std::memory_order_relaxed);
		if (slot->InUse.load(std::memory_order_acquire))
		{
			return false;
		}

		if (slot->TargetValue == counter)
		{
			expected = false;
			if (!std::atomic_compare_exchange_strong_explicit(&slot->InUse, &expected, true, std::memory_order_seq_cst,
			                                                  std::memory_order_relaxed))
			{
				return false;
			}

			freeWaitingSlots[i].store(true, std::memory_order_release);
			return true;
		}

		return false;
	}

	// Waiting Slots are full
	assert(L"Counter waiting slots are full!");
	return false;
}

void DX::DXJobSystem::BaseCounter::CheckWaitingFibers(Unit_t value) const
{
	for (size_t i = 0; i < waitingFibersCount; i++)
	{
		if (freeWaitingSlots[i].load(std::memory_order_acquire))
		{
			continue;
		}

		auto waitingSlot = &waitingFibers[i];
		if (waitingSlot->InUse.load(std::memory_order_acquire))
		{
			continue;
		}

		if (waitingSlot->TargetValue == value)
		{
			bool expected = false;
			if (!std::atomic_compare_exchange_strong_explicit(&waitingSlot->InUse, &expected, true,
			                                                  std::memory_order_seq_cst, std::memory_order_relaxed))
			{
				continue;
			}

			manager->GetCurrentTLS()->ReadyFibers.emplace_back(waitingSlot->FiberIndex, waitingSlot->FiberStored);
			freeWaitingSlots[i].store(true, std::memory_order_release);
		}
	}
}
