#include "RenderPath3D_Detail.h"
#include "TextureHelper.h"

namespace fsr2
{
#include "../Shaders/ffx-fsr2/ffx_core.h"
#include "../shaders/ffx-fsr2/ffx_fsr1.h"
#include "../shaders/ffx-fsr2/ffx_spd.h"
#include "../shaders/ffx-fsr2/ffx_fsr2_callbacks_hlsl.h"
#include "../shaders/ffx-fsr2/ffx_fsr2_common.h"
	int32_t ffxFsr2GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
	{
		const float basePhaseCount = 8.0f;
		const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
		return jitterPhaseCount;
	}
	static const int FFX_FSR2_MAXIMUM_BIAS_TEXTURE_WIDTH = 16;
	static const int FFX_FSR2_MAXIMUM_BIAS_TEXTURE_HEIGHT = 16;
	static const float ffxFsr2MaximumBias[] = {
		2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	1.876f,	1.809f,	1.772f,	1.753f,	1.748f,
		2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	1.869f,	1.801f,	1.764f,	1.745f,	1.739f,
		2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	1.976f,	1.841f,	1.774f,	1.737f,	1.716f,	1.71f,
		2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	1.914f,	1.784f,	1.716f,	1.673f,	1.649f,	1.641f,
		2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	1.793f,	1.676f,	1.604f,	1.562f,	1.54f,	1.533f,
		2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	1.802f,	1.619f,	1.536f,	1.492f,	1.467f,	1.454f,	1.449f,
		2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	1.812f,	1.575f,	1.496f,	1.456f,	1.432f,	1.416f,	1.408f,	1.405f,
		2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	1.555f,	1.479f,	1.438f,	1.413f,	1.398f,	1.387f,	1.381f,	1.379f,
		2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	1.812f,	1.555f,	1.474f,	1.43f,	1.404f,	1.387f,	1.376f,	1.368f,	1.363f,	1.362f,
		2.0f,	2.0f,	2.0f,	2.0f,	2.0f,	1.802f,	1.575f,	1.479f,	1.43f,	1.401f,	1.382f,	1.369f,	1.36f,	1.354f,	1.351f,	1.35f,
		2.0f,	2.0f,	1.976f,	1.914f,	1.793f,	1.619f,	1.496f,	1.438f,	1.404f,	1.382f,	1.367f,	1.357f,	1.349f,	1.344f,	1.341f,	1.34f,
		1.876f,	1.869f,	1.841f,	1.784f,	1.676f,	1.536f,	1.456f,	1.413f,	1.387f,	1.369f,	1.357f,	1.347f,	1.341f,	1.336f,	1.333f,	1.332f,
		1.809f,	1.801f,	1.774f,	1.716f,	1.604f,	1.492f,	1.432f,	1.398f,	1.376f,	1.36f,	1.349f,	1.341f,	1.335f,	1.33f,	1.328f,	1.327f,
		1.772f,	1.764f,	1.737f,	1.673f,	1.562f,	1.467f,	1.416f,	1.387f,	1.368f,	1.354f,	1.344f,	1.336f,	1.33f,	1.326f,	1.323f,	1.323f,
		1.753f,	1.745f,	1.716f,	1.649f,	1.54f,	1.454f,	1.408f,	1.381f,	1.363f,	1.351f,	1.341f,	1.333f,	1.328f,	1.323f,	1.321f,	1.32f,
		1.748f,	1.739f,	1.71f,	1.641f,	1.533f,	1.449f,	1.405f,	1.379f,	1.362f,	1.35f,	1.34f,	1.332f,	1.327f,	1.323f,	1.32f,	1.319f,

	};
	/// The value of Pi.
	const float FFX_PI = 3.141592653589793f;
	/// An epsilon value for floating point numbers.
	const float FFX_EPSILON = 1e-06f;
	// Lanczos
	static float lanczos2(float value)
	{
		return abs(value) < FFX_EPSILON ? 1.f : (sinf(FFX_PI * value) / (FFX_PI * value)) * (sinf(0.5f * FFX_PI * value) / (0.5f * FFX_PI * value));
	}
	// Calculate halton number for index and base.
	static float halton(int32_t index, int32_t base)
	{
		float f = 1.0f, result = 0.0f;

		for (int32_t currentIndex = index; currentIndex > 0;) {

			f /= (float)base;
			result = result + f * (float)(currentIndex % base);
			currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
		}

		return result;
	}
}

namespace vz::renderer
{
	XMFLOAT2 FSR2Resources::GetJitter() const
	{
		int32_t phaseCount = fsr2::ffxFsr2GetJitterPhaseCount(fsr2_constants.renderSize[0], fsr2_constants.displaySize[0]);
		float x = fsr2::halton((fsr2_constants.frameIndex % phaseCount) + 1, 2) - 0.5f;
		float y = fsr2::halton((fsr2_constants.frameIndex % phaseCount) + 1, 3) - 0.5f;
		x = 2 * x / (float)fsr2_constants.renderSize[0];
		y = -2 * y / (float)fsr2_constants.renderSize[1];
		return XMFLOAT2(x, y);
	}

