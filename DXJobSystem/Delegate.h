#pragma once
#include <tuple>
#include "detail.h"
#include "function_checks.h"

namespace DX
{
	namespace JobSystem
	{
		struct base_delegate
		{
			virtual ~base_delegate() = default;
			virtual void Call() = 0;
		};

		// callable: function pointer or lambda class
		template <typename TCallable, typename... Args>
		struct delegate_callable : base_delegate
		{
			TCallable _callable;
			std::tuple<Args...> _args;

			delegate_callable(TCallable callable, Args ... args) :
				_callable(callable),
				_args(args...)
			{
			};

			virtual ~delegate_callable() = default;

			void Call() override
			{
				apply(_callable, _args);
			}
		};

		// member: member function
		template <class TClass, typename Ret, typename... Args>
		struct delegate_member : base_delegate
		{
			using function_t = Ret(TClass::*)(Args ...);
			function_t _function;
			TClass* _instance;
			std::tuple<Args...> _args;

			delegate_member(function_t function, TClass* inst, Args ... args) :
				_function(function),
				_instance(inst),
				_args(args...)
			{
			};

			virtual ~delegate_member() = default;

			void Call() override
			{
				apply(_instance, _function, _args);
			}
		};
	}
}