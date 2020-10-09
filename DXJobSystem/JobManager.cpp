#include "pch.h"
#include "JobManager.h"

#include <cassert>

#include "Thread.h"
#include "Fiber.h"
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#else
#error Linux is not supported!
#endif

#include "TLS.h"

#ifdef _WIN32
#include <Windows.h>
#else
#error Linux is not supported!
#endif
namespace DX
{
	namespace JobSystem
	{

		JobManager::JobManager(const ManagerOptions& options)
			: _numThreads(options.NumThreads + 1 /* IO */),
			_hasThreadAffinity(options.ThreadAffinity),
			_autoSpawnThreads(options.AutoSpawnThreads),
			_numFibers(options.NumFibers),
			_highPriorityQueue(options.HighPriorityQueueSize),
			_normalPriorityQueue(options.NormalPriorityQueueSize),
			_lowPriorityQueue(options.LowPriorityQueueSize),
			_ioQueue(options.IOQueueSize),
			_shutdownAfterMain(options.ShutdownAfterMainCallback)
		{
		}

		JobManager::~JobManager()
		{
			delete[] _threads;
			delete[] _fibers;
			delete[] _idleFibers;
		}

		Thread* JobManager::GetThread(uint8_t idx)
		{
			assert(idx < _numThreads);
			return &_threads[idx];
		}

		bool JobManager::SpawnThread(uint8_t idx)
		{
			return GetThread(idx)->Spawn(ThreadCallback_Worker, this);
		}

		bool JobManager::SetupThread(uint8_t idx)
		{
			auto thread = GetThread(idx);
			if (thread->HasSpawned())
			{
				return false;
			}

			auto tls = GetCurrentTLS();
			if (tls)
			{
				return false;
			}

			thread->FromCurrentThread();
			tls = GetCurrentTLS();
			assert(tls->_threadIndex == idx);

			tls->_threadFiber.FromCurrentThread();
			tls->_currentFiberIndex = FindFreeFiber();

			return true;
		}

		JobManager::ReturnCode JobManager::Run(Main_t main)
		{
			if (_threads || _fibers)
			{
				return ReturnCode::AlreadyInitialized;
			}

			_threads = new Thread[_numThreads];

			// Current (Main) Thread
			auto mainThread = &_threads[0];
			mainThread->FromCurrentThread();

			TLS* mainThreadTLS = mainThread->GetTLS();
			mainThreadTLS->_threadFiber.FromCurrentThread();

			if (main)
			{
				if (_hasThreadAffinity)
				{
					mainThread->SetAffinity(1);
				}
			}

			// Create Fibers
			// This has to be done after Thread is converted to Fiber!
			if (_numFibers == 0)
			{
				return ReturnCode::InvalidNumFibers;
			}

			_fibers = new Fiber[_numFibers];
			_idleFibers = new std::atomic_bool[_numFibers];

			for (uint16_t i = 0; i < _numFibers; i++)
			{
				_fibers[i].SetCallback(FiberCallback_Worker);
				_idleFibers[i].store(true, std::memory_order_relaxed);
			}

			// Thread Affinity
			if (_hasThreadAffinity && (_numThreads == 0 || _numThreads > std::thread::hardware_concurrency() + 1))
			{
				return ReturnCode::ErrorThreadAffinity;
			}

			// Spawn Threads
			for (uint8_t i = 0; i < _numThreads; i++)
			{
				auto itTls = _threads[i].GetTLS();
				itTls->_threadIndex = i;

				if (i > 0) // 0 is Main Thread
				{
					if (i == (_numThreads - 1))
					{
						// IO Thread
						itTls->_isIO = true;
					}
					else
					{
						itTls->_hasAffinity = _hasThreadAffinity;
					}

					if (_autoSpawnThreads && !SpawnThread(i))
					{
						return ReturnCode::OSError;
					}
				}
			}

			mainThreadTLS->_currentFiberIndex = FindFreeFiber();

			// Main
			_mainCallback = main;
			if (_mainCallback == nullptr && _shutdownAfterMain)
			{
				return ReturnCode::NullCallback;
			}

			// Setup main Fiber
			const auto mainFiber = &_fibers[mainThreadTLS->_currentFiberIndex];
			mainFiber->SetCallback(FiberCallback_Main);

			if (_mainCallback)
			{
				mainThreadTLS->_threadFiber.SwitchTo(mainFiber, this);
			}

			if (_mainCallback)
			{
				// Wait for all Threads to shut down
				for (uint8_t i = 1; i < _numThreads; i++)
				{
					_threads[i].Join();
				}
			}

			return ReturnCode::Succes;
		}

