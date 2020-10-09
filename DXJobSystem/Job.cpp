#include "pch.h"
#include "Job.h"
namespace DX
{
	namespace JobSystem
	{
		void JobInfo::Execute()
		{
			if (!IsNull())
			{
				GetDelegate()->Call();
			}

			if (_counter)
			{
				_counter->Decrement();
			}
		}
	}
}