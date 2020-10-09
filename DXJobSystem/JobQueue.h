#pragma once
#include "Counter.h"
#include "Job.h" // Priority
#include <vector>
namespace DX
{
	namespace JobSystem
	{

		class JobManager;
		class Counter;

		class JobQueue
		{
			JobManager* _manager;
			JobPriority _defaultPriority;

			Counter _counter;
			std::vector<std::pair<JobPriority, JobInfo>> _queue;

		public:
			JobQueue(JobManager*, JobPriority defaultPriority = JobPriority::Normal);
			~JobQueue();

			// Add
			void Add(JobPriority, JobInfo);

			void Add(const JobInfo& job)
			{
				Add(_defaultPriority, job);
			}

			template <typename... Args>
			void Add(JobPriority prio, Args ... args)
			{
				_queue.emplace_back(prio, JobInfo(&_counter, args...));
			}

			template <typename... Args>
			void Add(Args ... args)
			{
				_queue.emplace_back(_defaultPriority, JobInfo(&_counter, args...));
			}

			JobQueue& operator+=(const JobInfo&);
			JobQueue& operator+=(JobInfo&&);

			// Execute all Jobs in Queue
			void Execute();

			// Execute first Job in Queue
			// returns false if queue is empty
			bool Step();
		};
	}
}