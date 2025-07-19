#include "RenderPath3D_Detail.h"

namespace vz::renderer
{
	/*
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
		auto range_gpu = profiler::BeginRangeGPU("Shadowmap Rendering", cmd);

		const bool predication_request =
			device->CheckCapability(GraphicsDeviceCapability::PREDICATION) &&
			renderer::isOcclusionCullingEnabled;

		const bool shadow_lod_override = renderer::isShadowLODOverride;

		BindCommonResources(cmd);

		BoundingFrustum cam_frustum;
		BoundingFrustum::CreateFromMatrix(cam_frustum, vis.camera->GetProjection());
		std::swap(cam_frustum.Near, cam_frustum.Far);
		cam_frustum.Transform(cam_frustum, vis.camera->GetInvView());
		XMStoreFloat4(&cam_frustum.Orientation, XMQuaternionNormalize(XMLoadFloat4(&cam_frustum.Orientation)));

		CameraCB cb;
		cb.init();

		const XMVECTOR EYE = XMLoadFloat4(&vis.camera->GetWorldEye());

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

		for (uint32_t lightIndex : vis.visibleLights)
		{
			const GLightComponent& light = scene_Gdetails->lightComponents[lightIndex];
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
			case LightComponent::DIRECTIONAL:
			{
				if (maxShadowResolution_2D == 0 && light.forced_shadow_resolution < 0)
					break;
				if (light.cascade_distances.empty())
					break;

				const uint32_t cascade_count = std::min((uint32_t)light.cascade_distances.size(), max_viewport_count);
				Viewport* viewports = (Viewport*)alloca(sizeof(Viewport) * cascade_count);
				Rect* scissors = (Rect*)alloca(sizeof(Rect) * cascade_count);
				SHCAM* shcams = (SHCAM*)alloca(sizeof(SHCAM) * cascade_count);
				CreateDirLightShadowCams(light, *vis.camera, shcams, cascade_count, shadow_rect);

				std::vector<geometrics::AABB>& aabb_renderables = scene_Gdetails->aabbRenderables;
				
				for (size_t i = 0; i < aabb_renderables.size(); ++i)
				{
					const AABB& aabb = aabb_renderables[i];
					if (aabb.layerMask & vis.layerMask)
					{
						const GRenderableComponent& renderable = scene_Gdetails->renderableComponents[i];
						if (renderable.IsRenderable() && renderable.IsCastingShadow())
						{
							const float distanceSq = math::DistanceSquared(EYE, renderable.center);
							if (distanceSq > sqr(renderable.draw_distance + renderable.radius)) // Note: here I use draw_distance instead of fadeDeistance because this doesn't account for impostor switch fade
								continue;

							// Determine which cascades the object is contained in:
							uint8_t camera_mask = 0;
							uint8_t shadow_lod = 0xFF;
							for (uint32_t cascade = 0; cascade < cascade_count; ++cascade)
							{
								if ((cascade < (cascade_count - renderable.cascadeMask)) && shcams[cascade].frustum.CheckBoxFast(aabb))
								{
									camera_mask |= 1 << cascade;
									if (shadow_lod_override)
									{
										const uint8_t candidate_lod = (uint8_t)vis.scene->ComputeObjectLODForView(renderable, aabb, vis.scene->meshes[renderable.mesh_index], shcams[cascade].view_projection);
										shadow_lod = std::min(shadow_lod, candidate_lod);
									}
								}
							}
							if (camera_mask == 0)
								continue;

							RenderBatch batch;
							batch.Create(renderable.mesh_index, uint32_t(i), 0, renderable.sort_bits, camera_mask, shadow_lod);

							const uint32_t filterMask = renderable.GetFilterMask();
							if (filterMask & FILTER_OPAQUE)
							{
								renderQueue.add(batch);
							}
							if ((filterMask & FILTER_TRANSPARENT) || (filterMask & FILTER_WATER))
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
					RenderMeshes(vis, renderQueue, RENDERPASS_SHADOW, FILTER_OPAQUE, cmd, 0, cascade_count);
					RenderMeshes(vis, renderQueue_transparent, RENDERPASS_SHADOW, FILTER_TRANSPARENT | FILTER_WATER, cmd, 0, cascade_count);
				}

				if (!vis.visibleHairs.empty())
				{
					cb.cameras[0].position = vis.camera->Eye;
					for (uint32_t cascade = 0; cascade < std::min(2u, cascade_count); ++cascade)
					{
						XMStoreFloat4x4(&cb.cameras[0].view_projection, shcams[cascade].view_projection);
						device->BindDynamicConstantBuffer(cb, CBSLOT_RENDERER_CAMERA, cmd);

						Viewport vp;
						vp.top_left_x = float(shadow_rect.x + cascade * shadow_rect.w);
						vp.top_left_y = float(shadow_rect.y);
						vp.width = float(shadow_rect.w);
						vp.height = float(shadow_rect.h);
						device->BindViewports(1, &vp, cmd);

						Rect scissor;
						scissor.from_viewport(vp);
						device->BindScissorRects(1, &scissor, cmd);

						for (uint32_t hairIndex : vis.visibleHairs)
						{
							const HairParticleSystem& hair = vis.scene->hairs[hairIndex];
							if (!shcams[cascade].frustum.CheckBoxFast(hair.aabb))
								continue;
							Entity entity = vis.scene->hairs.GetEntity(hairIndex);
							const MaterialComponent* material = vis.scene->materials.GetComponent(entity);
							if (material != nullptr)
							{
								hair.Draw(*material, RENDERPASS_SHADOW, cmd);
							}
						}
					}
				}
			}
			break;
			case LightComponent::SPOT:
			{
				if (max_shadow_resolution_2D == 0 && light.forced_shadow_resolution < 0)
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
						const ObjectComponent& object = vis.scene->objects[i];
						if (object.IsRenderable() && object.IsCastingShadow())
						{
							const float distanceSq = math::DistanceSquared(EYE, object.center);
							if (distanceSq > sqr(object.draw_distance + object.radius)) // Note: here I use draw_distance instead of fadeDeistance because this doesn't account for impostor switch fade
								continue;

							uint8_t shadow_lod = 0xFF;
							if (shadow_lod_override)
							{
								const uint8_t candidate_lod = (uint8_t)vis.scene->ComputeObjectLODForView(object, aabb, vis.scene->meshes[object.mesh_index], shcam.view_projection);
								shadow_lod = std::min(shadow_lod, candidate_lod);
							}

							RenderBatch batch;
							batch.Create(object.mesh_index, uint32_t(i), 0, object.sort_bits, 0xFF, shadow_lod);

							const uint32_t filterMask = object.GetFilterMask();
							if (filterMask & FILTER_OPAQUE)
							{
								renderQueue.add(batch);
							}
							if ((filterMask & FILTER_TRANSPARENT) || (filterMask & FILTER_WATER))
							{
								renderQueue_transparent.add(batch);
							}
						}
					}
				}

				if (predication_request && light.occlusionquery >= 0)
				{
					device->PredicationBegin(
						&vis.scene->queryPredicationBuffer,
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
					RenderMeshes(vis, renderQueue, RENDERPASS_SHADOW, FILTER_OPAQUE, cmd);
					RenderMeshes(vis, renderQueue_transparent, RENDERPASS_SHADOW, FILTER_TRANSPARENT | FILTER_WATER, cmd);
				}

				if (!vis.visibleHairs.empty())
				{
					cb.cameras[0].position = vis.camera->Eye;
					cb.cameras[0].options = SHADERCAMERA_OPTION_NONE;
					XMStoreFloat4x4(&cb.cameras[0].view_projection, shcam.view_projection);
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

					for (uint32_t hairIndex : vis.visibleHairs)
					{
						const HairParticleSystem& hair = vis.scene->hairs[hairIndex];
						if (!shcam.frustum.CheckBoxFast(hair.aabb))
							continue;
						Entity entity = vis.scene->hairs.GetEntity(hairIndex);
						const MaterialComponent* material = vis.scene->materials.GetComponent(entity);
						if (material != nullptr)
						{
							hair.Draw(*material, RENDERPASS_SHADOW, cmd);
						}
					}
				}

				if (predication_request && light.occlusionquery >= 0)
				{
					device->PredicationEnd(cmd);
				}
			}
			break;
			case LightComponent::POINT:
			{
				if (max_shadow_resolution_cube == 0 && light.forced_shadow_resolution < 0)
					break;

				Sphere boundingsphere(light.position, light.GetRange());

				const float zNearP = 0.1f;
				const float zFarP = std::max(1.0f, light.GetRange());
				SHCAM cameras[6];
				CreateCubemapCameras(light.position, zNearP, zFarP, cameras, arraysize(cameras));
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
						const ObjectComponent& object = vis.scene->objects[i];
						if (object.IsRenderable() && object.IsCastingShadow())
						{
							const float distanceSq = math::DistanceSquared(EYE, object.center);
							if (distanceSq > sqr(object.draw_distance + object.radius)) // Note: here I use draw_distance instead of fadeDeistance because this doesn't account for impostor switch fade
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
										const uint8_t candidate_lod = (uint8_t)vis.scene->ComputeObjectLODForView(object, aabb, vis.scene->meshes[object.mesh_index], cameras[camera_index].view_projection);
										shadow_lod = std::min(shadow_lod, candidate_lod);
									}
								}
							}
							if (camera_mask == 0)
								continue;

							RenderBatch batch;
							batch.Create(object.mesh_index, uint32_t(i), 0, object.sort_bits, camera_mask, shadow_lod);

							const uint32_t filterMask = object.GetFilterMask();
							if (filterMask & FILTER_OPAQUE)
							{
								renderQueue.add(batch);
							}
							if ((filterMask & FILTER_TRANSPARENT) || (filterMask & FILTER_WATER))
							{
								renderQueue_transparent.add(batch);
							}
						}
					}
				}

				if (predication_request && light.occlusionquery >= 0)
				{
					device->PredicationBegin(
						&vis.scene->queryPredicationBuffer,
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
					RenderMeshes(vis, renderQueue, RENDERPASS_SHADOW, FILTER_OPAQUE, cmd, 0, camera_count);
					RenderMeshes(vis, renderQueue_transparent, RENDERPASS_SHADOW, FILTER_TRANSPARENT | FILTER_WATER, cmd, 0, camera_count);
				}

				if (!vis.visibleHairs.empty())
				{
					cb.cameras[0].position = vis.camera->Eye;
					for (uint32_t shcam = 0; shcam < arraysize(cameras); ++shcam)
					{
						XMStoreFloat4x4(&cb.cameras[0].view_projection, cameras[shcam].view_projection);
						device->BindDynamicConstantBuffer(cb, CBSLOT_RENDERER_CAMERA, cmd);

						Viewport vp;
						vp.top_left_x = float(shadow_rect.x + shcam * shadow_rect.w);
						vp.top_left_y = float(shadow_rect.y);
						vp.width = float(shadow_rect.w);
						vp.height = float(shadow_rect.h);
						device->BindViewports(1, &vp, cmd);

						Rect scissor;
						scissor.from_viewport(vp);
						device->BindScissorRects(1, &scissor, cmd);

						for (uint32_t hairIndex : vis.visibleHairs)
						{
							const HairParticleSystem& hair = vis.scene->hairs[hairIndex];
							if (!cameras[shcam].frustum.CheckBoxFast(hair.aabb))
								continue;
							Entity entity = vis.scene->hairs.GetEntity(hairIndex);
							const MaterialComponent* material = vis.scene->materials.GetComponent(entity);
							if (material != nullptr)
							{
								hair.Draw(*material, RENDERPASS_SHADOW, cmd);
							}
						}
					}
				}

				if (predication_request && light.occlusionquery >= 0)
				{
					device->PredicationEnd(cmd);
				}

			}
			break;
			} // terminate switch
		}

		// Rain blocker:
		if (vis.scene->weather.rain_amount > 0)
		{
			SHCAM shcam;
			CreateDirLightShadowCams(vis.scene->rain_blocker_dummy_light, *vis.camera, &shcam, 1, vis.rain_blocker_shadow_rect);

			renderQueue.init();
			for (size_t i = 0; i < aabb_renderables.size(); ++i)
			{
				const AABB& aabb = aabb_renderables[i];
				if (aabb.layerMask & vis.layerMask)
				{
					const ObjectComponent& object = vis.scene->objects[i];
					if (object.IsRenderable())
					{
						uint8_t camera_mask = 0;
						if (shcam.frustum.CheckBoxFast(aabb))
						{
							camera_mask |= 1 << 0;
						}
						if (camera_mask == 0)
							continue;

						renderQueue.add(object.mesh_index, uint32_t(i), 0, object.sort_bits, camera_mask);
					}
				}
			}

			if (!renderQueue.empty())
			{
				device->EventBegin("Rain Blocker", cmd);
				const uint cascade = 0;
				XMStoreFloat4x4(&cb.cameras[cascade].view_projection, shcam.view_projection);
				cb.cameras[cascade].output_index = cascade;
				for (int i = 0; i < arraysize(cb.cameras[cascade].frustum.planes); ++i)
				{
					cb.cameras[cascade].frustum.planes[i] = shcam.frustum.planes[i];
				}

				Viewport vp;
				vp.top_left_x = float(vis.rain_blocker_shadow_rect.x + cascade * vis.rain_blocker_shadow_rect.w);
				vp.top_left_y = float(vis.rain_blocker_shadow_rect.y);
				vp.width = float(vis.rain_blocker_shadow_rect.w);
				vp.height = float(vis.rain_blocker_shadow_rect.h);

				device->BindDynamicConstantBuffer(cb, CBSLOT_RENDERER_CAMERA, cmd);
				device->BindViewports(1, &vp, cmd);

				Rect scissor;
				scissor.from_viewport(vp);
				device->BindScissorRects(1, &scissor, cmd);

				renderQueue.sort_opaque();
				RenderMeshes(vis, renderQueue, RENDERPASS_RAINBLOCKER, FILTER_OBJECT_ALL, cmd, 0, 1);
				device->EventEnd(cmd);
			}
		}

		device->RenderPassEnd(cmd);

		profiler::EndRange(range_gpu);
		profiler::EndRange(range_cpu);
		device->EventEnd(cmd);
	}
	/**/
}
