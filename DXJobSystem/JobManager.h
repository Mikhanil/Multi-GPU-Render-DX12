#pragma once
#include <stdint.h>
#include <thread>

#include "ConcurrentQueue.h"
#include "Job.h"
namespace DX
{
	namespace JobSystem
	{

		class Thread;
		struct TLS;
		class Fiber;

		class Counter;
		class BaseCounter;

		struct ManagerOptions
		{
			ManagerOptions() :
				NumThreads(std::thread::hardware_concurrency())
			{
			}

			~ManagerOptions() = default;

			// Threads & Fibers
			uint8_t NumThreads; // Amount of Worker Threads, default = amount of Cores
			uint16_t NumFibers = 25; // Amount of Fibers
			bool ThreadAffinity = false; // Lock each Thread to a processor core, requires NumThreads == amount of cores
			bool AutoSpawnThreads = true; // Spawn threads automatically

			// Worker Queue Sizes
			size_t HighPriorityQueueSize = 512; // High Priority
			size_t NormalPriorityQueueSize = 2048; // Normal Priority
			size_t LowPriorityQueueSize = 4096; // Low Priority
			size_t IOQueueSize = 32; // IO Tasks

			// Other
			bool ShutdownAfterMainCallback = true; // Shutdown everything after Main Callback returns?
		};

		class JobManager
		{
			friend class BaseCounter;

		public:
			enum class ReturnCode : uint8_t
			{
				Succes = 0,

				UnknownError,
				OSError,
				// OS-API call failed
				NullCallback,
				// callback is nullptr

				AlreadyInitialized,
				// Manager has already initialized
				InvalidNumFibers,
				// Fiber count is 0 or too high
				ErrorThreadAffinity,
				// ThreadAffinity is enabled, but Worker Thread Count > Available Cores
			};

			using Main_t = void(*)(JobManager*);

		protected:
			std::atomic_bool _shuttingDown = false;

			// Threads
			uint8_t _numThreads = 0;
			Thread* _threads = nullptr;
			bool _hasThreadAffinity = false;
			bool _autoSpawnThreads = true;

			// Fibers
			uint16_t _numFibers = 0;
			Fiber* _fibers = nullptr;
			std::atomic_bool* _idleFibers = nullptr;

			uint16_t FindFreeFiber();
			void CleanupPreviousFiber(TLS* = nullptr);

			// Thread
			uint8_t GetCurrentThreadIndex() const;
			Thread* GetCurrentThread() const;
			TLS* GetCurrentTLS() const;

			// Work Queue
			ConcurrentQueue<JobInfo> _highPriorityQueue;
			ConcurrentQueue<JobInfo> _normalPriorityQueue;
			ConcurrentQueue<JobInfo> _lowPriorityQueue;
			ConcurrentQueue<JobInfo> _ioQueue;

			ConcurrentQueue<JobInfo>* GetQueueByPriority(JobPriority);
			bool GetNextJob(JobInfo&, TLS*);

		private:
			Main_t _mainCallback = nullptr;
			bool _shutdownAfterMain = true;

			static void ThreadCallback_Worker(Thread*);
			static void FiberCallback_Worker(Fiber*);
			static void FiberCallback_Main(Fiber*);

		public:
			JobManager(const ManagerOptions & = ManagerOptions());
			~JobManager();

			// Initialize & Run Manager
			ReturnCode Run(Main_t);

			// Shutdown all Jobs/Threads/Fibers
			// blocking => wait for threads to exit
			void Shutdown(bool blocking = true);

			// Jobs
			void ScheduleJob(JobPriority, const JobInfo&);

			// Counter
			void WaitForCounter(BaseCounter*, uint32_t = 0, bool blocking = false);
			void WaitForSingle(JobPriority, JobInfo);

			// Getter
			bool IsShuttingDown() const { return _shuttingDown.load(std::memory_order_acquire); };
			const uint8_t GetNumThreads() const { return _numThreads; };
			const uint16_t GetNumFibers() const { return _numFibers; };

			// Threads
			Thread* GetThread(uint8_t idx);

			// when using AutoSpawnThreads = false, all GetNumThreads() have to be initialized:
			// 1) SpawnThread creates a Worker Thread
			bool SpawnThread(uint8_t idx);
			// 2) SetupThread only initializes all TLS values. The Thread can be created anyhwere else
			// but has to be initialized using this function before it can be used.
			// This function MUST be called from the target thread!
			bool SetupThread(uint8_t idx);

			// Easy Scheduling
			template <typename Callable, typename... Args>
			void ScheduleJob(JobPriority prio, Callable callable, Args ... args)
			{
				ScheduleJob(prio, JobInfo(callable, args...));
			}

			template <typename Callable, typename... Args>
			void ScheduleJob(JobPriority prio, BaseCounter* ctr, Callable callable, Args ... args)
			{
				ScheduleJob(prio, JobInfo(ctr, callable, args...));
			}

			template <typename Callable, typename... Args>
			void WaitForSingle(JobPriority prio, Callable callable, Args ... args)
			{
				WaitForSingle(prio, JobInfo(callable, args...));
			}
		};
	}
}