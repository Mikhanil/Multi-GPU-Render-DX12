#pragma once

#include "delegate.h"
#include "Counter.h"

namespace DX
{
	namespace JobSystem
	{
		class Counter;
		class BaseCounter;

		class JobInfo
		{
		public:
			// Delegate Buffer
			static constexpr size_t BufferSize = sizeof(void*) * (8);

		private:
			char m_buffer[BufferSize];
			base_delegate* GetDelegate() { return reinterpret_cast<base_delegate*>(m_buffer); };
			bool IsNull() const { return *(void**)m_buffer == nullptr; };

			void Reset()
			{
				if (!IsNull())
				{
					GetDelegate()->~base_delegate();
					*(void**)m_buffer = nullptr;
				}
			}

			// Store
			template <typename delegate_t, typename... Args>
			void StoreJobInfo(Args ... args)
			{
				delegate_size_checker<sizeof(delegate_t), BufferSize>::check();
				new(m_buffer) delegate_t(args...);
			}

			template <class TClass, typename... Args>
			void StoreJobInfo(TClass* inst, Args ... args)
			{
				using Ret = typename function_traits<TClass>::return_type;
				StoreJobInfo<delegate_member<TClass, Ret, Args...>>(&TClass::operator(), inst, args...);
			}

			// Counter
			BaseCounter* _counter = nullptr;

		public:
			JobInfo()
			{
				*(void**)m_buffer = nullptr;
			}

			// Callable class (Lambda / function with operator())
			template <typename TCallable, typename... Args>
			JobInfo(Counter* ctr, TCallable callable, Args ... args)
				: _counter(ctr)
			{
				function_checker<TCallable, Args...>::check();

				StoreJobInfo<delegate_callable<TCallable, Args...>>(callable, args...);
			}

			// Function
			template <typename Ret, typename... Args>
			JobInfo(Counter* ctr, Ret(*function)(Args ...), Args ... args)
				: _counter(ctr)
			{
				function_checker<decltype(function), Args...>::check();

				StoreJobInfo<delegate_callable<decltype(function), Args...>>(function, args...);
			}

			// Pointer to a callable class (operator())
			template <class TCallable, typename... Args>
			JobInfo(Counter* ctr, TCallable* callable, Args ... args)
				: _counter(ctr)
			{
				function_checker<TCallable, Args...>::check();

				StoreJobInfo(callable, args...);
			}

			// Member Function
			template <class TClass, typename Ret, typename... Args>
			JobInfo(Counter* ctr, Ret(TClass::* callable)(Args ...), TClass* inst, Args ... args)
				: _counter(ctr)
			{
				function_checker<decltype(callable), TClass*, Args...>::check();

				StoreJobInfo<delegate_member<TClass, Ret, Args...>>(callable, inst, args...);
			}

			// Constructor without Counter
			template <typename TCallable, typename... Args>
			JobInfo(TCallable callable, Args ... args)
				: JobInfo(static_cast<Counter*>(nullptr), callable, args...)
			{
			};

			template <typename Ret, typename... Args>
			JobInfo(Ret(*function)(Args ...), Args ... args)
				: JobInfo(static_cast<Counter*>(nullptr), function, args...)
			{
			};

			template <class TCallable, typename... Args>
			JobInfo(TCallable* callable, Args ... args)
				: JobInfo(static_cast<Counter*>(nullptr), callable, args...)
			{
			};

			template <class TClass, typename Ret, typename... Args>
			JobInfo(Ret(TClass::* callable)(Args ...), TClass* inst, Args ... args)
				: JobInfo(static_cast<Counter*>(nullptr), callable, inst, args...)
			{
			};

			// copy-constructors
			JobInfo(const JobInfo& other)
				: _counter(other._counter)
			{
				memcpy(m_buffer, other.m_buffer, BufferSize);
			}

			JobInfo(JobInfo&& other)
				: _counter(other._counter)
			{
				memcpy(m_buffer, other.m_buffer, BufferSize);
			}

			// Destructor
			~JobInfo()
			{
				Reset();
			}

			// Counter
			void SetCounter(BaseCounter* ctr)
			{
				_counter = ctr;
			}

			BaseCounter* GetCounter() const
			{
				return _counter;
			}

			// Execute Job
			void Execute();

			// Assign Operator
			JobInfo& operator=(const JobInfo& other)
			{
				Reset();

				memcpy(m_buffer, other.m_buffer, BufferSize);
				_counter = other._counter;

				return *this;
			}
		};

		static_assert(sizeof(JobInfo) == (JobInfo::BufferSize + sizeof(void*)), "JobInfo size mismatch");

		enum class JobPriority : uint8_t
		{
			High,
			// Jobs are executed ASAP
			Normal,
			Low,

			IO
		};
	}
}