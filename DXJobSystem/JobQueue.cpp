#include "pch.h"
#include "JobQueue.h"

#include "JobManager.h"

DX::DXJobSystem::JobQueue::JobQueue(JobManager* mgr, JobPriority defaultPriority) :
	m_manager(mgr),
	m_defaultPriority(defaultPriority),
	m_counter(mgr)
{
}

DX::DXJobSystem::JobQueue::~JobQueue()
{
}

void DX::DXJobSystem::JobQueue::Add(JobPriority prio, JobInfo job)
{
	job.SetCounter(&m_counter);
	m_queue.emplace_back(prio, job);
}

DX::DXJobSystem::JobQueue& DX::DXJobSystem::JobQueue::operator+=(const JobInfo& job)
{
	Add(m_defaultPriority, job);
	return *this;
}

bool DX::DXJobSystem::JobQueue::Step()
{
	if (m_queue.empty())
	{
		return false;
	}

	const auto& job = m_queue.front();
	m_manager->ScheduleJob(job.first, job.second);
	m_manager->WaitForCounter(&m_counter);

	m_queue.erase(m_queue.begin());
	return true;
}

void DX::DXJobSystem::JobQueue::Execute()
{
	while (Step());
}
