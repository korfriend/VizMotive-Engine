#pragma once
#include "GBackend/GBackendDevice.h"

namespace vz::gpusortlib
{
	// Perform bitonic sort on a GPU dataset
	//	maxCount				-	Maximum size of the dataset. GPU count can be smaller (see: counterBuffer_read param)
	//	comparisonBuffer_read	-	Buffer containing values to compare by (Read Only)
	//	counterBuffer_read		-	Buffer containing count of values to sort (Read Only)
	//	counterReadOffset		-	Byte offset into the counter buffer to read the count value (Read Only)
	//	indexBuffer_write		-	The index list which to sort. Contains index values which can index the sortBase_read buffer. This will be modified (Read + Write)

	enum COMPARISON_TYPE
	{
		COMPARISON_FLOAT,
		COMPARISON_UINT64,
	};

	void Sort(
		uint32_t maxCount,
		const COMPARISON_TYPE comparisonType,
		const graphics::GPUBuffer& comparisonBuffer_read,
		const graphics::GPUBuffer& counterBuffer_read,
		uint32_t counterReadOffset,
		const graphics::GPUBuffer& indexBuffer_write,
		graphics::CommandList cmd
	);

	void Initialize();
	void Deinitialize();
};
