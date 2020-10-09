#include "pch.h"
#include "JobQueue.h"
#include "JobManager.h"
namespace DX
{
	namespace JobSystem
	{
		JobQueue::JobQueue(JobManager* mgr, JobPriority defaultPriority)
			: _manager(mgr),
			_defaultPriority(defaultPriority),
			_counter(mgr)
		{
		}

		JobQueue::~JobQueue()
		{
		}

		void JobQueue::Add(JobPriority prio, JobInfo job)
		{
			job.SetCounter(&_counter);
			_queue.emplace_back(prio, job);
		}

		JobQueue& JobQueue::operator+=(const JobInfo& job)
		{
			Add(_defaultPriority, job);
			return *this;
		}

		JobQueue& JobQueue::operator+=(JobInfo&& job)
		{
			job.SetCounter(&_counter);
			_queue.emplace_back(_defaultPriority, job);

			return *this;
		}

		bool JobQueue::Step()
		{
			if (_queue.empty())
			{
				return false;
			}

			const auto& job = _queue.front();
			_manager->ScheduleJob(job.first, job.second);
			_manager->WaitForCounter(&_counter);

			_queue.erase(_queue.begin());
			return true;
		}

		void JobQueue::Execute()
		{
			while (Step());
		}
	}
}