#include "RenderPath3D_Detail.h"

namespace vz::renderer
{
	void GRenderPath3DDetails::DrawShadowmaps(
		const Visibility& vis,
		CommandList cmd
	)
	{
		if (renderer::isWireRender || !renderer::isShadowsEnabled)
			return;
		if (!shadowMapAtlas.IsValid())
			return;
		if (vis.visibleLights.empty())
			return;

		device->EventBegin("DrawShadowmaps", cmd);
		auto range_cpu = profiler::BeginRangeCPU("Shadowmap Rendering");
		auto range_gpu = profiler::BeginRangeGPU("Shadowmap Rendering", &cmd);

		const bool predication_request =
			device->CheckCapability(GraphicsDeviceCapability::PREDICATION) &&
			renderer::isOcclusionCullingEnabled;

		const bool shadow_lod_override = renderer::isShadowLODOverride;

		BindCommonResources(cmd);

		BoundingFrustum cam_frustum;
		XMMATRIX P = XMLoadFloat4x4(&vis.camera->GetProjection());
		BoundingFrustum::CreateFromMatrix(cam_frustum, P);
		std::swap(cam_frustum.Near, cam_frustum.Far);
		XMMATRIX V_inv = XMLoadFloat4x4(&vis.camera->GetInvView());
		cam_frustum.Transform(cam_frustum, V_inv);
		XMStoreFloat4(&cam_frustum.Orientation, XMQuaternionNormalize(XMLoadFloat4(&cam_frustum.Orientation)));

		CameraCB cb;
		cb.Init();

		const XMFLOAT3 EYE = vis.camera->GetWorldEye();

		const uint32_t max_viewport_count = device->GetMaxViewportCount();

		const RenderPassImage rp[] = {
			RenderPassImage::DepthStencil(
				&shadowMapAtlas,
				RenderPassImage::LoadOp::CLEAR,
				RenderPassImage::StoreOp::STORE,
				ResourceState::SHADER_RESOURCE,
				ResourceState::DEPTHSTENCIL,
				ResourceState::SHADER_RESOURCE
			),
			RenderPassImage::RenderTarget(
				&shadowMapAtlas_Transparent,
				RenderPassImage::LoadOp::CLEAR,
				RenderPassImage::StoreOp::STORE,
				ResourceState::SHADER_RESOURCE,
				ResourceState::SHADER_RESOURCE
			),
		};
		device->RenderPassBegin(rp, arraysize(rp), cmd);

		std::vector<geometrics::AABB>& aabb_renderables = scene_Gdetails->aabbRenderables;

		for (uint32_t lightIndex : vis.visibleLights)
		{
			const GLightComponent& light = *scene_Gdetails->lightComponents[lightIndex];
			if (light.IsInactive())
				continue;

			const bool shadow = light.IsCastingShadow() && !light.IsStatic();
			if (!shadow)
				continue;
			const rectpacker::Rect& shadow_rect = vis.visibleLightShadowRects[lightIndex];

			renderQueue.init();
			renderQueue_transparent.init();

			switch (light.GetType())
			{
			case LightComponent::LightType::DIRECTIONAL:
			{
				if (maxShadowResolution_2D == 0 && light.forcedShadowResolution < 0)
					break;
				if (light.cascadeDistances.empty())
					break;

				const uint32_t cascade_count = std::min((uint32_t)light.cascadeDistances.size(), max_viewport_count);
				Viewport* viewports = (Viewport*)alloca(sizeof(Viewport) * cascade_count);
				Rect* scissors = (Rect*)alloca(sizeof(Rect) * cascade_count);
				SHCAM* shcams = (SHCAM*)alloca(sizeof(SHCAM) * cascade_count);
				CreateDirLightShadowCams(light, *vis.camera, shcams, cascade_count, shadow_rect);
				
				for (size_t i = 0; i < aabb_renderables.size(); ++i)
				{
					const AABB& aabb = aabb_renderables[i];
					if (aabb.layerMask & vis.layerMask)
					{
						const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[i];
						if (renderable.IsRenderable() && !renderable.IsShadowCastDisabled())	// first chechk the renderable's shadow option 
						{
							const float distanceSq = math::DistanceSquared(EYE, renderable.center);
							if (distanceSq > sqr(renderable.GetFadeDistance() + renderable.radius))
								continue;

							// Determine which cascades the object is contained in:
							uint8_t camera_mask = 0;
							uint8_t shadow_lod = 0xFF;
							for (uint32_t cascade = 0; cascade < cascade_count; ++cascade)
							{
								if ((cascade < (cascade_count - renderable.GetCascadeMask())) && shcams[cascade].frustum.CheckBoxFast(aabb))
								{
									camera_mask |= 1 << cascade;
									if (shadow_lod_override)
									{
										const uint8_t candidate_lod = renderer::ComputeObjectLODForView(renderable, aabb, *renderable.geometry, shcams[cascade].view_projection);
										shadow_lod = std::min(shadow_lod, candidate_lod);
									}
								}
							}
							if (camera_mask == 0)
								continue;

							RenderBatch batch;
							batch.Create(renderable.geometry->geometryIndex, uint32_t(i), 0, renderable.sortBits, camera_mask, shadow_lod);

							const uint32_t filterMask = renderable.materialFilterFlags;
							if (filterMask & GMaterialComponent::FILTER_OPAQUE)
							{
								renderQueue.add(batch);
							}
							if ((filterMask & GMaterialComponent::FILTER_TRANSPARENT) || (filterMask & GMaterialComponent::FILTER_WATER))
							{
								renderQueue_transparent.add(batch);
							}
						}
					}
				}

				if (!renderQueue.empty() || !renderQueue_transparent.empty())
				{
					for (uint32_t cascade = 0; cascade < cascade_count; ++cascade)
					{
						XMStoreFloat4x4(&cb.cameras[cascade].view_projection, shcams[cascade].view_projection);
						cb.cameras[cascade].output_index = cascade;
						for (int i = 0; i < arraysize(cb.cameras[cascade].frustum.planes); ++i)
						{
							cb.cameras[cascade].frustum.planes[i] = shcams[cascade].frustum.planes[i];
						}
						cb.cameras[cascade].options = SHADERCAMERA_OPTION_ORTHO;
						cb.cameras[cascade].forward.x = -light.direction.x;
						cb.cameras[cascade].forward.y = -light.direction.y;
						cb.cameras[cascade].forward.z = -light.direction.z;

						Viewport vp;
						vp.top_left_x = float(shadow_rect.x + cascade * shadow_rect.w);
						vp.top_left_y = float(shadow_rect.y);
						vp.width = float(shadow_rect.w);
						vp.height = float(shadow_rect.h);
						viewports[cascade] = vp; // instead of reference, copy it to be sure every member is initialized (alloca)
						scissors[cascade].from_viewport(vp);
					}

					device->BindDynamicConstantBuffer(cb, CBSLOT_RENDERER_CAMERA, cmd);
					device->BindViewports(cascade_count, viewports, cmd);
					device->BindScissorRects(cascade_count, scissors, cmd);

					renderQueue.sort_opaque();
					renderQueue_transparent.sort_transparent();
					RenderMeshes(vis, renderQueue, RENDERPASS_SHADOW, GMaterialComponent::FILTER_OPAQUE, cmd, 0, cascade_count);
					RenderMeshes(vis, renderQueue_transparent, RENDERPASS_SHADOW, GMaterialComponent::FILTER_TRANSPARENT | GMaterialComponent::FILTER_WATER, cmd, 0, cascade_count);
				}
			}
			break;
			case LightComponent::LightType::SPOT:
			{
				if (maxShadowResolution_2D == 0 && light.forcedShadowResolution < 0)
					break;

				SHCAM shcam;
				CreateSpotLightShadowCam(light, shcam);
				if (!cam_frustum.Intersects(shcam.boundingfrustum))
					break;

				for (size_t i = 0; i < aabb_renderables.size(); ++i)
				{
					const AABB& aabb = aabb_renderables[i];
					if ((aabb.layerMask & vis.layerMask) && shcam.frustum.CheckBoxFast(aabb))
					{
						const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[i];
						if (renderable.IsRenderable() && !renderable.IsShadowCastDisabled())
						{
							const float distanceSq = math::DistanceSquared(EYE, renderable.center);
							if (distanceSq > sqr(renderable.GetFadeDistance() + renderable.radius)) // Note: here I use draw_distance instead of fadeDeistance because this doesn't account for impostor switch fade
								continue;

							uint8_t shadow_lod = 0xFF;
							if (shadow_lod_override)
							{
								const uint8_t candidate_lod = renderer::ComputeObjectLODForView(renderable, aabb, *renderable.geometry, shcam.view_projection);
								shadow_lod = std::min(shadow_lod, candidate_lod);
							}

							RenderBatch batch;
							batch.Create(renderable.geometry->geometryIndex, uint32_t(i), 0, renderable.sortBits, 0xFF, shadow_lod);

							const uint32_t filterMask = renderable.materialFilterFlags;
							if (filterMask & GMaterialComponent::FILTER_OPAQUE)
							{
								renderQueue.add(batch);
							}
							if ((filterMask & GMaterialComponent::FILTER_TRANSPARENT) || (filterMask & GMaterialComponent::FILTER_WATER))
							{
								renderQueue_transparent.add(batch);
							}
						}
					}
				}

				if (predication_request && light.occlusionquery >= 0)
				{
					device->PredicationBegin(
						&scene_Gdetails->queryPredicationBuffer,
						(uint64_t)light.occlusionquery * sizeof(uint64_t),
						PredicationOp::EQUAL_ZERO,
						cmd
					);
				}

				if (!renderQueue.empty() || !renderQueue_transparent.empty())
				{
					XMStoreFloat4x4(&cb.cameras[0].view_projection, shcam.view_projection);
					cb.cameras[0].output_index = 0;
					for (int i = 0; i < arraysize(cb.cameras[0].frustum.planes); ++i)
					{
						cb.cameras[0].frustum.planes[i] = shcam.frustum.planes[i];
					}
					device->BindDynamicConstantBuffer(cb, CBSLOT_RENDERER_CAMERA, cmd);

					Viewport vp;
					vp.top_left_x = float(shadow_rect.x);
					vp.top_left_y = float(shadow_rect.y);
					vp.width = float(shadow_rect.w);
					vp.height = float(shadow_rect.h);
					device->BindViewports(1, &vp, cmd);

					Rect scissor;
					scissor.from_viewport(vp);
					device->BindScissorRects(1, &scissor, cmd);

					renderQueue.sort_opaque();
					renderQueue_transparent.sort_transparent();
					RenderMeshes(vis, renderQueue, RENDERPASS_SHADOW, GMaterialComponent::FILTER_OPAQUE, cmd);
					RenderMeshes(vis, renderQueue_transparent, RENDERPASS_SHADOW, GMaterialComponent::FILTER_TRANSPARENT | GMaterialComponent::FILTER_WATER, cmd);
				}

				if (predication_request && light.occlusionquery >= 0)
				{
					device->PredicationEnd(cmd);
				}
			}
			break;
			case LightComponent::LightType::POINT:
			{
				if (maxShadowResolution_2D == 0 && light.forcedShadowResolution < 0)
					break;

				Sphere boundingsphere(light.position, light.GetRange());

				const float zNearP = 0.1f;
				const float zFarP = std::max(1.0f, light.GetRange());
				SHCAM cameras[6];
				renderer::CreateCubemapCameras(light.position, zNearP, zFarP, cameras, arraysize(cameras));
				Viewport vp[arraysize(cameras)];
				Rect scissors[arraysize(cameras)];
				Frustum frusta[arraysize(cameras)];
				uint32_t camera_count = 0;

				for (uint32_t shcam = 0; shcam < arraysize(cameras); ++shcam)
				{
					// always set up viewport and scissor just to be safe, even if this one is skipped:
					vp[shcam].top_left_x = float(shadow_rect.x + shcam * shadow_rect.w);
					vp[shcam].top_left_y = float(shadow_rect.y);
					vp[shcam].width = float(shadow_rect.w);
					vp[shcam].height = float(shadow_rect.h);
					scissors[shcam].from_viewport(vp[shcam]);

					// Check if cubemap face frustum is visible from main camera, otherwise, it will be skipped:
					if (cam_frustum.Intersects(cameras[shcam].boundingfrustum))
					{
						XMStoreFloat4x4(&cb.cameras[camera_count].view_projection, cameras[shcam].view_projection);
						// We no longer have a straight mapping from camera to viewport:
						//	- there will be always 6 viewports
						//	- there will be only as many cameras, as many cubemap face frustums are visible from main camera
						//	- output_index is mapping camera to viewport, used by shader to output to SV_ViewportArrayIndex
						cb.cameras[camera_count].output_index = shcam;
						cb.cameras[camera_count].options = SHADERCAMERA_OPTION_NONE;
						for (int i = 0; i < arraysize(cb.cameras[camera_count].frustum.planes); ++i)
						{
							cb.cameras[camera_count].frustum.planes[i] = cameras[shcam].frustum.planes[i];
						}
						frusta[camera_count] = cameras[shcam].frustum;
						camera_count++;
					}
				}

				for (size_t i = 0; i < aabb_renderables.size(); ++i)
				{
					const AABB& aabb = aabb_renderables[i];
					if ((aabb.layerMask & vis.layerMask) && boundingsphere.intersects(aabb))
					{
						const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[i];
						if (renderable.IsRenderable() && !renderable.IsShadowCastDisabled())
						{
							const float distanceSq = math::DistanceSquared(EYE, renderable.center);
							if (distanceSq > sqr(renderable.GetFadeDistance() + renderable.radius)) // Note: here I use draw_distance instead of fadeDeistance because this doesn't account for impostor switch fade
								continue;

							// Check for each frustum, if object is visible from it:
							uint8_t camera_mask = 0;
							uint8_t shadow_lod = 0xFF;
							for (uint32_t camera_index = 0; camera_index < camera_count; ++camera_index)
							{
								if (frusta[camera_index].CheckBoxFast(aabb))
								{
									camera_mask |= 1 << camera_index;
									if (shadow_lod_override)
									{
										const uint8_t candidate_lod = renderer::ComputeObjectLODForView(renderable, aabb, *renderable.geometry, cameras[camera_index].view_projection);
										shadow_lod = std::min(shadow_lod, candidate_lod);
									}
								}
							}
							if (camera_mask == 0)
								continue;
							
							RenderBatch batch;
							batch.Create(renderable.geometry->geometryIndex, uint32_t(i), 0, renderable.sortBits, camera_mask, shadow_lod);

							const uint32_t filterMask = renderable.materialFilterFlags;
							if (filterMask & GMaterialComponent::FILTER_OPAQUE)
							{
								renderQueue.add(batch);
							}
							if ((filterMask & GMaterialComponent::FILTER_TRANSPARENT) || (filterMask & GMaterialComponent::FILTER_WATER))
							{
								renderQueue_transparent.add(batch);
							}
						}
					}
				}

				if (predication_request && light.occlusionquery >= 0)
				{
					device->PredicationBegin(
						&scene_Gdetails->queryPredicationBuffer,
						(uint64_t)light.occlusionquery * sizeof(uint64_t),
						PredicationOp::EQUAL_ZERO,
						cmd
					);
				}

				if (!renderQueue.empty() || renderQueue_transparent.empty())
				{
					device->BindDynamicConstantBuffer(cb, CBSLOT_RENDERER_CAMERA, cmd);
					device->BindViewports(arraysize(vp), vp, cmd);
					device->BindScissorRects(arraysize(scissors), scissors, cmd);

					renderQueue.sort_opaque();
					renderQueue_transparent.sort_transparent();
					RenderMeshes(vis, renderQueue, RENDERPASS_SHADOW, GMaterialComponent::FILTER_OPAQUE, cmd, 0, camera_count);
					RenderMeshes(vis, renderQueue_transparent, RENDERPASS_SHADOW, GMaterialComponent::FILTER_TRANSPARENT | GMaterialComponent::FILTER_WATER, cmd, 0, camera_count);
				}

				if (predication_request && light.occlusionquery >= 0)
				{
					device->PredicationEnd(cmd);
				}

			}
			break;
			} // terminate switch
		}

		device->RenderPassEnd(cmd);

		profiler::EndRange(range_gpu);
		profiler::EndRange(range_cpu);
		device->EventEnd(cmd);
	}
}
