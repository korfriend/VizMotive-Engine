#include "JobSystem.h"
#include "Backlog.h"
#include "Platform.h"
#include "Timer.h"

#include "CommonInclude.h"

#include <assert.h>
#include <memory>
#include <algorithm>
#include <deque>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>

#ifdef PLATFORM_LINUX
#include <pthread.h>
#endif // PLATFORM_LINUX

namespace vz::jobsystem
{
	struct Job
	{
		std::function<void(JobArgs)> task;
		context* ctx = nullptr;
		contextConcurrency* ctxConcurrency = nullptr;
		uint32_t groupID = 0;
		uint32_t groupJobOffset = 0;
		uint32_t groupJobEnd = 0;
		uint32_t sharedmemory_size = 0;
		uint32_t concurrentID = 0;

		inline void execute()
		{
			static std::atomic<int> max_ctx_counter = 0;
			static std::mutex locker;
			{
				std::scoped_lock lock(locker);
				if (ctx->counter > max_ctx_counter.load())
				{
					backlog::post("Increases the capacity of job queues to " + std::to_string(ctx->counter), LogLevel::Info);
					max_ctx_counter = ctx->counter;
				}
			}

			JobArgs args;
			args.groupID = groupID;
			if (sharedmemory_size > 0)
			{
				args.sharedmemory = _malloca(sharedmemory_size);
				//args.sharedmemory = alloca(sharedmemory_size);
			}
			else
			{
				args.sharedmemory = nullptr;
			}

			for (uint32_t j = groupJobOffset; j < groupJobEnd; ++j)
			{
				args.jobIndex = j;
				args.groupIndex = j - groupJobOffset;
				args.isFirstJobInGroup = (j == groupJobOffset);
				args.isLastJobInGroup = (j == groupJobEnd - 1);
				task(args);
			}

			if (args.sharedmemory)
			{
				_freea(args.sharedmemory);
			}

			AtomicAdd(&ctx->counter, -1);
		}

		inline void executeConcurrent()
		{
			JobArgs args;
			args.groupID = groupID;
			if (sharedmemory_size > 0)
			{
				args.sharedmemory = _malloca(sharedmemory_size);
			}
			else
			{
				args.sharedmemory = nullptr;
			}

			for (uint32_t j = groupJobOffset; j < groupJobEnd; ++j)
			{
				args.jobIndex = j;
				args.groupIndex = j - groupJobOffset;
				args.isFirstJobInGroup = (j == groupJobOffset);
				args.isLastJobInGroup = (j == groupJobEnd - 1);
				task(args);
			}

			if (args.sharedmemory)
			{
				_freea(args.sharedmemory);
			}
		}
	};
	struct JobQueue
	{
		std::deque<Job> queue;
		std::mutex locker;

		inline void push_back(const Job& item)
		{
			std::scoped_lock lock(locker);
			queue.push_back(item);
		}
		inline bool pop_front(Job& item)
		{
			std::scoped_lock lock(locker);
			if (queue.empty())
			{
				return false;
			}
			item = std::move(queue.front());
			queue.pop_front();
			return true;
		}
	};

	struct JobConcurrentQueue
	{
		std::unordered_map<uint32_t, std::deque<Job>> mapQueue;
		std::mutex locker;

		int MAX_QUEUE = 5;

		inline static std::atomic_int max_concurrent_queue_count = 0u;

		inline void push_back(const Job& item, const uint32_t concurrentID)
		{
			std::scoped_lock lock(locker);

			std::deque<Job>& queue = mapQueue[concurrentID];
			queue.push_back(item);
			if (max_concurrent_queue_count.load() < queue.size())
			{
				max_concurrent_queue_count++;
				backlog::post("Increases the capacity of concurrent job queues to " + std::to_string(max_concurrent_queue_count));
			}
			if (queue.size() > MAX_QUEUE)
			{
				queue.pop_front();
			}
		}
		inline bool pop_back(Job& item)
		{
			std::scoped_lock lock(locker);

			bool isPopped = false;
			auto it = mapQueue.begin();
			for (; it != mapQueue.end(); it++)
			{
				std::deque<Job>& queue = it->second;
				if (queue.empty())
				{
					continue;
				}
				item = std::move(queue.back()); // get latest
				isPopped = true;
				//queue.clear();
				break;
			}
			if (isPopped)
			{
				mapQueue.erase(it);
			}
			return isPopped;
		}
	};

	struct PriorityResources
	{
		uint32_t numThreads = 0;
		std::vector<std::thread> threads;
		std::unique_ptr<JobQueue[]> jobQueuePerThread;
		std::unique_ptr<JobConcurrentQueue> jobConcurrentQueue; // all threads can access
		std::atomic<uint32_t> nextQueue{ 0 };
		std::condition_variable wakeCondition;
		std::mutex wakeMutex;