		void JobManager::Shutdown(bool blocking)
		{
			_shuttingDown.store(true, std::memory_order_release);

			if (blocking)
			{
				for (uint8_t i = 1; i < _numThreads; i++)
				{
					_threads[i].Join();
				}
			}
		}

		uint16_t JobManager::FindFreeFiber()
		{
			while (true)
			{
				for (uint16_t i = 0; i < _numFibers; i++)
				{
					if (!_idleFibers[i].load(std::memory_order_relaxed)
						|| !_idleFibers[i].load(std::memory_order_acquire))
					{
						continue;
					}

					bool expected = true;
					if (std::atomic_compare_exchange_weak_explicit(&_idleFibers[i], &expected, false, std::memory_order_release,
						std::memory_order_relaxed))
					{
						return i;
					}
				}

				// TODO: Add Debug Counter and error message
			}
		}

		void JobManager::CleanupPreviousFiber(TLS* tls)
		{
			if (tls == nullptr)
			{
				tls = GetCurrentTLS();
			}

			switch (tls->_previousFiberDestination)
			{
			case FiberDestination::None:
				return;

			case FiberDestination::Pool:
				_idleFibers[tls->_previousFiberIndex].store(true, std::memory_order_release);
				break;

			case FiberDestination::Waiting:
				tls->_previousFiberStored->store(true, std::memory_order_relaxed);
				break;

			default:
				break;
			}

			tls->Cleanup();
		}

		void JobManager::ThreadCallback_Worker(Thread* thread)
		{
			auto manager = reinterpret_cast<JobManager*>(thread->GetUserdata());
			auto tls = thread->GetTLS();

			// Thread Name
			if (tls->_hasAffinity)
			{
				thread->SetAffinity(tls->_threadIndex);
			}

			// Setup Thread Fiber
			tls->_threadFiber.FromCurrentThread();
			tls->_currentFiberIndex = manager->FindFreeFiber();

			const auto fiber = &manager->_fibers[tls->_currentFiberIndex];
			tls->_threadFiber.SwitchTo(fiber, manager);
		}

		void JobManager::FiberCallback_Main(Fiber* fiber)
		{
			auto manager = reinterpret_cast<JobManager*>(fiber->GetUserdata());

			if (manager->_mainCallback)
			{
				manager->_mainCallback(manager);
			}

			// Should we shutdown after main?
			if (!manager->_shutdownAfterMain && manager->_mainCallback)
			{
				// Switch to idle Fiber
				const auto tls = manager->GetCurrentTLS();
				tls->_currentFiberIndex = manager->FindFreeFiber();

				const auto fiber = &manager->_fibers[tls->_currentFiberIndex];
				tls->_threadFiber.SwitchTo(fiber, manager);
			}

			if (manager->_shutdownAfterMain)
			{
				manager->Shutdown(false);
			}

			// Switch back to Thread
			fiber->SwitchBack();
		}

		void JobManager::FiberCallback_Worker(Fiber* fiber)
		{
			auto manager = reinterpret_cast<JobManager*>(fiber->GetUserdata());
			manager->CleanupPreviousFiber();

			JobInfo job;

			while (!manager->IsShuttingDown())
			{
				auto tls = manager->GetCurrentTLS();

				if (manager->GetNextJob(job, tls))
				{
					job.Execute();
					continue;
				}

				Thread::SleepFor(1);
			}

			// Switch back to Thread
			fiber->SwitchBack();
		}

		ConcurrentQueue<JobInfo>* JobManager::GetQueueByPriority(JobPriority prio)
		{
			switch (prio)
			{
			case JobPriority::High:
				return &_highPriorityQueue;

			case JobPriority::Normal:
				return &_normalPriorityQueue;

			case JobPriority::Low:
				return &_lowPriorityQueue;

			case JobPriority::IO:
				return &_ioQueue;

			default:
				return nullptr;
			}
		}

