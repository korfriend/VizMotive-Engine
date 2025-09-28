#include "RenderPath3D_Detail.h"

#include "../Shaders/CommonHF/uvsphere.hlsli"
#include "../Shaders/CommonHF/cone.hlsli"

static const uint32_t vertexCount_uvsphere = arraysize(UVSPHERE);
static const uint32_t vertexCount_cone = arraysize(CONE);

namespace vz::renderer
{
	void GRenderPath3DDetails::DrawLightVisualizers(
		const Visibility& vis,
		CommandList cmd
	)
	{
		if (!vis.visibleLights.empty())
		{
			device->EventBegin("Light Visualizer Render", cmd);

			BindCommonResources(cmd);

			XMMATRIX camrot = XMLoadFloat3x3(&vis.camera->GetRotationToFaceCamera());

			for (uint type = SCU32(LightComponent::LightType::POINT); type < SCU32(LightComponent::LightType::COUNT); ++type)
			{
				device->BindPipelineState(&PSO_lightvisualizer[type], cmd);

				for (uint32_t lightIndex : vis.visibleLights)
				{
					const GLightComponent& light = *scene_Gdetails->lightComponents[lightIndex];

					float outer_cone_angle = light.GetOuterConeAngle();
					float inner_cone_angle = light.GetInnerConeAngle();
					if (SCU32(light.GetType()) == type && light.IsVisualizerEnabled())
					{
						VolumeLightCB lcb;
						const XMFLOAT3 light_color = light.GetColor();
						const float light_intensity = light.GetIntensity();
						const float light_range = light.GetRange();
						const float light_radius = light.GetRadius();
						const float light_length = light.GetLength();

						lcb.xLightColor = XMFLOAT4(light_color.x, light_color.y, light_color.z, 1.f);
						lcb.xLightEnerdis = XMFLOAT4(light_intensity, light_range, outer_cone_angle, light_intensity);

						if (type == SCU32(LightComponent::LightType::POINT))
						{
							const float sphere_volume = std::max(1.0f, math::SphereVolume(light_radius));
							lcb.xLightColor.x *= light_intensity / sphere_volume;
							lcb.xLightColor.y *= light_intensity / sphere_volume;
							lcb.xLightColor.z *= light_intensity / sphere_volume;
							lcb.xLightEnerdis.w = light_range * 0.025f; // scale
							XMStoreFloat4x4(&lcb.xLightWorld,
								XMMatrixScaling(lcb.xLightEnerdis.w, lcb.xLightEnerdis.w, lcb.xLightEnerdis.w) *
								camrot *
								XMMatrixTranslationFromVector(XMLoadFloat3(&light.position))
							);
							device->BindDynamicConstantBuffer(lcb, CB_GETBINDSLOT(VolumeLightCB), cmd);

							uint32_t vertexCount = vertexCount_uvsphere;
							uint32_t segmentCount_cylinder = 32;
							if (light_length > 0)
							{
								vertexCount += segmentCount_cylinder * 2 * 3;
							}
							GraphicsDevice::GPUAllocation allocation = device->AllocateGPU(vertexCount * sizeof(float4), cmd);
							float4* dst = (float4*)allocation.data;
							float rad = std::max(0.025f, light_radius);
							if (light_length > 0)
							{
								// Capsule from two half spheres and an uncapped cylinder:
								XMMATRIX M =
									XMMatrixScaling(rad, rad, rad) *
									XMMatrixTranslation(-light_length * 0.5f, 0, 0) *
									XMMatrixRotationQuaternion(XMLoadFloat4(&light.rotation)) *
									XMMatrixTranslation(light.position.x, light.position.y, light.position.z)
									;
								for (uint32_t i = 0; i < vertexCount_uvsphere; i += 3)
								{
									if (UVSPHERE[i].x <= 0.01f && UVSPHERE[i + 1].x <= 0.01f && UVSPHERE[i + 2].x <= 0.01f)
									{
										XMVECTOR pos = XMLoadFloat4(&UVSPHERE[i]);
										pos = XMVector3Transform(pos, M);
										XMStoreFloat4(dst, pos);
										dst++;
										pos = XMLoadFloat4(&UVSPHERE[i + 1]);
										pos = XMVector3Transform(pos, M);
										XMStoreFloat4(dst, pos);
										dst++;
										pos = XMLoadFloat4(&UVSPHERE[i + 2]);
										pos = XMVector3Transform(pos, M);
										XMStoreFloat4(dst, pos);
										dst++;
									}
								}
								M =
									XMMatrixScaling(rad, rad, rad) *
									XMMatrixTranslation(light_length * 0.5f, 0, 0) *
									XMMatrixRotationQuaternion(XMLoadFloat4(&light.rotation)) *
									XMMatrixTranslation(light.position.x, light.position.y, light.position.z)
									;
								for (uint32_t i = 0; i < vertexCount_uvsphere; i += 3)
								{
									if (UVSPHERE[i].x >= -0.01f && UVSPHERE[i + 1].x >= -0.01f && UVSPHERE[i + 2].x >= -0.01f)
									{
										XMVECTOR pos = XMLoadFloat4(&UVSPHERE[i]);
										pos = XMVector3Transform(pos, M);
										XMStoreFloat4(dst, pos);
										dst++;
										pos = XMLoadFloat4(&UVSPHERE[i + 1]);
										pos = XMVector3Transform(pos, M);
										XMStoreFloat4(dst, pos);
										dst++;
										pos = XMLoadFloat4(&UVSPHERE[i + 2]);
										pos = XMVector3Transform(pos, M);
										XMStoreFloat4(dst, pos);
										dst++;
									}
								}
								M =
									XMMatrixScaling(light_length * 0.5f, rad, rad) *
									XMMatrixRotationQuaternion(XMLoadFloat4(&light.rotation)) *
									XMMatrixTranslation(light.position.x, light.position.y, light.position.z)
									;
								for (uint32_t i = 0; i < segmentCount_cylinder; ++i)
								{
									float t1 = float(i) / segmentCount_cylinder * XM_2PI;
									float t2 = float(i + 1) / segmentCount_cylinder * XM_2PI;
									XMVECTOR pos = XMVectorSet(-1, std::sin(t1), std::cos(t1), 1);
									pos = XMVector3Transform(pos, M);
									XMStoreFloat4(dst, pos);
									dst++;
									pos = XMVectorSet(1, std::sin(t2), std::cos(t2), 1);
									pos = XMVector3Transform(pos, M);
									XMStoreFloat4(dst, pos);
									dst++;
									pos = XMVectorSet(1, std::sin(t1), std::cos(t1), 1);
									pos = XMVector3Transform(pos, M);
									XMStoreFloat4(dst, pos);
									dst++;
									pos = XMVectorSet(-1, std::sin(t1), std::cos(t1), 1);
									pos = XMVector3Transform(pos, M);
									XMStoreFloat4(dst, pos);
									dst++;
									pos = XMVectorSet(-1, std::sin(t2), std::cos(t2), 1);
									pos = XMVector3Transform(pos, M);
									XMStoreFloat4(dst, pos);
									dst++;
									pos = XMVectorSet(1, std::sin(t2), std::cos(t2), 1);
									pos = XMVector3Transform(pos, M);
									XMStoreFloat4(dst, pos);
									dst++;
								}
							}
							else
							{
								// Sphere:
								XMMATRIX M =
									XMMatrixScaling(rad, rad, rad) *
									XMMatrixTranslation(-light_length * 0.5f, 0, 0) *
									XMMatrixRotationQuaternion(XMLoadFloat4(&light.rotation)) *
									XMMatrixTranslation(light.position.x, light.position.y, light.position.z)
									;
								for (uint32_t i = 0; i < vertexCount_uvsphere; ++i)
								{
									XMVECTOR pos = XMLoadFloat4(&UVSPHERE[i]);
									pos = XMVector3Transform(pos, M);
									XMStoreFloat4(dst, pos);
									dst++;
								}
							}
							const GPUBuffer* vbs[] = {
								&allocation.buffer,
							};
							const uint32_t strides[] = {
								sizeof(float4),
							};
							const uint64_t offsets[] = {
								allocation.offset,
							};
							device->BindVertexBuffers(vbs, 0, arraysize(vbs), strides, offsets, cmd);

							device->Draw(vertexCount, 0, cmd);
						}
						else if (type == SCU32(LightComponent::LightType::SPOT))
						{
							const XMMATRIX CONE_ROT = XMMatrixRotationX(XM_PIDIV2) * XMMatrixRotationQuaternion(XMLoadFloat4(&light.rotation));
							const float SPOT_LIGHT_RANGE_SCALE = 0.15f;
							if (inner_cone_angle > 0)
							{
								float coneS = (float)(std::min(inner_cone_angle, outer_cone_angle) * 2 / XM_PIDIV4);
								lcb.xLightEnerdis.w = light.GetRange() * SPOT_LIGHT_RANGE_SCALE; // scale
								XMStoreFloat4x4(&lcb.xLightWorld,
									XMMatrixScaling(coneS * lcb.xLightEnerdis.w, lcb.xLightEnerdis.w, coneS * lcb.xLightEnerdis.w) *
									CONE_ROT *
									XMMatrixTranslationFromVector(XMLoadFloat3(&light.position))
								);

								device->BindDynamicConstantBuffer(lcb, CB_GETBINDSLOT(VolumeLightCB), cmd);

								device->Draw(vertexCount_cone, 0, cmd);
							}

							float coneS = (float)(outer_cone_angle * 2 / XM_PIDIV4);
							lcb.xLightEnerdis.w = light.GetRange() * SPOT_LIGHT_RANGE_SCALE; // scale
							XMStoreFloat4x4(&lcb.xLightWorld,
								XMMatrixScaling(coneS * lcb.xLightEnerdis.w, lcb.xLightEnerdis.w, coneS * lcb.xLightEnerdis.w) *
								CONE_ROT *
								XMMatrixTranslationFromVector(XMLoadFloat3(&light.position))
							);

							device->BindDynamicConstantBuffer(lcb, CB_GETBINDSLOT(VolumeLightCB), cmd);

							device->Draw(vertexCount_cone, 0, cmd);
						}
					}
				}

			}

			device->EventEnd(cmd);

		}
	}

