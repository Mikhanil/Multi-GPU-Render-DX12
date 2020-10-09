#include "pch.h"
#include "Fiber.h"

#include <cassert>

#ifdef _WIN32
#include <Windows.h>
#endif
namespace DX
{
	namespace JobSystem
	{
		// TODO: Add exceptions for invalid stuff?

		static void LaunchFiber(Fiber* fiber)
		{
			auto callback = fiber->GetCallback();
			if (callback == nullptr)
			{
				assert("LaunchFiber: callback is nullptr");
			}

			callback(fiber);
		}

		Fiber::Fiber()
		{
#ifdef _WIN32
			_fiber = CreateFiber(0, (LPFIBER_START_ROUTINE)LaunchFiber, this);
			_threadFiber = false;
#endif
			// TODO mac/linux impl
		}

		Fiber::~Fiber()
		{
#ifdef _WIN32
			if (_fiber && !_threadFiber)
			{
				DeleteFiber(_fiber);
			}
#endif
			// TODO mac/linux impl
		}

		void Fiber::FromCurrentThread()
		{
#ifdef _WIN32
			if (_fiber && !_threadFiber)
			{
				DeleteFiber(_fiber);
			}

			_fiber = ConvertThreadToFiber(nullptr);
			_threadFiber = true;
#endif
			// TODO mac/linux impl
		}

		void Fiber::SetCallback(Callback_t cb)
		{
			if (cb == nullptr)
			{
				//assert("EX_CALLBACK_IS_NULL");
			}

			_callback = cb;
		}

		void Fiber::SwitchTo(Fiber* fiber, void* userdata)
		{
			if (fiber == nullptr || fiber->_fiber == nullptr)
			{
				assert("EX_INVALID_FIBER");
			}

			fiber->_userData = userdata;
			fiber->_returnFiber = this;

			SwitchToFiber(fiber->_fiber);
		}

		void Fiber::SwitchBack()
		{
			if (_returnFiber && _returnFiber
				->
				_fiber
				)
			{
				SwitchToFiber(_returnFiber->_fiber);
				return;
			}

			assert("EX_UNABLE_TO_SWITCH_BACK_FROM_FIBER");
		}
	}
}