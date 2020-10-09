#pragma once
#include "detail.h"

namespace DX
{
	namespace DXJobSystem
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
			TCallable  callable;
			std::tuple<Args...>  args;

			delegate_callable(TCallable callable, Args ... args) :
				 callable(callable),
				 args(args...)
			{
			}

			virtual ~delegate_callable() = default;

			void Call() override
			{
				apply( callable,  args);
			}
		};

		// member: member function
		template <class TClass, typename Ret, typename... Args>
		struct delegate_member : base_delegate
		{
			using function_t = Ret(TClass::*)(Args ...);
			function_t  function;
			TClass*  instance;
			std::tuple<Args...>  args;

			delegate_member(function_t function, TClass* inst, Args ... args) :
				 function(function),
				 instance(inst),
				 args(args...)
			{
			}

			virtual ~delegate_member() = default;

			void Call() override
			{
				apply( instance,  function,  args);
			}
		};
	}
}
