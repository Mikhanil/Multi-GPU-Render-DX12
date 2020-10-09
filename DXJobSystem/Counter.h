#pragma once
#include <stdint.h>
#include <atomic>

namespace DX
{
	namespace JobSystem
	{
		

class JobManager;


class BaseCounter
{
	friend class JobManager;

protected:
	using Unit_t = uint32_t;

	// Counter
	std::atomic<Unit_t> _counter = 0;

	// Waiting Fibers
	struct WaitingFibers
	{
		uint16_t _fiberIndex = UINT16_MAX;
		std::atomic_bool* _fiberStored = nullptr;
		Unit_t _targetValue = 0;

		std::atomic_bool _inUse = true;
	};

	const uint8_t _numWaitingFibers = 0;
	WaitingFibers* _waitingFibers = nullptr;
	std::atomic_bool* _freeWaitingSlots = nullptr;

	JobManager* _manager = nullptr;
	void InternalInit();

	// Methods
protected:
	bool AddWaitingFiber(uint16_t, Unit_t, std::atomic_bool*);
	void CheckWaitingFibers(Unit_t);

public:
	BaseCounter(JobManager* mgr, uint8_t numWaitingFibers, WaitingFibers* waitingFibers,
	            std::atomic_bool* freeWaitingSlots);
	virtual ~BaseCounter() = default;

	// Modifiers
	Unit_t Increment(Unit_t by = 1);
	Unit_t Decrement(Unit_t by = 1);

	// Counter Value
	Unit_t GetValue() const;
};

struct TinyCounter : public BaseCounter
{
	TinyCounter(JobManager*);
	~TinyCounter() = default;

	std::atomic_bool _freeWaitingSlot;
	WaitingFibers _waitingFiber;
};


class Counter : public BaseCounter
{
public:
	static constexpr uint8_t MAX_WAITING = 4;

private:
	std::atomic_bool _impl_freeWaitingSlots[MAX_WAITING];
	WaitingFibers _impl_waitingFibers[MAX_WAITING];

public:
	Counter(JobManager*);
	~Counter() = default;
};
	}
}
