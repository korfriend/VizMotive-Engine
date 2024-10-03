#include "Profiler.h"
#include "Common/Backend/GBackendDevice.h"
#include "Utils/Helpers.h"
#include "Utils/Backlog.h"
#include "Utils/Timer.h"
#include "Utils/EventHandler.h"

#if __has_include("Superluminal/PerformanceAPI_capi.h")
#include "Superluminal/PerformanceAPI_capi.h"
#include "Superluminal/PerformanceAPI_loader.h"
#endif // superluminal

#include <unordered_map>
#include <string>
#include <stack>
#include <mutex>
#include <atomic>
#include <sstream>

using namespace vz::graphics;

namespace vz::profiler
{
	bool ENABLED = false;
	bool ENABLED_REQUEST = false;
	bool initialized = false;
	std::mutex lock;
	range_id cpu_frame;
	range_id gpu_frame;
	GPUQueryHeap queryHeap;
	GPUBuffer queryResultBuffer[GraphicsDevice::GetBufferCount()];
	std::atomic<uint32_t> nextQuery{ 0 };
	uint32_t queryheap_idx = 0;
	bool drawn_this_frame = false;

#if PERFORMANCEAPI_ENABLED
	PerformanceAPI_ModuleHandle superluminal_handle = {};
	PerformanceAPI_Functions superluminal_functions = {};
#endif // PERFORMANCEAPI_ENABLED

	struct Range
	{
		bool in_use = false;
		std::string name;
		float times[20] = {};
		int avg_counter = 0;
		float time = 0;
		CommandList cmd;

		vz::Timer cpuTimer;

		int gpuBegin[arraysize(queryResultBuffer)];
		int gpuEnd[arraysize(queryResultBuffer)];

		bool IsCPURange() const { return !cmd.IsValid(); }
	};
	std::unordered_map<size_t, Range> ranges;

	void BeginFrame()
	{
		if (ENABLED_REQUEST != ENABLED)
		{
			ranges.clear();
			ENABLED = ENABLED_REQUEST;
		}

		if (!ENABLED)
			return;

		if (!initialized)
		{
			initialized = true;

			ranges.reserve(100);

			GraphicsDevice* device = graphics::GetDevice();

			GPUQueryHeapDesc desc;
			desc.type = GpuQueryType::TIMESTAMP;
			desc.query_count = 1024;
			bool success = device->CreateQueryHeap(&desc, &queryHeap);
			assert(success);

			GPUBufferDesc bd;
			bd.usage = Usage::READBACK;
			bd.size = desc.query_count * sizeof(uint64_t);

			for (int i = 0; i < arraysize(queryResultBuffer); ++i)
			{
				success = device->CreateBuffer(&bd, nullptr, &queryResultBuffer[i]);
				assert(success);
			}

#if PERFORMANCEAPI_ENABLED
			superluminal_handle = PerformanceAPI_LoadFrom(L"PerformanceAPI.dll", &superluminal_functions);
			if (superluminal_handle)
			{
				backlog::post("[profiler] Superluminal Performance API loaded");
			}
#endif // PERFORMANCEAPI_ENABLED
		}

		cpu_frame = BeginRangeCPU("CPU Frame");

		GraphicsDevice* device = graphics::GetDevice();
		CommandList cmd = device->BeginCommandList();
		queryheap_idx = device->GetBufferIndex();

		// Read results of previous timings:
		// This should be done before we begin reallocating new queries for current buffer index
		const uint64_t* queryResults = (const uint64_t*)queryResultBuffer[queryheap_idx].mapped_data;
		double gpu_frequency = (double)device->GetTimestampFrequency() / 1000.0;
		for (auto& x : ranges)
		{
			auto& range = x.second;
			if (!range.in_use)
				continue;

			if (!range.IsCPURange())
			{
				const int begin_idx = range.gpuBegin[queryheap_idx];
				const int end_idx = range.gpuEnd[queryheap_idx];
				if (queryResults != nullptr && begin_idx >= 0 && end_idx >= 0)
				{
					const uint64_t begin_result = queryResults[begin_idx];
					const uint64_t end_result = queryResults[end_idx];
					range.time = (float)abs((double)(end_result - begin_result) / gpu_frequency);
				}
				range.gpuBegin[queryheap_idx] = -1;
				range.gpuEnd[queryheap_idx] = -1;
			}
			range.times[range.avg_counter++ % arraysize(range.times)] = range.time;

			if (range.avg_counter > arraysize(range.times))
			{
				float avg_time = 0;
				for (int i = 0; i < arraysize(range.times); ++i)
				{
					avg_time += range.times[i];
				}
				range.time = avg_time / arraysize(range.times);
			}

			range.in_use = false;
		}

		device->QueryReset(
			&queryHeap,
			0,
			queryHeap.desc.query_count,
			cmd
		);

		gpu_frame = BeginRangeGPU("GPU Frame", &cmd);
		drawn_this_frame = false;
	}
	void EndFrame(CommandList* cmd)
	{
		if (!ENABLED || !initialized)
			return;

		GraphicsDevice* device = graphics::GetDevice();

		// note: read the GPU Frame end range manually because it will be on a separate command list than start point:
		auto& gpu_range = ranges[gpu_frame];
		gpu_range.gpuEnd[queryheap_idx] = nextQuery.fetch_add(1);
		device->QueryEnd(&queryHeap, gpu_range.gpuEnd[queryheap_idx], *cmd);

		EndRange(cpu_frame);

		device->QueryResolve(
			&queryHeap,
			0,
			nextQuery.load(),
			&queryResultBuffer[queryheap_idx],
			0ull,
			*cmd
		);

		nextQuery.store(0);
	}

