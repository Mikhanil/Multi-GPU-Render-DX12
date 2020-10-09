#include "pch.h"
#include "Fiber.h"

#include <cassert>


#include "d3dUtil.h"
#ifdef _WIN32
#include <Windows.h>
#endif

static void LaunchFiber(DX::DXJobSystem::Fiber* fiber)
{
	const auto callback = fiber->GetCallback();

	assert(callback != nullptr && L"LaunchFiber: callback is nullptr");	

	callback(fiber);
}

DX::DXJobSystem::Fiber::Fiber()
{
#ifdef _WIN32
	m_fiber = CreateFiber(0, (LPFIBER_START_ROUTINE)LaunchFiber, this);
	m_thread_fiber = false;
#endif
}

DX::DXJobSystem::Fiber::~Fiber()
{
#ifdef _WIN32
	if (m_fiber && !m_thread_fiber)
	{
		DeleteFiber(m_fiber);
	}
#endif
}

void DX::DXJobSystem::Fiber::FromCurrentThread()
{
#ifdef _WIN32
	if (m_fiber && !m_thread_fiber)
	{
		DeleteFiber(m_fiber);
	}

	m_fiber = ConvertThreadToFiber(nullptr);
	m_thread_fiber = true;
#endif
}

void DX::DXJobSystem::Fiber::SetCallback(Callback_t cb)
{
	assert(cb != nullptr && L"callback cannot be nullptr");	

	m_callback = cb;
}

void DX::DXJobSystem::Fiber::SwitchTo(Fiber* fiber, void* userdata)
{	
	if (fiber == nullptr || fiber->m_fiber == nullptr)
	{
		assert(L"Invalid fiber (nullptr or invalid)");
	}

	fiber->m_userdata = userdata;
	fiber->m_return_fiber = this;

	SwitchToFiber(fiber->m_fiber);
}

void DX::DXJobSystem::Fiber::SwitchBack()
{
	if (m_return_fiber && m_return_fiber->m_fiber)
	{
		SwitchToFiber(m_return_fiber->m_fiber);
	}
	else
	{
		assert(L"Unable to switch back from Fiber (none or invalid return fiber)");
	}
}