	void GRenderPath3DDetails::DrawDebugWorld(
		const Scene& scene,
		const CameraComponent& camera,
		CommandList cmd
	)
	{
		if (!wirecubeVB.IsValid())
		{
			XMFLOAT4 min = XMFLOAT4(-1, -1, -1, 1);
			XMFLOAT4 max = XMFLOAT4(1, 1, 1, 1);

			XMFLOAT4 verts[] = {
				min,							XMFLOAT4(1,1,1,1),
				XMFLOAT4(min.x,max.y,min.z,1),	XMFLOAT4(1,1,1,1),
				XMFLOAT4(min.x,max.y,max.z,1),	XMFLOAT4(1,1,1,1),
				XMFLOAT4(min.x,min.y,max.z,1),	XMFLOAT4(1,1,1,1),
				XMFLOAT4(max.x,min.y,min.z,1),	XMFLOAT4(1,1,1,1),
				XMFLOAT4(max.x,max.y,min.z,1),	XMFLOAT4(1,1,1,1),
				max,							XMFLOAT4(1,1,1,1),
				XMFLOAT4(max.x,min.y,max.z,1),	XMFLOAT4(1,1,1,1),
			};

			GPUBufferDesc bd;
			bd.usage = Usage::DEFAULT;
			bd.size = sizeof(verts);
			bd.bind_flags = BindFlag::VERTEX_BUFFER;
			device->CreateBuffer(&bd, verts, &wirecubeVB);

			uint16_t indices[] = {
				0,1,1,2,0,3,0,4,1,5,4,5,
				5,6,4,7,2,6,3,7,2,3,6,7
			};

			bd.usage = Usage::DEFAULT;
			bd.size = sizeof(indices);
			bd.bind_flags = BindFlag::INDEX_BUFFER;
			device->CreateBuffer(&bd, indices, &wirecubeIB);
		}

		device->EventBegin("DrawDebugWorld", cmd);

		BindCommonResources(cmd);

		if (options.debugCameras)
		{
			device->EventBegin("Debug Cameras", cmd);

			if (!wirecamVB.IsValid())
			{
				XMFLOAT4 verts[] = {
					XMFLOAT4(-0.1f,-0.1f,-1,1),	XMFLOAT4(1,1,1,1),
					XMFLOAT4(0.1f,-0.1f,-1,1),	XMFLOAT4(1,1,1,1),
					XMFLOAT4(0.1f,0.1f,-1,1),	XMFLOAT4(1,1,1,1),
					XMFLOAT4(-0.1f,0.1f,-1,1),	XMFLOAT4(1,1,1,1),
					XMFLOAT4(-1,-1,1,1),	XMFLOAT4(1,1,1,1),
					XMFLOAT4(1,-1,1,1),	XMFLOAT4(1,1,1,1),
					XMFLOAT4(1,1,1,1),	XMFLOAT4(1,1,1,1),
					XMFLOAT4(-1,1,1,1),	XMFLOAT4(1,1,1,1),
					XMFLOAT4(0,1.5f,1,1),	XMFLOAT4(1,1,1,1),
					XMFLOAT4(0,0,-1,1),	XMFLOAT4(1,1,1,1),
				};

				GPUBufferDesc bd;
				bd.usage = Usage::DEFAULT;
				bd.size = sizeof(verts);
				bd.bind_flags = BindFlag::VERTEX_BUFFER;
				device->CreateBuffer(&bd, verts, &wirecamVB);

				uint16_t indices[] = {
					0,1,1,2,0,3,0,4,1,5,4,5,
					5,6,4,7,2,6,3,7,2,3,6,7,
					6,8,7,8,
					0,2,1,3
				};

				bd.usage = Usage::DEFAULT;
				bd.size = sizeof(indices);
				bd.bind_flags = BindFlag::INDEX_BUFFER;
				device->CreateBuffer(&bd, indices, &wirecamIB);
			}

			device->BindPipelineState(&PSO_RenderableShapes[DEBUG_RENDERING_CUBE_DEPTH], cmd);

			const GPUBuffer* vbs[] = {
				&wirecamVB,
			};
			const uint32_t strides[] = {
				sizeof(XMFLOAT4) + sizeof(XMFLOAT4),
			};
			device->BindVertexBuffers(vbs, 0, arraysize(vbs), strides, nullptr, cmd);
			device->BindIndexBuffer(&wirecamIB, IndexBufferFormat::UINT16, 0, cmd);

			MiscCB sb;
			sb.g_xColor = XMFLOAT4(1, 1, 1, 1);
			
			const std::vector<Entity>& cameras = scene.GetCameraEntities();
			for (size_t i = 0; i < cameras.size(); ++i)
			{
				const CameraComponent& cam = cameras[i];

				float w, h;
				cam.GetWidthHeight(&w, &h);
				const float aspect = w / h;
				XMMATRIX V_INV = XMLoadFloat4x4(&cam.GetInvView());
				XMMATRIX VP = XMLoadFloat4x4(&cam.GetViewProjection());
				XMStoreFloat4x4(&sb.g_xTransform, XMMatrixScaling(aspect * 0.5f, 0.5f, 0.5f) * V_INV * VP);

				device->BindDynamicConstantBuffer(sb, CB_GETBINDSLOT(MiscCB), cmd);

				device->DrawIndexed(32, 0, 0, cmd);

				float aperture_size = cam.GetApertureSize();
				XMFLOAT2 aperture_shape = cam.GetApertureShape();

				if (aperture_size > 0)
				{
					// focal length line:
					RenderableLine line;
					line.color_start = line.color_end = XMFLOAT4(1, 1, 1, 1);
					line.start = cam.GetWorldEye();
					XMVECTOR DIR = XMLoadFloat3(&cam.GetWorldForward());
					XMVECTOR EYE = XMLoadFloat3(&line.start);
					XMVECTOR L = EYE + DIR * cam.GetFocalLength();
					XMStoreFloat3(&line.end, L);

					renderPathDebugShapes.AddDrawLine(line, false);

					// aperture size/shape circle:
					int segmentcount = 36;
					for (int j = 0; j < segmentcount; ++j)
					{
						const float angle0 = float(j) / float(segmentcount) * XM_2PI;
						const float angle1 = float(j + 1) / float(segmentcount) * XM_2PI;
						line.start = XMFLOAT3(std::sin(angle0), std::cos(angle0), 0);
						line.end = XMFLOAT3(std::sin(angle1), std::cos(angle1), 0);
						XMVECTOR S = XMLoadFloat3(&line.start);
						XMVECTOR E = XMLoadFloat3(&line.end); 
						XMMATRIX R = XMLoadFloat3x3(&cam.GetRotationToFaceCamera());
						XMMATRIX APERTURE = R * XMMatrixScaling(aperture_size * aperture_shape.x, aperture_size * aperture_shape.y, aperture_size);
						S = XMVector3TransformNormal(S, APERTURE);
						E = XMVector3TransformNormal(E, APERTURE);
						S += L;
						E += L;
						XMStoreFloat3(&line.start, S);
						XMStoreFloat3(&line.end, E);

						renderPathDebugShapes.AddDrawLine(line, false);
					}
				}
			}

			device->EventEnd(cmd);
		}

		// draw basic debug shapes
		{
			scene_Gdetails->debugShapes.DrawLines(camera, cmd, false);
			renderPathDebugShapes.DrawLines(camera, cmd, false);
			// add... cube, capsule, sphere, point, ... TODO
		}

		if (options.debugEnvProbes)
		{
			device->EventBegin("Debug EnvProbes", cmd);
			// Envmap spheres:

			device->BindPipelineState(&PSO_RenderableShapes[DEBUG_RENDERING_ENVPROBE], cmd);

			const std::vector<GProbeComponent*>& probes = scene.GetProbeComponents();

			MiscCB sb;
			for (size_t i = 0; i < probes.size(); ++i)
			{
				const GProbeComponent& probe = *probes[i];

				XMStoreFloat4x4(&sb.g_xTransform, XMMatrixTranslationFromVector(XMLoadFloat3(&probe.position)));
				device->BindDynamicConstantBuffer(sb, CB_GETBINDSLOT(MiscCB), cmd);

				device->BindResource(&probe.texture, 0, cmd);

				device->Draw(vertexCount_uvsphere, 0, cmd);
			}


			// Local proxy boxes:

			device->BindPipelineState(&PSO_RenderableShapes[DEBUG_RENDERING_CUBE_DEPTH], cmd);

			const GPUBuffer* vbs[] = {
				&wirecubeVB,
			};
			const uint32_t strides[] = {
				sizeof(XMFLOAT4) + sizeof(XMFLOAT4),
			};
			device->BindVertexBuffers(vbs, 0, arraysize(vbs), strides, nullptr, cmd);
			device->BindIndexBuffer(&wirecubeIB, IndexBufferFormat::UINT16, 0, cmd);

			XMMATRIX VP = XMLoadFloat4x4(&camera.GetViewProjection());

			for (size_t i = 0; i < probes.size(); ++i)
			{
				const GProbeComponent& probe = *probes[i];
				Entity entity = probe.GetEntity();
				const TransformComponent* transform = compfactory::GetTransformComponent(entity);
				assert(transform);

				XMStoreFloat4x4(&sb.g_xTransform, XMLoadFloat4x4(&transform->GetWorldMatrix()) * VP);
				sb.g_xColor = float4(0, 1, 1, 1);

				device->BindDynamicConstantBuffer(sb, CB_GETBINDSLOT(MiscCB), cmd);

				device->DrawIndexed(24, 0, 0, cmd);
			}

			device->EventEnd(cmd);
		}
		
		if (options.debugDDGI && scene_Gdetails->ddgi.probeBuffer.IsValid())
		{
			device->EventBegin("Debug DDGI", cmd);

			device->BindPipelineState(&PSO_RenderableShapes[DEBUG_RENDERING_DDGI], cmd);
			device->DrawInstanced(2880, scene_Gdetails->shaderscene.ddgi.probe_count, 0, 0, cmd); // uv-sphere

			device->EventEnd(cmd);
		}

		if (options.debugGridHelpers)
		{
			device->EventBegin("GridHelper", cmd);

			device->BindPipelineState(&PSO_RenderableShapes[DEBUG_RENDERING_GRID], cmd);

			static float alpha = 0.75f;
			const float channel_min = 0.2f;
			static uint32_t gridVertexCount = 0;

			if (!gridVB.IsValid())
			{
				const float h = 0.01f; // avoid z-fight with zero plane
				const int a = 20;
				XMFLOAT4 verts[((a + 1) * 2 + (a + 1) * 2) * 2];

				int count = 0;
				for (int i = 0; i <= a; ++i)
				{
					verts[count++] = XMFLOAT4(i - a * 0.5f, h, -a * 0.5f, 1);
					verts[count++] = (i == a / 2 ? XMFLOAT4(channel_min, channel_min, 1, alpha) : XMFLOAT4(1, 1, 1, alpha));

					verts[count++] = XMFLOAT4(i - a * 0.5f, h, +a * 0.5f, 1);
					verts[count++] = (i == a / 2 ? XMFLOAT4(channel_min, channel_min, 1, alpha) : XMFLOAT4(1, 1, 1, alpha));
				}
				for (int j = 0; j <= a; ++j)
				{
					verts[count++] = XMFLOAT4(-a * 0.5f, h, j - a * 0.5f, 1);
					verts[count++] = (j == a / 2 ? XMFLOAT4(1, channel_min, channel_min, alpha) : XMFLOAT4(1, 1, 1, alpha));

					verts[count++] = XMFLOAT4(+a * 0.5f, h, j - a * 0.5f, 1);
					verts[count++] = (j == a / 2 ? XMFLOAT4(1, channel_min, channel_min, alpha) : XMFLOAT4(1, 1, 1, alpha));
				}

				gridVertexCount = arraysize(verts) / 2;

				GPUBufferDesc bd;
				bd.size = sizeof(verts);
				bd.bind_flags = BindFlag::VERTEX_BUFFER;
				device->CreateBuffer(&bd, verts, &gridVB);
			}

			MiscCB sb;
			sb.g_xTransform = camera.GetViewProjection();
			sb.g_xColor = float4(1, 1, 1, 1);

			device->BindDynamicConstantBuffer(sb, CB_GETBINDSLOT(MiscCB), cmd);

			const GPUBuffer* vbs[] = {
				&gridVB,
			};
			const uint32_t strides[] = {
				sizeof(XMFLOAT4) + sizeof(XMFLOAT4),
			};
			device->BindVertexBuffers(vbs, 0, arraysize(vbs), strides, nullptr, cmd);
			device->Draw(gridVertexCount, 0, cmd);

			device->EventEnd(cmd);
		}

		//if (options.debugVXGI && scene.vxgi.radiance.IsValid())
		//{
		//	device->EventBegin("Debug Voxels", cmd);
		//
		//	device->BindPipelineState(&PSO_RenderableShapes[DEBUG_RENDERING_VOXEL], cmd);
		//
		//	MiscCB sb;
		//	sb.g_xTransform = camera.GetViewProjection();
		//	sb.g_xColor = float4((float)VXGI_DEBUG_CLIPMAP, 1, 1, 1);
		//
		//	device->BindDynamicConstantBuffer(sb, CB_GETBINDSLOT(MiscCB), cmd);
		//
		//	uint32_t vertexCount = scene.vxgi.res * scene.vxgi.res * scene.vxgi.res;
		//	if (VXGI_DEBUG_CLIPMAP == VXGI_CLIPMAP_COUNT)
		//	{
		//		vertexCount *= VXGI_CLIPMAP_COUNT;
		//	}
		//	device->Draw(vertexCount, 0, cmd);
		//
		//	device->EventEnd(cmd);
		//}

		//if (options.debugRT_BVH)
		//{
		//	RayTraceSceneBVH(scene, cmd);
		//}

		if (!options.debugTextStorage)
		{
			scene_Gdetails->debugShapes.DrawDebugTextStorage(camera, cmd, false);
			renderPathDebugShapes.DrawDebugTextStorage(camera, cmd, true);
		}

		device->EventEnd(cmd);
	}
}