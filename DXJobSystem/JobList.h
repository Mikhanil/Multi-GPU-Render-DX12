#pragma once
#include "Counter.h"
#include "Job.h" // Priority
#include "JobManager.h"
namespace DX
{
	namespace JobSystem
	{
		class JobManager;
		class Counter;

		class JobList
		{
			JobManager* _manager;
			JobPriority _defaultPriority;

			Counter _counter;

		public:
			JobList(JobManager*, JobPriority defaultPriority = JobPriority::Normal);
			~JobList();

			// Add
			void Add(JobPriority, JobInfo);

			void Add(const JobInfo& job)
			{
				Add(_defaultPriority, job);
			}

			template <typename... Args>
			void Add(JobPriority prio, Args ... args)
			{
				_manager->ScheduleJob(prio, &_counter, args...);
			}

			template <typename... Args>
			void Add(Args ... args)
			{
				_manager->ScheduleJob(_defaultPriority, &_counter, args...);
			}

			JobList& operator+=(const JobInfo&);

			// Wait
			void Wait(uint32_t targetValue = 0);
		};
	}
}