#include "pch.h"
#include "JobList.h"

#include <cstdint>
namespace DX
{
	namespace JobSystem
	{
		JobList::JobList(JobManager* mgr, JobPriority defaultPriority)
			: _manager(mgr),
			_defaultPriority(defaultPriority),
			_counter(mgr)
		{
		}

		JobList::~JobList()
		{
		}

		void JobList::Add(JobPriority prio, JobInfo job)
		{
			job.SetCounter(&_counter);

			_manager->ScheduleJob(prio, job);
		}

		JobList& JobList::operator+=(const JobInfo& job)
		{
			Add(_defaultPriority, job);
			return *this;
		}

		void JobList::Wait(uint32_t targetValue)
		{
			_manager->WaitForCounter(&_counter, targetValue);
		}
	}
}