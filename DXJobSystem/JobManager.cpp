#include "pch.h"
#include "JobManager.h"
#include "Thread.h"
#include "Fiber.h"
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#else
#error Linux is not supported!
#endif


DX::DXJobSystem::JobManager::JobManager(const ManagerOptions& options) :
	threadsCount(options.NumThreads),
	threadAffinity(options.ThreadAffinity),
	fibersCount(options.NumFibers),
	highPriorityQueue(options.HighPriorityQueueSize),
	normalPriorityQueue(options.NormalPriorityQueueSize),
	lowPriorityQueue(options.LowPriorityQueueSize),
	shutdownAfterMain(options.ShutdownAfterMainCallback)
{
}

DX::DXJobSystem::JobManager::~JobManager()
{
}

DX::DXJobSystem::JobManager::ReturnCode DX::DXJobSystem::JobManager::Run(Main_t main)
{
	if (!threads.empty() || !fibers.empty())
	{
		return ReturnCode::AlreadyInitialized;
	}

	// Threads
	threads = std::vector<Thread>(threadsCount);
		
	// Current (Main) Thread
	auto mainThread = &threads[0];
	mainThread->FromCurrentThread();

	if (threadAffinity)
	{
		mainThread->SetAffinity(1);
	}

	auto mainThreadTLS = mainThread->GetTLS();
	mainThreadTLS->ThreadFiber.FromCurrentThread();

	// Create Fibers
	// This has to be done after Thread is converted to Fiber!
	if (fibersCount == 0)
	{
		return ReturnCode::InvalidNumFibers;
	}

	fibers = std::vector<Fiber>(fibersCount);
	idleFibers = std::vector<std::atomic_bool>(fibersCount);

	for (uint16_t i = 0; i < fibersCount; i++)
	{
		fibers[i].SetCallback(FiberCallback_Worker);
		idleFibers[i].store(true, std::memory_order_relaxed);
	}

	// Thread Affinity
	if (threadAffinity && threadsCount > std::thread::hardware_concurrency())
	{
		return ReturnCode::ErrorThreadAffinity;
	}

	// Spawn Threads
	for (uint8_t i = 0; i < threadsCount; i++)
	{
		const auto ttls = threads[i].GetTLS();
		ttls->ThreadIndex = i;

		if (i > 0) // 0 is Main Thread
		{
			ttls->SetAffinity = threadAffinity;

			if (!threads[i].Spawn(ThreadCallback_Worker, this))
			{
				return ReturnCode::OSError;
			}
		}
	}

	// Main
	if (main == nullptr)
	{
		return ReturnCode::NullCallback;
	}

	mainCallback = main;

	// Setup main Fiber
	mainThreadTLS->CurrentFiberIndex = FindFreeFiber();
	auto mainFiber = &fibers[mainThreadTLS->CurrentFiberIndex];
	mainFiber->SetCallback(FiberCallback_Main);

	mainThreadTLS->ThreadFiber.SwitchTo(mainFiber, this);

	// Wait for all Threads to shut down
	for (uint8_t i = 1; i < threadsCount; i++)
	{
		threads[i].Join();
	}

	// Done
	return ReturnCode::Succes;
}

void DX::DXJobSystem::JobManager::Shutdown(bool blocking)
{
	m_shuttingDown.store(true, std::memory_order_release);

	if (blocking)
	{
		for (uint8_t i = 1; i < threadsCount; i++)
		{
			threads[i].Join();
		}
	}
}

uint16_t DX::DXJobSystem::JobManager::FindFreeFiber()
{
	while (true)
	{
		for (uint16_t i = 0; i < fibersCount; i++)
		{
			if (!idleFibers[i].load(std::memory_order_relaxed) ||
				!idleFibers[i].load(std::memory_order_acquire))
			{
				continue;
			}

			bool expected = true;
						
			if (std::atomic_compare_exchange_weak_explicit(&idleFibers[i], &expected, false,
			                                               std::memory_order_release, std::memory_order_relaxed))
			{
				return i;
			}
		}
	}
}

void DX::DXJobSystem::JobManager::CleanupPreviousFiber(TLS* tls)
{
	if (tls == nullptr)
	{
		tls = GetCurrentTLS();
	}

	switch (tls->PreviousFiberDestination)
	{
	case FiberDestination::None:
		return;

	case FiberDestination::Pool:
		{
			idleFibers[tls->PreviousFiberIndex].store(true, std::memory_order_release);
			break;
		}

	case FiberDestination::Waiting:
		tls->PreviousFiberStored->store(true, std::memory_order_relaxed);
		break;

	default:
		break;
	}

	// Cleanup TLS
	tls->PreviousFiberIndex = UINT16_MAX;
	tls->PreviousFiberDestination = FiberDestination::None;
	tls->PreviousFiberStored = nullptr;
}

void DX::DXJobSystem::JobManager::ThreadCallback_Worker(Thread* thread)
{
	auto manager = reinterpret_cast<JobManager*>(thread->GetUserData());
	auto tls = thread->GetTLS();

	// Thread Affinity
	if (tls->SetAffinity)
		thread->SetAffinity(tls->ThreadIndex);

	// Setup Thread Fiber
	tls->ThreadFiber.FromCurrentThread();

	// Fiber
	tls->CurrentFiberIndex = manager->FindFreeFiber();

	const auto fiber = &manager->fibers[tls->CurrentFiberIndex];
	tls->ThreadFiber.SwitchTo(fiber, manager);
}

