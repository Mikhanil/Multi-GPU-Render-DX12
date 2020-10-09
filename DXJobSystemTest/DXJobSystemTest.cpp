#include "JobManager.h"
#include <iostream>
#include <Windows.h>

#include "JobList.h"
#include "JobQueue.h"

using namespace DX::JobSystem;

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
	for (uint16_t i = 0, length = 10000; i < length; ++i) {
		mgr->WaitForSingle(JobPriority::Normal, [&count]() {
			//std::cout << "lambda with " << count << std::endl;
			count++;
			});
	}

	// 3: Member Function
	test_job_2 tj2_inst;
	mgr->WaitForSingle(JobPriority::Normal, &tj2_inst, &count);

	// 3: Class operator()
	mgr->WaitForSingle(JobPriority::Normal, &tj2_inst, &count);



	// List / Queues
	JobList list(mgr);
	list.Add(JobPriority::Normal, test_job_1, &count);
	list.Wait();

	JobQueue queue(mgr, JobPriority::High); // default Priority is high
	queue.Add(test_job_1, &count);
	queue += JobInfo(test_job_1, &count);
	
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

	return 0;
}