#include "pch.h"
#include "Thread.h"
#include <mutex>
#include <cassert>
#include <cstdint>

#ifdef _WIN32
#include <Windows.h>
#endif

#if LINUX
#include <libgen.h>
#include <signal.h>
#include <unistd.h>
#endif

#if MACOS
#include <unistd.h>
#endif
namespace DX
{
	namespace JobSystem
	{
		// TODO overall macos/linux impl

#ifdef _WIN32
		static void WINAPI LaunchThread(void* ptr)
		{
			auto thread = reinterpret_cast<Thread*>(ptr);
			auto callback = thread->GetCallback();

			if (callback == nullptr)
			{
				assert("LaunchThread: callback is nullptr");
			}

			thread->WaitForReady();
			callback(thread);
		}
#endif

		bool Thread::Spawn(Callback_t callback, void* userdata)
		{
			_handle = nullptr;
			_id = UINT32_MAX;
			_callback = callback;
			_userdata = userdata;

			{
				std::lock_guard<std::mutex> lock(_startupIdMutex);
#ifdef _WIN32
				_handle = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)LaunchThread, this, 0, (DWORD*)&_id);
#endif
			}

			_cvReceivedId.notify_all();
			return HasSpawned();
		}

		void Thread::SetAffinity(size_t i)
		{
#ifdef _WIN32
			if (!HasSpawned())
			{
				return;
			}

			DWORD_PTR mask = 1ull << i;
			SetThreadAffinityMask(_handle, mask);
#endif
		}

		void Thread::Join()
		{
			if (!HasSpawned())
			{
				return;
			}

#ifdef _WIN32
			WaitForSingleObject(_handle, INFINITE);
#endif
		}

		void Thread::FromCurrentThread()
		{
			_handle = GetCurrentThread();
			_id = GetCurrentThreadId();
		}

		void Thread::WaitForReady()
		{
			// Check if we have an ID already
			{
				std::lock_guard<std::mutex> lock(_startupIdMutex);
				if (_id != UINT32_MAX)
				{
					return;
				}
			}

			// Wait
			std::mutex mutex;

			std::unique_lock<std::mutex> lock(mutex);
			_cvReceivedId.wait(lock);
		}

		void Thread::SleepFor(uint32_t ms)
		{
#ifdef _WIN32
			Sleep(ms);
#elif LINUX
			sleep(ms);
#elif MACOS
			usleep(ms * 1000); // unix takes microseconds, not milliseconds (https://linux.die.net/man/3/usleep)
#endif
		}
	}
}