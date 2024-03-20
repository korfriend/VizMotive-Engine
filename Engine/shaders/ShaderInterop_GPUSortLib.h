#ifndef VZ_SHADERINTEROP_GPUSORTLIB_H
#define VZ_SHADERINTEROP_GPUSORTLIB_H

#include "ShaderInterop.h"

struct SortConstants
{
	int3 job_params;
	uint counterReadOffset;
};
PUSHCONSTANT(sort, SortConstants);

#endif // VZ_SHADERINTEROP_GPUSORTLIB_H