		// Start working on a job queue
		//	After the job queue is finished, it can switch to an other queue and steal jobs from there
		inline void work(uint32_t startingQueue)
		{
			Job job;
			for (uint32_t i = 0; i < numThreads; ++i)
			{
				uint32_t thread_id = startingQueue % numThreads;
				JobQueue& job_queue = jobQueuePerThread[thread_id];
				while (job_queue.pop_front(job))
				{
					job.execute();
				}

				//JobConcurrentQueue& job_concurrent_queue = jobConcurrentQueuePerThread[thread_id];
				//while (job_concurrent_queue.pop_back(job))
				//{
				//	job.executeConcurrent();
				//}
				startingQueue++; // go to next queue
			}

			// while executing the popped job, other threads can steal the remaining jobs
			while (jobConcurrentQueue->pop_back(job))
			{
				job.executeConcurrent();
			}
		}
	};

	// This structure is responsible to stop worker thread loops.
	//	Once this is destroyed, worker threads will be woken up and end their loops.
	struct InternalState
	{
		uint32_t numCores = 0;
		PriorityResources resources[int(Priority::Count)];
		std::atomic_bool alive{ true };
		void ShutDown()
		{
			if (IsShuttingDown())
				return;

			vzlog("Jobsystem Shutdown...");
			WaitAll();

			for (auto& x : resources)
			{
				x.jobQueuePerThread.reset();
				x.jobConcurrentQueue.reset();
				x.threads.clear();
				x.numThreads = 0;
			}
			numCores = 0;
		}
		void WaitAll()
		{
			alive.store(false); // indicate that new jobs cannot be started from this point
			bool wake_loop = true;
			std::thread waker([&] {
				while (wake_loop)
				{
					for (auto& x : resources)
					{
						x.wakeCondition.notify_all(); // wakes up sleeping worker threads
					}
				}
				});
			for (auto& x : resources) // priorities
			{
				for (auto& thread : x.threads)
				{
					if (thread.joinable())  // Check if the thread can be joined
					{
						thread.join();
					}
				}
			}
			wake_loop = false;
			waker.join();
		}
		~InternalState()
		{
			ShutDown();
		}
	} static internal_state;

