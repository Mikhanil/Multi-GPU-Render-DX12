#pragma once
#include <cstdint>
#include <atomic>

namespace DX
{
	namespace DXJobSystem
	{
		class JobManager;

		class BaseCounter
		{
			friend class JobManager;

		protected:
			using Unit_t = uint32_t;

			// Counter
			std::atomic<Unit_t>  count = 0;

			// Waiting Fibers
			struct WaitingFibers
			{
				uint16_t FiberIndex = UINT16_MAX;
				std::atomic_bool* FiberStored = nullptr;
				Unit_t TargetValue = 0;

				std::atomic_bool InUse = true;
			};

			const uint8_t  waitingFibersCount;
			WaitingFibers* waitingFibers;
			std::atomic_bool* freeWaitingSlots;

			// Manager
			JobManager* manager;

			// Methods
			bool AddWaitingFiber(uint16_t, Unit_t, std::atomic_bool*);
			void CheckWaitingFibers(Unit_t) const;

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

			std::atomic_bool  freeWaitingSlot;
			WaitingFibers  waitingFiber;
		};


		class Counter :
			public BaseCounter
		{
		public:
			static constexpr uint8_t MAX_WAITING = 4;

		private:
			std::atomic_bool  impl_freeWaitingSlots[MAX_WAITING];
			WaitingFibers  impl_waitingFibers[MAX_WAITING];

		public:
			Counter(JobManager*);
			~Counter() = default;
		};
	}
}