		bool JobManager::GetNextJob(JobInfo& job, TLS* tls)
		{
			if (tls == nullptr)
			{
				tls = GetCurrentTLS();
			}

			// IO only does IO jobs
			if (tls->_isIO)
			{
				return _ioQueue.dequeue(job);
			}

			// High Priority Jobs always come first
			if (_highPriorityQueue.dequeue(job))
			{
				return true;
			}

			// Ready Fibers
			for (auto it = tls->_readyFibers.begin(); it != tls->_readyFibers.end(); ++it)
			{
				uint16_t fiberIndex = it->first;

				// Make sure Fiber is stored
				if (!it->second->load(std::memory_order_relaxed)) continue;

				// Erase
				delete it->second;
				tls->_readyFibers.erase(it);

				// Update TLS
				tls->_previousFiberIndex = tls->_currentFiberIndex;
				tls->_previousFiberDestination = FiberDestination::Pool;
				tls->_currentFiberIndex = fiberIndex;

				// Switch to Fiber
				tls->_threadFiber.SwitchTo(&_fibers[fiberIndex], this);
				CleanupPreviousFiber(tls);

				break;
			}

			// Normal & Low Priority Jobs
			return _normalPriorityQueue.dequeue(job) || _lowPriorityQueue.dequeue(job);
		}

		void JobManager::ScheduleJob(JobPriority prio, const JobInfo& job)
		{
			auto queue = GetQueueByPriority(prio);
			if (!queue)
			{
				return;
			}

			if (job.GetCounter())
			{
				job.GetCounter()->Increment();
			}

			if (!queue->enqueue(job))
			{
				assert("EX_JOB_QUEUE_FULL");
			}
		}

#include <condition_variable>

		void WaitForCounter_Proxy(JobManager* mgr, BaseCounter* counter, uint32_t targetValue, std::condition_variable* cv)
		{
			mgr->WaitForCounter(counter, targetValue, false);
			cv->notify_all();
		}

		void JobManager::WaitForCounter(BaseCounter* counter, uint32_t targetValue, bool blocking)
		{
			if (counter == nullptr || counter->GetValue() == targetValue)
			{
				return;
			}

			auto tls = GetCurrentTLS();
			if (blocking)
			{
				if (counter->GetValue() == targetValue)
				{
					return;
				}

				std::condition_variable cv;
				ScheduleJob(JobPriority::High, WaitForCounter_Proxy, this, counter, targetValue, &cv);

				std::mutex mutex;
				std::unique_lock<std::mutex> lock(mutex);
				cv.wait(lock);

				return;
			}

			auto fiberStored = new std::atomic_bool(false);

			// Check if we're already done
			if (counter->AddWaitingFiber(tls->_currentFiberIndex, targetValue, fiberStored))
			{
				delete fiberStored;
				return;
			}

			// Update TLS
			tls->_previousFiberIndex = tls->_currentFiberIndex;
			tls->_previousFiberDestination = FiberDestination::Waiting;
			tls->_previousFiberStored = fiberStored;

			// Switch to idle Fiber
			tls->_currentFiberIndex = FindFreeFiber();
			tls->_threadFiber.SwitchTo(&_fibers[tls->_currentFiberIndex], this);

			// Cleanup
			CleanupPreviousFiber();
		}

		void JobManager::WaitForSingle(JobPriority prio, JobInfo info)
		{
			TinyCounter ctr(this);
			info.SetCounter(&ctr);

			ScheduleJob(prio, info);
			WaitForCounter(&ctr);
		}

		uint8_t JobManager::GetCurrentThreadIndex() const
		{
#ifdef _WIN32
			uint32_t idx = GetCurrentThreadId();
			for (uint8_t i = 0; i < _numThreads; i++)
			{
				if (_threads[i].GetID() == idx)
				{
					return i;
				}
			}
#endif
			// TODO macos/linux impl

			return UINT8_MAX;
		}

		Thread* JobManager::GetCurrentThread() const
		{
#ifdef _WIN32
			uint32_t idx = GetCurrentThreadId();
			for (uint8_t i = 0; i < _numThreads; i++)
			{
				if (_threads[i].GetID() == idx)
				{
					return &_threads[i];
				}
			}
#endif
			// TODO macos/linux impl

			return nullptr;
		}

		TLS* JobManager::GetCurrentTLS() const
		{
#ifdef _WIN32
			uint32_t idx = GetCurrentThreadId();
			for (uint8_t i = 0; i < _numThreads; i++)
			{
				if (_threads[i].GetID() == idx)
				{
					return _threads[i].GetTLS();
				}
			}
#endif
			// TODO macos/linux impl

			return nullptr;
		}
	}
}