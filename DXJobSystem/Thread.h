#pragma once
#include <cstdint>
#include "TLS.h"

namespace DX
{
	namespace DXJobSystem
	{
		class Fiber;

		class Thread
		{
		public:
			using Callback_t = void(*)(Thread*);

		private:
			void* handle = nullptr;
			uint32_t id = UINT32_MAX;
			TLS tls;

			Callback_t callback = nullptr;
			void* userdata = nullptr;

			// Constructor for CurrentThread
			// Required since Thread cannot be copied
			Thread(void* h, uint32_t id);

		public:
			Thread();
			Thread(const Thread&) = delete;
			virtual ~Thread(); // Note: destructor does not despawn Thread

			// Spawns Thread with given Callback & Userdata
			bool Spawn(Callback_t callback, void* userdata = nullptr);
			void SetAffinity(size_t) const;

			// Waits for Thread
			void Join() const;

			// Takes handle & id from currently running Thread
			void FromCurrentThread();

			// Getter
			TLS* GetTLS();
			Callback_t GetCallback() const;
			void* GetUserData() const;
			bool HasSpawned() const;
			const uint32_t GetID() const;

			// Static Methods
			static void SleepFor(uint32_t ms);
		};
	}
}