	range_id BeginRangeCPU(const char* name)
	{
		if (!ENABLED || !initialized)
			return 0;

#if PERFORMANCEAPI_ENABLED
		if (superluminal_handle)
		{
			superluminal_functions.BeginEvent(name, nullptr, 0xFF0000FF);
		}
#endif // PERFORMANCEAPI_ENABLED

		range_id id = helper::string_hash(name);

		lock.lock();

		// If one range name is hit multiple times, differentiate between them!
		size_t differentiator = 0;
		while (ranges[id].in_use)
		{
			helper::hash_combine(id, differentiator++);
		}
		ranges[id].in_use = true;
		ranges[id].name = name;
		ranges[id].cpuTimer.record();

		lock.unlock();

		return id;
	}
	range_id BeginRangeGPU(const char* name, CommandList* cmd)
	{
		if (!ENABLED || !initialized)
			return 0;

		range_id id = helper::string_hash(name);

		lock.lock();

		// If one range name is hit multiple times, differentiate between them!
		size_t differentiator = 0;
		while (ranges[id].in_use)
		{
			helper::hash_combine(id, differentiator++);
		}
		ranges[id].in_use = true;
		ranges[id].name = name;
		ranges[id].cmd = *cmd;

		GraphicsDevice* device = graphics::GetDevice();
		ranges[id].gpuBegin[queryheap_idx] = nextQuery.fetch_add(1);
		device->QueryEnd(&queryHeap, ranges[id].gpuBegin[queryheap_idx], *cmd);

		lock.unlock();

		return id;
	}
	void EndRange(range_id id)
	{
		if (!ENABLED || !initialized)
			return;

		lock.lock();

		auto it = ranges.find(id);
		if (it != ranges.end())
		{
			if (it->second.IsCPURange())
			{
				it->second.time = (float)it->second.cpuTimer.elapsed();

#if PERFORMANCEAPI_ENABLED
				if (superluminal_handle)
				{
					superluminal_functions.EndEvent();
				}
#endif // PERFORMANCEAPI_ENABLED
			}
			else
			{
				GraphicsDevice* device = graphics::GetDevice();
				ranges[id].gpuEnd[queryheap_idx] = nextQuery.fetch_add(1);
				device->QueryEnd(&queryHeap, it->second.gpuEnd[queryheap_idx], it->second.cmd);
			}
		}
		else
		{
			assert(0);
		}

		lock.unlock();
	}


	struct Hits
	{
		uint32_t num_hits = 0;
		float total_time = 0;
	};
	std::unordered_map<std::string, Hits> time_cache_cpu;
	std::unordered_map<std::string, Hits> time_cache_gpu;
	void DisableDrawForThisFrame()
	{
		drawn_this_frame = true;
	}

	void SetEnabled(bool value)
	{
		// Don't enable/disable the profiler immediately, only on the next frame
		//	to avoid enabling inside a Begin/End by mistake
		ENABLED_REQUEST = value;
	}

	bool IsEnabled()
	{
		return ENABLED;
	}
}
