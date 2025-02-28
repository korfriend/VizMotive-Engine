#include "RenderPath3D_Detail.h"
#include "TextureHelper.h"

namespace vz::renderer
{
	// camera-level GPU renderer updates
	//	c.f., scene-level (including animations) GPU-side updates performed in GSceneDetails::Update(..)
	// 
	// must be called after scene->update()
	void GRenderPath3DDetails::UpdateView(View& view)
	{
		// Perform parallel frustum culling and obtain closest reflector:
		jobsystem::context ctx;
		auto range = profiler::BeginRangeCPU("Frustum Culling");

		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();

		assert(view.scene != nullptr); // User must provide a scene!
		assert(view.camera != nullptr); // User must provide a camera!

		// The parallel frustum culling is first performed in shared memory, 
		//	then each group writes out it's local list to global memory
		//	The shared memory approach reduces atomics and helps the list to remain
		//	more coherent (less randomly organized compared to original order)
		static const uint32_t groupSize = 64;
		static const size_t sharedmemory_size = (groupSize + 1) * sizeof(uint32_t); // list + counter per group

		// Initialize visible indices:
		view.Clear();

		// TODO : add frustum culling processs
		if (!isFreezeCullingCameraEnabled) // just for debug
		{
			view.frustum = view.camera->GetFrustum();
		}
		if (!isOcclusionCullingEnabled || isFreezeCullingCameraEnabled)
		{
			view.flags &= ~View::ALLOW_OCCLUSION_CULLING;
		}

		if (view.flags & View::ALLOW_LIGHTS)
		{
			// Cull lights:
			const uint32_t light_loop = (uint32_t)view.scene->GetLightCount();
			view.visibleLights.resize(light_loop);
			//vis.visibleLightShadowRects.clear();
			//vis.visibleLightShadowRects.resize(light_loop);
			jobsystem::Dispatch(ctx, light_loop, groupSize, [&](jobsystem::JobArgs args) {

				const std::vector<Entity>& light_entities = view.scene->GetLightEntities();
				Entity entity = light_entities[args.jobIndex];
				const LightComponent& light = *compfactory::GetLightComponent(entity);
				assert(!light.IsDirty());

				// Setup stream compaction:
				uint32_t& group_count = *(uint32_t*)args.sharedmemory;
				uint32_t* group_list = (uint32_t*)args.sharedmemory + 1;
				if (args.isFirstJobInGroup)
				{
					group_count = 0; // first thread initializes local counter
				}

				const AABB& aabb = light.GetAABB();

				if ((aabb.layerMask & view.layerMask) && view.frustum.CheckBoxFast(aabb))
				{
					if (!light.IsInactive())
					{
						// Local stream compaction:
						//	(also compute light distance for shadow priority sorting)
						group_list[group_count] = args.jobIndex;
						group_count++;
						//if (light.IsVolumetricsEnabled())
						//{
						//	vis.volumetricLightRequest.store(true);
						//}

						//if (vis.flags & View::ALLOW_OCCLUSION_CULLING)
						//{
						//	if (!light.IsStatic() && light.GetType() != LightComponent::DIRECTIONAL || light.occlusionquery < 0)
						//	{
						//		if (!aabb.intersects(vis.camera->Eye))
						//		{
						//			light.occlusionquery = vis.scene->queryAllocator.fetch_add(1); // allocate new occlusion query from heap
						//		}
						//	}
						//}
					}
				}

				// Global stream compaction:
				if (args.isLastJobInGroup && group_count > 0)
				{
					uint32_t prev_count = view.lightCounter.fetch_add(group_count);
					for (uint32_t i = 0; i < group_count; ++i)
					{
						view.visibleLights[prev_count + i] = group_list[i];
					}
				}

				}, sharedmemory_size);
		}

		if (view.flags & View::ALLOW_RENDERABLES)
		{
			// Cull objects:
			const uint32_t renderable_loop = (uint32_t)view.scene->GetRenderableCount();
			view.visibleRenderables.resize(renderable_loop);
			jobsystem::Dispatch(ctx, renderable_loop, groupSize, [&](jobsystem::JobArgs args) {

				const RenderableComponent& renderable = *scene_Gdetails->renderableComponents[args.jobIndex];
				//assert(!renderable.IsDirty());

				// Setup stream compaction:
				uint32_t& group_count = *(uint32_t*)args.sharedmemory;
				uint32_t* group_list = (uint32_t*)args.sharedmemory + 1;
				if (args.isFirstJobInGroup)
				{
					group_count = 0; // first thread initializes local counter
				}

				const AABB& aabb = renderable.GetAABB();

				if ((aabb.layerMask & view.layerMask) && view.frustum.CheckBoxFast(aabb))
				{
					// Local stream compaction:
					group_list[group_count++] = args.jobIndex;

					GSceneDetails::OcclusionResult& occlusion_result = scene_Gdetails->occlusionResultsObjects[args.jobIndex];
					bool occluded = false;
					if (view.flags & View::ALLOW_OCCLUSION_CULLING)
					{
						occluded = occlusion_result.IsOccluded();
					}

					if (view.flags & View::ALLOW_OCCLUSION_CULLING)
					{
						if (renderable.IsMeshRenderable() && occlusion_result.occlusionQueries[scene_Gdetails->queryheapIdx] < 0)
						{
							if (aabb.intersects(view.camera->GetWorldEye()))
							{
								// camera is inside the instance, mark it as visible in this frame:
								occlusion_result.occlusionHistory |= 1;
							}
							else
							{
								occlusion_result.occlusionQueries[scene_Gdetails->queryheapIdx] = scene_Gdetails->queryAllocator.fetch_add(1); // allocate new occlusion query from heap
							}
						}
					}
				}

				// Global stream compaction:
				if (args.isLastJobInGroup && group_count > 0)
				{
					uint32_t prev_count = view.renderableCounter.fetch_add(group_count);
					for (uint32_t i = 0; i < group_count; ++i)
					{
						view.visibleRenderables[prev_count + i] = group_list[i];
					}
				}

				}, sharedmemory_size);
		}

		jobsystem::Wait(ctx);

		// finalize stream compaction: (memory safe)
		view.visibleRenderables.resize((size_t)view.renderableCounter.load());
		view.visibleLights.resize((size_t)view.lightCounter.load());

		profiler::EndRange(range); // Frustum Culling
	}
	void GRenderPath3DDetails::UpdatePerFrameData(Scene& scene, const View& vis, FrameCB& frameCB, float dt)
	{
		GraphicsDevice* device = graphics::GetDevice();
		GSceneDetails* scene_Gdetails = (GSceneDetails*)scene.GetGSceneHandle();

		// Update CPU-side frame constant buffer:
		frameCB.Init();
		frameCB.delta_time = dt * renderingSpeed;
		frameCB.time_previous = frameCB.time;
		frameCB.time += frameCB.delta_time;
		frameCB.frame_count = (uint)device->GetFrameCount();
		frameCB.blue_noise_phase = (frameCB.frame_count & 0xFF) * 1.6180339887f;

		frameCB.temporalaa_samplerotation = 0;
		if (isTemporalAAEnabled)
		{
			uint x = frameCB.frame_count % 4;
			uint y = frameCB.frame_count / 4;
			frameCB.temporalaa_samplerotation = (x & 0x000000FF) | ((y & 0x000000FF) << 8);
		}

		frameCB.gi_boost = giBoost;

		frameCB.options = 0;
		if (isTemporalAAEnabled)
		{
			frameCB.options |= OPTION_BIT_TEMPORALAA_ENABLED;
		}
		//frameCB.options |= OPTION_BIT_DISABLE_ALBEDO_MAPS;
		frameCB.options |= OPTION_BIT_FORCE_DIFFUSE_LIGHTING;

		frameCB.scene = scene_Gdetails->shaderscene;

		frameCB.texture_random64x64_index = device->GetDescriptorIndex(texturehelper::getRandom64x64(), SubresourceType::SRV);
		frameCB.texture_bluenoise_index = device->GetDescriptorIndex(texturehelper::getBlueNoise(), SubresourceType::SRV);
		frameCB.texture_sheenlut_index = device->GetDescriptorIndex(&textures[TEXTYPE_2D_SHEENLUT], SubresourceType::SRV);

		// Fill Entity Array with decals + envprobes + lights in the frustum:
		uint envprobearray_offset = 0;
		uint envprobearray_count = 0;
		uint lightarray_offset_directional = 0;
		uint lightarray_count_directional = 0;
		uint lightarray_offset_spot = 0;
		uint lightarray_count_spot = 0;
		uint lightarray_offset_point = 0;
		uint lightarray_count_point = 0;
		uint lightarray_offset = 0;
		uint lightarray_count = 0;
		uint decalarray_offset = 0;
		uint decalarray_count = 0;
		uint forcefieldarray_offset = 0;
		uint forcefieldarray_count = 0;
		frameCB.entity_culling_count = 0;
		{
			ShaderEntity* entity_array = frameCB.entityArray;
			float4x4* light_matrix_array = frameCB.matrixArray;

			uint32_t entity_counter = 0;
			uint32_t matrix_counter = 0;

			// Write decals into entity array:
			//decalarray_offset = entityCounter;
			//const size_t decal_iterations = std::min((size_t)MAX_SHADER_DECAL_COUNT, vis.visibleDecals.size());
			//for (size_t i = 0; i < decal_iterations; ++i)
			//{
			//	if (entity_counter == SHADER_ENTITY_COUNT)
			//	{
			//		backlog::post("Shader Entity Overflow!! >> DECALS");
			//		entity_counter--;
			//		break;
			//	}
			//	if (matrix_counter >= MATRIXARRAY_COUNT)
			//	{
			//		matrix_counter--;
			//		break;
			//	}
			//	ShaderEntity shaderentity = {};
			//	XMMATRIX shadermatrix;
			//
			//	const uint32_t decalIndex = vis.visibleDecals[vis.visibleDecals.size() - 1 - i]; // note: reverse order, for correct blending!
			//	const DecalComponent& decal = vis.scene->decals[decalIndex];
			//
			//	shaderentity.layerMask = ~0u;
			//
			//	Entity entity = vis.scene->decals.GetEntity(decalIndex);
			//	const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
			//	if (layer != nullptr)
			//	{
			//		shaderentity.layerMask = layer->layerMask;
			//	}
			//
			//	shaderentity.SetType(ENTITY_TYPE_DECAL);
			//	if (decal.IsBaseColorOnlyAlpha())
			//	{
			//		shaderentity.SetFlags(ENTITY_FLAG_DECAL_BASECOLOR_ONLY_ALPHA);
			//	}
			//	shaderentity.position = decal.position;
			//	shaderentity.SetRange(decal.range);
			//	float emissive_mul = 1 + decal.emissive;
			//	shaderentity.SetColor(float4(decal.color.x * emissive_mul, decal.color.y * emissive_mul, decal.color.z * emissive_mul, decal.color.w));
			//	shaderentity.shadowAtlasMulAdd = decal.texMulAdd;
			//	shaderentity.SetConeAngleCos(decal.slopeBlendPower);
			//	shaderentity.SetDirection(decal.front);
			//	shaderentity.SetAngleScale(decal.normal_strength);
			//	shaderentity.SetLength(decal.displacement_strength);
			//
			//	shaderentity.SetIndices(matrixCounter, 0);
			//	shadermatrix = XMMatrixInverse(nullptr, XMLoadFloat4x4(&decal.world));
			//
			//	int texture = -1;
			//	if (decal.texture.IsValid())
			//	{
			//		texture = device->GetDescriptorIndex(&decal.texture.GetTexture(), SubresourceType::SRV, decal.texture.GetTextureSRGBSubresource());
			//	}
			//	int normal = -1;
			//	if (decal.normal.IsValid())
			//	{
			//		normal = device->GetDescriptorIndex(&decal.normal.GetTexture(), SubresourceType::SRV);
			//	}
			//	int surfacemap = -1;
			//	if (decal.surfacemap.IsValid())
			//	{
			//		surfacemap = device->GetDescriptorIndex(&decal.surfacemap.GetTexture(), SubresourceType::SRV);
			//	}
			//	int displacementmap = -1;
			//	if (decal.displacementmap.IsValid())
			//	{
			//		displacementmap = device->GetDescriptorIndex(&decal.displacementmap.GetTexture(), SubresourceType::SRV);
			//	}
			//
			//	shadermatrix.r[0] = XMVectorSetW(shadermatrix.r[0], *(float*)&texture);
			//	shadermatrix.r[1] = XMVectorSetW(shadermatrix.r[1], *(float*)&normal);
			//	shadermatrix.r[2] = XMVectorSetW(shadermatrix.r[2], *(float*)&surfacemap);
			//	shadermatrix.r[3] = XMVectorSetW(shadermatrix.r[3], *(float*)&displacementmap);
			//
			//	XMStoreFloat4x4(matrixArray + matrixCounter, shadermatrix);
			//	matrixCounter++;
			//
			//	std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
			//	entityCounter++;
			//	decalarray_count++;
			//}

			// Write environment probes into entity array:
			//envprobearray_offset = entityCounter;
			//const size_t probe_iterations = std::min((size_t)MAX_SHADER_PROBE_COUNT, vis.visibleEnvProbes.size());
			//for (size_t i = 0; i < probe_iterations; ++i)
			//{
			//	if (entity_counter == SHADER_ENTITY_COUNT)
			//	{
			//		backlog::post("Shader Entity Overflow!! >> LIGHT PROBES");
			//		entity_counter--;
			//		break;
			//	}
			//	if (matrix_counter >= MATRIXARRAY_COUNT)
			//	{
			//		matrix_counter--;
			//		break;
			//	}
			//	ShaderEntity shaderentity = {};
			//	XMMATRIX shadermatrix;
			//
			//	const uint32_t probeIndex = vis.visibleEnvProbes[vis.visibleEnvProbes.size() - 1 - i]; // note: reverse order, for correct blending!
			//	const EnvironmentProbeComponent& probe = vis.scene->probes[probeIndex];
			//
			//	shaderentity = {}; // zero out!
			//	shaderentity.layerMask = ~0u;
			//
			//	Entity entity = vis.scene->probes.GetEntity(probeIndex);
			//	const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
			//	if (layer != nullptr)
			//	{
			//		shaderentity.layerMask = layer->layerMask;
			//	}
			//
			//	shaderentity.SetType(ENTITY_TYPE_ENVMAP);
			//	shaderentity.position = probe.position;
			//	shaderentity.SetRange(probe.range);
			//
			//	shaderentity.SetIndices(matrixCounter, 0);
			//	shadermatrix = XMLoadFloat4x4(&probe.inverseMatrix);
			//
			//	int texture = -1;
			//	if (probe.texture.IsValid())
			//	{
			//		texture = device->GetDescriptorIndex(&probe.texture, SubresourceType::SRV);
			//	}
			//
			//	shadermatrix.r[0] = XMVectorSetW(shadermatrix.r[0], *(float*)&texture);
			//	shadermatrix.r[1] = XMVectorSetW(shadermatrix.r[1], 0);
			//	shadermatrix.r[2] = XMVectorSetW(shadermatrix.r[2], 0);
			//	shadermatrix.r[3] = XMVectorSetW(shadermatrix.r[3], 0);
			//
			//	XMStoreFloat4x4(matrixArray + matrixCounter, shadermatrix);
			//	matrixCounter++;
			//
			//	std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
			//	entityCounter++;
			//	envprobearray_count++;
			//}

			//const XMFLOAT2 atlas_dim_rcp = XMFLOAT2(1.0f / float(shadowMapAtlas.desc.width), 1.0f / float(shadowMapAtlas.desc.height));

			const std::vector<Entity>& light_entities = vis.scene->GetLightEntities();
			// Write directional lights into entity array:
			lightarray_offset = entity_counter;
			lightarray_offset_directional = entity_counter;
			for (uint32_t lightIndex : vis.visibleLights)
			{
				if (entity_counter == SHADER_ENTITY_COUNT)
				{
					backlog::post("Shader Entity Overflow!! >> Directional Light");
					entity_counter--;
					break;
				}

				const GLightComponent& light = *(GLightComponent*)compfactory::GetLightComponent(light_entities[lightIndex]);
				if (light.GetLightType() != LightComponent::LightType::DIRECTIONAL || light.IsInactive())
					continue;

				ShaderEntity shaderentity = {};
				shaderentity.layerMask = ~0u;

				shaderentity.SetType(SCU32(light.GetLightType()));
				shaderentity.position = light.position;
				shaderentity.SetRange(light.GetRange());
				shaderentity.SetRadius(light.GetRadius());
				shaderentity.SetLength(light.GetLength());
				// note: the light direction used in shader refers to the direction to the light source
				shaderentity.SetDirection(XMFLOAT3(-light.direction.x, -light.direction.y, -light.direction.z));
				XMFLOAT3 light_color = light.GetLightColor();
				float light_intensity = light.GetLightIntensity();
				shaderentity.SetColor(float4(light_color.x * light_intensity, light_color.y * light_intensity, light_color.z * light_intensity, 1.f));

				// mark as no shadow by default:
				shaderentity.indices = ~0;

				bool shadow = false;// IsShadowsEnabled() && light.IsCastingShadow() && !light.IsStatic();
				//const rectpacker::Rect& shadow_rect = vis.visibleLightShadowRects[lightIndex];
				if (shadow)
				{
					//shaderentity.shadowAtlasMulAdd.x = shadow_rect.w * atlas_dim_rcp.x;
					//shaderentity.shadowAtlasMulAdd.y = shadow_rect.h * atlas_dim_rcp.y;
					//shaderentity.shadowAtlasMulAdd.z = shadow_rect.x * atlas_dim_rcp.x;
					//shaderentity.shadowAtlasMulAdd.w = shadow_rect.y * atlas_dim_rcp.y;
					//shaderentity.SetIndices(matrixCounter, 0);
				}

				const uint cascade_count = std::min((uint)light.cascadeDistances.size(), SHADER_ENTITY_COUNT - matrix_counter);
				shaderentity.SetShadowCascadeCount(cascade_count);

				//if (shadow && !light.cascade_distances.empty())
				//{
				//	SHCAM* shcams = (SHCAM*)alloca(sizeof(SHCAM) * cascade_count);
				//	CreateDirLightShadowCams(light, *vis.camera, shcams, cascade_count, shadow_rect);
				//	for (size_t cascade = 0; cascade < cascade_count; ++cascade)
				//	{
				//		XMStoreFloat4x4(&light_matrix_array[light_matrix_counter++], shcams[cascade].view_projection);
				//	}
				//}

				//if (light.IsStatic())
				//{
				//	shaderentity.SetFlags(ENTITY_FLAG_LIGHT_STATIC);
				//}

				std::memcpy(entity_array + entity_counter, &shaderentity, sizeof(ShaderEntity));
				entity_counter++;
				lightarray_count_directional++;
			}


			/*
			// Write spot lights into entity array:
			lightarray_offset_spot = entity_counter;
			for (uint32_t lightIndex : vis.visibleLights)
			{
				if (entity_counter == SHADER_ENTITY_COUNT)
				{
					entity_counter--;
					break;
				}

				const LightComponent& light = vis.scene->lights[lightIndex];
				if (light.GetType() != LightComponent::SPOT || light.IsInactive())
					continue;

				ShaderEntity shaderentity = {};
				shaderentity.layerMask = ~0u;

				Entity entity = vis.scene->lights.GetEntity(lightIndex);
				const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
				if (layer != nullptr)
				{
					shaderentity.layerMask = layer->layerMask;
				}

				shaderentity.SetType(light.GetType());
				shaderentity.position = light.position;
				shaderentity.SetRange(light.GetRange());
				shaderentity.SetRadius(light.radius);
				shaderentity.SetLength(light.length);
				// note: the light direction used in shader refers to the direction to the light source
				shaderentity.SetDirection(XMFLOAT3(-light.direction.x, -light.direction.y, -light.direction.z));
				shaderentity.SetColor(float4(light.color.x * light.intensity, light.color.y * light.intensity, light.color.z * light.intensity, 1));

				// mark as no shadow by default:
				shaderentity.indices = ~0;

				bool shadow = IsShadowsEnabled() && light.IsCastingShadow() && !light.IsStatic();
				const rectpacker::Rect& shadow_rect = vis.visibleLightShadowRects[lightIndex];

				if (shadow)
				{
					shaderentity.shadowAtlasMulAdd.x = shadow_rect.w * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.y = shadow_rect.h * atlas_dim_rcp.y;
					shaderentity.shadowAtlasMulAdd.z = shadow_rect.x * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.w = shadow_rect.y * atlas_dim_rcp.y;
					shaderentity.SetIndices(matrix_counter, 0);
				}

				const float outerConeAngle = light.outerConeAngle;
				const float innerConeAngle = std::min(light.innerConeAngle, outerConeAngle);
				const float outerConeAngleCos = std::cos(outerConeAngle);
				const float innerConeAngleCos = std::cos(innerConeAngle);

				// https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual#inner-and-outer-cone-angles
				const float lightAngleScale = 1.0f / std::max(0.001f, innerConeAngleCos - outerConeAngleCos);
				const float lightAngleOffset = -outerConeAngleCos * lightAngleScale;

				shaderentity.SetConeAngleCos(outerConeAngleCos);
				shaderentity.SetAngleScale(lightAngleScale);
				shaderentity.SetAngleOffset(lightAngleOffset);

				if (shadow)
				{
					SHCAM shcam;
					CreateSpotLightShadowCam(light, shcam);
					XMStoreFloat4x4(&matrixArray[matrix_counter++], shcam.view_projection);
				}

				if (light.IsStatic())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_STATIC);
				}

				if (light.IsVolumetricCloudsEnabled())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_VOLUMETRICCLOUDS);
				}

				std::memcpy(entityArray + entity_counter, &shaderentity, sizeof(ShaderEntity));
				entity_counter++;
				lightarray_count_spot++;
			}

			// Write point lights into entity array:
			lightarray_offset_point = entity_counter;
			for (uint32_t lightIndex : vis.visibleLights)
			{
				if (entity_counter == SHADER_ENTITY_COUNT)
				{
					entity_counter--;
					break;
				}

				const LightComponent& light = vis.scene->lights[lightIndex];
				if (light.GetType() != LightComponent::POINT || light.IsInactive())
					continue;

				ShaderEntity shaderentity = {};
				shaderentity.layerMask = ~0u;

				Entity entity = vis.scene->lights.GetEntity(lightIndex);
				const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
				if (layer != nullptr)
				{
					shaderentity.layerMask = layer->layerMask;
				}

				shaderentity.SetType(light.GetType());
				shaderentity.position = light.position;
				shaderentity.SetRange(light.GetRange());
				shaderentity.SetRadius(light.radius);
				shaderentity.SetLength(light.length);
				// note: the light direction used in shader refers to the direction to the light source
				shaderentity.SetDirection(XMFLOAT3(-light.direction.x, -light.direction.y, -light.direction.z));
				shaderentity.SetColor(float4(light.color.x * light.intensity, light.color.y * light.intensity, light.color.z * light.intensity, 1));

				// mark as no shadow by default:
				shaderentity.indices = ~0;

				bool shadow = IsShadowsEnabled() && light.IsCastingShadow() && !light.IsStatic();
				const rectpacker::Rect& shadow_rect = vis.visibleLightShadowRects[lightIndex];

				if (shadow)
				{
					shaderentity.shadowAtlasMulAdd.x = shadow_rect.w * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.y = shadow_rect.h * atlas_dim_rcp.y;
					shaderentity.shadowAtlasMulAdd.z = shadow_rect.x * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.w = shadow_rect.y * atlas_dim_rcp.y;
					shaderentity.SetIndices(matrix_counter, 0);
				}

				if (shadow)
				{
					const float FarZ = 0.1f;	// watch out: reversed depth buffer! Also, light near plane is constant for simplicity, this should match on cpu side!
					const float NearZ = std::max(1.0f, light.GetRange()); // watch out: reversed depth buffer!
					const float fRange = FarZ / (FarZ - NearZ);
					const float cubemapDepthRemapNear = fRange;
					const float cubemapDepthRemapFar = -fRange * NearZ;
					shaderentity.SetCubeRemapNear(cubemapDepthRemapNear);
					shaderentity.SetCubeRemapFar(cubemapDepthRemapFar);
				}

				if (light.IsStatic())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_STATIC);
				}

				if (light.IsVolumetricCloudsEnabled())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_VOLUMETRICCLOUDS);
				}

				std::memcpy(entityArray + entity_counter, &shaderentity, sizeof(ShaderEntity));
				entity_counter++;
				lightarray_count_point++;
			}
			/**/

			lightarray_count = lightarray_count_directional + lightarray_count_spot + lightarray_count_point;
			frameCB.entity_culling_count = lightarray_count + decalarray_count + envprobearray_count;

			/*
			// Write colliders into entity array:
			forcefieldarray_offset = entityCounter;
			for (size_t i = 0; i < vis.scene->collider_count_gpu; ++i)
			{
				if (entityCounter == SHADER_ENTITY_COUNT)
				{
					entityCounter--;
					break;
				}
				ShaderEntity shaderentity = {};

				const ColliderComponent& collider = vis.scene->colliders_gpu[i];
				shaderentity.layerMask = collider.layerMask;

				switch (collider.shape)
				{
				case ColliderComponent::Shape::Sphere:
					shaderentity.SetType(ENTITY_TYPE_COLLIDER_SPHERE);
					shaderentity.position = collider.sphere.center;
					shaderentity.SetRange(collider.sphere.radius);
					break;
				case ColliderComponent::Shape::Capsule:
					shaderentity.SetType(ENTITY_TYPE_COLLIDER_CAPSULE);
					shaderentity.position = collider.capsule.base;
					shaderentity.SetColliderTip(collider.capsule.tip);
					shaderentity.SetRange(collider.capsule.radius);
					break;
				case ColliderComponent::Shape::Plane:
					shaderentity.SetType(ENTITY_TYPE_COLLIDER_PLANE);
					shaderentity.position = collider.plane.origin;
					shaderentity.SetDirection(collider.plane.normal);
					shaderentity.SetIndices(matrixCounter, ~0u);
					matrixArray[matrixCounter++] = collider.plane.projection;
					break;
				default:
					assert(0);
					break;
				}

				std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
				entityCounter++;
				forcefieldarray_count++;
			}

			// Write force fields into entity array:
			for (size_t i = 0; i < vis.scene->forces.GetCount(); ++i)
			{
				if (entityCounter == SHADER_ENTITY_COUNT)
				{
					entityCounter--;
					break;
				}
				ShaderEntity shaderentity = {};

				const ForceFieldComponent& force = vis.scene->forces[i];

				shaderentity.layerMask = ~0u;

				Entity entity = vis.scene->forces.GetEntity(i);
				const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
				if (layer != nullptr)
				{
					shaderentity.layerMask = layer->layerMask;
				}

				switch (force.type)
				{
				default:
				case ForceFieldComponent::Type::Point:
					shaderentity.SetType(ENTITY_TYPE_FORCEFIELD_POINT);
					break;
				case ForceFieldComponent::Type::Plane:
					shaderentity.SetType(ENTITY_TYPE_FORCEFIELD_PLANE);
					break;
				}
				shaderentity.position = force.position;
				shaderentity.SetGravity(force.gravity);
				shaderentity.SetRange(std::max(0.001f, force.GetRange()));
				// The default planar force field is facing upwards, and thus the pull direction is downwards:
				shaderentity.SetDirection(force.direction);

				std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
				entityCounter++;
				forcefieldarray_count++;
			}
			/**/

		}



		frameCB.probes = ShaderEntityIterator(envprobearray_offset, envprobearray_count);
		frameCB.directional_lights = ShaderEntityIterator(lightarray_offset_directional, lightarray_count_directional);
		//frameCB.spotlights = ShaderEntityIterator(lightarray_offset_spot, lightarray_count_spot);
		//frameCB.pointlights = ShaderEntityIterator(lightarray_offset_point, lightarray_count_point);
		frameCB.lights = ShaderEntityIterator(lightarray_offset, lightarray_count);
		frameCB.decals = ShaderEntityIterator(decalarray_offset, decalarray_count);
		//frameCB.forces = ShaderEntityIterator(forcefieldarray_offset, forcefieldarray_count);
	}

