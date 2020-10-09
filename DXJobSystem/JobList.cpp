#include "pch.h"
#include "JobList.h"

DX::DXJobSystem::JobList::JobList(JobManager* mgr, JobPriority defaultPriority) :
	manager(mgr),
	priority(defaultPriority),
	counter(mgr)
{
}

DX::DXJobSystem::JobList::~JobList()
{
}

void DX::DXJobSystem::JobList::Add(JobPriority prio, JobInfo job)
{
	job.SetCounter(&counter);

	manager->ScheduleJob(prio, job);
}


DX::DXJobSystem::JobList& DX::DXJobSystem::JobList::operator+=(const JobInfo& job)
{
	Add(priority, job);
	return *this;
}

void DX::DXJobSystem::JobList::Wait(uint32_t targetValue)
{
	manager->WaitForCounter(&counter, targetValue);
}
