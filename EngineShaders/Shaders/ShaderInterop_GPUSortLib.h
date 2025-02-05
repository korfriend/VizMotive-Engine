#ifndef SHADERINTEROP_GPUSORTLIB_H
#define SHADERINTEROP_GPUSORTLIB_H

#include "ShaderInterop.h"

struct SortConstants
{
	int3 job_params;
	uint counterReadOffset;
};
PUSHCONSTANT(sort, SortConstants);

#endif // SHADERINTEROP_GPUSORTLIB_H