	void CreateSpotLightShadowCam(const GLightComponent& light, SHCAM& shcam)
	{
		shcam.init(light.position, light.rotation, 0.1f, light.GetRange(), light.GetOuterConeAngle() * 2.f);
	}
	void CreateDirLightShadowCams(const GLightComponent& light, CameraComponent camera, SHCAM* shcams, size_t shcam_count, const rectpacker::Rect& shadow_rect)
	{
		// remove camera jittering
		camera.jitter = XMFLOAT2(0, 0);
		camera.UpdateMatrix();

		const XMMATRIX lightRotation = XMMatrixRotationQuaternion(XMLoadFloat4(&light.rotation));
		const XMVECTOR to = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f), lightRotation);
		const XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), lightRotation);
		const XMMATRIX lightView = VZMatrixLookTo(XMVectorZero(), to, up); // important to not move (zero out eye vector) the light view matrix itself because texel snapping must be done on projection matrix!
		float farPlane;
		camera.GetNearFar(nullptr, &farPlane);

		// Unproject main frustum corners into world space (notice the reversed Z projection!):
		const XMMATRIX unproj = XMLoadFloat4x4(&camera.GetInvViewProjection());
		const XMVECTOR frustum_corners[] =
		{
			XMVector3TransformCoord(XMVectorSet(-1, -1, 1, 1), unproj),	// near
			XMVector3TransformCoord(XMVectorSet(-1, -1, 0, 1), unproj),	// far
			XMVector3TransformCoord(XMVectorSet(-1, 1, 1, 1), unproj),	// near
			XMVector3TransformCoord(XMVectorSet(-1, 1, 0, 1), unproj),	// far
			XMVector3TransformCoord(XMVectorSet(1, -1, 1, 1), unproj),	// near
			XMVector3TransformCoord(XMVectorSet(1, -1, 0, 1), unproj),	// far
			XMVector3TransformCoord(XMVectorSet(1, 1, 1, 1), unproj),	// near
			XMVector3TransformCoord(XMVectorSet(1, 1, 0, 1), unproj),	// far
		};

		// Compute shadow cameras:
		for (int cascade = 0; cascade < shcam_count; ++cascade)
		{
			// Compute cascade bounds in light-view-space from the main frustum corners:
			const float split_near = cascade == 0 ? 0 : light.cascadeDistances[cascade - 1] / farPlane;
			const float split_far = light.cascadeDistances[cascade] / farPlane;
			const XMVECTOR corners[] =
			{
				XMVector3Transform(XMVectorLerp(frustum_corners[0], frustum_corners[1], split_near), lightView),
				XMVector3Transform(XMVectorLerp(frustum_corners[0], frustum_corners[1], split_far), lightView),
				XMVector3Transform(XMVectorLerp(frustum_corners[2], frustum_corners[3], split_near), lightView),
				XMVector3Transform(XMVectorLerp(frustum_corners[2], frustum_corners[3], split_far), lightView),
				XMVector3Transform(XMVectorLerp(frustum_corners[4], frustum_corners[5], split_near), lightView),
				XMVector3Transform(XMVectorLerp(frustum_corners[4], frustum_corners[5], split_far), lightView),
				XMVector3Transform(XMVectorLerp(frustum_corners[6], frustum_corners[7], split_near), lightView),
				XMVector3Transform(XMVectorLerp(frustum_corners[6], frustum_corners[7], split_far), lightView),
			};

			// Compute cascade bounding sphere center:
			XMVECTOR center = XMVectorZero();
			for (int j = 0; j < arraysize(corners); ++j)
			{
				center = XMVectorAdd(center, corners[j]);
			}
			center = center / float(arraysize(corners));

			// Compute cascade bounding sphere radius:
			float radius = 0;
			for (int j = 0; j < arraysize(corners); ++j)
			{
				radius = std::max(radius, XMVectorGetX(XMVector3Length(XMVectorSubtract(corners[j], center))));
			}

			// Fit AABB onto bounding sphere:
			XMVECTOR vRadius = XMVectorReplicate(radius);
			XMVECTOR vMin = XMVectorSubtract(center, vRadius);
			XMVECTOR vMax = XMVectorAdd(center, vRadius);

			// Snap cascade to texel grid:
			const XMVECTOR extent = XMVectorSubtract(vMax, vMin);
			const XMVECTOR texelSize = extent / float(shadow_rect.w);
			vMin = XMVectorFloor(vMin / texelSize) * texelSize;
			vMax = XMVectorFloor(vMax / texelSize) * texelSize;
			center = (vMin + vMax) * 0.5f;

			XMFLOAT3 _center;
			XMFLOAT3 _min;
			XMFLOAT3 _max;
			XMStoreFloat3(&_center, center);
			XMStoreFloat3(&_min, vMin);
			XMStoreFloat3(&_max, vMax);

			// Extrude bounds to avoid early shadow clipping:
			float ext = abs(_center.z - _min.z);
			ext = std::max(ext, std::min(1500.0f, farPlane) * 0.5f);
			_min.z = _center.z - ext;
			_max.z = _center.z + ext;

			const XMMATRIX lightProjection = VZMatrixOrthographicOffCenter(_min.x, _max.x, _min.y, _max.y, _max.z, _min.z); // notice reversed Z!

			shcams[cascade].view_projection = XMMatrixMultiply(lightView, lightProjection);
			shcams[cascade].frustum.Create(shcams[cascade].view_projection);
		}

	}
	void CreateCubemapCameras(const XMFLOAT3& position, float zNearP, float zFarP, SHCAM* shcams, size_t shcam_count)
	{
		assert(shcam_count == 6);
		shcams[0].init(position, XMFLOAT4(0.5f, -0.5f, -0.5f, -0.5f), zNearP, zFarP, XM_PIDIV2); //+x
		shcams[1].init(position, XMFLOAT4(0.5f, 0.5f, 0.5f, -0.5f), zNearP, zFarP, XM_PIDIV2); //-x
		shcams[2].init(position, XMFLOAT4(1, 0, 0, -0), zNearP, zFarP, XM_PIDIV2); //+y
		shcams[3].init(position, XMFLOAT4(0, 0, 0, -1), zNearP, zFarP, XM_PIDIV2); //-y
		shcams[4].init(position, XMFLOAT4(0.707f, 0, 0, -0.707f), zNearP, zFarP, XM_PIDIV2); //+z
		shcams[5].init(position, XMFLOAT4(0, 0.707f, 0.707f, 0), zNearP, zFarP, XM_PIDIV2); //-z
	}
}