void DX::DXJobSystem::JobManager::FiberCallback_Main(Fiber* fiber)
{
	auto manager = reinterpret_cast<JobManager*>(fiber->GetUserdata());

	// Main
	manager->mainCallback(manager);

	// Shutdown after Main
	if (!manager->shutdownAfterMain)
	{
		// Switch to idle Fiber
		auto tls = manager->GetCurrentTLS();
		tls->CurrentFiberIndex = manager->FindFreeFiber();

		const auto fiber = &manager->fibers[tls->CurrentFiberIndex];
		tls->ThreadFiber.SwitchTo(fiber, manager);
	}

	// Shutdown
	manager->Shutdown(false);

	// Switch back to Main Thread
	fiber->SwitchBack();
}

void DX::DXJobSystem::JobManager::FiberCallback_Worker(Fiber* fiber)
{
	auto manager = reinterpret_cast<JobManager*>(fiber->GetUserdata());
	manager->CleanupPreviousFiber();

	JobInfo job;

	while (!manager->IsShuttingDown())
	{
		const auto tls = manager->GetCurrentTLS();

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

ConcurrentQueue<DX::DXJobSystem::JobInfo>* DX::DXJobSystem::JobManager::GetQueueByPriority(JobPriority prio)
{
	switch (prio)
	{
	case JobPriority::High:
		return &highPriorityQueue;

	case JobPriority::Normal:
		return &normalPriorityQueue;

	case JobPriority::Low:
		return &lowPriorityQueue;

	default:
		return nullptr;
	}
}

bool DX::DXJobSystem::JobManager::GetNextJob(JobInfo& job, TLS* tls)
{
	// High Priority Jobs always come first
	if (highPriorityQueue.dequeue(job))
	{
		return true;
	}

	// Ready Fibers
	if (tls == nullptr)
	{
		tls = GetCurrentTLS();
	}

	for (auto it = tls->ReadyFibers.begin(); it != tls->ReadyFibers.end(); ++it)
	{
		const uint16_t fiberIndex = it->first;

		// Make sure Fiber is stored
		if (!it->second->load(std::memory_order_relaxed))
			continue;

		// Erase
		delete it->second;
		tls->ReadyFibers.erase(it);

		// Update TLS
		tls->PreviousFiberIndex = tls->CurrentFiberIndex;
		tls->PreviousFiberDestination = FiberDestination::Pool;
		tls->CurrentFiberIndex = fiberIndex;

		// Switch to Fiber
		tls->ThreadFiber.SwitchTo(&fibers[fiberIndex], this);
		CleanupPreviousFiber(tls);

		break;
	}

	// Normal & Low Priority Jobs
	return
		normalPriorityQueue.dequeue(job) ||
		lowPriorityQueue.dequeue(job);
}

void DX::DXJobSystem::JobManager::ScheduleJob(JobPriority prio, const JobInfo& job)
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
		assert(L"Job Queue is full!");
	}
}

void DX::DXJobSystem::JobManager::WaitForCounter(BaseCounter* counter, uint32_t targetValue)
{
	if (counter == nullptr || counter->GetValue() == targetValue)
	{
		return;
	}

	auto tls = GetCurrentTLS();
	const auto fiberStored = new std::atomic_bool(false);

	if (counter->AddWaitingFiber(tls->CurrentFiberIndex, targetValue, fiberStored))
	{
		// Already done
		delete fiberStored;
		return;
	}

	// Update TLS
	tls->PreviousFiberIndex = tls->CurrentFiberIndex;
	tls->PreviousFiberDestination = FiberDestination::Waiting;
	tls->PreviousFiberStored = fiberStored;

	// Switch to idle Fiber
	tls->CurrentFiberIndex = FindFreeFiber();
	tls->ThreadFiber.SwitchTo(&fibers[tls->CurrentFiberIndex], this);

	// Cleanup
	CleanupPreviousFiber();
}

void DX::DXJobSystem::JobManager::WaitForSingle(JobPriority prio, JobInfo info)
{
	TinyCounter ctr(this);
	info.SetCounter(&ctr);

	ScheduleJob(prio, info);
	WaitForCounter(&ctr);
}

uint8_t DX::DXJobSystem::JobManager::GetCurrentThreadIndex() const
{
#ifdef _WIN32
	const uint32_t idx = GetCurrentThreadId();
	for (uint8_t i = 0; i < threadsCount; i++)
	{
		if (threads[i].GetID() == idx)
		{
			return i;
		}
	}
#endif

	return UINT8_MAX;
}

DX::DXJobSystem::Thread* DX::DXJobSystem::JobManager::GetCurrentThread()
{
#ifdef _WIN32
	const uint32_t idx = GetCurrentThreadId();
	for (uint8_t i = 0; i < threadsCount; i++)
	{
		if (threads[i].GetID() == idx)
		{
			return &threads[i];
		}
	}
#endif

	return nullptr;
}

DX::DXJobSystem::TLS* DX::DXJobSystem::JobManager::GetCurrentTLS()
{
#ifdef _WIN32
	const uint32_t idx = GetCurrentThreadId();
	for (uint8_t i = 0; i < threadsCount; i++)
	{
		if (threads[i].GetID() == idx)
		{
			return threads[i].GetTLS();
		}
	}
#endif

	return nullptr;
}
