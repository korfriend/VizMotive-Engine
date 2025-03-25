#include "SortLib.h"
#include "ShaderLoader.h"
#include "../Shaders/ShaderInterop_GPUSortLib.h"
#include "Utils/Timer.h"
#include "Utils/Backlog.h"
#include "Utils/EventHandler.h"

namespace vz::gpusortlib
{
	using namespace graphics;

	static GPUBuffer indirectBuffer;
	static Shader kickoffSortCS;

	static Shader sortCS;
	static Shader sortInnerCS;
	static Shader sortStepCS;

	static Shader sortCS_uint64;
	static Shader sortInnerCS_uint64;
	static Shader sortStepCS_uint64;


	void LoadShaders()
	{
		shader::LoadShader(ShaderStage::CS, kickoffSortCS, "gpusortlib_kickoffSortCS.cso");
		
		shader::LoadShader(ShaderStage::CS, sortCS, "gpusortlib_sortCS.cso");
		shader::LoadShader(ShaderStage::CS, sortInnerCS, "gpusortlib_sortInnerCS.cso");
		shader::LoadShader(ShaderStage::CS, sortStepCS, "gpusortlib_sortStepCS.cso");

		shader::LoadShader(ShaderStage::CS, sortCS_uint64, "gpusortlib_sortCS_uint64.cso");
		shader::LoadShader(ShaderStage::CS, sortInnerCS_uint64, "gpusortlib_sortInnerCS_uint64.cso");
		shader::LoadShader(ShaderStage::CS, sortStepCS_uint64, "gpusortlib_sortStepCS_uint64.cso");
	}

	void Initialize()
	{
		Timer timer;

		GPUBufferDesc bd;
		bd.usage = Usage::DEFAULT;
		bd.bind_flags = BindFlag::UNORDERED_ACCESS;
		bd.misc_flags = ResourceMiscFlag::INDIRECT_ARGS | ResourceMiscFlag::BUFFER_RAW;
		bd.size = sizeof(IndirectDispatchArgs);
		graphics::GetDevice()->CreateBuffer(&bd, nullptr, &indirectBuffer);

		static eventhandler::Handle handle = eventhandler::Subscribe(eventhandler::EVENT_RELOAD_SHADERS, [](uint64_t userdata) { LoadShaders(); });
		LoadShaders();

		vzlog("vz::gpusortlib Initialized (%d ms)", (int)std::round(timer.elapsed()));
	}

	void Deinitialize()
	{
		indirectBuffer = {};
		kickoffSortCS = {};

		sortCS = {};
		sortInnerCS = {};
		sortStepCS = {};

		sortCS_uint64 = {};
		sortInnerCS_uint64 = {};
		sortStepCS_uint64 = {};
	}

	void Sort(
		uint32_t maxCount, const COMPARISON_TYPE comparisonType,
		const GPUBuffer& comparisonBuffer_read,
		const GPUBuffer& counterBuffer_read,
		uint32_t counterReadOffset,
		const GPUBuffer& indexBuffer_write,
		CommandList cmd)
	{
		GraphicsDevice* device = graphics::GetDevice();

		device->EventBegin("GPUSortLib", cmd);


		SortConstants sort;
		sort.counterReadOffset = counterReadOffset;

		// initialize sorting arguments:
		{
			device->BindComputeShader(&kickoffSortCS, cmd);

			const GPUResource* res[] = {
				&counterBuffer_read,
			};
			device->BindResources(res, 0, arraysize(res), cmd);

			const GPUResource* uavs[] = {
				&indirectBuffer,
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Buffer(&indirectBuffer, ResourceState::INDIRECT_ARGUMENT, ResourceState::UNORDERED_ACCESS)
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->PushConstants(&sort, sizeof(sort), cmd);
			device->Dispatch(1, 1, 1, cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Memory(),
					GPUBarrier::Buffer(&indirectBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::INDIRECT_ARGUMENT)
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

		}


		const GPUResource* uavs[] = {
			&indexBuffer_write,
		};
		device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

		const GPUResource* resources[] = {
			&counterBuffer_read,
			&comparisonBuffer_read,
		};
		device->BindResources(resources, 0, arraysize(resources), cmd);

		// initial sorting:
		bool bDone = true;
		{
			// calculate how many threads we'll require:
			//   we'll sort 512 elements per CU (threadgroupsize 256)
			//     maybe need to optimize this or make it changeable during init
			//     TGS=256 is a good intermediate value

			unsigned int numThreadGroups = ((maxCount - 1) >> 9) + 1;

			//assert(numThreadGroups <= 1024);

			if (numThreadGroups > 1)
			{
				bDone = false;
			}

			// sort all buffers of size 512 (and presort bigger ones)

			switch (comparisonType)
			{
			case COMPARISON_FLOAT: device->BindComputeShader(&sortCS, cmd); break;
			case COMPARISON_UINT64: device->BindComputeShader(&sortCS_uint64, cmd); break;
			}
			device->PushConstants(&sort, sizeof(sort), cmd);
			device->DispatchIndirect(&indirectBuffer, 0, cmd);

			GPUBarrier barriers[] = {
				GPUBarrier::Memory(),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		int presorted = 512;
		while (!bDone)
		{
			// Incremental sorting:

			bDone = true;
			switch (comparisonType)
			{
			case COMPARISON_FLOAT: device->BindComputeShader(&sortStepCS, cmd); break;
			case COMPARISON_UINT64: device->BindComputeShader(&sortStepCS_uint64, cmd); break;
			}

			// prepare thread group description data
			uint32_t numThreadGroups = 0;

			if (maxCount > (uint32_t)presorted)
			{
				if (maxCount > (uint32_t)presorted * 2)
					bDone = false;

				uint32_t pow2 = presorted;
				while (pow2 < maxCount)
					pow2 *= 2;
				numThreadGroups = pow2 >> 9;
			}

			uint32_t nMergeSize = presorted * 2;
			for (uint32_t nMergeSubSize = nMergeSize >> 1; nMergeSubSize > 256; nMergeSubSize = nMergeSubSize >> 1)
			{
				SortConstants sort;
				sort.job_params.x = nMergeSubSize;
				if (nMergeSubSize == nMergeSize >> 1)
				{
					sort.job_params.y = (2 * nMergeSubSize - 1);
					sort.job_params.z = -1;
				}
				else
				{
					sort.job_params.y = nMergeSubSize;
					sort.job_params.z = 1;
				}
				sort.counterReadOffset = counterReadOffset;

				device->PushConstants(&sort, sizeof(sort), cmd);
				device->Dispatch(numThreadGroups, 1, 1, cmd);

				GPUBarrier barriers[] = {
					GPUBarrier::Memory(),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			switch (comparisonType)
			{
			case COMPARISON_FLOAT: device->BindComputeShader(&sortInnerCS, cmd); break;
			case COMPARISON_UINT64: device->BindComputeShader(&sortInnerCS_uint64, cmd); break;
			}
			device->PushConstants(&sort, sizeof(sort), cmd);
			device->Dispatch(numThreadGroups, 1, 1, cmd);

			GPUBarrier barriers[] = {
				GPUBarrier::Memory(),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);

			presorted *= 2;
		}

		device->EventEnd(cmd);
	}

}
