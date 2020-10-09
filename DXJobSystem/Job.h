#pragma once
#include "Delegate.h"
#include "Counter.h"
#include "function_checks.h"

namespace DX
{
	namespace DXJobSystem
	{
		class Counter;
		class BaseCounter;

		class JobInfo
		{
			// Delegate Buffer
			static constexpr size_t BufferSize = sizeof(void*) * (8);

			char  buffer[BufferSize] = {0};
			base_delegate* GetDelegate() { return reinterpret_cast<base_delegate*>( buffer); }
			bool IsNull() const { return *(void**) buffer == nullptr; }

			void Reset()
			{
				if (!IsNull())
				{
					GetDelegate()->~base_delegate();
					*(void**) buffer = nullptr;
				}
			}

			// Store
			template <typename delegate_t, typename... Args>
			void StoreJobInfo(Args ... args)
			{
				delegate_size_checker<sizeof(delegate_t), BufferSize>::check();
				new( buffer) delegate_t(args...);
			}

			template <class TClass, typename... Args>
			void StoreJobInfo(TClass* inst, Args ... args)
			{
				using Ret = typename function_traits<TClass>::return_type;
				StoreJobInfo<delegate_member<TClass, Ret, Args...>>(&TClass::operator(), inst, args...);
			}

			// Counter
			BaseCounter* counter = nullptr;

		public:
			JobInfo() = default;

			// Callable class (Lambda / function with operator())
			template <typename TCallable, typename... Args>
			JobInfo(Counter* ctr, TCallable callable, Args ... args) :
				counter(ctr)
			{
				Reset();
				function_checker<TCallable, Args...>::check();

				StoreJobInfo<delegate_callable<TCallable, Args...>>(callable, args...);
			}

			// Function
			template <typename Ret, typename... Args>
			JobInfo(Counter* ctr, Ret (*function)(Args ...), Args ... args) :
				counter(ctr)
			{
				Reset();
				function_checker<decltype(function), Args...>::check();

				StoreJobInfo<delegate_callable<decltype(function), Args...>>(function, args...);
			}

			// Pointer to a callable class (operator())
			template <class TCallable, typename... Args>
			JobInfo(Counter* ctr, TCallable* callable, Args ... args) :
				counter(ctr)
			{
				Reset();

				function_checker<TCallable, Args...>::check();
				StoreJobInfo(callable, args...);
			}

			// Member Function
			template <class TClass, typename Ret, typename... Args>
			JobInfo(Counter* ctr, Ret (TClass::* callable)(Args ...), TClass* inst, Args ... args) :
				counter(ctr)
			{
				Reset();
				function_checker<decltype(callable), TClass*, Args...>::check();

				StoreJobInfo<delegate_member<TClass, Ret, Args...>>(callable, inst, args...);
			}

			// Constructor without Counter
			template <typename TCallable, typename... Args>
			JobInfo(TCallable callable, Args ... args) :
				JobInfo(static_cast<Counter*>(nullptr), callable, args...)
			{
			}

			template <typename Ret, typename... Args>
			JobInfo(Ret (*function)(Args ...), Args ... args) :
				JobInfo(static_cast<Counter*>(nullptr), function, args...)
			{
			}

			template <class TCallable, typename... Args>
			JobInfo(TCallable* callable, Args ... args) :
				JobInfo(static_cast<Counter*>(nullptr), callable, args...)
			{
			}

			template <class TClass, typename Ret, typename... Args>
			JobInfo(Ret (TClass::* callable)(Args ...), TClass* inst, Args ... args) :
				JobInfo(static_cast<Counter*>(nullptr), callable, inst, args...)
			{
			}

			~JobInfo()
			{
				Reset();
			}

			// Counter
			void SetCounter(BaseCounter* ctr)
			{
				counter = ctr;
			}

			BaseCounter* GetCounter() const
			{
				return  counter;
			}

			// Execute Job
			void Execute();

			// Assign Operator
			JobInfo& operator=(const JobInfo& other)
			{
				memcpy( buffer, other. buffer, BufferSize);
				counter = other.counter;

				return *this;
			}
		};

		enum class JobPriority : uint8_t
		{
			High,
			// Jobs are executed ASAP
			Normal,
			Low
		};
	}
}
