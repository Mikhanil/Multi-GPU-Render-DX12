#pragma once
#include "Counter.h"
#include "Job.h" // Priority
#include "JobManager.h"

namespace DX
{
	namespace DXJobSystem
	{
		class JobManager;
		class Counter;

		class JobList
		{
			JobManager* manager;
			JobPriority  priority;

			Counter  counter;

		public:
			JobList(JobManager*, JobPriority defaultPriority = JobPriority::Normal);
			~JobList();

			// Add
			void Add(JobPriority, JobInfo);

			void Add(const JobInfo& job)
			{
				Add(priority, job);
			}

			template <typename... Args>
			void Add(JobPriority prio, Args ... args)
			{
				manager->ScheduleJob(prio, &counter, args...);
			}

			template <typename... Args>
			void Add(Args ... args)
			{
				manager->ScheduleJob(priority, &counter, args...);
			}

			JobList& operator+=(const JobInfo&);

			// Wait
			void Wait(uint32_t targetValue = 0);
		};
	}
}
