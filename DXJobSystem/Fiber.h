#pragma once

namespace DX
{
	namespace JobSystem
	{
		class Fiber
		{
		public:
			using Callback_t = void(*)(Fiber*);

		private:
			void* _fiber = nullptr;
			bool _threadFiber = false;

			Fiber* _returnFiber = nullptr;

			Callback_t _callback = nullptr;
			void* _userData = nullptr;

			Fiber(void* fiber)
				: _fiber(fiber)
			{
			};

		public:
			Fiber();
			Fiber(const Fiber&) = delete;
			~Fiber();

			// Converts current Thread to a Fiber
			void FromCurrentThread();

			// Set Callback
			void SetCallback(Callback_t);

			// Fiber Switching
			void SwitchTo(Fiber*, void* = nullptr);
			void SwitchBack();

			// Getter
			Callback_t GetCallback() const { return _callback; };
			void* GetUserdata() const { return _userData; };
			bool IsValid() const { return _fiber && _callback; };
		};
	}
}