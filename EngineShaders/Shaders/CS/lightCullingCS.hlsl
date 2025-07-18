#include "../Globals.hlsli"
#include "../CommonHF/cullingShaderHF.hlsli"
#include "../CommonHF/lightingHF.hlsli"

#define entityCount (GetFrame().entity_culling_count)

RWStructuredBuffer<uint> entityTiles : register(u0);

// Group shared variables.
groupshared uint uMinDepth;
groupshared uint uMaxDepth;
groupshared uint uDepthMask;		// Harada Siggraph 2012 2.5D culling
groupshared uint tile_opaque[SHADER_ENTITY_TILE_BUCKET_COUNT];
groupshared uint tile_transparent[SHADER_ENTITY_TILE_BUCKET_COUNT];
#ifdef DEBUG_TILEDLIGHTCULLING
groupshared uint entityCountDebug;
RWTexture2D<unorm float4> DebugTexture : register(u3);
#endif // DEBUG_TILEDLIGHTCULLING

void AppendEntity_Opaque(uint entityIndex)
{
	const uint bucket_index = entityIndex / 32;
	const uint bucket_place = entityIndex % 32;
	InterlockedOr(tile_opaque[bucket_index], 1u << bucket_place);

#ifdef DEBUG_TILEDLIGHTCULLING
	InterlockedAdd(entityCountDebug, 1);
#endif // DEBUG_TILEDLIGHTCULLING
}

void AppendEntity_Transparent(uint entityIndex)
{
	const uint bucket_index = entityIndex / 32;
	const uint bucket_place = entityIndex % 32;
	InterlockedOr(tile_transparent[bucket_index], 1u << bucket_place);
}

inline uint ConstructEntityMask(in float depthRangeMin, in float depthRangeRecip, in Sphere bounds)
{
	// We create a entity mask to decide if the entity is really touching something
	// If we do an OR operation with the depth slices mask, we instantly get if the entity is contributing or not
	// we do this in view space

	const float fMin = bounds.c.z - bounds.r;
	const float fMax = bounds.c.z + bounds.r;
	const uint __entitymaskcellindexSTART = clamp(floor((fMin - depthRangeMin) * depthRangeRecip), 0, 31);
	const uint __entitymaskcellindexEND = clamp(floor((fMax - depthRangeMin) * depthRangeRecip), 0, 31);

	//// Unoptimized mask construction with loop:
	//// Construct mask from START to END:
	////          END         START
	////	0000000000111111111110000000000
	//uint uLightMask = 0;
	//for (uint c = __entitymaskcellindexSTART; c <= __entitymaskcellindexEND; ++c)
	//{
	//	uLightMask |= 1u << c;
	//}
	
	// Optimized mask construction, without loop:
	//	- First, fill full mask:
	//	1111111111111111111111111111111
	uint uLightMask = 0xFFFFFFFF;
	//	- Then Shift right with spare amount to keep mask only:
	//	0000000000000000000011111111111
	uLightMask >>= 31u - (__entitymaskcellindexEND - __entitymaskcellindexSTART);
	//	- Last, shift left with START amount to correct mask position:
	//	0000000000111111111110000000000
	uLightMask <<= __entitymaskcellindexSTART;

	return uLightMask;
}