	void GRenderPath3DDetails::View_Prepare(
		const ViewResources& res,
		const Texture& input_primitiveID_1, // can be MSAA
		const Texture& input_primitiveID_2, // can be MSAA
		CommandList cmd
	)
	{
		device->EventBegin("View_Prepare", cmd);
		auto range = profiler::BeginRangeGPU("View_Prepare", &cmd);

		BindCommonResources(cmd);

		// Note: the tile_count here must be valid whether the ViewResources was created or not!
		XMUINT2 tile_count = GetViewTileCount(XMUINT2(input_primitiveID_1.desc.width, input_primitiveID_1.desc.height));

		// Beginning barriers, clears:
		if (res.IsValid())
		{
			ShaderTypeBin bins[SHADERTYPE_BIN_COUNT + 1];
			for (uint i = 0; i < arraysize(bins); ++i)
			{
				ShaderTypeBin& bin = bins[i];
				bin.dispatchX = 0; // will be used for atomic add in shader
				bin.dispatchY = 1;
				bin.dispatchZ = 1;
				bin.shaderType = i;
			}
			device->UpdateBuffer(&res.bins, bins, cmd);
			barrierStack.push_back(GPUBarrier::Buffer(&res.bins, ResourceState::COPY_DST, ResourceState::UNORDERED_ACCESS));
			barrierStack.push_back(GPUBarrier::Buffer(&res.binned_tiles, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
			BarrierStackFlush(cmd);
		}

		// Resolve:
		//	PrimitiveID -> depth, lineardepth
		//	Binning classification
		{
			device->EventBegin("Resolve", cmd);
			const bool msaa = input_primitiveID_1.GetDesc().sample_count > 1;

			device->BindResource(&input_primitiveID_1, 0, cmd);
			device->BindResource(&input_primitiveID_2, 1, cmd);

			GPUResource unbind;

			if (res.IsValid())
			{
				device->BindUAV(&res.bins, 0, cmd);
				device->BindUAV(&res.binned_tiles, 1, cmd);
			}
			else
			{
				device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);
			}

			if (res.depthbuffer)
			{
				device->BindUAV(res.depthbuffer, 3, cmd, 0);
				device->BindUAV(res.depthbuffer, 4, cmd, 1);
				device->BindUAV(res.depthbuffer, 5, cmd, 2);
				device->BindUAV(res.depthbuffer, 6, cmd, 3);
				device->BindUAV(res.depthbuffer, 7, cmd, 4);
				barrierStack.push_back(GPUBarrier::Image(res.depthbuffer, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
			}
			else
			{
				device->BindUAV(&unbind, 3, cmd);
				device->BindUAV(&unbind, 4, cmd);
				device->BindUAV(&unbind, 5, cmd);
				device->BindUAV(&unbind, 6, cmd);
				device->BindUAV(&unbind, 7, cmd);
			}
			if (res.lineardepth)
			{
				device->BindUAV(res.lineardepth, 8, cmd, 0);
				device->BindUAV(res.lineardepth, 9, cmd, 1);
				device->BindUAV(res.lineardepth, 10, cmd, 2);
				device->BindUAV(res.lineardepth, 11, cmd, 3);
				device->BindUAV(res.lineardepth, 12, cmd, 4);
				barrierStack.push_back(GPUBarrier::Image(res.lineardepth, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
			}
			else
			{
				device->BindUAV(&unbind, 8, cmd);
				device->BindUAV(&unbind, 9, cmd);
				device->BindUAV(&unbind, 10, cmd);
				device->BindUAV(&unbind, 11, cmd);
				device->BindUAV(&unbind, 12, cmd);
			}
			if (res.primitiveID_1_resolved)
			{
				device->BindUAV(res.primitiveID_1_resolved, 13, cmd);
				device->BindUAV(res.primitiveID_2_resolved, 14, cmd);
				barrierStack.push_back(GPUBarrier::Image(res.primitiveID_1_resolved, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
				barrierStack.push_back(GPUBarrier::Image(res.primitiveID_2_resolved, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
			}
			else
			{
				device->BindUAV(&unbind, 13, cmd);
				device->BindUAV(&unbind, 14, cmd);
			}
			BarrierStackFlush(cmd);

			device->BindComputeShader(&shaders[msaa ? CSTYPE_VIEW_RESOLVE_MSAA : CSTYPE_VIEW_RESOLVE], cmd);

			device->Dispatch(
				tile_count.x,
				tile_count.y,
				1,
				cmd
			);

			if (res.depthbuffer)
			{
				barrierStack.push_back(GPUBarrier::Image(res.depthbuffer, ResourceState::UNORDERED_ACCESS, res.depthbuffer->desc.layout));
			}
			if (res.lineardepth)
			{
				barrierStack.push_back(GPUBarrier::Image(res.lineardepth, ResourceState::UNORDERED_ACCESS, res.lineardepth->desc.layout));
			}
			if (res.primitiveID_1_resolved)
			{
				barrierStack.push_back(GPUBarrier::Image(res.primitiveID_1_resolved, ResourceState::UNORDERED_ACCESS, res.primitiveID_1_resolved->desc.layout));
				barrierStack.push_back(GPUBarrier::Image(res.primitiveID_2_resolved, ResourceState::UNORDERED_ACCESS, res.primitiveID_2_resolved->desc.layout));
			}
			if (res.IsValid())
			{
				barrierStack.push_back(GPUBarrier::Buffer(&res.bins, ResourceState::UNORDERED_ACCESS, ResourceState::INDIRECT_ARGUMENT));
				barrierStack.push_back(GPUBarrier::Buffer(&res.binned_tiles, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));
			}
			BarrierStackFlush(cmd);

			device->EventEnd(cmd);
		}

		profiler::EndRange(range);

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::View_Surface(
		const ViewResources& res,
		const Texture& output,
		CommandList cmd
	)
	{
		device->EventBegin("View_Surface", cmd);
		auto range = profiler::BeginRangeGPU("View_Surface", &cmd);

		BindCommonResources(cmd);

		// First, do a bunch of resource discards to initialize texture metadata:
		barrierStack.push_back(GPUBarrier::Image(&output, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_normals, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_roughness, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_payload_0, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_payload_1, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
		BarrierStackFlush(cmd);

		device->BindResource(&res.binned_tiles, 0, cmd);
		device->BindUAV(&output, 0, cmd);
		device->BindUAV(&res.texture_normals, 1, cmd);
		device->BindUAV(&res.texture_roughness, 2, cmd);
		device->BindUAV(&res.texture_payload_0, 3, cmd);
		device->BindUAV(&res.texture_payload_1, 4, cmd);

		const uint visibility_tilecount_flat = res.tile_count.x * res.tile_count.y;
		uint visibility_tile_offset = 0;

		// surface dispatches per material type:
		device->EventBegin("Surface parameters", cmd);
		for (uint i = 0; i < SHADERTYPE_BIN_COUNT; ++i)
		{
			device->BindComputeShader(&shaders[CSTYPE_VIEW_SURFACE_PERMUTATION__BEGIN + i], cmd);
			device->PushConstants(&visibility_tile_offset, sizeof(visibility_tile_offset), cmd);
			device->DispatchIndirect(&res.bins, i * sizeof(ShaderTypeBin) + offsetof(ShaderTypeBin, dispatchX), cmd);
			visibility_tile_offset += visibility_tilecount_flat;
		}
		device->EventEnd(cmd);

		// Ending barriers:
		//	These resources will be used by other post processing effects
		barrierStack.push_back(GPUBarrier::Image(&res.texture_normals, ResourceState::UNORDERED_ACCESS, res.texture_normals.desc.layout));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_roughness, ResourceState::UNORDERED_ACCESS, res.texture_roughness.desc.layout));
		BarrierStackFlush(cmd);

		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
	void GRenderPath3DDetails::View_Surface_Reduced(
		const ViewResources& res,
		CommandList cmd
	)
	{
		assert(0 && "Not Yet Supported!");
		device->EventBegin("View_Surface_Reduced", cmd);
		auto range = profiler::BeginRangeGPU("View_Surface_Reduced", &cmd);

		BindCommonResources(cmd);

		barrierStack.push_back(GPUBarrier::Image(&res.texture_normals, res.texture_normals.desc.layout, ResourceState::UNORDERED_ACCESS));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_roughness, res.texture_roughness.desc.layout, ResourceState::UNORDERED_ACCESS));
		BarrierStackFlush(cmd);

		device->BindResource(&res.binned_tiles, 0, cmd);
		device->BindUAV(&res.texture_normals, 1, cmd);
		device->BindUAV(&res.texture_roughness, 2, cmd);

		const uint visibility_tilecount_flat = res.tile_count.x * res.tile_count.y;
		uint visibility_tile_offset = 0;

		// surface dispatches per material type:
		device->EventBegin("Surface parameters", cmd);
		for (uint i = 0; i < SHADERTYPE_BIN_COUNT; ++i)
		{
			if (i != SCU32(MaterialComponent::ShaderType::UNLIT)) // this won't need surface parameter write out
			{
				device->BindComputeShader(&shaders[CSTYPE_VIEW_SURFACE_REDUCED_PERMUTATION__BEGIN + i], cmd);
				device->PushConstants(&visibility_tile_offset, sizeof(visibility_tile_offset), cmd);
				device->DispatchIndirect(&res.bins, i * sizeof(ShaderTypeBin) + offsetof(ShaderTypeBin, dispatchX), cmd);
			}
			visibility_tile_offset += visibility_tilecount_flat;
		}
		device->EventEnd(cmd);

		// Ending barriers:
		//	These resources will be used by other post processing effects
		barrierStack.push_back(GPUBarrier::Image(&res.texture_normals, ResourceState::UNORDERED_ACCESS, res.texture_normals.desc.layout));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_roughness, ResourceState::UNORDERED_ACCESS, res.texture_roughness.desc.layout));
		BarrierStackFlush(cmd);

		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
	void GRenderPath3DDetails::View_Shade(
		const ViewResources& res,
		const Texture& output,
		CommandList cmd
	)
	{
		device->EventBegin("View_Shade", cmd);
		auto range = profiler::BeginRangeGPU("View_Shade", &cmd);

		BindCommonResources(cmd);

		barrierStack.push_back(GPUBarrier::Image(&res.texture_payload_0, ResourceState::UNORDERED_ACCESS, res.texture_payload_0.desc.layout));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_payload_1, ResourceState::UNORDERED_ACCESS, res.texture_payload_1.desc.layout));
		BarrierStackFlush(cmd);

		device->BindResource(&res.binned_tiles, 0, cmd);
		device->BindResource(&res.texture_payload_0, 2, cmd);
		device->BindResource(&res.texture_payload_1, 3, cmd);
		device->BindUAV(&output, 0, cmd);

		const uint visibility_tilecount_flat = res.tile_count.x * res.tile_count.y;
		uint visibility_tile_offset = 0;

		// shading dispatches per material type:
		for (uint i = 0; i < SHADERTYPE_BIN_COUNT; ++i)
		{
			if (i != SCU32(MaterialComponent::ShaderType::UNLIT)) // the unlit shader is special, it had already written out its final color in the surface shader
			{
				device->BindComputeShader(&shaders[CSTYPE_VIEW_SHADE_PERMUTATION__BEGIN + i], cmd);
				device->PushConstants(&visibility_tile_offset, sizeof(visibility_tile_offset), cmd);
				device->DispatchIndirect(&res.bins, i * sizeof(ShaderTypeBin) + offsetof(ShaderTypeBin, dispatchX), cmd);
			}
			visibility_tile_offset += visibility_tilecount_flat;
		}

		barrierStack.push_back(GPUBarrier::Image(&output, ResourceState::UNORDERED_ACCESS, output.desc.layout));
		BarrierStackFlush(cmd);

		profiler::EndRange(range);
		device->EventEnd(cmd);
	}

	// note : this function is supposed to be called in jobsystem
	//	so, the internal parameters must not be declared outside the function (e.g., member parameter)
	void GRenderPath3DDetails::BindCameraCB(const CameraComponent& camera, const CameraComponent& cameraPrevious, const CameraComponent& cameraReflection, CommandList cmd)
	{
		CameraCB* cameraCB = new CameraCB();
		cameraCB->Init();
		ShaderCamera& shadercam = cameraCB->cameras[0];

		// NOTE:
		//  the following parameters need to be set according to 
		//	* shadercam.options : RenderPath3D's property
		//  * shadercam.clip_plane  : RenderPath3D's property
		//  * shadercam.reflection_plane : Scene's property

		shadercam.options = SHADERCAMERA_OPTION_NONE;//camera.shadercamera_options;
		if (camera.IsOrtho())
		{
			shadercam.options |= SHADERCAMERA_OPTION_ORTHO;
		}

		shadercam.view_projection = camera.GetViewProjection();
		shadercam.view = camera.GetView();
		shadercam.projection = camera.GetProjection();
		shadercam.position = camera.GetWorldEye();
		shadercam.inverse_view = camera.GetInvView();
		shadercam.inverse_projection = camera.GetInvProjection();
		shadercam.inverse_view_projection = camera.GetInvViewProjection();
		XMMATRIX invVP = XMLoadFloat4x4(&shadercam.inverse_view_projection);
		shadercam.forward = camera.GetWorldForward();
		shadercam.up = camera.GetWorldUp();
		camera.GetNearFar(&shadercam.z_near, &shadercam.z_far);
		shadercam.z_near_rcp = 1.0f / std::max(0.0001f, shadercam.z_near);
		shadercam.z_far_rcp = 1.0f / std::max(0.0001f, shadercam.z_far);
		shadercam.z_range = abs(shadercam.z_far - shadercam.z_near);
		shadercam.z_range_rcp = 1.0f / std::max(0.0001f, shadercam.z_range);
		shadercam.clip_plane = XMFLOAT4(0, 0, 0, 0); // default: no clip plane
		shadercam.reflection_plane = XMFLOAT4(0, 0, 0, 0);

		const Frustum& cam_frustum = camera.GetFrustum();
		static_assert(arraysize(cam_frustum.planes) == arraysize(shadercam.frustum.planes), "Mismatch!");
		for (int i = 0; i < arraysize(cam_frustum.planes); ++i)
		{
			shadercam.frustum.planes[i] = cam_frustum.planes[i];
		}

		XMVECTOR cornersNEAR[4];
		cornersNEAR[0] = XMVector3TransformCoord(XMVectorSet(-1, 1, 1, 1), invVP);
		cornersNEAR[1] = XMVector3TransformCoord(XMVectorSet(1, 1, 1, 1), invVP);
		cornersNEAR[2] = XMVector3TransformCoord(XMVectorSet(-1, -1, 1, 1), invVP);
		cornersNEAR[3] = XMVector3TransformCoord(XMVectorSet(1, -1, 1, 1), invVP);

		XMStoreFloat4(&shadercam.frustum_corners.cornersNEAR[0], cornersNEAR[0]);
		XMStoreFloat4(&shadercam.frustum_corners.cornersNEAR[1], cornersNEAR[1]);
		XMStoreFloat4(&shadercam.frustum_corners.cornersNEAR[2], cornersNEAR[2]);
		XMStoreFloat4(&shadercam.frustum_corners.cornersNEAR[3], cornersNEAR[3]);

		if (!camera.IsCurvedSlicer())
		{
			XMStoreFloat4(&shadercam.frustum_corners.cornersFAR[0], XMVector3TransformCoord(XMVectorSet(-1, 1, 0, 1), invVP));
			XMStoreFloat4(&shadercam.frustum_corners.cornersFAR[1], XMVector3TransformCoord(XMVectorSet(1, 1, 0, 1), invVP));
			XMStoreFloat4(&shadercam.frustum_corners.cornersFAR[2], XMVector3TransformCoord(XMVectorSet(-1, -1, 0, 1), invVP));
			XMStoreFloat4(&shadercam.frustum_corners.cornersFAR[3], XMVector3TransformCoord(XMVectorSet(1, -1, 0, 1), invVP));
		}
		else
		{
			float dot_v = XMVectorGetX(XMVector3Dot(XMLoadFloat3(&camera.GetWorldForward()), XMVectorSet(0, 0, 1, 0))) - 1.f;
			float dot_up = XMVectorGetX(XMVector3Dot(XMLoadFloat3(&camera.GetWorldUp()), XMVectorSet(0, 1, 0, 0))) - 1.f;
			vzlog_assert(dot_v * dot_v < 0.001f, "camera.GetWorldForward() must be (0, 0, 1)!");
			vzlog_assert(dot_up * dot_up < 0.001f, "camera.GetWorldUp() must be (0, 1, 0)!");
			//XMMATRIX invP = XMLoadFloat4x4(&shadercam.inverse_projection);
			//cornersNEAR[0] = XMVector3TransformCoord(XMVectorSet(-1, 1, 1, 1), invP);
			//cornersNEAR[1] = XMVector3TransformCoord(XMVectorSet(1, 1, 1, 1), invP);
			//cornersNEAR[2] = XMVector3TransformCoord(XMVectorSet(-1, -1, 1, 1), invP);
			//cornersNEAR[3] = XMVector3TransformCoord(XMVectorSet(1, -1, 1, 1), invP);

			// SLICER
			SlicerComponent* slicer = (SlicerComponent*)&camera;
			float cplane_width = slicer->GetCurvedPlaneWidth();
			float cplane_height = slicer->GetCurvedPlaneHeight();
			//float cplane_thickness = slicer->GetThickness(); // to Push Constant

			int num_interpolation = slicer->GetHorizontalCurveInterpPoints().size();
			float cplane_width_pixel = (float)num_interpolation;
			float pitch = cplane_width / cplane_width_pixel;
			float cplane_height_pixel = cplane_height / pitch;

			float cplane_width_half = (cplane_width * 0.5f);
			float cplane_height_half = (cplane_height * 0.5f);

			XMMATRIX S = XMMatrixScaling(pitch, pitch, pitch);
			//XMMATRIX T = XMMatrixTranslation(-cplane_width_half, -cplane_height_half, -pitch * 0.5f);
			XMMATRIX T = XMMatrixTranslation(-cplane_width_half, -cplane_height_half, 0.f);

			XMMATRIX mat_COS2CWS = S * T;
			XMMATRIX mat_CWS2COS = XMMatrixInverse(nullptr, mat_COS2CWS);

			// PACKED TO shadercam.frustum_corners.cornersFAR[0-3]
			XMStoreFloat4(&shadercam.frustum_corners.cornersFAR[0], XMVector3TransformCoord(cornersNEAR[0], mat_CWS2COS)); // TL
			XMStoreFloat4(&shadercam.frustum_corners.cornersFAR[1], XMVector3TransformCoord(cornersNEAR[1], mat_CWS2COS)); // TR
			XMStoreFloat4(&shadercam.frustum_corners.cornersFAR[2], XMVector3TransformCoord(cornersNEAR[2], mat_CWS2COS)); // BL
			XMStoreFloat4(&shadercam.frustum_corners.cornersFAR[3], XMVector3TransformCoord(cornersNEAR[3], mat_CWS2COS)); // BR
			shadercam.frustum_corners.cornersFAR[0].w = cplane_width_pixel;
			shadercam.frustum_corners.cornersFAR[1].w = pitch;
			shadercam.frustum_corners.cornersFAR[2].w = cplane_height_pixel;
			shadercam.up = slicer->GetCurvedPlaneUp();

			shadercam.options |= SHADERCAMERA_OPTION_CURVED_SLICER;
			shadercam.options |= slicer->IsReverseSide()? SHADERCAMERA_OPTION_CURVED_SLICER_REVERSE_SIDE : 0;
		}

		if (camera.IsSlicer())
		{
			GSlicerComponent* slicer = (GSlicerComponent*)&camera;
			shadercam.curvePointsBufferIndex = device->GetDescriptorIndex(&slicer->curveInterpPointsBuffer, SubresourceType::SRV);
			shadercam.sliceThickness = slicer->GetThickness();
			// Compute pixelSpace
			{
				auto unproj = [&](float screenX, float screenY, float screenZ,
					float viewportX, float viewportY, float viewportWidth, float viewportHeight,
					const XMMATRIX& invViewProj)
					{
						float ndcX = ((screenX - viewportX) / viewportWidth) * 2.0f - 1.0f;
						float ndcY = 1.0f - ((screenY - viewportY) / viewportHeight) * 2.0f; // y�� ����
						float ndcZ = screenZ; // ���� 0~1 ������ ������

						XMVECTOR ndcPos = XMVectorSet(ndcX, ndcY, ndcZ, 1.0f);

						XMVECTOR worldPos = XMVector4Transform(ndcPos, invViewProj);

						worldPos = XMVectorScale(worldPos, 1.0f / XMVectorGetW(worldPos));

						return worldPos;
					};

				XMMATRIX inv_vp = XMLoadFloat4x4(&slicer->GetInvViewProjection());
				XMVECTOR world_pos0 = unproj(0.0f, 0.0f, 0.0f, viewport.top_left_x, viewport.top_left_y, viewport.width, viewport.height, inv_vp);
				XMVECTOR world_pos1 = unproj(1.0f, 0.0f, 0.0f, viewport.top_left_x, viewport.top_left_y, viewport.width, viewport.height, inv_vp);

				XMVECTOR diff = XMVectorSubtract(world_pos0, world_pos1);
				shadercam.pixelSize = XMVectorGetX(XMVector3Length(diff));

				shadercam.options |= SHADERCAMERA_OPTION_SLICER;
			}
		}

		shadercam.temporalaa_jitter = camera.jitter;
		shadercam.temporalaa_jitter_prev = cameraPrevious.jitter;

		shadercam.previous_view = cameraPrevious.GetView();
		shadercam.previous_projection = cameraPrevious.GetProjection();
		shadercam.previous_view_projection = cameraPrevious.GetViewProjection();
		shadercam.previous_inverse_view_projection = cameraPrevious.GetInvViewProjection();
		shadercam.reflection_view_projection = cameraReflection.GetViewProjection();
		shadercam.reflection_inverse_view_projection = cameraReflection.GetInvViewProjection();
		XMStoreFloat4x4(&shadercam.reprojection,
			XMLoadFloat4x4(&camera.GetInvViewProjection()) * XMLoadFloat4x4(&cameraPrevious.GetViewProjection()));

		shadercam.focal_length = camera.GetFocalLength();
		shadercam.aperture_size = camera.GetApertureSize();
		shadercam.aperture_shape = camera.GetApertureShape();

		shadercam.internal_resolution = uint2((uint)canvasWidth_, (uint)canvasHeight_);
		shadercam.internal_resolution_rcp = float2(1.0f / std::max(1u, shadercam.internal_resolution.x), 1.0f / std::max(1u, shadercam.internal_resolution.y));

		shadercam.scissor.x = scissor.left;
		shadercam.scissor.y = scissor.top;
		shadercam.scissor.z = scissor.right;
		shadercam.scissor.w = scissor.bottom;

		// scissor_uv is also offset by 0.5 (half pixel) to avoid going over last pixel center with bilinear sampler:
		shadercam.scissor_uv.x = (shadercam.scissor.x + 0.5f) * shadercam.internal_resolution_rcp.x;
		shadercam.scissor_uv.y = (shadercam.scissor.y + 0.5f) * shadercam.internal_resolution_rcp.y;
		shadercam.scissor_uv.z = (shadercam.scissor.z - 0.5f) * shadercam.internal_resolution_rcp.x;
		shadercam.scissor_uv.w = (shadercam.scissor.w - 0.5f) * shadercam.internal_resolution_rcp.y;

		shadercam.entity_culling_tilecount = GetEntityCullingTileCount(shadercam.internal_resolution);
		shadercam.entity_culling_tile_bucket_count_flat = shadercam.entity_culling_tilecount.x * shadercam.entity_culling_tilecount.y * SHADER_ENTITY_TILE_BUCKET_COUNT;
		shadercam.sample_count = depthBufferMain.desc.sample_count;
		shadercam.visibility_tilecount = GetViewTileCount(shadercam.internal_resolution);
		shadercam.visibility_tilecount_flat = shadercam.visibility_tilecount.x * shadercam.visibility_tilecount.y;

		shadercam.texture_primitiveID_1_index = device->GetDescriptorIndex(&rtPrimitiveID_1, SubresourceType::SRV);
		shadercam.texture_primitiveID_2_index = device->GetDescriptorIndex(&rtPrimitiveID_2, SubresourceType::SRV);
		shadercam.texture_depth_index = device->GetDescriptorIndex(&depthBuffer_Copy, SubresourceType::SRV);
		shadercam.texture_lineardepth_index = device->GetDescriptorIndex(&rtLinearDepth, SubresourceType::SRV);
		//shadercam.texture_velocity_index = camera.texture_velocity_index;
		shadercam.texture_normal_index = device->GetDescriptorIndex(&viewResources.texture_normals, SubresourceType::SRV);
		shadercam.texture_roughness_index = device->GetDescriptorIndex(&viewResources.texture_roughness, SubresourceType::SRV);
		shadercam.buffer_entitytiles_index = device->GetDescriptorIndex(&tiledLightResources.entityTiles, SubresourceType::SRV);
		//shadercam.texture_reflection_index = camera.texture_reflection_index;
		//shadercam.texture_reflection_depth_index = camera.texture_reflection_depth_index;
		//shadercam.texture_refraction_index = camera.texture_refraction_index;
		//shadercam.texture_waterriples_index = camera.texture_waterriples_index;
		//shadercam.texture_ao_index = camera.texture_ao_index;
		//shadercam.texture_ssr_index = camera.texture_ssr_index;
		//shadercam.texture_ssgi_index = camera.texture_ssgi_index;
		//shadercam.texture_rtshadow_index = camera.texture_rtshadow_index;
		//shadercam.texture_rtdiffuse_index = camera.texture_rtdiffuse_index;
		//shadercam.texture_surfelgi_index = camera.texture_surfelgi_index;
		//shadercam.texture_depth_index_prev = cameraPrevious.texture_depth_index;
		//shadercam.texture_vxgi_diffuse_index = camera.texture_vxgi_diffuse_index;
		//shadercam.texture_vxgi_specular_index = camera.texture_vxgi_specular_index;
		//shadercam.texture_reprojected_depth_index = camera.texture_reprojected_depth_index;

		device->BindDynamicConstantBuffer(*cameraCB, CBSLOT_RENDERER_CAMERA, cmd);
		delete cameraCB;
	}

	void GRenderPath3DDetails::BindCommonResources(CommandList cmd)
	{
		device->BindConstantBuffer(&buffers[BUFFERTYPE_FRAMECB], CBSLOT_RENDERER_FRAME, cmd);
	}

	void GRenderPath3DDetails::UpdateRenderData(const View& view, const FrameCB& frameCB, CommandList cmd)
	{
		device->EventBegin("UpdateRenderData", cmd);

		auto prof_updatebuffer_cpu = profiler::BeginRangeCPU("Update Buffers (CPU)");
		auto prof_updatebuffer_gpu = profiler::BeginRangeGPU("Update Buffers (GPU)", &cmd);

		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();

		barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->meshletBuffer, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS));
		barrierStack.push_back(GPUBarrier::Buffer(&buffers[BUFFERTYPE_FRAMECB], ResourceState::CONSTANT_BUFFER, ResourceState::COPY_DST));
		if (scene_Gdetails->instanceBuffer.IsValid())
		{
			barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->instanceBuffer, ResourceState::SHADER_RESOURCE, ResourceState::COPY_DST));
		}
		if (scene_Gdetails->geometryBuffer.IsValid())
		{
			barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->geometryBuffer, ResourceState::SHADER_RESOURCE, ResourceState::COPY_DST));
		}
		if (scene_Gdetails->materialBuffer.IsValid())
		{
			barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->materialBuffer, ResourceState::SHADER_RESOURCE, ResourceState::COPY_DST));
		}
		BarrierStackFlush(cmd);

		device->UpdateBuffer(&buffers[BUFFERTYPE_FRAMECB], &frameCB, cmd);
		barrierStack.push_back(GPUBarrier::Buffer(&buffers[BUFFERTYPE_FRAMECB], ResourceState::COPY_DST, ResourceState::CONSTANT_BUFFER));

		if (scene_Gdetails->instanceBuffer.IsValid() && scene_Gdetails->instanceArraySize > 0)
		{
			device->CopyBuffer(
				&scene_Gdetails->instanceBuffer,
				0,
				&scene_Gdetails->instanceUploadBuffer[device->GetBufferIndex()],
				0,
				scene_Gdetails->instanceArraySize * sizeof(ShaderMeshInstance),
				cmd
			);
			barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->instanceBuffer, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE));
		}

		if (scene_Gdetails->geometryBuffer.IsValid() && scene_Gdetails->geometryArraySize > 0)
		{
			device->CopyBuffer(
				&scene_Gdetails->geometryBuffer,
				0,
				&scene_Gdetails->geometryUploadBuffer[device->GetBufferIndex()],
				0,
				scene_Gdetails->geometryArraySize * sizeof(ShaderGeometry),
				cmd
			);
			barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->geometryBuffer, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE));
		}

		if (scene_Gdetails->materialBuffer.IsValid() && scene_Gdetails->materialArraySize > 0)
		{
			device->CopyBuffer(
				&scene_Gdetails->materialBuffer,
				0,
				&scene_Gdetails->materialUploadBuffer[device->GetBufferIndex()],
				0,
				scene_Gdetails->materialArraySize * sizeof(ShaderMaterial),
				cmd
			);
			barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->materialBuffer, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE));
		}

		//barrierStack.push_back(GPUBarrier::Image(&common::textures[TEXTYPE_2D_CAUSTICS], common::textures[TEXTYPE_2D_CAUSTICS].desc.layout, ResourceState::UNORDERED_ACCESS));

		// Flush buffer updates:
		BarrierStackFlush(cmd);

		profiler::EndRange(prof_updatebuffer_cpu);
		profiler::EndRange(prof_updatebuffer_gpu);

		BindCommonResources(cmd);

		//{
		//	//device->ClearUAV(&textures[TEXTYPE_2D_CAUSTICS], 0, cmd);
		//	device->Barrier(GPUBarrier::Memory(), cmd);
		//}
		//{
		//	auto range = profiler::BeginRangeGPU("Caustics", cmd);
		//	device->EventBegin("Caustics", cmd);
		//	device->BindComputeShader(&shaders[CSTYPE_CAUSTICS], cmd);
		//	device->BindUAV(&textures[TEXTYPE_2D_CAUSTICS], 0, cmd);
		//	const TextureDesc& desc = textures[TEXTYPE_2D_CAUSTICS].GetDesc();
		//	device->Dispatch(desc.width / 8, desc.height / 8, 1, cmd);
		//	barrierStack.push_back(GPUBarrier::Image(&textures[TEXTYPE_2D_CAUSTICS], ResourceState::UNORDERED_ACCESS, textures[TEXTYPE_2D_CAUSTICS].desc.layout));
		//	device->EventEnd(cmd);
		//	profiler::EndRange(range);
		//}
		//
		//barrierStackFlush(cmd); // wind/skinning flush

		device->EventEnd(cmd);
	}
	void GRenderPath3DDetails::UpdateRenderDataAsync(const View& view, const FrameCB& frameCB, CommandList cmd)
	{
		device->EventBegin("UpdateRenderDataAsync", cmd);

		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();

		BindCommonResources(cmd);

		// Wetmaps will be initialized:
		for (uint32_t renderableIndex = 0, n = (uint32_t)view.scene->GetRenderableCount(); renderableIndex < n; ++renderableIndex)
		{
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[renderableIndex];
			if (!renderable.IsMeshRenderable())
			{
				continue;
			}
			Entity geometry_entity = renderable.GetGeometry();
			GGeometryComponent& geomety = *(GGeometryComponent*)compfactory::GetGeometryComponent(geometry_entity);
			if (!geomety.HasRenderData())
			{
				continue;
			}

			size_t num_parts = geomety.GetNumParts();
			bool has_buffer_effect = num_parts == renderable.bufferEffects.size();
			for (size_t part_index = 0; part_index < num_parts; ++part_index)
			{
				if (geomety.allowGaussianSplatting && renderer::isGaussianSplattingEnabled)
				{
					GGeometryComponent::GPrimBuffers* prim_buffers = geomety.GetGPrimBuffer(part_index);
					if (prim_buffers)
					{
						device->ClearUAV(&prim_buffers->gaussianSplattingBuffers.touchedTiles_0, 0, cmd);
					}
				}

				if (!has_buffer_effect)
				{
					continue;
				}
				const GPrimEffectBuffers& prim_effect_buffers = renderable.bufferEffects[part_index];

				if (prim_effect_buffers.wetmapCleared || !prim_effect_buffers.wetmapBuffer.IsValid())
				{
					continue;
				}
				device->ClearUAV(&prim_effect_buffers.wetmapBuffer, 0, cmd);
				barrierStack.push_back(GPUBarrier::Buffer(&prim_effect_buffers.wetmapBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
				prim_effect_buffers.wetmapCleared = true;
			}
		}

		BarrierStackFlush(cmd);

		if (scene_Gdetails->textureStreamingFeedbackBuffer.IsValid())
		{
			device->ClearUAV(&scene_Gdetails->textureStreamingFeedbackBuffer, 0, cmd);
		}

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::OcclusionCulling_Reset(const View& view, CommandList cmd)
	{
		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();
		const GPUQueryHeap& queryHeap = scene_Gdetails->queryHeap;

		if (!renderer::isOcclusionCullingEnabled || renderer::isFreezeCullingCameraEnabled || !queryHeap.IsValid())
		{
			return;
		}
		if (view.visibleRenderables.empty() && view.visibleLights.empty())
		{
			return;
		}

		device->QueryReset(
			&queryHeap,
			0,
			queryHeap.desc.query_count,
			cmd
		);
	}
	void GRenderPath3DDetails::OcclusionCulling_Render(const CameraComponent& camera, const View& view, CommandList cmd)
	{
		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();
		const GPUQueryHeap& queryHeap = scene_Gdetails->queryHeap;

		if (!renderer::isOcclusionCullingEnabled || renderer::isFreezeCullingCameraEnabled || !queryHeap.IsValid())
		{
			return;
		}
		if (view.visibleRenderables.empty() && view.visibleLights.empty())
		{
			return;
		}

		auto range = profiler::BeginRangeGPU("Occlusion Culling Render", &cmd);

		device->BindPipelineState(&PSO_occlusionquery, cmd);

		XMMATRIX VP = XMLoadFloat4x4(&camera.GetViewProjection());

		int query_write = scene_Gdetails->queryheapIdx;

		if (!view.visibleRenderables.empty())
		{
			device->EventBegin("Occlusion Culling Objects", cmd);

			for (uint32_t instanceIndex : view.visibleRenderables)
			{
				const GSceneDetails::OcclusionResult& occlusion_result = scene_Gdetails->occlusionResultsObjects[instanceIndex];
				GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instanceIndex];

				int queryIndex = occlusion_result.occlusionQueries[query_write];
				if (queryIndex >= 0)
				{
					const AABB& aabb = renderable.GetAABB();
					const XMMATRIX transform = aabb.getAsBoxMatrix() * VP;
					device->PushConstants(&transform, sizeof(transform), cmd);

					// render bounding box to later read the occlusion status
					device->QueryBegin(&queryHeap, queryIndex, cmd);
					device->Draw(14, 0, cmd);
					device->QueryEnd(&queryHeap, queryIndex, cmd);
				}
			}

			device->EventEnd(cmd);
		}

		if (!view.visibleLights.empty())
		{
			device->EventBegin("Occlusion Culling Lights", cmd);

			for (uint32_t lightIndex : view.visibleLights)
			{
				const LightComponent& light = *scene_Gdetails->lightComponents[lightIndex];

				if (light.occlusionquery >= 0)
				{
					uint32_t queryIndex = (uint32_t)light.occlusionquery;
					const AABB& aabb = light.GetAABB();
					const XMMATRIX transform = aabb.getAsBoxMatrix() * VP;
					device->PushConstants(&transform, sizeof(transform), cmd);

					device->QueryBegin(&queryHeap, queryIndex, cmd);
					device->Draw(14, 0, cmd);
					device->QueryEnd(&queryHeap, queryIndex, cmd);
				}
			}

			device->EventEnd(cmd);
		}

		profiler::EndRange(range); // Occlusion Culling Render
	}
	void GRenderPath3DDetails::OcclusionCulling_Resolve(const View& view, CommandList cmd)
	{
		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();
		const GPUQueryHeap& queryHeap = scene_Gdetails->queryHeap;

		if (!renderer::isOcclusionCullingEnabled || renderer::isFreezeCullingCameraEnabled || !queryHeap.IsValid())
		{
			return;
		}
		if (view.visibleRenderables.empty() && view.visibleLights.empty())
		{
			return;
		}

		int query_write = scene_Gdetails->queryheapIdx;
		uint32_t queryCount = scene_Gdetails->queryAllocator.load();

		// Resolve into readback buffer:
		device->QueryResolve(
			&queryHeap,
			0,
			queryCount,
			&scene_Gdetails->queryResultBuffer[query_write],
			0ull,
			cmd
		);

		if (device->CheckCapability(GraphicsDeviceCapability::PREDICATION))
		{
			// Resolve into predication buffer:
			device->QueryResolve(
				&queryHeap,
				0,
				queryCount,
				&scene_Gdetails->queryPredicationBuffer,
				0ull,
				cmd
			);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Buffer(&scene_Gdetails->queryPredicationBuffer, ResourceState::COPY_DST, ResourceState::PREDICATION),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}
		}
	}

	void GRenderPath3DDetails::RefreshLightmaps(const Scene& scene, CommandList cmd)
	{
		GSceneDetails* scene_Gdetails = (GSceneDetails*)scene.GetGSceneHandle();

		// TODO for lightmap_request_allocator
		/*
		const uint32_t lightmap_request_count = lightmapRequestAllocator.load();
		if (lightmap_request_count > 0)
		{
			auto range = profiler::BeginRangeGPU("Lightmap Processing", cmd);

			if (!scene.TLAS.IsValid() && !scene.BVH.IsValid())
				return;

			jobsystem::Wait(raytracing_ctx);

			BindCommonResources(cmd);

			// Render lightmaps for each object:
			for (uint32_t requestIndex = 0; requestIndex < lightmap_request_count; ++requestIndex)
			{
				uint32_t objectIndex = *(scene.lightmap_requests.data() + requestIndex);
				const ObjectComponent& object = scene.objects[objectIndex];
				if (!object.lightmap.IsValid())
					continue;

				if (object.IsLightmapRenderRequested())
				{
					device->EventBegin("RenderObjectLightMap", cmd);

					const MeshComponent& mesh = scene.meshes[object.mesh_index];
					assert(!mesh.vertex_atlas.empty());
					assert(mesh.vb_atl.IsValid());

					const TextureDesc& desc = object.lightmap.GetDesc();

					if (object.lightmapIterationCount == 0)
					{
						RenderPassImage rp = RenderPassImage::RenderTarget(&object.lightmap, RenderPassImage::LoadOp::CLEAR);
						device->RenderPassBegin(&rp, 1, cmd);
					}
					else
					{
						RenderPassImage rp = RenderPassImage::RenderTarget(&object.lightmap, RenderPassImage::LoadOp::LOAD);
						device->RenderPassBegin(&rp, 1, cmd);
					}

					Viewport vp;
					vp.width = (float)desc.width;
					vp.height = (float)desc.height;
					device->BindViewports(1, &vp, cmd);

					device->BindPipelineState(&PSO_renderlightmap, cmd);

					device->BindIndexBuffer(&mesh.generalBuffer, mesh.GetIndexFormat(), mesh.ib.offset, cmd);

					LightmapPushConstants push;
					push.vb_pos_w = mesh.vb_pos_wind.descriptor_srv;
					push.vb_nor = mesh.vb_nor.descriptor_srv;
					push.vb_atl = mesh.vb_atl.descriptor_srv;
					push.instanceIndex = objectIndex;
					device->PushConstants(&push, sizeof(push), cmd);

					RaytracingCB cb;
					cb.xTraceResolution.x = desc.width;
					cb.xTraceResolution.y = desc.height;
					cb.xTraceResolution_rcp.x = 1.0f / cb.xTraceResolution.x;
					cb.xTraceResolution_rcp.y = 1.0f / cb.xTraceResolution.y;
					XMFLOAT4 halton = math::GetHaltonSequence(object.lightmapIterationCount); // for jittering the rasterization (good for eliminating atlas border artifacts)
					cb.xTracePixelOffset.x = (halton.x * 2 - 1) * cb.xTraceResolution_rcp.x;
					cb.xTracePixelOffset.y = (halton.y * 2 - 1) * cb.xTraceResolution_rcp.y;
					cb.xTracePixelOffset.x *= 1.4f;	// boost the jitter by a bit
					cb.xTracePixelOffset.y *= 1.4f;	// boost the jitter by a bit
					cb.xTraceAccumulationFactor = 1.0f / (object.lightmapIterationCount + 1.0f); // accumulation factor (alpha)
					cb.xTraceUserData.x = raytraceBounceCount;
					uint8_t instanceInclusionMask = 0xFF;
					cb.xTraceUserData.y = instanceInclusionMask;
					cb.xTraceSampleIndex = object.lightmapIterationCount;
					device->BindDynamicConstantBuffer(cb, CB_GETBINDSLOT(RaytracingCB), cmd);

					uint32_t first_subset = 0;
					uint32_t last_subset = 0;
					mesh.GetLODSubsetRange(0, first_subset, last_subset);
					for (uint32_t subsetIndex = first_subset; subsetIndex < last_subset; ++subsetIndex)
					{
						const MeshComponent::MeshSubset& subset = mesh.subsets[subsetIndex];
						if (subset.indexCount == 0)
							continue;
						device->DrawIndexed(subset.indexCount, subset.indexOffset, 0, cmd);
					}
					object.lightmapIterationCount++;

					device->RenderPassEnd(cmd);

					device->EventEnd(cmd);
				}
			}

			profiler::EndRange(range);
		}
		/**/
	}

	void GRenderPath3DDetails::RefreshWetmaps(const View& view, CommandList cmd)
	{
		return; // this will be useful for wetmap simulation for rainny weather...

		device->EventBegin("RefreshWetmaps", cmd);
		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();

		BindCommonResources(cmd);
		device->BindComputeShader(&shaders[CSTYPE_WETMAP_UPDATE], cmd);

		WetmapPush push = {};
		push.wet_amount = 1.f;

		// Note: every object wetmap is updated, not just visible
		for (uint32_t renderableIndex = 0, n = (uint32_t)view.scene->GetRenderableCount(); renderableIndex < n; ++renderableIndex)
		{
			push.instanceIndex = renderableIndex;
			GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[renderableIndex];

			if (!renderable.IsMeshRenderable())
			{
				continue;
			}

			Entity geometry_entity = renderable.GetGeometry();
			GGeometryComponent& geometry = *(GGeometryComponent*)compfactory::GetGeometryComponent(geometry_entity);

			std::vector<Entity> materials(renderable.GetNumParts());
			assert(renderable.GetNumParts() == renderable.bufferEffects.size());
			renderable.GetMaterials(materials.data());
			for (size_t part_index = 0, n = renderable.bufferEffects.size(); part_index < n; ++part_index)
			{
				GPrimEffectBuffers& prim_effect_buffers = renderable.bufferEffects[part_index];
				GMaterialComponent& material = *(GMaterialComponent*)compfactory::GetMaterialComponent(materials[part_index]);
				if (!material.IsWetmapEnabled() && prim_effect_buffers.wetmapBuffer.IsValid())
					continue;
				uint32_t vertex_count = uint32_t(prim_effect_buffers.wetmapBuffer.desc.size
					/ GetFormatStride(prim_effect_buffers.wetmapBuffer.desc.format));
				push.wetmap = device->GetDescriptorIndex(&prim_effect_buffers.wetmapBuffer, SubresourceType::UAV);
				if (push.wetmap < 0)
					continue;

				push.subsetIndex = part_index;;

				device->PushConstants(&push, sizeof(push), cmd);
				device->Dispatch((vertex_count + 63u) / 64u, 1, 1, cmd);
			}
		}

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::CreateTiledLightResources(TiledLightResources& res, XMUINT2 resolution)
	{
		res.tileCount = GetEntityCullingTileCount(resolution);

		GPUBufferDesc bd;
		bd.stride = sizeof(uint);
		bd.size = res.tileCount.x * res.tileCount.y * bd.stride * SHADER_ENTITY_TILE_BUCKET_COUNT * 2; // *2: opaque and transparent arrays
		bd.usage = Usage::DEFAULT;
		bd.bind_flags = BindFlag::UNORDERED_ACCESS | BindFlag::SHADER_RESOURCE;
		bd.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
		device->CreateBuffer(&bd, nullptr, &res.entityTiles);
		device->SetName(&res.entityTiles, "entityTiles");
	}

	void GRenderPath3DDetails::ComputeTiledLightCulling(
		const TiledLightResources& res,
		const View& vis,
		const Texture& debugUAV,
		CommandList cmd
	)
	{
		auto range = profiler::BeginRangeGPU("Entity Culling", &cmd);

		device->Barrier(GPUBarrier::Buffer(&res.entityTiles, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS), cmd);

		if (
			vis.visibleLights.empty() //&&
			//vis.visibleDecals.empty() &&
			//vis.visibleEnvProbes.empty()
			)
		{
			device->EventBegin("Tiled Entity Clear Only", cmd);
			device->ClearUAV(&res.entityTiles, 0, cmd);
			device->Barrier(GPUBarrier::Buffer(&res.entityTiles, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE), cmd);
			device->EventEnd(cmd);
			profiler::EndRange(range);
			return;
		}

		BindCommonResources(cmd);

		// Perform the culling
		{
			device->EventBegin("Entity Culling", cmd);

			if (isDebugLightCulling && debugUAV.IsValid())
			{
				device->BindComputeShader(&shaders[isAdvancedLightCulling ? CSTYPE_LIGHTCULLING_ADVANCED_DEBUG : CSTYPE_LIGHTCULLING_DEBUG], cmd);
				device->BindUAV(&debugUAV, 3, cmd);
			}
			else
			{
				device->BindComputeShader(&shaders[isAdvancedLightCulling ? CSTYPE_LIGHTCULLING_ADVANCED : CSTYPE_LIGHTCULLING], cmd);
			}

			const GPUResource* uavs[] = {
				&res.entityTiles,
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			device->Dispatch(res.tileCount.x, res.tileCount.y, 1, cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Buffer(&res.entityTiles, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->EventEnd(cmd);
		}

		// Unbind from UAV slots:
		GPUResource empty;
		const GPUResource* uavs[] = {
			&empty,
			&empty
		};
		device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

		profiler::EndRange(range);
	}

	void GRenderPath3DDetails::CreateViewResources(ViewResources& res, XMUINT2 resolution)
	{
		res.tile_count = GetViewTileCount(resolution);
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(ShaderTypeBin);
			desc.size = desc.stride * (SCU32(MaterialComponent::ShaderType::COUNT) + 1); // +1 for sky
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED | ResourceMiscFlag::INDIRECT_ARGS;
			bool success = device->CreateBuffer(&desc, nullptr, &res.bins);
			assert(success);
			device->SetName(&res.bins, "res.bins");

			desc.stride = sizeof(ViewTile);
			desc.size = desc.stride * res.tile_count.x * res.tile_count.y * (SCU32(MaterialComponent::ShaderType::COUNT) + 1); // +1 for sky
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
			success = device->CreateBuffer(&desc, nullptr, &res.binned_tiles);
			assert(success);
			device->SetName(&res.binned_tiles, "res.binned_tiles");
		}
		{
			TextureDesc desc;
			desc.width = resolution.x;
			desc.height = resolution.y;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;

			desc.format = Format::R16G16_FLOAT;
			device->CreateTexture(&desc, nullptr, &res.texture_normals);
			device->SetName(&res.texture_normals, "res.texture_normals");

			desc.format = Format::R8_UNORM;
			device->CreateTexture(&desc, nullptr, &res.texture_roughness);
			device->SetName(&res.texture_roughness, "res.texture_roughness");

			desc.format = Format::R32G32B32A32_UINT;
			device->CreateTexture(&desc, nullptr, &res.texture_payload_0);
			device->SetName(&res.texture_payload_0, "res.texture_payload_0");
			device->CreateTexture(&desc, nullptr, &res.texture_payload_1);
			device->SetName(&res.texture_payload_1, "res.texture_payload_1");
		}
	}
}