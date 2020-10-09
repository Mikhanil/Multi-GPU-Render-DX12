#pragma once
#include <stdint.h>
#include "TLS.h"

#include <mutex>
#include <condition_variable>
namespace DX
{
	namespace JobSystem
	{

		class Fiber;

		class Thread
		{
		public:
			using Callback_t = void(*)(Thread*);

		private:
			void* _handle = nullptr;
			uint32_t _id = UINT32_MAX;
			TLS _tls;

			std::condition_variable _cvReceivedId;
			std::mutex _startupIdMutex;

			Callback_t _callback = nullptr;
			void* _userdata = nullptr;

			// Constructor for CurrentThread
			// Required since Thread cannot be copied
			Thread(void* h, uint32_t id) :
				_handle(h), _id(id)
			{
			};

		public:
			Thread() = default;
			Thread(const Thread&) = delete;
			virtual ~Thread() = default; // Note: destructor does not despawn Thread

			// Spawns Thread with given Callback & Userdata
			bool Spawn(Callback_t callback, void* userdata = nullptr);
			void SetAffinity(size_t);

			// Waits for Thread
			void Join();

			// Takes handle & id from currently running Thread
			void FromCurrentThread();

			// Getter
			TLS* GetTLS() { return &_tls; };
			Callback_t GetCallback() const { return _callback; };
			void* GetUserdata() const { return _userdata; };
			bool HasSpawned() const { return _id != UINT32_MAX; };
			const uint32_t GetID() const { return _id; };

			// Thread may launch before an ID was assigned (especially in Win32)
			// MSDN: If the thread is created in a runnable state (that is, if the
			//		 CREATE_SUSPENDED flag is not used), the thread can start running
			//		 before CreateThread returns and, in particular, before the caller
			//		 receives the handle and identifier of the created thread.
			// This scenario can cause a crash when the resulting callback wants to
			// access TLS.
			void WaitForReady();

			// Static Methods
			static void SleepFor(uint32_t ms);
		};
	}
}