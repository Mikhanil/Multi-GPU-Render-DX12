#pragma once
#include "Counter.h"
#include "Job.h" // Priority
#include <vector>

namespace DX
{
	namespace DXJobSystem
	{
		class JobManager;
		class Counter;

		class JobQueue
		{
			JobManager* m_manager;
			JobPriority  m_defaultPriority;

			Counter  m_counter;
			std::vector<std::pair<JobPriority, JobInfo>>  m_queue;

		public:
			JobQueue(JobManager*, JobPriority defaultPriority = JobPriority::Normal);
			~JobQueue();

			// Add
			void Add(JobPriority, JobInfo);

			void Add(const JobInfo& job)
			{
				Add(m_defaultPriority, job);
			}

			template <typename... Args>
			void Add(JobPriority prio, Args ... args)
			{
				m_queue.emplace_back(prio, JobInfo(&m_counter, args...));
			}

			template <typename... Args>
			void Add(Args ... args)
			{
				m_queue.emplace_back(m_defaultPriority, JobInfo(&m_counter, args...));
			}

			JobQueue& operator+=(const JobInfo&);

			// Execute all Jobs in Queue
			void Execute();

			// Execute first Job in Queue
			// returns false if queue is empty
			bool Step();
		};
	}
}