namespace vz::renderer
{
	// camera-level GPU renderer updates
	//	c.f., scene-level (including animations) GPU-side updates performed in GSceneDetails::Update(..)
	// 
	// must be called after scene->update()
	void GRenderPath3DDetails::UpdateVisibility(Visibility& vis)
	{
		// Perform parallel frustum culling and obtain closest reflector:
		jobsystem::context ctx;
		auto range = profiler::BeginRangeCPU("Frustum Culling");

		assert(vis.camera != nullptr); // User must provide a camera!

		// The parallel frustum culling is first performed in shared memory, 
		//	then each group writes out it's local list to global memory
		//	The shared memory approach reduces atomics and helps the list to remain
		//	more coherent (less randomly organized compared to original order)
		static constexpr uint32_t groupSize = 64u;
		static const size_t sharedmemory_size = (groupSize + 1) * sizeof(uint32_t); // list + counter per group

		// Initialize visible indices:
		vis.Clear();

		// TODO : add frustum culling processs
		if (!isFreezeCullingCameraEnabled) // just for debug
		{
			vis.frustum = vis.camera->GetFrustum();
		}
 		if (!isOcclusionCullingEnabled || isFreezeCullingCameraEnabled)
		{
			vis.flags &= ~Visibility::ALLOW_OCCLUSION_CULLING;
		}

		if (vis.flags & Visibility::ALLOW_LIGHTS)
		{
			// Cull lights:
			const uint32_t light_loop = (uint32_t)scene_Gdetails->lightComponents.size();
			vis.visibleLights.resize(light_loop);
			vis.visibleLightShadowRects.clear();
			vis.visibleLightShadowRects.resize(light_loop);
			jobsystem::Dispatch(ctx, light_loop, groupSize, [&](jobsystem::JobArgs args) {

				const GLightComponent& light = *scene_Gdetails->lightComponents[args.jobIndex];
				assert(!light.IsDirty());
				assert(light.lightIndex == args.jobIndex);

				// Setup stream compaction:
				uint32_t& group_count = *(uint32_t*)args.sharedmemory;
				uint32_t* group_list = (uint32_t*)args.sharedmemory + 1;
				if (args.isFirstJobInGroup)
				{
					group_count = 0; // first thread initializes local counter
				}

				const AABB& aabb = scene_Gdetails->aabbLights[args.jobIndex];

				if ((aabb.layerMask & vis.layerMask) && vis.frustum.CheckBoxFast(aabb))
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

						if (vis.flags & Visibility::ALLOW_OCCLUSION_CULLING)
						{
							if (!light.IsStatic() && light.GetType() != LightComponent::LightType::DIRECTIONAL || light.occlusionquery < 0)
							{
								if (!aabb.intersects(vis.camera->GetWorldEye()))
								{
									light.occlusionquery = scene_Gdetails->queryAllocator.fetch_add(1); // allocate new occlusion query from heap
								}
							}
						}
					}
				}

				// Global stream compaction:
				if (args.isLastJobInGroup && group_count > 0)
				{
					uint32_t prev_count = vis.counterLight.fetch_add(group_count);
					for (uint32_t i = 0; i < group_count; ++i)
					{
						vis.visibleLights[prev_count + i] = group_list[i];
					}
				}

				}, sharedmemory_size);
		}

		if (vis.flags & Visibility::ALLOW_RENDERABLES)
		{
			// Cull objects:
			const uint32_t renderable_loop = (uint32_t)scene_Gdetails->renderableComponents.size();

			const Scene* scene = scene_Gdetails->GetScene();
			vis.visibleRenderables_Mesh.resize(scene->GetRenderableMeshCount());
			vis.visibleRenderables_Volume.resize(scene->GetRenderableVolumeCount());
			vis.visibleRenderables_GSplat.resize(scene->GetRenderableGSplatCount());

			jobsystem::Dispatch(ctx, renderable_loop, groupSize, [&](jobsystem::JobArgs args) {

				uint32_t renderable_index = args.jobIndex;
				const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[renderable_index];
				assert(renderable.renderableIndex == args.jobIndex);
				switch (renderable.GetRenderableType())
				{
				case RenderableType::MESH_RENDERABLE:
				case RenderableType::VOLUME_RENDERABLE:
				case RenderableType::GSPLAT_RENDERABLE:
					break;
				case RenderableType::SPRITE_RENDERABLE:
				case RenderableType::SPRITEFONT_RENDERABLE:
					return;
				default: 
					vzlog_assert(0, "Non-renderable Type! ShaderEngine's Scene Update"); return;
				}

				const AABB& aabb = scene_Gdetails->aabbRenderables[args.jobIndex];

				if ((aabb.layerMask & vis.layerMask) && vis.frustum.CheckBoxFast(aabb))
				{
					switch (renderable.GetRenderableType())
					{
					case RenderableType::MESH_RENDERABLE:
						vis.visibleRenderables_Mesh[vis.counterRenderableMesh.fetch_add(1)] = renderable_index;
						break;
					case RenderableType::VOLUME_RENDERABLE:
						vis.visibleRenderables_Volume[vis.counterRenderableVolume.fetch_add(1)] = renderable_index;
						break;
					case RenderableType::GSPLAT_RENDERABLE:
						vis.visibleRenderables_GSplat[vis.counterRenderableGSplat.fetch_add(1)] = renderable_index;
						break;
					default:
						vzlog_assert(0, "MUST BE RENDERABLE!");
						return;
					}

					GSceneDetails::OcclusionResult& occlusion_result = scene_Gdetails->occlusionResultsObjects[renderable_index];
					bool occluded = false;

					if (vis.flags & Visibility::ALLOW_OCCLUSION_CULLING)
					{
						occluded = occlusion_result.IsOccluded();
						assert(renderable.IsRenderable());
						if (occlusion_result.occlusionQueries[scene_Gdetails->queryheapIdx] < 0)
						{
							if (aabb.intersects(vis.camera->GetWorldEye()))
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

				});
		}

		if (vis.flags & Visibility::ALLOW_ENVPROBES)
		{
			// Note: probes must be appended in order for correct blending, must not use parallelization!
			jobsystem::Execute(ctx, [&](jobsystem::JobArgs args) {
				for (size_t i = 0; i < scene_Gdetails->aabbProbes.size(); ++i)
				{
					const AABB& aabb = scene_Gdetails->aabbProbes[i];

					if ((aabb.layerMask & vis.layerMask) && vis.frustum.CheckBoxFast(aabb))
					{
						vis.visibleEnvProbes.push_back((uint32_t)i);
					}
				}
				});
		}

		jobsystem::Wait(ctx);

		// finalize stream compaction: (memory safe)
		size_t num_Meshes = (size_t)vis.counterRenderableMesh.load();
		size_t num_Volumes = (size_t)vis.counterRenderableVolume.load();
		size_t num_GSplats = (size_t)vis.counterRenderableGSplat.load();
		vis.visibleRenderables_Mesh.resize(num_Meshes);
		vis.visibleRenderables_Volume.resize(num_Volumes);
		vis.visibleRenderables_GSplat.resize(num_GSplats);
		vis.visibleLights.resize((size_t)vis.counterLight.load());

		vis.visibleRenderables_All.resize(num_Meshes + num_Volumes + num_GSplats);

		if (num_Meshes > 0)
			memcpy(&vis.visibleRenderables_All[0], &vis.visibleRenderables_Mesh[0], sizeof(uint32_t)* num_Meshes);
		if (num_Volumes > 0)
			memcpy(&vis.visibleRenderables_All[num_Meshes], &vis.visibleRenderables_Volume[0], sizeof(uint32_t)* num_Volumes);
		if (num_GSplats > 0)
			memcpy(&vis.visibleRenderables_All[num_Meshes + num_Volumes], &vis.visibleRenderables_GSplat[0], sizeof(uint32_t)* num_GSplats);

		// Shadow atlas packing:
		if (isShadowsEnabled && (vis.flags & Visibility::ALLOW_SHADOW_ATLAS_PACKING) && !vis.visibleLights.empty())
		{
			auto range = profiler::BeginRangeCPU("Shadowmap packing");
			float iterative_scaling = 1;

			while (iterative_scaling > 0.03f)
			{
				vis.shadowPacker.clear();
				for (uint32_t lightIndex : vis.visibleLights)
				{
					const GLightComponent& light = *scene_Gdetails->lightComponents[lightIndex];
					if (light.IsInactive())
						continue;
					if (!light.IsCastingShadow() || light.IsStatic())
						continue;

					const float dist = math::Distance(vis.camera->GetWorldEye(), light.position);
					const float range = light.GetRange();
					const float amount = std::min(1.0f, range / std::max(0.001f, dist)) * iterative_scaling;

					rectpacker::Rect rect = {};
					rect.id = int(lightIndex);
					switch (light.GetType())
					{
					case LightComponent::LightType::DIRECTIONAL:
						if (light.forcedShadowResolution >= 0)
						{
							rect.w = light.forcedShadowResolution * int(light.cascadeDistances.size());
							rect.h = light.forcedShadowResolution;
						}
						else
						{
							rect.w = int(maxShadowResolution_2D * iterative_scaling) * int(light.cascadeDistances.size());
							rect.h = int(maxShadowResolution_2D * iterative_scaling);
						}
						break;
					case LightComponent::LightType::SPOT:
						if (light.forcedShadowResolution >= 0)
						{
							rect.w = int(light.forcedShadowResolution);
							rect.h = int(light.forcedShadowResolution);
						}
						else
						{
							rect.w = int(maxShadowResolution_2D * amount);
							rect.h = int(maxShadowResolution_2D * amount);
						}
						break;
					case LightComponent::LightType::POINT:
						if (light.forcedShadowResolution >= 0)
						{
							rect.w = int(light.forcedShadowResolution) * 6;
							rect.h = int(light.forcedShadowResolution);
						}
						else
						{
							rect.w = int(maxShadowResolution_2D * amount) * 6;
							rect.h = int(maxShadowResolution_2D * amount);
						}
						break;
					}
					if (rect.w > 8 && rect.h > 8)
					{
						vis.shadowPacker.add_rect(rect);
					}
				}
				if (!vis.shadowPacker.rects.empty())
				{
					if (vis.shadowPacker.pack(8192))
					{
						for (auto& rect : vis.shadowPacker.rects)
						{
							if (rect.id == -1)
							{
								continue;
							}
							uint32_t lightIndex = uint32_t(rect.id);
							rectpacker::Rect& lightrect = vis.visibleLightShadowRects[lightIndex];
							const GLightComponent& light = *scene_Gdetails->lightComponents[lightIndex];
							if (rect.was_packed)
							{
								lightrect = rect;

								// Remove slice multipliers from rect:
								switch (light.GetType())
								{
								case LightComponent::LightType::DIRECTIONAL:
									lightrect.w /= int(light.cascadeDistances.size());
									break;
								case LightComponent::LightType::POINT:
									lightrect.w /= 6;
									break;
								}
							}
						}

						if ((int)shadowMapAtlas.desc.width < vis.shadowPacker.width || (int)shadowMapAtlas.desc.height < vis.shadowPacker.height)
						{
							TextureDesc desc;
							desc.width = uint32_t(vis.shadowPacker.width);
							desc.height = uint32_t(vis.shadowPacker.height);
							desc.format = FORMAT_depthbufferShadowmap;
							desc.bind_flags = BindFlag::DEPTH_STENCIL | BindFlag::SHADER_RESOURCE;
							desc.layout = ResourceState::SHADER_RESOURCE;
							desc.misc_flags = ResourceMiscFlag::TEXTURE_COMPATIBLE_COMPRESSION;
							device->CreateTexture(&desc, nullptr, &shadowMapAtlas);
							device->SetName(&shadowMapAtlas, "shadowMapAtlas");

							desc.format = FORMAT_rendertargetShadowmap;
							desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
							desc.layout = ResourceState::SHADER_RESOURCE;
							desc.clear.color[0] = 1;
							desc.clear.color[1] = 1;
							desc.clear.color[2] = 1;
							desc.clear.color[3] = 0;
							device->CreateTexture(&desc, nullptr, &shadowMapAtlas_Transparent);
							device->SetName(&shadowMapAtlas_Transparent, "shadowMapAtlas_Transparent");

						}

						break;
					}
					else
					{
						iterative_scaling *= 0.5f;
					}
				}
				else
				{
					iterative_scaling = 0.0; //PE: fix - endless loop if some lights do not have shadows.
				}
			}
			profiler::EndRange(range);
		}

		profiler::EndRange(range); // Frustum Culling
	}
	void GRenderPath3DDetails::UpdatePerFrameData(Scene& scene, const Visibility& vis, FrameCB& frameCB, float dt)
	{
		// Calculate volumetric cloud shadow data:
		GEnvironmentComponent* environment = scene_Gdetails->environment;
		if (environment->IsVolumetricClouds() && environment->IsVolumetricCloudsCastShadow())
		{
			if (!textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW].IsValid())
			{
				TextureDesc desc;
				desc.type = TextureDesc::Type::TEXTURE_2D;
				desc.width = 512;
				desc.height = 512;
				desc.format = Format::R11G11B10_FLOAT;
				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
				device->CreateTexture(&desc, nullptr, &textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW]);
				device->SetName(&textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW], "textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW]");
				device->CreateTexture(&desc, nullptr, &textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW_GAUSSIAN_TEMP]);
				device->SetName(&textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW_GAUSSIAN_TEMP], "textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW_GAUSSIAN_TEMP]");
			}

			const float cloudShadowSnapLength = 5000.0f;
			const float cloudShadowExtent = 10000.0f; // The cloud shadow bounding box size
			const float cloudShadowNearPlane = 0.0f;
			const float cloudShadowFarPlane = cloudShadowExtent * 2.0;

			const float metersToSkyUnit = 0.001f; // Engine units are in meters (must be same as globals.hlsli)
			const float skyUnitToMeters = 1.0f / metersToSkyUnit;

			const EnvironmentComponent::AtmosphereParameters& atmosphere_parameters = environment->GetAtmosphereParameters();

			XMVECTOR atmosphereCenter = XMLoadFloat3(&atmosphere_parameters.planetCenter);
			XMVECTOR sunDirection = XMLoadFloat3(&environment->GetSunDirection());
			const float planetRadius = atmosphere_parameters.bottomRadius;

			// Point on the surface of the planet relative to camera position and planet normal
			XMVECTOR lookAtPosition = XMVector3Normalize(XMLoadFloat3(&camera->GetWorldEye()) - (atmosphereCenter * skyUnitToMeters));
			lookAtPosition = (atmosphereCenter + lookAtPosition * planetRadius) * skyUnitToMeters;

			// Snap with user defined value
			lookAtPosition = XMVectorFloor(XMVectorAdd(lookAtPosition, XMVectorReplicate(0.5f * cloudShadowSnapLength)) / cloudShadowSnapLength) * cloudShadowSnapLength;

			XMVECTOR lightPosition = lookAtPosition + sunDirection * cloudShadowExtent; // far plane not needed here

			const XMMATRIX lightRotation = XMMatrixRotationQuaternion(XMLoadFloat4(&environment->starsRotationQuaternion)); // We only care about prioritized directional light anyway
			const XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), lightRotation);

			XMMATRIX cloudShadowProjection = XMMatrixOrthographicOffCenterLH(-cloudShadowExtent, cloudShadowExtent, -cloudShadowExtent, cloudShadowExtent, cloudShadowNearPlane, cloudShadowFarPlane);
			XMMATRIX cloudShadowView = XMMatrixLookAtLH(lightPosition, lookAtPosition, up);

			XMMATRIX cloudShadowLightSpaceMatrix = XMMatrixMultiply(cloudShadowView, cloudShadowProjection);
			XMMATRIX cloudShadowLightSpaceMatrixInverse = XMMatrixInverse(nullptr, cloudShadowLightSpaceMatrix);

			XMStoreFloat4x4(&frameCB.cloudShadowLightSpaceMatrix, cloudShadowLightSpaceMatrix);
			XMStoreFloat4x4(&frameCB.cloudShadowLightSpaceMatrixInverse, cloudShadowLightSpaceMatrixInverse);
			frameCB.cloudShadowFarPlaneKm = cloudShadowFarPlane * metersToSkyUnit;
			frameCB.texture_volumetricclouds_shadow_index = device->GetDescriptorIndex(&textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW], SubresourceType::SRV);
		}

		if (environment->IsRealisticSky() && !textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT].IsValid())
		{
			TextureDesc desc;
			desc.type = TextureDesc::Type::TEXTURE_2D;
			desc.width = 256;
			desc.height = 64;
			desc.format = Format::R16G16B16A16_FLOAT;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
			device->CreateTexture(&desc, nullptr, &textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT]);
			device->SetName(&textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT], "textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT]");

			desc.type = TextureDesc::Type::TEXTURE_2D;
			desc.width = 32;
			desc.height = 32;
			desc.format = Format::R16G16B16A16_FLOAT;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
			device->CreateTexture(&desc, nullptr, &textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT]);
			device->SetName(&textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT], "textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT]");

			desc.type = TextureDesc::Type::TEXTURE_2D;
			desc.width = 192;
			desc.height = 104;
			desc.format = Format::R16G16B16A16_FLOAT;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
			device->CreateTexture(&desc, nullptr, &textures[TEXTYPE_2D_SKYATMOSPHERE_SKYVIEWLUT]);
			device->SetName(&textures[TEXTYPE_2D_SKYATMOSPHERE_SKYVIEWLUT], "textures[TEXTYPE_2D_SKYATMOSPHERE_SKYVIEWLUT]");

			desc.type = TextureDesc::Type::TEXTURE_2D;
			desc.width = 1;
			desc.height = 1;
			desc.format = Format::R16G16B16A16_FLOAT;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
			device->CreateTexture(&desc, nullptr, &textures[TEXTYPE_2D_SKYATMOSPHERE_SKYLUMINANCELUT]);
			device->SetName(&textures[TEXTYPE_2D_SKYATMOSPHERE_SKYLUMINANCELUT], "textures[TEXTYPE_2D_SKYATMOSPHERE_SKYLUMINANCELUT]");

			desc.type = TextureDesc::Type::TEXTURE_3D;
			desc.width = 32;
			desc.height = 32;
			desc.depth = 32;
			desc.format = Format::R16G16B16A16_FLOAT;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
			device->CreateTexture(&desc, nullptr, &textures[TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT]);
			device->SetName(&textures[TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT], "textures[TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT]");
		}
		 
		// Update CPU-side frame constant buffer:
		frameCB.Init();
		frameCB.delta_time = dt * renderingSpeed;
		frameCB.time_previous = frameCB.time;
		frameCB.time += frameCB.delta_time;
		frameCB.frame_count = (uint)device->GetFrameCount();
		frameCB.blue_noise_phase = (frameCB.frame_count & 0xFF) * 1.6180339887f;

		frameCB.temporalaa_samplerotation = 0;
		if (isTemporalAAEnabled && !camera->IsSlicer())
		{
			uint x = frameCB.frame_count % 4;
			uint y = frameCB.frame_count / 4;
			frameCB.temporalaa_samplerotation = (x & 0x000000FF) | ((y & 0x000000FF) << 8);
		}

		frameCB.giboost_packed = math::pack_half2(giBoost, 0);

		frameCB.options = 0;
		if (isTemporalAAEnabled && !camera->IsSlicer())
		{
			frameCB.options |= OPTION_BIT_TEMPORALAA_ENABLED;
		}
		if (isDisableAlbedoMaps)
		{
			frameCB.options |= OPTION_BIT_DISABLE_ALBEDO_MAPS;
		}
		if (isForceDiffuseLighting)
		{
			frameCB.options |= OPTION_BIT_FORCE_DIFFUSE_LIGHTING;
		}
		
		if (scene_Gdetails->environment->skyMap.IsValid() && !has_flag(scene_Gdetails->environment->skyMap.GetTexture().desc.misc_flags, ResourceMiscFlag::TEXTURECUBE))
		{
			frameCB.options |= OPTION_BIT_STATIC_SKY_SPHEREMAP;
		}

		frameCB.scene = scene_Gdetails->shaderscene;

		frameCB.texture_random64x64_index = device->GetDescriptorIndex(texturehelper::getRandom64x64(), SubresourceType::SRV);
		frameCB.texture_bluenoise_index = device->GetDescriptorIndex(texturehelper::getBlueNoise(), SubresourceType::SRV);
		frameCB.texture_sheenlut_index = device->GetDescriptorIndex(&textures[TEXTYPE_2D_SHEENLUT], SubresourceType::SRV);

		// See if indirect debug buffer needs to be resized:
		if (indirectDebugStatsReadback_available[device->GetBufferIndex()] && indirectDebugStatsReadback[device->GetBufferIndex()].mapped_data != nullptr)
		{
			const IndirectDrawArgsInstanced* indirectDebugStats = (const IndirectDrawArgsInstanced*)indirectDebugStatsReadback[device->GetBufferIndex()].mapped_data;
			const uint64_t required_debug_buffer_size = sizeof(IndirectDrawArgsInstanced) + (sizeof(XMFLOAT4) + sizeof(XMFLOAT4)) * clamp(indirectDebugStats->VertexCountPerInstance, 0u, 4000000u);
			if (buffers[BUFFERTYPE_INDIRECT_DEBUG_0].desc.size < required_debug_buffer_size)
			{
				GPUBufferDesc bd;
				bd.size = required_debug_buffer_size;
				bd.bind_flags = BindFlag::VERTEX_BUFFER | BindFlag::UNORDERED_ACCESS;
				bd.misc_flags = ResourceMiscFlag::BUFFER_RAW | ResourceMiscFlag::INDIRECT_ARGS;
				device->CreateBuffer(&bd, nullptr, &buffers[BUFFERTYPE_INDIRECT_DEBUG_0]);
				device->SetName(&buffers[BUFFERTYPE_INDIRECT_DEBUG_0], "buffers[BUFFERTYPE_INDIRECT_DEBUG_0]");
				device->CreateBuffer(&bd, nullptr, &buffers[BUFFERTYPE_INDIRECT_DEBUG_1]);
				device->SetName(&buffers[BUFFERTYPE_INDIRECT_DEBUG_1], "buffers[BUFFERTYPE_INDIRECT_DEBUG_1]");
				std::memset(indirectDebugStatsReadback_available, 0, sizeof(indirectDebugStatsReadback_available));
			}
		}

		// Note: shadow maps always assumed to be valid to avoid shader branching logic
		const Texture& shadowMap = shadowMapAtlas.IsValid() ? shadowMapAtlas : *texturehelper::getBlack();
		const Texture& shadowMapTransparent = shadowMapAtlas_Transparent.IsValid() ? shadowMapAtlas_Transparent : *texturehelper::getWhite();
		frameCB.texture_shadowatlas_index = device->GetDescriptorIndex(&shadowMap, SubresourceType::SRV);
		frameCB.texture_shadowatlas_transparent_index = device->GetDescriptorIndex(&shadowMapTransparent, SubresourceType::SRV);
		frameCB.shadow_atlas_resolution.x = shadowMap.desc.width;
		frameCB.shadow_atlas_resolution.y = shadowMap.desc.height;
		frameCB.shadow_atlas_resolution_rcp.x = 1.0f / frameCB.shadow_atlas_resolution.x;
		frameCB.shadow_atlas_resolution_rcp.y = 1.0f / frameCB.shadow_atlas_resolution.y;

		// Indirect debug buffers: 0 is always WRITE, 1 is always READ
		std::swap(buffers[BUFFERTYPE_INDIRECT_DEBUG_0], buffers[BUFFERTYPE_INDIRECT_DEBUG_1]);
		frameCB.indirect_debugbufferindex = device->GetDescriptorIndex(&buffers[BUFFERTYPE_INDIRECT_DEBUG_0], SubresourceType::UAV);

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
			float4x4* matrix_array = frameCB.matrixArray;

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
			envprobearray_offset = entity_counter;
			const size_t probe_iterations = std::min((size_t)MAX_SHADER_PROBE_COUNT, vis.visibleEnvProbes.size());
			for (size_t i = 0; i < probe_iterations; ++i)
			{
				if (entity_counter == SHADER_ENTITY_COUNT)
				{
					backlog::post("Shader Entity Overflow!! >> LIGHT PROBES");
					entity_counter--;
					break;
				}
				if (matrix_counter >= MATRIXARRAY_COUNT)
				{
					matrix_counter--;
					break;
				}
				ShaderEntity shaderentity = {};
				XMMATRIX shadermatrix;

				const uint32_t probeIndex = vis.visibleEnvProbes[vis.visibleEnvProbes.size() - 1 - i]; // note: reverse order, for correct blending!
				const GProbeComponent& probe = *scene_Gdetails->probeComponents[probeIndex];

				shaderentity = {}; // zero out!
				shaderentity.layerMask = ~0u;

				const LayeredMaskComponent* layer = probe.layeredmask;
				if (layer != nullptr)
				{
					shaderentity.layerMask = layer->GetVisibleLayerMask();
				}

				shaderentity.SetType(ENTITY_TYPE_ENVMAP);
				shaderentity.position = probe.position;
				shaderentity.SetRange(probe.range);

				shaderentity.SetIndices(matrix_counter, 0);
				shadermatrix = XMLoadFloat4x4(&probe.inverseMatrix);

				int texture = -1;
				if (probe.texture.IsValid())
				{
					texture = device->GetDescriptorIndex(&probe.texture, SubresourceType::SRV);
				}

				shadermatrix.r[0] = XMVectorSetW(shadermatrix.r[0], *(float*)&texture);
				shadermatrix.r[1] = XMVectorSetW(shadermatrix.r[1], 0);
				shadermatrix.r[2] = XMVectorSetW(shadermatrix.r[2], 0);
				shadermatrix.r[3] = XMVectorSetW(shadermatrix.r[3], 0);

				XMStoreFloat4x4(matrix_array + matrix_counter, shadermatrix);
				matrix_counter++;

				std::memcpy(entity_array + matrix_counter, &shaderentity, sizeof(ShaderEntity));
				matrix_counter++;
				envprobearray_count++;
			}

			const XMFLOAT2 atlas_dim_rcp = XMFLOAT2(1.0f / float(shadowMapAtlas.desc.width), 1.0f / float(shadowMapAtlas.desc.height));

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

				const GLightComponent& light = *scene_Gdetails->lightComponents[lightIndex];
				if (light.GetType() != LightComponent::LightType::DIRECTIONAL || light.IsInactive())
					continue;

				ShaderEntity shaderentity = {};
				shaderentity.layerMask = ~0u;
				const LayeredMaskComponent* layer = light.layeredmask;
				if (layer != nullptr)
				{
					shaderentity.layerMask = layer->GetVisibleLayerMask();
				}

				shaderentity.SetType(SCU32(light.GetType()));
				shaderentity.position = light.position;
				shaderentity.SetRange(light.GetRange());
				shaderentity.SetRadius(light.GetRadius());
				shaderentity.SetLength(light.GetLength());
				// note: the light direction used in shader refers to the direction to the light source
				shaderentity.SetDirection(XMFLOAT3(-light.direction.x, -light.direction.y, -light.direction.z));
				XMFLOAT3 light_color = light.GetColor();
				float light_intensity = light.GetIntensity();
				shaderentity.SetColor(float4(light_color.x * light_intensity, light_color.y * light_intensity, light_color.z * light_intensity, 1.f));

				// mark as no shadow by default:
				shaderentity.indices = ~0;

				const bool shadowmap = isShadowsEnabled && light.IsCastingShadow() && !light.IsStatic();
				const rectpacker::Rect& shadow_rect = vis.visibleLightShadowRects[lightIndex];
				
				if (shadowmap)
				{
					shaderentity.shadowAtlasMulAdd.x = shadow_rect.w * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.y = shadow_rect.h * atlas_dim_rcp.y;
					shaderentity.shadowAtlasMulAdd.z = shadow_rect.x * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.w = shadow_rect.y * atlas_dim_rcp.y;
				}

				shaderentity.SetIndices(matrix_counter, 0);

				const uint cascade_count = std::min((uint)light.cascadeDistances.size(), MATRIXARRAY_COUNT - matrix_counter);
				shaderentity.SetShadowCascadeCount(cascade_count);

				if (shadowmap && !light.cascadeDistances.empty())
				{
					SHCAM* shcams = (SHCAM*)alloca(sizeof(SHCAM) * cascade_count);
					CreateDirLightShadowCams(light, *vis.camera, shcams, cascade_count, shadow_rect);
					for (size_t cascade = 0; cascade < cascade_count; ++cascade)
					{
						XMStoreFloat4x4(&matrix_array[matrix_counter++], shcams[cascade].view_projection);
					}
				}

				if (light.IsCastingShadow())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_CASTING_SHADOW);
				}
				if (light.IsStatic())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_STATIC);
				}

				//if (light.IsVolumetricCloudsEnabled())
				//{
				//	shaderentity.SetFlags(ENTITY_FLAG_LIGHT_VOLUMETRICCLOUDS);
				//}

				std::memcpy(entity_array + entity_counter, &shaderentity, sizeof(ShaderEntity));
				entity_counter++;
				lightarray_count_directional++;
			}

			// Write spot lights into entity array:
			lightarray_offset_spot = entity_counter;
			for (uint32_t lightIndex : vis.visibleLights)
			{
				if (entity_counter == SHADER_ENTITY_COUNT)
				{
					entity_counter--;
					break;
				}

				const GLightComponent& light = *scene_Gdetails->lightComponents[lightIndex];
				if (light.GetType() != LightComponent::LightType::SPOT || light.IsInactive())
					continue;

				ShaderEntity shaderentity = {};
				shaderentity.layerMask = ~0u;

				const LayeredMaskComponent* layer = light.layeredmask;
				if (layer != nullptr)
				{
					shaderentity.layerMask = layer->GetVisibleLayerMask();
				}

				shaderentity.SetType(static_cast<uint>(light.GetType()));
				shaderentity.position = light.position;
				shaderentity.SetRange(light.GetRange());
				shaderentity.SetRadius(light.GetRadius());
				shaderentity.SetLength(light.GetLength());
				// note: the light direction used in shader refers to the direction to the light source
				shaderentity.SetDirection(XMFLOAT3(-light.direction.x, -light.direction.y, -light.direction.z));
				XMFLOAT3 light_color = light.GetColor();
				float light_intensity = light.GetIntensity();
				shaderentity.SetColor(float4(light_color.x* light_intensity, light_color.y* light_intensity, light_color.z* light_intensity, 1.f));

				// mark as no shadow by default:
				shaderentity.indices = ~0;

				const bool shadowmap = isShadowsEnabled && light.IsCastingShadow() && !light.IsStatic();
				const rectpacker::Rect& shadow_rect = vis.visibleLightShadowRects[lightIndex];
				
				const uint maskTex = light.maskTexDescriptor < 0 ? 0 : light.maskTexDescriptor;

				if (shadowmap)
				{
					shaderentity.shadowAtlasMulAdd.x = shadow_rect.w * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.y = shadow_rect.h * atlas_dim_rcp.y;
					shaderentity.shadowAtlasMulAdd.z = shadow_rect.x * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.w = shadow_rect.y * atlas_dim_rcp.y;
				}

				shaderentity.SetIndices(matrix_counter, maskTex);

				const float outerConeAngle = light.GetOuterConeAngle();
				const float innerConeAngle = std::min(light.GetInnerConeAngle(), outerConeAngle);
				const float outerConeAngleCos = std::cos(outerConeAngle);
				const float innerConeAngleCos = std::cos(innerConeAngle);

				// https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual#inner-and-outer-cone-angles
				const float lightAngleScale = 1.0f / std::max(0.001f, innerConeAngleCos - outerConeAngleCos);
				const float lightAngleOffset = -outerConeAngleCos * lightAngleScale;

				shaderentity.SetConeAngleCos(outerConeAngleCos);
				shaderentity.SetAngleScale(lightAngleScale);
				shaderentity.SetAngleOffset(lightAngleOffset);

				if (shadowmap || (maskTex > 0))
				{
					SHCAM shcam;
					CreateSpotLightShadowCam(light, shcam);
					XMStoreFloat4x4(&matrix_array[matrix_counter++], shcam.view_projection);
				}

				if (light.IsCastingShadow())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_CASTING_SHADOW);
				}

				if (light.IsStatic())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_STATIC);
				}

				//if (light.IsVolumetricCloudsEnabled())
				//{
				//	shaderentity.SetFlags(ENTITY_FLAG_LIGHT_VOLUMETRICCLOUDS);
				//}

				std::memcpy(entity_array + entity_counter, &shaderentity, sizeof(ShaderEntity));
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

				const GLightComponent& light = *scene_Gdetails->lightComponents[lightIndex];
				if (light.GetType() != LightComponent::LightType::POINT || light.IsInactive())
					continue;

				ShaderEntity shaderentity = {};
				shaderentity.layerMask = ~0u;

				const LayeredMaskComponent* layer = light.layeredmask;
				if (layer != nullptr)
				{
					shaderentity.layerMask = layer->GetVisibleLayerMask();
				}

				shaderentity.SetType(static_cast<uint>(light.GetType()));
				shaderentity.position = light.position;
				shaderentity.SetRange(light.GetRange());
				shaderentity.SetRadius(light.GetRadius());
				shaderentity.SetLength(light.GetLength());
				// note: the light direction used in shader refers to the direction to the light source
				shaderentity.SetDirection(XMFLOAT3(-light.direction.x, -light.direction.y, -light.direction.z));
				XMFLOAT3 light_color = light.GetColor();
				float light_intensity = light.GetIntensity();
				shaderentity.SetColor(float4(light_color.x* light_intensity, light_color.y* light_intensity, light_color.z* light_intensity, 1.f));

				// mark as no shadow by default:
				shaderentity.indices = ~0;

				const bool shadowmap = isShadowsEnabled && light.IsCastingShadow() && !light.IsStatic();
				const rectpacker::Rect& shadow_rect = vis.visibleLightShadowRects[lightIndex];

				const uint maskTex = light.maskTexDescriptor < 0 ? 0 : light.maskTexDescriptor;

				if (shadowmap)
				{
					shaderentity.shadowAtlasMulAdd.x = shadow_rect.w * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.y = shadow_rect.h * atlas_dim_rcp.y;
					shaderentity.shadowAtlasMulAdd.z = shadow_rect.x * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.w = shadow_rect.y * atlas_dim_rcp.y;
				}

				shaderentity.SetIndices(matrix_counter, maskTex);

				if (shadowmap)
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

				//if (light.IsVolumetricCloudsEnabled())
				//{
				//	shaderentity.SetFlags(ENTITY_FLAG_LIGHT_VOLUMETRICCLOUDS);
				//}

				std::memcpy(entity_array + entity_counter, &shaderentity, sizeof(ShaderEntity));
				entity_counter++;
				lightarray_count_point++;
			}

			lightarray_count = lightarray_count_directional + lightarray_count_spot + lightarray_count_point;
			frameCB.entity_culling_count = lightarray_count + decalarray_count + envprobearray_count;

			// TODO: JOLT PHYSICS
			// Write colliders into entity array:
			//forcefieldarray_offset = entity_counter;
			//for (size_t i = 0; i < vis.scene->collider_count_gpu; ++i)
			//{
			//	if (entityCounter == SHADER_ENTITY_COUNT)
			//	{
			//		entityCounter--;
			//		break;
			//	}
			//	ShaderEntity shaderentity = {};
			//
			//	const ColliderComponent& collider = vis.scene->colliders_gpu[i];
			//	shaderentity.layerMask = collider.layerMask;
			//
			//	switch (collider.shape)
			//	{
			//	case ColliderComponent::Shape::Sphere:
			//		shaderentity.SetType(ENTITY_TYPE_COLLIDER_SPHERE);
			//		shaderentity.position = collider.sphere.center;
			//		shaderentity.SetRange(collider.sphere.radius);
			//		break;
			//	case ColliderComponent::Shape::Capsule:
			//		shaderentity.SetType(ENTITY_TYPE_COLLIDER_CAPSULE);
			//		shaderentity.position = collider.capsule.base;
			//		shaderentity.SetColliderTip(collider.capsule.tip);
			//		shaderentity.SetRange(collider.capsule.radius);
			//		break;
			//	case ColliderComponent::Shape::Plane:
			//		shaderentity.SetType(ENTITY_TYPE_COLLIDER_PLANE);
			//		shaderentity.position = collider.plane.origin;
			//		shaderentity.SetDirection(collider.plane.normal);
			//		shaderentity.SetIndices(matrixCounter, ~0u);
			//		matrixArray[matrixCounter++] = collider.plane.projection;
			//		break;
			//	default:
			//		assert(0);
			//		break;
			//	}
			//
			//	std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
			//	entityCounter++;
			//	forcefieldarray_count++;
			//}
			//
			//// Write force fields into entity array:
			//for (size_t i = 0; i < vis.scene->forces.GetCount(); ++i)
			//{
			//	if (entityCounter == SHADER_ENTITY_COUNT)
			//	{
			//		entityCounter--;
			//		break;
			//	}
			//	ShaderEntity shaderentity = {};
			//
			//	const ForceFieldComponent& force = vis.scene->forces[i];
			//
			//	shaderentity.layerMask = ~0u;
			//
			//	Entity entity = vis.scene->forces.GetEntity(i);
			//	const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
			//	if (layer != nullptr)
			//	{
			//		shaderentity.layerMask = layer->layerMask;
			//	}
			//
			//	switch (force.type)
			//	{
			//	default:
			//	case ForceFieldComponent::Type::Point:
			//		shaderentity.SetType(ENTITY_TYPE_FORCEFIELD_POINT);
			//		break;
			//	case ForceFieldComponent::Type::Plane:
			//		shaderentity.SetType(ENTITY_TYPE_FORCEFIELD_PLANE);
			//		break;
			//	}
			//	shaderentity.position = force.position;
			//	shaderentity.SetGravity(force.gravity);
			//	shaderentity.SetRange(std::max(0.001f, force.GetRange()));
			//	// The default planar force field is facing upwards, and thus the pull direction is downwards:
			//	shaderentity.SetDirection(force.direction);
			//
			//	std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
			//	entityCounter++;
			//	forcefieldarray_count++;
			//}
		}

		frameCB.probes = ShaderEntityIterator(envprobearray_offset, envprobearray_count);
		frameCB.directional_lights = ShaderEntityIterator(lightarray_offset_directional, lightarray_count_directional);
		frameCB.spot_lights = ShaderEntityIterator(lightarray_offset_spot, lightarray_count_spot);
		frameCB.point_lights = ShaderEntityIterator(lightarray_offset_point, lightarray_count_point);
		frameCB.lights = ShaderEntityIterator(lightarray_offset, lightarray_count);
		frameCB.decals = ShaderEntityIterator(decalarray_offset, decalarray_count);
		//frameCB.forces = ShaderEntityIterator(forcefieldarray_offset, forcefieldarray_count);
	}

	void GRenderPath3DDetails::Visibility_Prepare(
		const VisibilityResources& res,
		const Texture& input_primitiveID_1, // can be MSAA
		const Texture& input_primitiveID_2, // can be MSAA
		CommandList cmd
	)
	{
		device->EventBegin("Visibility_Prepare", cmd);
		auto range = profiler::BeginRangeGPU("Visibility_Prepare", &cmd);

		BindCommonResources(cmd);

		// Note: the tile_count here must be valid whether the VisibilityResources was created or not!
		XMUINT2 tile_count = GetVisibilityTileCount(XMUINT2(input_primitiveID_1.desc.width, input_primitiveID_1.desc.height));

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

	void GRenderPath3DDetails::Visibility_Surface(
		const VisibilityResources& res,
		const Texture& output,
		CommandList cmd
	)
	{
		device->EventBegin("Visibility_Surface", cmd);
		auto range = profiler::BeginRangeGPU("Visibility_Surface", &cmd);

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
	void GRenderPath3DDetails::Visibility_Surface_Reduced(
		const VisibilityResources& res,
		CommandList cmd
	)
	{
		assert(0 && "Not Yet Supported!");
		device->EventBegin("Visibility_Surface_Reduced", cmd);
		auto range = profiler::BeginRangeGPU("Visibility_Surface_Reduced", &cmd);

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
	void GRenderPath3DDetails::Visibility_Shade(
		const VisibilityResources& res,
		const Texture& output,
		CommandList cmd
	)
	{
		device->EventBegin("Visibility_Shade", cmd);
		auto range = profiler::BeginRangeGPU("Visibility_Shade", &cmd);

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
		//  * shadercam.clip_plane : RenderPath3D's property
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
						float ndcY = 1.0f - ((screenY - viewportY) / viewportHeight) * 2.0f; // y는 반전
						float ndcZ = screenZ; // 보통 0~1 범위로 제공됨

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

		shadercam.inverse_vp = matToScreenInv;

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
		shadercam.visibility_tilecount = GetVisibilityTileCount(shadercam.internal_resolution);
		shadercam.visibility_tilecount_flat = shadercam.visibility_tilecount.x * shadercam.visibility_tilecount.y;

		shadercam.texture_primitiveID_1_index = device->GetDescriptorIndex(&rtPrimitiveID_1, SubresourceType::SRV);
		shadercam.texture_primitiveID_2_index = device->GetDescriptorIndex(&rtPrimitiveID_2, SubresourceType::SRV);
		shadercam.texture_depth_index = device->GetDescriptorIndex(&depthBuffer_Copy, SubresourceType::SRV);
		shadercam.texture_lineardepth_index = device->GetDescriptorIndex(&rtLinearDepth, SubresourceType::SRV);
		//shadercam.texture_velocity_index = camera.texture_velocity_index;
		shadercam.texture_normal_index = device->GetDescriptorIndex(&visibilityResources.texture_normals, SubresourceType::SRV);
		shadercam.texture_roughness_index = device->GetDescriptorIndex(&visibilityResources.texture_roughness, SubresourceType::SRV);
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

	void GRenderPath3DDetails::UpdateRenderData(const Visibility& vis, const FrameCB& frameCB, CommandList cmd)
	{
		device->EventBegin("UpdateRenderData", cmd);

		auto prof_updatebuffer_cpu = profiler::BeginRangeCPU("Update Buffers (CPU)");
		auto prof_updatebuffer_gpu = profiler::BeginRangeGPU("Update Buffers (GPU)", &cmd);

		device->CopyBuffer(&indirectDebugStatsReadback[device->GetBufferIndex()], 0, &buffers[BUFFERTYPE_INDIRECT_DEBUG_0], 0, sizeof(IndirectDrawArgsInstanced), cmd);
		indirectDebugStatsReadback_available[device->GetBufferIndex()] = true;

		barrierStack.push_back(GPUBarrier::Buffer(&buffers[BUFFERTYPE_INDIRECT_DEBUG_0], ResourceState::COPY_SRC, ResourceState::COPY_DST));
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

		// Indirect debug buffer - clear indirect args:
		IndirectDrawArgsInstanced debug_indirect = {};
		debug_indirect.VertexCountPerInstance = 0;
		debug_indirect.InstanceCount = 1;
		debug_indirect.StartVertexLocation = 0;
		debug_indirect.StartInstanceLocation = 0;
		device->UpdateBuffer(&buffers[BUFFERTYPE_INDIRECT_DEBUG_0], &debug_indirect, cmd, sizeof(debug_indirect));
		barrierStack.push_back(GPUBarrier::Buffer(&buffers[BUFFERTYPE_INDIRECT_DEBUG_0], ResourceState::COPY_DST, ResourceState::UNORDERED_ACCESS));
		barrierStack.push_back(GPUBarrier::Buffer(&buffers[BUFFERTYPE_INDIRECT_DEBUG_1], ResourceState::UNORDERED_ACCESS, ResourceState::VERTEX_BUFFER | ResourceState::INDIRECT_ARGUMENT | ResourceState::COPY_SRC));

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
	void GRenderPath3DDetails::UpdateRenderDataAsync(const Visibility& vis, const FrameCB& frameCB, CommandList cmd)
	{
		device->EventBegin("UpdateRenderDataAsync", cmd);

		BindCommonResources(cmd);

		// Wetmaps will be initialized:
		for (uint32_t i = 0, n = (uint32_t)vis.visibleRenderables_Mesh.size(); i < n; ++i)
		{
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[vis.visibleRenderables_Mesh[i]];
			vzlog_assert(renderable.GetRenderableType() == RenderableType::MESH_RENDERABLE, compfactory::GetNameComponent(renderable.GetEntity())->GetName().c_str());

			GGeometryComponent& geometry = *renderable.geometry;
			if (!geometry.HasRenderData())
			{
				continue;
			}

			size_t num_parts = geometry.GetNumParts();
			bool has_buffer_effect = num_parts == renderable.bufferEffects.size();
			for (size_t part_index = 0; part_index < num_parts; ++part_index)
			{
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

	void GRenderPath3DDetails::OcclusionCulling_Reset(const Visibility& vis, CommandList cmd)
	{
		const GPUQueryHeap& queryHeap = scene_Gdetails->queryHeap;

		if (!renderer::isOcclusionCullingEnabled || renderer::isFreezeCullingCameraEnabled || !queryHeap.IsValid())
		{
			return;
		}
		if (vis.visibleRenderables_All.empty() && vis.visibleLights.empty())
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
	void GRenderPath3DDetails::OcclusionCulling_Render(const CameraComponent& camera, const Visibility& vis, CommandList cmd)
	{
		const GPUQueryHeap& queryHeap = scene_Gdetails->queryHeap;

		if (!renderer::isOcclusionCullingEnabled || renderer::isFreezeCullingCameraEnabled || !queryHeap.IsValid())
		{
			return;
		}
		if (vis.visibleRenderables_All.empty() && vis.visibleLights.empty())
		{
			return;
		}

		auto range = profiler::BeginRangeGPU("Occlusion Culling Render", &cmd);

		device->BindPipelineState(&PSO_occlusionquery, cmd);

		XMMATRIX VP = XMLoadFloat4x4(&camera.GetViewProjection());

		int query_write = scene_Gdetails->queryheapIdx;

		if (!vis.visibleRenderables_All.empty())
		{
			device->EventBegin("Occlusion Culling Objects", cmd);

			for (uint32_t instanceIndex : vis.visibleRenderables_All)
			{
				const GSceneDetails::OcclusionResult& occlusion_result = scene_Gdetails->occlusionResultsObjects[instanceIndex];
				GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instanceIndex];

				int queryIndex = occlusion_result.occlusionQueries[query_write];
				if (queryIndex >= 0)
				{
					const AABB& aabb = scene_Gdetails->aabbRenderables[renderable.renderableIndex];
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

		if (!vis.visibleLights.empty())
		{
			device->EventBegin("Occlusion Culling Lights", cmd);

			for (uint32_t lightIndex : vis.visibleLights)
			{
				const GLightComponent& light = *scene_Gdetails->lightComponents[lightIndex];

				if (light.occlusionquery >= 0)
				{
					uint32_t queryIndex = (uint32_t)light.occlusionquery;
					const AABB& aabb = scene_Gdetails->aabbLights[light.lightIndex];
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
	void GRenderPath3DDetails::OcclusionCulling_Resolve(const Visibility& vis, CommandList cmd)
	{
		const GPUQueryHeap& queryHeap = scene_Gdetails->queryHeap;

		if (!renderer::isOcclusionCullingEnabled || renderer::isFreezeCullingCameraEnabled || !queryHeap.IsValid())
		{
			return;
		}
		if (vis.visibleRenderables_All.empty() && vis.visibleLights.empty())
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

	ForwardEntityMaskCB GRenderPath3DDetails::ForwardEntityCullingCPU(const Visibility& vis, const AABB& batch_aabb, RENDERPASS renderPass)
	{
		// Performs CPU light culling for a renderable batch:
		//	Similar to GPU-based tiled light culling, but this is only for simple forward passes (drawcall-granularity)

		ForwardEntityMaskCB cb;
		cb.xForwardLightMask.x = 0;
		cb.xForwardLightMask.y = 0;
		cb.xForwardDecalMask = 0;
		cb.xForwardEnvProbeMask = 0;

		uint32_t buckets[2] = { 0,0 };
		for (size_t i = 0; i < std::min(size_t(64), vis.visibleLights.size()); ++i) // only support indexing 64 lights at max for now
		{
			const uint32_t lightIndex = vis.visibleLights[i];
			const AABB& light_aabb = scene_Gdetails->aabbLights[lightIndex];
			if (light_aabb.intersects(batch_aabb))
			{
				const uint8_t bucket_index = uint8_t(i / 32);
				const uint8_t bucket_place = uint8_t(i % 32);
				buckets[bucket_index] |= 1 << bucket_place;
			}
		}
		cb.xForwardLightMask.x = buckets[0];
		cb.xForwardLightMask.y = buckets[1];

		//for (size_t i = 0; i < std::min(size_t(32), vis.visibleDecals.size()); ++i)
		//{
		//	const uint32_t decalIndex = vis.visibleDecals[vis.visibleDecals.size() - 1 - i]; // note: reverse order, for correct blending!
		//	const AABB& decal_aabb = vis.scene->aabb_decals[decalIndex];
		//	if (decal_aabb.intersects(batch_aabb))
		//	{
		//		const uint8_t bucket_place = uint8_t(i % 32);
		//		cb.xForwardDecalMask |= 1 << bucket_place;
		//	}
		//}

		if (renderPass != RENDERPASS_ENVMAPCAPTURE)
		{
			for (size_t i = 0; i < std::min(size_t(32), vis.visibleEnvProbes.size()); ++i)
			{
				const uint32_t probeIndex = vis.visibleEnvProbes[vis.visibleEnvProbes.size() - 1 - i]; // note: reverse order, for correct blending!
				const AABB& probe_aabb = scene_Gdetails->aabbProbes[probeIndex];
				if (probe_aabb.intersects(batch_aabb))
				{
					const uint8_t bucket_place = uint8_t(i % 32);
					cb.xForwardEnvProbeMask |= 1 << bucket_place;
				}
			}
		}

		return cb;
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
		const Visibility& vis,
		const Texture& debugUAV,
		CommandList cmd
	)
	{
		auto range = profiler::BeginRangeGPU("Entity Culling", &cmd);

		device->Barrier(GPUBarrier::Buffer(&res.entityTiles, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS), cmd);

		if (
			vis.visibleLights.empty() &&
			//vis.visibleDecals.empty() &&
			vis.visibleEnvProbes.empty()
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

	void GRenderPath3DDetails::CreateVisibilityResources(VisibilityResources& res, XMUINT2 resolution)
	{
		res.tile_count = GetVisibilityTileCount(resolution);
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(ShaderTypeBin);
			desc.size = desc.stride * (SCU32(MaterialComponent::ShaderType::COUNT) + 1); // +1 for sky
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED | ResourceMiscFlag::INDIRECT_ARGS;
			bool success = device->CreateBuffer(&desc, nullptr, &res.bins);
			assert(success);
			device->SetName(&res.bins, "res.bins");

			desc.stride = sizeof(VisibilityTile);
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