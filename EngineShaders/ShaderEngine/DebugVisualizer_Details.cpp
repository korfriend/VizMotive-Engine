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
		static GPUBuffer wirecubeVB;
		static GPUBuffer wirecubeIB;
	}
}