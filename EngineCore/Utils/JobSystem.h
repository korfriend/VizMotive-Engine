#pragma once

#include <functional>
#include <atomic>

#ifndef UTIL_EXPORT
#ifdef _WIN32
#define UTIL_EXPORT __declspec(dllexport)
#else
#define UTIL_EXPORT __attribute__((visibility("default")))
#endif
#endif

namespace vz::jobsystem
{
	UTIL_EXPORT void Initialize(uint32_t maxThreadCount = ~0u);
	UTIL_EXPORT void ShutDown();

	struct JobArgs
	{
		uint32_t jobIndex;		// job index relative to dispatch (like SV_DispatchThreadID in HLSL)
		uint32_t groupID;		// group index relative to dispatch (like SV_GroupID in HLSL)
		uint32_t groupIndex;	// job index relative to group (like SV_GroupIndex in HLSL)
		bool isFirstJobInGroup;	// is the current job the first one in the group?
		bool isLastJobInGroup;	// is the current job the last one in the group?
		void* sharedmemory;		// stack memory shared within the current group (jobs within a group execute serially)
	};

	enum class Priority
	{
		High = 0,		// Default
		Low,		// Pool of low priority threads, useful for generic tasks that shouldn't interfere with high priority tasks
		Streaming,	// Single low priority thread, for streaming resources
		Count
	};

	// Defines a state of execution, can be waited on
	struct context
	{
		volatile long counter = 0;
		Priority priority = Priority::High;
	};

	struct contextConcurrency
	{
		uint32_t concurrentID = 0;
	};

	UTIL_EXPORT uint32_t GetThreadCount(Priority priority = Priority::High);

	// Add a task to execute asynchronously. Any idle thread will execute this.
	UTIL_EXPORT void Execute(context& ctx, const std::function<void(JobArgs)>& task);

	UTIL_EXPORT void ExecuteConcurrency(contextConcurrency& ctx, const std::function<void(JobArgs)>& task);

	// Divide a task onto multiple jobs and execute in parallel.
	//	jobCount	: how many jobs to generate for this task.
	//	groupSize	: how many jobs to execute per thread. Jobs inside a group execute serially. It might be worth to increase for small jobs
	//	task		: receives a JobArgs as parameter
	UTIL_EXPORT void Dispatch(context& ctx, uint32_t jobCount, uint32_t groupSize, const std::function<void(JobArgs)>& task, size_t sharedmemory_size = 0);

	// Returns the amount of job groups that will be created for a set number of jobs and group size
	UTIL_EXPORT uint32_t DispatchGroupCount(uint32_t jobCount, uint32_t groupSize);

	// Check if any threads are working currently or not
	UTIL_EXPORT bool IsBusy(const context& ctx);

	// Wait until all threads become idle
	//	Current thread will become a worker thread, executing jobs
	UTIL_EXPORT void Wait(const context& ctx);

	UTIL_EXPORT void WaitAllJobs();
}