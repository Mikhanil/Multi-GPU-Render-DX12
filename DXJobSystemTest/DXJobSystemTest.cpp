#include "JobManager.h"
#include <iostream>
#include <Windows.h>

#include "JobList.h"
#include "JobQueue.h"

using namespace DX::DXJobSystem;

void test_job_1(int* x)
{
	std::cout << "test_job_1 with " << *x << std::endl;
	(*x)++;
}

struct test_job_2
{
	void Execute(int* x)
	{
		std::cout << "test_job_2::Execute with " << *x << std::endl;
		(*x)++;
	}

	void operator()(int* x)
	{
		std::cout << "test_job_2::operator() with " << *x << std::endl;
		(*x)++;
	}
};

void main_test(JobManager* mgr)
{
	int count = 1;

	// 1: Function
	mgr->WaitForSingle(JobPriority::Normal, test_job_1, &count);

	// 2: Lambda
	mgr->WaitForSingle(JobPriority::Normal, [&count]() {
		std::cout << "lambda with " << count << std::endl;
		count++;
		});

	// 3: Member Function
	test_job_2 tj2_inst;
	mgr->WaitForSingle(JobPriority::Normal, &test_job_2::Execute, &tj2_inst, &count);

	// 3: Class operator()
	mgr->WaitForSingle(JobPriority::Normal, &tj2_inst, &count);

	// Counter
	Counter counter(mgr);

	// It's also possible to create a JobInfo yourself
	// First argument can be a Counter
	JobInfo test_job(&counter, test_job_1, &count);
	mgr->ScheduleJob(JobPriority::Normal, test_job);
	mgr->WaitForCounter(&counter);

	// List / Queues
	JobList list(mgr);
	list.Add(JobPriority::Normal, test_job_1, &count);
	//list += test_job; This would be unsafe, Jobs might execute in parallel

	list.Wait();

	JobQueue queue(mgr, JobPriority::High); // default Priority is high
	queue.Add(test_job_1, &count);
	queue += test_job; // Safe, Jobs are executed consecutively

	queue.Execute();
}

int main()
{
	// Setup Job Manager
	ManagerOptions managerOptions;
	managerOptions.NumFibers = managerOptions.NumThreads * 10;
	managerOptions.ThreadAffinity = true;

	managerOptions.HighPriorityQueueSize = 128;
	managerOptions.NormalPriorityQueueSize = 256;
	managerOptions.LowPriorityQueueSize = 256;

	managerOptions.ShutdownAfterMainCallback = true;

	// Manager
	JobManager manager(managerOptions);

	if (manager.Run(main_test) != JobManager::ReturnCode::Succes)
		std::cout << "oh no" << std::endl;
	else
		std::cout << "done" << std::endl;

	// Don't close
	getchar();
	return 0;
}