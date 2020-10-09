#include "pch.h"
#include "Job.h"
#include "Counter.h"

void DX::DXJobSystem::JobInfo::Execute()
{
	if (!IsNull())
	{
		GetDelegate()->Call();
	}

	if (counter)
	{
		counter->Decrement();
	}
}
