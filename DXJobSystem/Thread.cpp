#include "pch.h"
#include "Thread.h"

#include <cassert>


#include "d3dUtil.h"
#include "Fiber.h"
#ifdef _WIN32
#include <Windows.h>
#endif

#ifdef _WIN32
static void WINAPI LaunchThread(void* ptr)
{
	const auto thread = reinterpret_cast<DX::DXJobSystem::Thread*>(ptr);
	const auto callback = thread->GetCallback();

	if (callback == nullptr)
	{
		assert(L"LaunchThread: callback is nullptr");
	}

	callback(thread);
}
#endif

DX::DXJobSystem::Thread::Thread(void* h, uint32_t id):
	handle(h), id(id)
{
}

DX::DXJobSystem::Thread::Thread() = default;

DX::DXJobSystem::Thread::~Thread() = default;

DX::DXJobSystem::TLS* DX::DXJobSystem::Thread::GetTLS()
{
	return &tls;
}

DX::DXJobSystem::Thread::Callback_t DX::DXJobSystem::Thread::GetCallback() const
{
	return callback;
}

void* DX::DXJobSystem::Thread::GetUserData() const
{
	return userdata;
}

bool DX::DXJobSystem::Thread::HasSpawned() const
{
	return id != UINT32_MAX;
}

const uint32_t DX::DXJobSystem::Thread::GetID() const
{
	return id;
}

bool DX::DXJobSystem::Thread::Spawn(Callback_t callback, void* userdata)
{
	handle = nullptr;
	id = UINT32_MAX;
	this->callback = callback;
	this->userdata = userdata;

#ifdef _WIN32
	handle = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)LaunchThread, this, 0, (DWORD*)&id);
#endif

	return HasSpawned();
}

void DX::DXJobSystem::Thread::SetAffinity(size_t i) const
{
#ifdef _WIN32
	if (!HasSpawned())
	{
		return;
	}

	const DWORD_PTR mask = 1ull << i;
	SetThreadAffinityMask(handle, mask);
#endif
}

void DX::DXJobSystem::Thread::Join() const
{
	if (!HasSpawned())
	{
		return;
	}

#ifdef _WIN32
	WaitForSingleObject(handle, INFINITE);
#endif
}

void DX::DXJobSystem::Thread::FromCurrentThread()
{
	handle = GetCurrentThread();
	id = GetCurrentThreadId();
}

void DX::DXJobSystem::Thread::SleepFor(uint32_t ms)
{
#ifdef _WIN32
	Sleep(ms);
#endif
}