	void Initialize(uint32_t maxThreadCount)
	{
		if (internal_state.numCores > 0)
			return;
		maxThreadCount = std::max(1u, maxThreadCount);

		vz::Timer timer;

		// Retrieve the number of hardware threads in this system:
		internal_state.numCores = std::thread::hardware_concurrency();

		for (int prio = 0; prio < int(Priority::Count); ++prio)
		{
			const Priority priority = (Priority)prio;
			PriorityResources& res = internal_state.resources[prio];

			// Calculate the actual number of worker threads we want:
			switch (priority)
			{
			case Priority::High:
				res.numThreads = internal_state.numCores - 1; // -1 for main thread
				break;
			case Priority::Low:
				res.numThreads = internal_state.numCores - 2; // -1 for main thread, -1 for streaming
				break;
			case Priority::Streaming:
				res.numThreads = 1;
				break;
			default:
				assert(0);
				break;
			}
			res.numThreads = clamp(res.numThreads, 1u, maxThreadCount);
			res.jobQueuePerThread.reset(new JobQueue[res.numThreads]);
			res.jobConcurrentQueue.reset(new JobConcurrentQueue);
			res.threads.reserve(res.numThreads);

			for (uint32_t threadID = 0; threadID < res.numThreads; ++threadID)
			{
#ifdef PLATFORM_LINUX
				std::thread& worker = res.threads.emplace_back([threadID, priority, &res] {

					switch (priority) {
					case Priority::Low:
					case Priority::Streaming:
						// from the sched(2) manpage:
						// In the current [Linux 2.6.23+] implementation, each unit of
						// difference in the nice values of two processes results in a
						// factor of 1.25 in the degree to which the scheduler favors
						// the higher priority process.
						//
						// so 3 would mean that other (prio 0) threads are around twice as important
						if (setpriority(PRIO_PROCESS, 0, 3) != 0)
						{
							perror("setpriority");
						}
						break;
					case Priority::High:
						// nothing to do
						break;
					default:
						assert(0);
					}
#else
				std::thread& worker = res.threads.emplace_back([threadID, &res] {
#endif
					while (internal_state.alive.load())
					{
						res.work(threadID);

						// finished with jobs, put to sleep
						std::unique_lock<std::mutex> lock(res.wakeMutex);
						res.wakeCondition.wait(lock);
					}

					});

				auto handle = worker.native_handle();

				int core = threadID + 1; // put threads on increasing cores starting from 2nd
				if (priority == Priority::Streaming)
				{
					// Put streaming to last core:
					core = internal_state.numCores - 1 - threadID;
				}

#ifdef _WIN32
				// Do Windows-specific thread setup:

				// Put each thread on to dedicated core:
				DWORD_PTR affinityMask = 1ull << core;
				DWORD_PTR affinity_result = SetThreadAffinityMask(handle, affinityMask);
				assert(affinity_result > 0);

				if (priority == Priority::High)
				{
					BOOL priority_result = SetThreadPriority(handle, THREAD_PRIORITY_NORMAL);
					assert(priority_result != 0);

					std::wstring wthreadname = L"vz::job_" + std::to_wstring(threadID);
					HRESULT hr = SetThreadDescription(handle, wthreadname.c_str());
					assert(SUCCEEDED(hr));
				}
				else if (priority == Priority::Low)
				{
					BOOL priority_result = SetThreadPriority(handle, THREAD_PRIORITY_LOWEST);
					assert(priority_result != 0);

					std::wstring wthreadname = L"vz::job_lo_" + std::to_wstring(threadID);
					HRESULT hr = SetThreadDescription(handle, wthreadname.c_str());
					assert(SUCCEEDED(hr));
				}
				else if (priority == Priority::Streaming)
				{
					BOOL priority_result = SetThreadPriority(handle, THREAD_PRIORITY_LOWEST);
					assert(priority_result != 0);

					std::wstring wthreadname = L"vz::job_st_" + std::to_wstring(threadID);
					HRESULT hr = SetThreadDescription(handle, wthreadname.c_str());
					assert(SUCCEEDED(hr));
				}

#elif defined(PLATFORM_LINUX)
#define handle_error_en(en, msg) \
               do { errno = en; perror(msg); } while (0)

				int ret;
				cpu_set_t cpuset;
				CPU_ZERO(&cpuset);
				size_t cpusetsize = sizeof(cpuset);

				CPU_SET(core, &cpuset);
				ret = pthread_setaffinity_np(handle, cpusetsize, &cpuset);
				if (ret != 0)
					handle_error_en(ret, std::string(" pthread_setaffinity_np[" + std::to_string(threadID) + ']').c_str());


				if (priority == Priority::High)
				{
					std::string thread_name = "vz::job_" + std::to_string(threadID);
					ret = pthread_setname_np(handle, thread_name.c_str());
					if (ret != 0)
						handle_error_en(ret, std::string(" pthread_setname_np[" + std::to_string(threadID) + ']').c_str());
				}
				else if (priority == Priority::Low)
				{
					// TODO: set lower priority

					std::string thread_name = "vz::job_lo_" + std::to_string(threadID);
					ret = pthread_setname_np(handle, thread_name.c_str());
					if (ret != 0)
						handle_error_en(ret, std::string(" pthread_setname_np[" + std::to_string(threadID) + ']').c_str());
				}
				else if (priority == Priority::Streaming)
				{
					// TODO: set lower priority

					std::string thread_name = "vz::job_st_" + std::to_string(threadID);
					ret = pthread_setname_np(handle, thread_name.c_str());
					if (ret != 0)
						handle_error_en(ret, std::string(" pthread_setname_np[" + std::to_string(threadID) + ']').c_str());
				}

#undef handle_error_en
#endif // _WIN32
			}
		}

		char msg[256] = {};
		snprintf(msg, arraysize(msg), "vz::jobsystem Initialized with %d cores in %.2f ms\n\tHigh priority threads: %d\n\tLow priority threads: %d\n\tStreaming threads: %d", internal_state.numCores, timer.elapsed(), GetThreadCount(Priority::High), GetThreadCount(Priority::Low), GetThreadCount(Priority::Streaming));
		vz::backlog::post(msg);
	}

	void ShutDown()
	{
		internal_state.ShutDown();
	}

	bool IsShuttingDown()
	{
		return internal_state.alive.load() == false;
	}

	uint32_t GetThreadCount(Priority priority)
	{
		return internal_state.resources[int(priority)].numThreads;
	}

	void Execute(context& ctx, const std::function<void(JobArgs)>& task)
	{
		vzlog_assert(ctx.IsAvailable(), "CRITICAL JOBSYSTEM MISUSE!: Invalid context passed to jobsystem API");

		PriorityResources& res = internal_state.resources[int(ctx.priority)];

		// Context state is updated:
		AtomicAdd(&ctx.counter, 1);

		Job job;
		job.ctx = &ctx;
		job.task = task;
		job.groupID = 0;
		job.groupJobOffset = 0;
		job.groupJobEnd = 1;
		job.sharedmemory_size = 0;

		if (res.numThreads < 1)
		{
			// If job system is not yet initialized, or only has one threads, job will be executed immediately here instead of thread:
			job.execute();
			return;
		}

		res.jobQueuePerThread[res.nextQueue.fetch_add(1) % res.numThreads].push_back(job);

		res.wakeCondition.notify_one();
	}

	void ExecuteConcurrency(contextConcurrency& ctx, const std::function<void(JobArgs)>& task)
	{
		vzlog_assert(ctx.IsAvailable(), "CRITICAL JOBSYSTEM MISUSE!: Invalid context passed to jobsystem API");

		PriorityResources& res = internal_state.resources[int(Priority::Low)];

		Job job;
		job.ctxConcurrency = &ctx;
		job.task = task;
		job.groupID = 0;
		job.groupJobOffset = 0;
		job.groupJobEnd = 1;
		job.sharedmemory_size = 0;
		job.concurrentID = ctx.concurrentID;

		if (res.numThreads < 1)
		{
			// If job system is not yet initialized, or only has one threads, job will be executed immediately here instead of thread:
			job.executeConcurrent();
			return;
		}

		//res.jobConcurrentQueuePerThread[res.nextQueue.fetch_add(1) % res.numThreads].push_back(job, ctx.concurrentID);
		res.jobConcurrentQueue->push_back(job, ctx.concurrentID);

		res.wakeCondition.notify_one();
	}

	void Dispatch(context& ctx, uint32_t jobCount, uint32_t groupSize, const std::function<void(JobArgs)>& task, size_t sharedmemory_size)
	{
		vzlog_assert(ctx.IsAvailable(), "CRITICAL JOBSYSTEM MISUSE!: Invalid context passed to jobsystem API");

		if (jobCount == 0 || groupSize == 0)
		{
			return;
		}
		PriorityResources& res = internal_state.resources[int(ctx.priority)];

		const uint32_t groupCount = DispatchGroupCount(jobCount, groupSize);

		// Context state is updated:
		AtomicAdd(&ctx.counter, groupCount);

		Job job;
		job.ctx = &ctx;
		job.task = task;
		job.sharedmemory_size = (uint32_t)sharedmemory_size;

		for (uint32_t groupID = 0; groupID < groupCount; ++groupID)
		{
			// For each group, generate one real job:
			job.groupID = groupID;
			job.groupJobOffset = groupID * groupSize;
			job.groupJobEnd = std::min(job.groupJobOffset + groupSize, jobCount);

			if (res.numThreads < 1)
			{
				// If job system is not yet initialized, job will be executed immediately here instead of thread:
				job.execute();
			}
			else
			{
				res.jobQueuePerThread[res.nextQueue.fetch_add(1) % res.numThreads].push_back(job);
			}
		}

		if (res.numThreads > 1)
		{
			res.wakeCondition.notify_all();
		}
	}

	uint32_t DispatchGroupCount(uint32_t jobCount, uint32_t groupSize)
	{
		// Calculate the amount of job groups to dispatch (overestimate, or "ceil"):
		return (jobCount + groupSize - 1) / groupSize;
	}

	bool IsBusy(const context& ctx)
	{
		vzlog_assert(ctx.IsAvailable(), "CRITICAL JOBSYSTEM MISUSE!: Invalid context passed to jobsystem API");

		// Whenever the context label is greater than zero, it means that there is still work that needs to be done
		return AtomicLoad(&ctx.counter) > 0;
	}

	void Wait(const context& ctx)
	{
		vzlog_assert(ctx.IsAvailable(), "CRITICAL JOBSYSTEM MISUSE!: Invalid context passed to jobsystem API");
		if (IsBusy(ctx))
		{
			PriorityResources& res = internal_state.resources[int(ctx.priority)];

			// Wake any threads that might be sleeping:
			res.wakeCondition.notify_all();

			// work() will pick up any jobs that are on stand by and execute them on this thread:
			res.work(res.nextQueue.fetch_add(1) % res.numThreads);

			while (IsBusy(ctx))
			{
				// If we are here, then there are still remaining jobs that work() couldn't pick up.
				//	In this case those jobs are not standing by on a queue but currently executing
				//	on other threads, so they cannot be picked up by this thread.
				//	Allow to swap out this thread by OS to not spin endlessly for nothing
				std::this_thread::yield();
			}
		}
	}

	void WaitAllJobs()
	{
		bool is_alive_state = internal_state.alive.load();
		internal_state.WaitAll();
		if (is_alive_state)
		{
			internal_state.alive.store(true);
		}
	}
}