[numthreads(TILED_CULLING_THREADSIZE, TILED_CULLING_THREADSIZE, 1)]
void main(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
	uint2 dim;
	texture_depth.GetDimensions(dim.x, dim.y);
	float2 dim_rcp = rcp(dim);

	// Each thread will zero out one bucket in the LDS:
	for (uint i = groupIndex; i < SHADER_ENTITY_TILE_BUCKET_COUNT; i += TILED_CULLING_THREADSIZE * TILED_CULLING_THREADSIZE)
	{
		tile_opaque[i] = 0;
		tile_transparent[i] = 0;
	}

	// First thread zeroes out other LDS data:
	if (groupIndex == 0)
	{
		uMinDepth = 0xffffffff;
		uMaxDepth = 0;
		uDepthMask = 0;

#ifdef DEBUG_TILEDLIGHTCULLING
		entityCountDebug = 0;
#endif //  DEBUG_TILEDLIGHTCULLING
	}

	// Calculate min & max depth in threadgroup / tile.
	float depth[TILED_CULLING_GRANULARITY * TILED_CULLING_GRANULARITY];
	float depthMinUnrolled = 10000000;
	float depthMaxUnrolled = -10000000;

	[unroll]
	for (uint granularity = 0; granularity < TILED_CULLING_GRANULARITY * TILED_CULLING_GRANULARITY; ++granularity)
	{
		uint2 pixel = DTid.xy * uint2(TILED_CULLING_GRANULARITY, TILED_CULLING_GRANULARITY) + unflatten2D(granularity, TILED_CULLING_GRANULARITY);
		pixel = min(pixel, dim - 1); // avoid loading from outside the texture, it messes up the min-max depth!
		float zDepth = texture_depth[pixel];
		depth[granularity] = zDepth;
		depthMinUnrolled = min(depthMinUnrolled, zDepth);
		depthMaxUnrolled = max(depthMaxUnrolled, zDepth);
	}

	GroupMemoryBarrierWithGroupSync();

	float wave_local_min = WaveActiveMin(depthMinUnrolled);
	float wave_local_max = WaveActiveMax(depthMaxUnrolled);
	if (WaveIsFirstLane())
	{
		InterlockedMin(uMinDepth, asuint(wave_local_min));
		InterlockedMax(uMaxDepth, asuint(wave_local_max));
	}

	GroupMemoryBarrierWithGroupSync();

	// reversed depth buffer!
	float fMinDepth = asfloat(uMaxDepth);
	float fMaxDepth = asfloat(uMinDepth);

	Frustum GroupFrustum;
	AABB GroupAABB;			// frustum AABB around min-max depth in View Space
	AABB GroupAABB_WS;		// frustum AABB in world space
	{
		// View space frustum corners:
		float3 viewSpace[8];
		// Top left point, near
		viewSpace[0] = ScreenToView(float4(Gid.xy * TILED_CULLING_BLOCKSIZE, fMinDepth, 1.0f), dim_rcp).xyz;
		// Top right point, near
		viewSpace[1] = ScreenToView(float4(float2(Gid.x + 1, Gid.y) * TILED_CULLING_BLOCKSIZE, fMinDepth, 1.0f), dim_rcp).xyz;
		// Bottom left point, near
		viewSpace[2] = ScreenToView(float4(float2(Gid.x, Gid.y + 1) * TILED_CULLING_BLOCKSIZE, fMinDepth, 1.0f), dim_rcp).xyz;
		// Bottom right point, near
		viewSpace[3] = ScreenToView(float4(float2(Gid.x + 1, Gid.y + 1) * TILED_CULLING_BLOCKSIZE, fMinDepth, 1.0f), dim_rcp).xyz;
		// Top left point, far
		viewSpace[4] = ScreenToView(float4(Gid.xy * TILED_CULLING_BLOCKSIZE, fMaxDepth, 1.0f), dim_rcp).xyz;
		// Top right point, far
		viewSpace[5] = ScreenToView(float4(float2(Gid.x + 1, Gid.y) * TILED_CULLING_BLOCKSIZE, fMaxDepth, 1.0f), dim_rcp).xyz;
		// Bottom left point, far
		viewSpace[6] = ScreenToView(float4(float2(Gid.x, Gid.y + 1) * TILED_CULLING_BLOCKSIZE, fMaxDepth, 1.0f), dim_rcp).xyz;
		// Bottom right point, far
		viewSpace[7] = ScreenToView(float4(float2(Gid.x + 1, Gid.y + 1) * TILED_CULLING_BLOCKSIZE, fMaxDepth, 1.0f), dim_rcp).xyz;
	
		// Left plane
		GroupFrustum.planes[0] = ComputePlane(viewSpace[2], viewSpace[0], viewSpace[4]);
		// Right plane
		GroupFrustum.planes[1] = ComputePlane(viewSpace[1], viewSpace[3], viewSpace[5]);
		// Top plane
		GroupFrustum.planes[2] = ComputePlane(viewSpace[0], viewSpace[1], viewSpace[4]);
		// Bottom plane
		GroupFrustum.planes[3] = ComputePlane(viewSpace[3], viewSpace[2], viewSpace[6]);
		
		// I construct an AABB around the minmax depth bounds to perform tighter culling:
		// The frustum is asymmetric so we must consider all corners!
		float3 minAABB = 10000000;
		float3 maxAABB = -10000000;
		[unroll]
		for (uint i = 0; i < 8; ++i)
		{
			minAABB = min(minAABB, viewSpace[i]);
			maxAABB = max(maxAABB, viewSpace[i]);
		}

		AABBfromMinMax(GroupAABB, minAABB, maxAABB);

		// We can perform coarse AABB intersection tests with this:
		GroupAABB_WS = GroupAABB;
		AABBtransform(GroupAABB_WS, GetCamera().inverse_view);
	}

	// Convert depth values to view space.
	float minDepthVS = -ScreenToView(float4(0, 0, fMinDepth, 1), dim_rcp).z;
	float maxDepthVS = -ScreenToView(float4(0, 0, fMaxDepth, 1), dim_rcp).z;
	float nearClipVS = -ScreenToView(float4(0, 0, 1, 1), dim_rcp).z;

#ifdef ADVANCED_CULLING
	// We divide the minmax depth bounds to 32 equal slices
	// then we mark the occupied depth slices with atomic or from each thread
	// we do all this in linear (view) space
	const float __depthRangeRecip = 31.0f / (maxDepthVS - minDepthVS);
	uint __depthmaskUnrolled = 0;

	[unroll]
	for (uint granularity = 0; granularity < TILED_CULLING_GRANULARITY * TILED_CULLING_GRANULARITY; ++granularity)
	{
		float realDepthVS = -ScreenToView(float4(0, 0, depth[granularity], 1), dim_rcp).z;
		const uint __depthmaskcellindex = max(0, min(31, floor((realDepthVS - minDepthVS) * __depthRangeRecip)));
		__depthmaskUnrolled |= 1u << __depthmaskcellindex;
	}

	uint wave_depth_mask = WaveActiveBitOr(__depthmaskUnrolled);
	if (WaveIsFirstLane())
	{
		InterlockedOr(uDepthMask, wave_depth_mask);
	}
#endif

	GroupMemoryBarrierWithGroupSync();

	const uint depth_mask = uDepthMask; // take out from groupshared into register

	// IMPORTANT
	// This is an optimized version of the above, separated entity processing loops by type, reduces divergence and increases performance:

	// Point lights:
	for (uint i = pointlights().first_item() + groupIndex; i < pointlights().end_item(); i += TILED_CULLING_THREADSIZE * TILED_CULLING_THREADSIZE)
	{
		ShaderEntity entity = load_entity(i);
		
		if (entity.GetFlags() & ENTITY_FLAG_LIGHT_STATIC)
			continue; // static lights will be skipped here (they are used at lightmap baking)
		float3 positionVS = mul(GetCamera().view, float4(entity.position, 1)).xyz;
		
		Sphere sphere = { positionVS.xyz, entity.GetRange() + entity.GetLength() };

		if (SphereInsideFrustumVS(sphere, GroupFrustum, nearClipVS, maxDepthVS))
		{
			AppendEntity_Transparent(i);

			if (SphereIntersectsAABB(sphere, GroupAABB)) // tighter fit than sphere-frustum culling
			{
#ifdef ADVANCED_CULLING
				if (depth_mask & ConstructEntityMask(minDepthVS, __depthRangeRecip, sphere))
#endif
				{
					AppendEntity_Opaque(i);
				}
			}
		}
	}

	// Spot lights:
	for (uint i = spotlights().first_item() + groupIndex; i < spotlights().end_item(); i += TILED_CULLING_THREADSIZE * TILED_CULLING_THREADSIZE)
	{
		ShaderEntity entity = load_entity(i);

		if (entity.GetFlags() & ENTITY_FLAG_LIGHT_STATIC)
			continue; // static lights will be skipped here (they are used at lightmap baking)
		float3 positionVS = mul(GetCamera().view, float4(entity.position, 1)).xyz;
		float3 directionVS = mul((float3x3)GetCamera().view, entity.GetDirection());
		// Construct a tight fitting sphere around the spotlight cone:
		const float r = entity.GetRange() * 0.5f / (entity.GetConeAngleCos() * entity.GetConeAngleCos());
		Sphere sphere = { positionVS - directionVS * r, r };
		if (SphereInsideFrustumVS(sphere, GroupFrustum, nearClipVS, maxDepthVS))
		{
			AppendEntity_Transparent(i);

			if (SphereIntersectsAABB(sphere, GroupAABB)) // tighter fit than sphere-frustum culling
			{
#ifdef ADVANCED_CULLING
				if (depth_mask & ConstructEntityMask(minDepthVS, __depthRangeRecip, sphere))
#endif
				{
					AppendEntity_Opaque(i);
				}
			}

		}
	}

	// Directional lights:
	for (uint i = directional_lights().first_item() + groupIndex; i < directional_lights().end_item(); i += TILED_CULLING_THREADSIZE * TILED_CULLING_THREADSIZE)
	{
		ShaderEntity entity = load_entity(i);
		
		if (entity.GetFlags() & ENTITY_FLAG_LIGHT_STATIC)
			continue; // static lights will be skipped here (they are used at lightmap baking)
		AppendEntity_Transparent(i);
		AppendEntity_Opaque(i);
	}

	// Decals:
	for (uint i = decals().first_item() + groupIndex; i < decals().end_item(); i += TILED_CULLING_THREADSIZE * TILED_CULLING_THREADSIZE)
	{
		ShaderEntity entity = load_entity(i);
		float3 positionVS = mul(GetCamera().view, float4(entity.position, 1)).xyz;
		Sphere sphere = { positionVS.xyz, entity.GetRange() };
		if (SphereInsideFrustumVS(sphere, GroupFrustum, nearClipVS, maxDepthVS))
		{
			AppendEntity_Transparent(i);

			// unit AABB: 
			AABB a;
			a.c = 0;
			a.e = 1.0;

			// frustum AABB in world space transformed into the space of the probe/decal OBB:
			AABB b = GroupAABB_WS;
			AABBtransform(b, load_entitymatrix(entity.GetMatrixIndex()));

			if (IntersectAABB(a, b))
			{
#ifdef ADVANCED_CULLING
				if (depth_mask & ConstructEntityMask(minDepthVS, __depthRangeRecip, sphere))
#endif
				{
					AppendEntity_Opaque(i);
				}
			}
		}
	}

	// Environment probes:
	for (uint i = probes().first_item() + groupIndex; i < probes().end_item(); i += TILED_CULLING_THREADSIZE * TILED_CULLING_THREADSIZE)
	{
		ShaderEntity entity = load_entity(i);
		float3 positionVS = mul(GetCamera().view, float4(entity.position, 1)).xyz;
		Sphere sphere = { positionVS.xyz, entity.GetRange() };
		if (SphereInsideFrustumVS(sphere, GroupFrustum, nearClipVS, maxDepthVS))
		{
			AppendEntity_Transparent(i);

			// unit AABB: 
			AABB a;
			a.c = 0;
			a.e = 1.0;

			// frustum AABB in world space transformed into the space of the probe/decal OBB:
			AABB b = GroupAABB_WS;
			AABBtransform(b, load_entitymatrix(entity.GetMatrixIndex()));

			if (IntersectAABB(a, b))
			{
#ifdef ADVANCED_CULLING
				if (depth_mask & ConstructEntityMask(minDepthVS, __depthRangeRecip, sphere))
#endif
				{
					AppendEntity_Opaque(i);
				}
			}
		}
	}

	GroupMemoryBarrierWithGroupSync();
	
	const uint flatTileIndex = flatten2D(Gid.xy, GetCamera().entity_culling_tilecount.xy);
	const uint tileBucketsAddress = flatTileIndex * SHADER_ENTITY_TILE_BUCKET_COUNT;

	// Each thread will export one bucket from LDS to global memory:
	for (uint i = groupIndex; i < SHADER_ENTITY_TILE_BUCKET_COUNT; i += TILED_CULLING_THREADSIZE * TILED_CULLING_THREADSIZE)
	{
		entityTiles[tileBucketsAddress + i] = tile_opaque[i];
		entityTiles[GetCamera().entity_culling_tile_bucket_count_flat + tileBucketsAddress + i] = tile_transparent[i];
	}

#ifdef DEBUG_TILEDLIGHTCULLING
	for (uint granularity = 0; granularity < TILED_CULLING_GRANULARITY * TILED_CULLING_GRANULARITY; ++granularity)
	{
		uint2 pixel = DTid.xy * uint2(TILED_CULLING_GRANULARITY, TILED_CULLING_GRANULARITY) + unflatten2D(granularity, TILED_CULLING_GRANULARITY);

		const float3 mapTex[] = {
			float3(0,0,0),
			float3(0,0,1),
			float3(0,1,1),
			float3(0,1,0),
			float3(1,1,0),
			float3(1,0,0),
		};
		const uint mapTexLen = 5;
		const uint maxHeat = 50;
		float l = saturate((float)entityCountDebug / maxHeat) * mapTexLen;
		float3 a = mapTex[floor(l)];
		float3 b = mapTex[ceil(l)];
		float4 heatmap = float4(lerp(a, b, l - floor(l)), 0.8);
		DebugTexture[pixel] = heatmap;
	}
#endif // DEBUG_TILEDLIGHTCULLING

}
