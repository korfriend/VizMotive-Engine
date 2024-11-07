#include "Renderer.h"

#include "Utils/Timer.h"
#include "Utils/Backlog.h"

namespace vz
{
	using GPrimBuffers = GGeometryComponent::GPrimBuffers;
	using GPrimEffectBuffers = GRenderableComponent::GPrimEffectBuffers;
	using Primitive = GeometryComponent::Primitive;

	const uint32_t SMALL_SUBTASK_GROUPSIZE = 64u;

	GScene* NewGScene(Scene* scene)
	{
		return new GSceneDetails(scene);
	}

	void GSceneDetails::RunPrimtiveUpdateSystem(jobsystem::context& ctx)
	{
		jobsystem::Dispatch(ctx, (uint32_t)geometryComponents.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			GGeometryComponent& geometry = *geometryComponents[args.jobIndex];

			const std::vector<Primitive>& primitives = geometry.GetPrimitives();

			float tessealation_factor = geometry.GetTessellationFactor();
			for (size_t part_index = 0, n = primitives.size(); part_index < n; ++part_index)
			{
				GPrimBuffers* prim_buffer_ptr = geometry.GetGBuffer(part_index);
				if (!prim_buffer_ptr)
					continue;

				GPrimBuffers& prim_buffer = *prim_buffer_ptr;
				const Primitive& primitive = primitives[part_index];

				if (prim_buffer.soPosW.IsValid() && prim_buffer.soPre.IsValid())
				{
					std::swap(prim_buffer.soPosW, prim_buffer.soPre);
				}

				if (geometryArrayMapped != nullptr)
				{
					ShaderGeometry shader_geometry_part;
					shader_geometry_part.Init();
					shader_geometry_part.ib = prim_buffer.ib.descriptor_srv;
					if (prim_buffer.soPosW.IsValid())
					{
						shader_geometry_part.vb_pos_w = prim_buffer.soPosW.descriptor_srv;
					}
					else
					{
						shader_geometry_part.vb_pos_w = prim_buffer.vbPosW.descriptor_srv;
					}
					if (prim_buffer.soNormal.IsValid())
					{
						shader_geometry_part.vb_nor = prim_buffer.soNormal.descriptor_srv;
					}
					else
					{
						shader_geometry_part.vb_nor = prim_buffer.vbNormal.descriptor_srv;
					}
					if (prim_buffer.soTangent.IsValid())
					{
						shader_geometry_part.vb_tan = prim_buffer.soTangent.descriptor_srv;
					}
					else
					{
						shader_geometry_part.vb_tan = prim_buffer.vbTangent.descriptor_srv;
					}
					shader_geometry_part.vb_col = prim_buffer.vbColor.descriptor_srv;
					shader_geometry_part.vb_uvs = prim_buffer.vbUVs.descriptor_srv;
					shader_geometry_part.vb_pre = prim_buffer.soPre.descriptor_srv;
					shader_geometry_part.aabb_min = primitive.GetAABB()._min;
					shader_geometry_part.aabb_max = primitive.GetAABB()._max;
					shader_geometry_part.uv_range_min = primitive.GetUVRangeMin();
					shader_geometry_part.uv_range_max = primitive.GetUVRangeMax();
					shader_geometry_part.tessellation_factor = tessealation_factor;

					//shader_geometry.meshletCount += subsetGeometry.meshletCount;
					std::memcpy(geometryArrayMapped + geometry.geometryOffset + part_index, &shader_geometry_part, sizeof(shader_geometry_part));

				}
			}

			});
	}
	void GSceneDetails::RunMaterialUpdateSystem(jobsystem::context& ctx)
	{
		isWetmapProcessingRequired = false;

		jobsystem::Dispatch(ctx, (uint32_t)materialComponents.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			GMaterialComponent& material = *materialComponents[args.jobIndex];

			if (material.IsOutlineEnabled())
			{
				//material.engineStencilRef = STENCILREF_OUTLINE;
			}

			if (material.IsWetmapEnabled())
			{
				isWetmapProcessingRequired = true;
			}

			if (material.IsDirty())
			{
				material.SetDirty(false);
			}

			auto writeShaderMaterial = [&](ShaderMaterial* dest)
				{
					using namespace vz::math;

					ShaderMaterial shader_material;
					shader_material.Init();
					shader_material.baseColor = pack_half4(material.GetBaseColor());
					XMFLOAT4 emissive_color = material.GetEmissiveColor();
					XMFLOAT4 specular_color = material.GetSpecularColor();
					XMFLOAT4 tex_mul_add = material.GetTexMulAdd();

					shader_material.emissive_cloak = pack_half4(XMFLOAT4(emissive_color.x * emissive_color.w, emissive_color.y * emissive_color.w, emissive_color.z * emissive_color.w, 0));
					shader_material.specular_chromatic = pack_half4(XMFLOAT4(specular_color.x * specular_color.w, specular_color.y * specular_color.w, specular_color.z * specular_color.w, 0));
					shader_material.texMulAdd = tex_mul_add;
					const float alphaRef = material.GetAlphaRef();

					// will add the material feature..
					const float roughness = 0.f;
					const float reflectance = 0.f;
					const float metalness = 0.f;
					const float refraction = 0.f;
					const float normalMapStrength = 0.f;
					const float parallaxOcclusionMapping = 0.f;
					const float displacementMapping = 0.f;
					const XMFLOAT4 subsurfaceScattering = XMFLOAT4(1, 1, 1, 0);
					const XMFLOAT4 sheenColor = XMFLOAT4(1, 1, 1, 1);
					const float transmission = 0.f;
					const float sheenRoughness = 0.f;
					const float clearcoat = 0.f;
					const float clearcoatRoughness = 0.f;

					shader_material.roughness_reflectance_metalness_refraction = pack_half4(roughness, reflectance, metalness, refraction);
					shader_material.normalmap_pom_alphatest_displacement = pack_half4(normalMapStrength, parallaxOcclusionMapping, 1 - alphaRef, displacementMapping);
					XMFLOAT4 sss = subsurfaceScattering;
					sss.x *= sss.w;
					sss.y *= sss.w;
					sss.z *= sss.w;
					XMFLOAT4 sss_inv = XMFLOAT4(
						sss_inv.x = 1.0f / ((1 + sss.x) * (1 + sss.x)),
						sss_inv.y = 1.0f / ((1 + sss.y) * (1 + sss.y)),
						sss_inv.z = 1.0f / ((1 + sss.z) * (1 + sss.z)),
						sss_inv.w = 1.0f / ((1 + sss.w) * (1 + sss.w))
					);
					shader_material.subsurfaceScattering = pack_half4(sss);
					shader_material.subsurfaceScattering_inv = pack_half4(sss_inv);

					shader_material.sheenColor = pack_half3(XMFLOAT3(sheenColor.x, sheenColor.y, sheenColor.z));
					shader_material.transmission_sheenroughness_clearcoat_clearcoatroughness = pack_half4(transmission, sheenRoughness, clearcoat, clearcoatRoughness);
					float _anisotropy_strength = 0;
					float _anisotropy_rotation_sin = 0;
					float _anisotropy_rotation_cos = 0;
					float _blend_with_terrain_height_rcp = 0;
					//if (shaderType == SHADERTYPE_PBR_ANISOTROPIC)
					//{
					//	_anisotropy_strength = clamp(anisotropy_strength, 0.0f, 0.99f);
					//	_anisotropy_rotation_sin = std::sin(anisotropy_rotation);
					//	_anisotropy_rotation_cos = std::cos(anisotropy_rotation);
					//}
					shader_material.aniso_anisosin_anisocos_terrainblend = pack_half4(_anisotropy_strength, _anisotropy_rotation_sin, _anisotropy_rotation_cos, _blend_with_terrain_height_rcp);
					shader_material.shaderType = (uint)material.GetShaderType();
					shader_material.userdata = uint4(0, 0, 0, 0);

					shader_material.options_stencilref = 0;

					for (int i = 0; i < TEXTURESLOT_COUNT; ++i)
					{
						VUID texture_vuid = material.GetTextureVUID(static_cast<MaterialComponent::TextureSlot>(i));
						GTextureComponent* texture_comp = nullptr;
						if (texture_vuid != INVALID_VUID)
						{
							texture_comp = (GTextureComponent*)compfactory::GetComponentByVUID(texture_vuid);
							assert(texture_comp && texture_comp->GetComponentType() == ComponentType::TEXTURE);
							shader_material.textures[i].uvset_lodclamp = (texture_comp->GetUVSet() & 1) | (XMConvertFloatToHalf(texture_comp->GetLodClamp()) << 1u);
							if (texture_comp->IsValid())
							{
								int subresource = -1;
								switch (i)
								{
								case BASECOLORMAP:
									//case EMISSIVEMAP:
									//case SPECULARMAP:
									//case SHEENCOLORMAP:
									subresource = texture_comp->GetTextureSRGBSubresource();
									break;
								default:
									break;
								}
								shader_material.textures[i].texture_descriptor = device->GetDescriptorIndex(texture_comp->GetGPUResource(), SubresourceType::SRV, subresource);
							}
							else
							{
								shader_material.textures[i].texture_descriptor = -1;
							}
							shader_material.textures[i].sparse_residencymap_descriptor = texture_comp->GetSparseResidencymapDescriptor();
							shader_material.textures[i].sparse_feedbackmap_descriptor = texture_comp->GetSparseFeedbackmapDescriptor();
						}
					}
					for (int i = 0; i < VOLUME_TEXTURESLOT_COUNT; ++i)
					{
						VUID volume_vuid = material.GetVolumeTextureVUID(static_cast<MaterialComponent::VolumeTextureSlot>(i));
						GVolumeComponent* volume_comp = nullptr;
						if (volume_vuid != INVALID_VUID)
						{
							volume_comp = (GVolumeComponent*)compfactory::GetComponentByVUID(volume_vuid);
							assert(volume_comp && volume_comp->GetComponentType() == ComponentType::VOLUMETEXTURE);
							if (volume_comp->IsValid())
							{
								shader_material.volume_textures[i].texture_descriptor = device->GetDescriptorIndex(volume_comp->GetGPUResource(), SubresourceType::SRV);
							}
							else
							{
								shader_material.volume_textures[i].texture_descriptor = -1;
							}
							shader_material.volume_textures[i].sparse_residencymap_descriptor = volume_comp->GetSparseResidencymapDescriptor();
							shader_material.volume_textures[i].sparse_feedbackmap_descriptor = volume_comp->GetSparseFeedbackmapDescriptor();
						}
					}

					if (material.samplerDescriptor < 0)
					{
						shader_material.sampler_descriptor = device->GetDescriptorIndex(renderer::GetSampler(SAMPLER_OBJECTSHADER));
					}
					else
					{
						shader_material.sampler_descriptor = material.samplerDescriptor;
					}

					std::memcpy(dest, &shader_material, sizeof(ShaderMaterial)); // memcpy whole structure into mapped pointer to avoid read from uncached memory
				};

			writeShaderMaterial(materialArrayMapped + args.jobIndex);

			if (textureStreamingFeedbackMapped != nullptr)
			{
				const uint32_t request_packed = textureStreamingFeedbackMapped[args.jobIndex];
				if (request_packed != 0)
				{
					const uint32_t request_uvset0 = request_packed & 0xFFFF;
					const uint32_t request_uvset1 = (request_packed >> 16u) & 0xFFFF;

					for (size_t slot = 0, n = SCU32(MaterialComponent::TextureSlot::TEXTURESLOT_COUNT);
						slot < n; ++slot)
					{
						VUID texture_vuid = material.GetTextureVUID(static_cast<MaterialComponent::TextureSlot>(slot));
						if (texture_vuid != INVALID_VUID)
						{
							Entity material_entity = compfactory::GetEntityByVUID(texture_vuid);
							GTextureComponent* texture_comp = dynamic_cast<GTextureComponent*>(compfactory::GetTextureComponent(material_entity));
							if (texture_comp)
							{
								if (texture_comp->IsValid())
								{
									texture_comp->StreamingRequestResolution(texture_comp->GetUVSet() == 0 ? request_uvset0 : request_uvset1);
								}
							}
						}
					}
				}
			}
			});
	}
	void GSceneDetails::RunRenderableUpdateSystem(jobsystem::context& ctx)
	{
		size_t num_renderables = renderableComponents.size();
		matrixRenderables.resize(num_renderables);
		matrixRenderablesPrev.resize(num_renderables);
		occlusionResultsObjects.resize(num_renderables);
		// GPUs
		jobsystem::Dispatch(ctx, (uint32_t)num_renderables, SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			GRenderableComponent& renderable = *renderableComponents[args.jobIndex];
			TransformComponent* transform = compfactory::GetTransformComponent(renderable.GetEntity());

			// Update occlusion culling status:
			OcclusionResult& occlusion_result = occlusionResultsObjects[args.jobIndex];
			if (!cameraFreezeCullingEnabled)
			{
				occlusion_result.occlusionHistory <<= 1u; // advance history by 1 frame
				int query_id = occlusion_result.occlusionQueries[queryheapIdx];
				if (queryResultBuffer[queryheapIdx].mapped_data != nullptr && query_id >= 0)
				{
					uint64_t visible = ((uint64_t*)queryResultBuffer[queryheapIdx].mapped_data)[query_id];
					if (visible)
					{
						occlusion_result.occlusionHistory |= 1; // visible
					}
				}
				else
				{
					occlusion_result.occlusionHistory |= 1; // visible
				}
			}
			occlusion_result.occlusionQueries[queryheapIdx] = -1; // invalidate query

			renderable.renderFlags = 0u;
			renderable.renderableIndex = ~0u;
			if (renderable.IsMeshRenderable())
			{
				// These will only be valid for a single frame:
				Entity geo_entity = renderable.GetGeometry();
				GGeometryComponent& geometry = *(GGeometryComponent*)compfactory::GetGeometryComponent(geo_entity);
				const std::vector<GeometryComponent::Primitive>& primitives = geometry.GetPrimitives();
				assert(primitives.size() > 0); // if (renderable.IsRenderable())		

				union SortBits
				{
					struct
					{
						uint32_t shadertype : SCU32(MaterialComponent::ShaderType::COUNT);
						uint32_t blendmode : BLENDMODE_COUNT;
						uint32_t doublesided : 1;	// bool
						uint32_t tessellation : 1;	// bool
						uint32_t alphatest : 1;		// bool
						uint32_t customshader : 8;
						uint32_t sort_priority : 4;
					} bits;
					uint32_t value;
				};
				static_assert(sizeof(SortBits) == sizeof(uint32_t));

				SortBits sort_bits;
				sort_bits.bits.sort_priority = renderable.sortPriority;

				size_t num_parts = primitives.size();

				renderable.renderableIndex = Scene::GetIndex(renderableEntities, renderable.GetEntity());
				renderable.geometryIndex = Scene::GetIndex(geometryEntities, geo_entity);

				renderable.materialIndices.assign(num_parts, ~0u);
				renderable.materialFilterFlags = 0;
				// Create GPU instance data:
				ShaderMeshInstance inst;
				inst.Init();

				bool hasBufferEffect = renderable.bufferEffects.size() == num_parts;

				for (uint32_t part_index = 0; part_index < num_parts; ++part_index)
				{
					const Primitive& part = primitives[part_index];
					Entity material_entity = renderable.GetMaterial(part_index);
					const GMaterialComponent& material = *(GMaterialComponent*)compfactory::GetMaterialComponent(material_entity);

					ShaderInstanceResLookup inst_res_lookup;
					inst_res_lookup.Init();
					inst_res_lookup.materialIndex = Scene::GetIndex(materialEntities, material.GetEntity());
					if (hasBufferEffect)
					{
						GPrimEffectBuffers& effect_buffers = renderable.bufferEffects[part_index];
						if (effect_buffers.vbWetmap.IsValid() && material.IsWetmapEnabled())
						{
							inst_res_lookup.vb_wetmap = effect_buffers.vbWetmap.descriptor_srv;
						}
						if (effect_buffers.vbAO.IsValid() && material.IsVertexAOEnabled())
						{
							inst_res_lookup.vb_ao = effect_buffers.vbAO.descriptor_srv;
						}
					}

					renderable.materialIndices[part_index] = inst_res_lookup.materialIndex;

					renderable.materialFilterFlags |= material.GetFilterMaskFlags();
					renderable.renderFlags |= material.GetRenderFlags();

					sort_bits.bits.tessellation |= material.IsTesellated();
					sort_bits.bits.doublesided |= material.IsDoubleSided();

					sort_bits.bits.shadertype |= 1 << SCU32(material.GetShaderType());
					sort_bits.bits.blendmode |= 1 << SCU32(material.GetBlendMode());
					sort_bits.bits.alphatest |= material.IsAlphaTestEnabled();

					std::memcpy(instanceResLookupMapped + renderable.resLookupOffset + part_index, 
						&inst_res_lookup, sizeof(ShaderInstanceResLookup));
				}

				renderable.sortBits = sort_bits.value;

				XMFLOAT4X4 world_matrix_prev = matrixRenderables[args.jobIndex];
				XMFLOAT4X4 world_matrix = transform->GetWorldMatrix();
				matrixRenderablesPrev[args.jobIndex] = world_matrix_prev;
				matrixRenderables[args.jobIndex] = world_matrix;

				inst.transformRaw.Create(world_matrix);
				inst.transform.Create(world_matrix);
				inst.transformPrev.Create(world_matrix_prev);

				// Get the quaternion from W because that reflects changes by other components (eg. softbody)
				XMMATRIX W = XMLoadFloat4x4(&world_matrix);
				XMVECTOR S, R, T;
				XMMatrixDecompose(&S, &R, &T, W);
				XMStoreFloat4(&inst.quaternion, R);
				float size = std::max(XMVectorGetX(S), std::max(XMVectorGetY(S), XMVectorGetZ(S)));

				const geometrics::AABB& aabb = renderable.GetAABB();

				inst.uid = renderable.GetEntity();
				inst.baseGeometryOffset = geometry.geometryOffset;
				inst.resLookupOffset = renderable.resLookupOffset;
				inst.geometryOffset = inst.baseGeometryOffset;// inst.baseGeometryOffset + first_part;
				inst.geometryCount = (uint)num_parts;;//last_part - first_part;

				inst.aabbCenter = aabb.getCenter();	
				inst.aabbRadius = aabb.getRadius();
				//inst.vb_ao = renderable.vb_ao_srv;
				//inst.vb_wetmap = device->GetDescriptorIndex(&renderable.wetmap, SubresourceType::SRV);

				inst.alphaTest_size = math::pack_half2(XMFLOAT2(0, size));
				//inst.SetUserStencilRef(renderable.userStencilRef);

				std::memcpy(instanceArrayMapped + args.jobIndex, &inst, sizeof(inst)); // memcpy whole structure into mapped pointer to avoid read from uncached memory
			}

			});
	}

	bool GSceneDetails::Update(const float dt)
	{
		deltaTime = dt;
		jobsystem::context ctx;

		device = GetGraphicsDevice();

		renderableEntities = scene_->GetRenderableEntities();
		renderableComponents.resize(renderableEntities.size());
		size_t num_renderables = renderableComponents.size();
		jobsystem::Dispatch(ctx, (uint32_t)num_renderables, SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {
			renderableComponents[args.jobIndex] = (GRenderableComponent*)compfactory::GetRenderableComponent(renderableEntities[args.jobIndex]);
			});

		lightEntities = scene_->GetLightEntities();
		lightComponents.resize(lightEntities.size());
		size_t num_lights = lightComponents.size();
		jobsystem::Dispatch(ctx, (uint32_t)num_lights, SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {
			lightComponents[args.jobIndex] = (GLightComponent*)compfactory::GetLightComponent(lightEntities[args.jobIndex]);
			});

		materialEntities = scene_->ScanMaterialEntities();
		materialComponents.resize(materialEntities.size());
		size_t num_materials = materialComponents.size();
		jobsystem::Dispatch(ctx, (uint32_t)num_materials, SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {
			materialComponents[args.jobIndex] = (GMaterialComponent*)compfactory::GetMaterialComponent(materialEntities[args.jobIndex]);
			});

		geometryEntities = scene_->ScanGeometryEntities();
		geometryComponents.resize(geometryEntities.size());
		size_t num_geometries = geometryComponents.size();
		jobsystem::Dispatch(ctx, (uint32_t)num_geometries, SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {
			geometryComponents[args.jobIndex] = (GGeometryComponent*)compfactory::GetGeometryComponent(geometryEntities[args.jobIndex]);
			});

		uint32_t pingpong_buffer_index = device->GetBufferIndex();

		// GPU Setting-up for Update IF necessary
		{
			// 1. dynamic rendering (such as particles and terrain, cloud...) kickoff
			// TODO

			// 2. constant (pingpong) buffers for renderables (Non Thread Task)
			instanceArraySize = num_renderables;
			if (instanceUploadBuffer[0].desc.size < (instanceArraySize * sizeof(ShaderMeshInstance)))
			{
				GPUBufferDesc desc;
				desc.stride = sizeof(ShaderMeshInstance);
				desc.size = desc.stride * instanceArraySize * 2; // *2 to grow fast
				desc.bind_flags = BindFlag::SHADER_RESOURCE;
				desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
				// if CACHE_COHERENT_UMA is allowed, then use instanceUploadBuffer directly.
				// otherwise, use instanceBuffer.
				if (!device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
				{
					// Non-UMA: separate Default usage buffer
					device->CreateBuffer(&desc, nullptr, &instanceBuffer);
					device->SetName(&instanceBuffer, "GSceneDetails::instanceBuffer");

					// Upload buffer shouldn't be used by shaders with Non-UMA:
					desc.bind_flags = BindFlag::NONE;
					desc.misc_flags = ResourceMiscFlag::NONE;
				}

				desc.usage = Usage::UPLOAD;
				for (int i = 0; i < arraysize(instanceUploadBuffer); ++i)
				{
					device->CreateBuffer(&desc, nullptr, &instanceUploadBuffer[i]);
					device->SetName(&instanceUploadBuffer[i], "GSceneDetails::instanceUploadBuffer");
				}
			}
			instanceArrayMapped = (ShaderMeshInstance*)instanceUploadBuffer[pingpong_buffer_index].mapped_data;

			// 3. material (pingpong) buffers for shaders (Non Thread Task)
			materialArraySize = num_materials;
			if (materialUploadBuffer[0].desc.size < (materialArraySize * sizeof(ShaderMaterial)))
			{
				GPUBufferDesc desc;
				desc.stride = sizeof(ShaderMaterial);
				desc.size = desc.stride * materialArraySize * 2; // *2 to grow fast
				desc.bind_flags = BindFlag::SHADER_RESOURCE;
				desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
				if (!device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
				{
					// Non-UMA: separate Default usage buffer
					device->CreateBuffer(&desc, nullptr, &materialBuffer);
					device->SetName(&materialBuffer, "GSceneDetails::materialBuffer");

					// Upload buffer shouldn't be used by shaders with Non-UMA:
					desc.bind_flags = BindFlag::NONE;
					desc.misc_flags = ResourceMiscFlag::NONE;
				}

				desc.usage = Usage::UPLOAD;
				for (int i = 0; i < arraysize(materialUploadBuffer); ++i)
				{
					device->CreateBuffer(&desc, nullptr, &materialUploadBuffer[i]);
					device->SetName(&materialUploadBuffer[i], "GSceneDetails::materialUploadBuffer");
				}
			}
			materialArrayMapped = (ShaderMaterial*)materialUploadBuffer[pingpong_buffer_index].mapped_data;

			if (textureStreamingFeedbackBuffer.desc.size < materialArraySize * sizeof(uint32_t))
			{
				GPUBufferDesc desc;
				desc.stride = sizeof(uint32_t);
				desc.size = desc.stride * materialArraySize * 2; // *2 to grow fast
				desc.bind_flags = BindFlag::UNORDERED_ACCESS;
				desc.format = Format::R32_UINT;
				device->CreateBuffer(&desc, nullptr, &textureStreamingFeedbackBuffer);
				device->SetName(&textureStreamingFeedbackBuffer, "GSceneDetails::textureStreamingFeedbackBuffer");

				// Readback buffer shouldn't be used by shaders:
				desc.usage = Usage::READBACK;
				desc.bind_flags = BindFlag::NONE;
				desc.misc_flags = ResourceMiscFlag::NONE;
				for (int i = 0; i < arraysize(materialUploadBuffer); ++i)
				{
					device->CreateBuffer(&desc, nullptr, &textureStreamingFeedbackBuffer_readback[i]);
					device->SetName(&textureStreamingFeedbackBuffer_readback[i], "GSceneDetails::textureStreamingFeedbackBuffer_readback");
				}
			}
			textureStreamingFeedbackMapped = (const uint32_t*)textureStreamingFeedbackBuffer_readback[pingpong_buffer_index].mapped_data;

			// 4. Occlusion culling read: (Non Thread Task)
			//	per each MeshInstance
			if (occlusionQueryEnabled && !cameraFreezeCullingEnabled)
			{
				uint32_t min_query_count = instanceArraySize + num_lights;
				if (queryHeap.desc.query_count < min_query_count)
				{
					GPUQueryHeapDesc desc;
					desc.type = GpuQueryType::OCCLUSION_BINARY;
					desc.query_count = min_query_count * 2; // *2 to grow fast
					bool success = device->CreateQueryHeap(&desc, &queryHeap);
					assert(success);

					GPUBufferDesc bd;
					bd.usage = Usage::READBACK;
					bd.size = desc.query_count * sizeof(uint64_t);

					for (int i = 0; i < arraysize(queryResultBuffer); ++i)
					{
						success = device->CreateBuffer(&bd, nullptr, &queryResultBuffer[i]);
						assert(success);
						device->SetName(&queryResultBuffer[i], "GSceneDetails::queryResultBuffer");
					}

					if (device->CheckCapability(GraphicsDeviceCapability::PREDICATION))
					{
						bd.usage = Usage::DEFAULT;
						bd.misc_flags |= ResourceMiscFlag::PREDICATION;
						success = device->CreateBuffer(&bd, nullptr, &queryPredicationBuffer);
						assert(success);
						device->SetName(&queryPredicationBuffer, "GSceneDetails::queryPredicationBuffer");
					}
				}

				// Advance to next query result buffer to use (this will be the oldest one that was written)
				queryheapIdx = pingpong_buffer_index;

				// Clear query allocation state:
				queryAllocator.store(0);
			}
		}

		if (dt > 0)
		{
			// Scan mesh subset counts and skinning data sizes to allocate GPU geometry data:
			geometryAllocator.store(0u);
			jobsystem::Dispatch(ctx, (uint32_t)num_geometries, SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {
				GGeometryComponent* geometry = geometryComponents[args.jobIndex];
				geometry->geometryOffset = geometryAllocator.fetch_add((uint32_t)geometry->GetNumParts());
				});

			// Scan renderable's materials 
			instanceResLookupAllocator.store(0u);
			jobsystem::Dispatch(ctx, (uint32_t)num_renderables, SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {
				GRenderableComponent* renderable = renderableComponents[args.jobIndex];
				if (!renderable->IsMeshRenderable())
				{
					return;
				}
				size_t num_parts = compfactory::GetGeometryComponent(renderable->GetGeometry())->GetNumParts();
				renderable->resLookupOffset = instanceResLookupAllocator.fetch_add((uint32_t)num_parts);
				});

			// initialize ShaderRenderable 
			jobsystem::Execute(ctx, [&](jobsystem::JobArgs args) {
				// Must not keep inactive instances, so init them for safety:
				ShaderMeshInstance inst;
				inst.Init();
				for (uint32_t i = 0; i < instanceArraySize; ++i)
				{
					std::memcpy(instanceArrayMapped + i, &inst, sizeof(inst));
				}
				});
		}

		jobsystem::Wait(ctx); // dependencies

		// GPU subset count allocation is ready at this point:
		geometryArraySize = geometryAllocator.load();
		if (geometryUploadBuffer[0].desc.size < (geometryArraySize * sizeof(ShaderGeometry)))
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(ShaderGeometry);
			desc.size = desc.stride * geometryArraySize * 2; // *2 to grow fast
			desc.bind_flags = BindFlag::SHADER_RESOURCE;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
			if (!device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
			{
				// Non-UMA: separate Default usage buffer
				device->CreateBuffer(&desc, nullptr, &geometryBuffer);
				device->SetName(&geometryBuffer, "GSceneDetails::geometryBuffer");

				// Upload buffer shouldn't be used by shaders with Non-UMA:
				desc.bind_flags = BindFlag::NONE;
				desc.misc_flags = ResourceMiscFlag::NONE;
			}

			desc.usage = Usage::UPLOAD;
			for (int i = 0; i < arraysize(geometryUploadBuffer); ++i)
			{
				device->CreateBuffer(&desc, nullptr, &geometryUploadBuffer[i]);
				device->SetName(&geometryUploadBuffer[i], "GSceneDetails::geometryUploadBuffer");
			}
		}
		geometryArrayMapped = (ShaderGeometry*)geometryUploadBuffer[pingpong_buffer_index].mapped_data;

		// GPU instance-mapping material count allocation is ready at this point:
		instanceResLookupSize = instanceResLookupAllocator.load();
		if (instanceResLookupUploadBuffer[0].desc.size < (instanceResLookupSize * sizeof(uint)))
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(ShaderInstanceResLookup);
			desc.size = desc.stride * instanceResLookupSize * 2; // *2 to grow fast
			desc.bind_flags = BindFlag::SHADER_RESOURCE;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
			if (!device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
			{
				// Non-UMA: separate Default usage buffer
				device->CreateBuffer(&desc, nullptr, &instanceResLookupBuffer);
				device->SetName(&instanceResLookupBuffer, "GSceneDetails::instanceMaterialLookup");

				// Upload buffer shouldn't be used by shaders with Non-UMA:
				desc.bind_flags = BindFlag::NONE;
				desc.misc_flags = ResourceMiscFlag::NONE;
			}

			desc.usage = Usage::UPLOAD;
			for (int i = 0; i < arraysize(instanceResLookupUploadBuffer); ++i)
			{
				device->CreateBuffer(&desc, nullptr, &instanceResLookupUploadBuffer[i]);
				device->SetName(&instanceResLookupUploadBuffer[i], "GSceneDetails::instanceMaterialLookupUploadBuffer");
			}
		}
		instanceResLookupMapped = (ShaderInstanceResLookup*)instanceResLookupUploadBuffer[pingpong_buffer_index].mapped_data;

		RunPrimtiveUpdateSystem(ctx);
		RunMaterialUpdateSystem(ctx);

		jobsystem::Wait(ctx); // dependencies

		RunRenderableUpdateSystem(ctx);
		//RunCameraUpdateSystem(ctx); .. in render function
		//RunProbeUpdateSystem(ctx); .. future feature
		//RunParticleUpdateSystem(ctx); .. future feature
		//RunSpriteUpdateSystem(ctx); .. future feature
		//RunFontUpdateSystem(ctx); .. future feature

		jobsystem::Wait(ctx); // dependencies

		// Merge parallel bounds computation (depends on object update system):
		//bounds = AABB();
		//for (auto& group_bound : parallel_bounds)
		//{
		//	bounds = AABB::Merge(bounds, group_bound);
		//}

		//BVH.Update(*this); // scene

		// content updates...

		// Shader scene resources:
		shaderscene.Init();
		if (device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
		{
			shaderscene.instancebuffer = device->GetDescriptorIndex(&instanceUploadBuffer[pingpong_buffer_index], SubresourceType::SRV);
			shaderscene.geometrybuffer = device->GetDescriptorIndex(&geometryUploadBuffer[pingpong_buffer_index], SubresourceType::SRV);
			shaderscene.materialbuffer = device->GetDescriptorIndex(&materialUploadBuffer[pingpong_buffer_index], SubresourceType::SRV);
			shaderscene.instanceResLookupBuffer = device->GetDescriptorIndex(&instanceResLookupUploadBuffer[pingpong_buffer_index], SubresourceType::SRV);
			shaderscene.meshletbuffer = -1;
		}
		else
		{
			shaderscene.instancebuffer = device->GetDescriptorIndex(&instanceBuffer, SubresourceType::SRV);
			shaderscene.geometrybuffer = device->GetDescriptorIndex(&geometryBuffer, SubresourceType::SRV);
			shaderscene.materialbuffer = device->GetDescriptorIndex(&materialBuffer, SubresourceType::SRV);
			shaderscene.instanceResLookupBuffer = device->GetDescriptorIndex(&instanceResLookupBuffer, SubresourceType::SRV);
			shaderscene.meshletbuffer = -1;
		}
		shaderscene.texturestreamingbuffer = device->GetDescriptorIndex(&textureStreamingFeedbackBuffer, SubresourceType::UAV);
		//if (weather.skyMap.IsValid())
		//{
		//	shaderscene.globalenvmap = device->GetDescriptorIndex(&weather.skyMap.GetTexture(), SubresourceType::SRV, weather.skyMap.GetTextureSRGBSubresource());
		//}
		//else
		{
			shaderscene.globalenvmap = -1;
		}

		//if (probes.GetCount() > 0 && probes[0].texture.IsValid())
		//{
		//	shaderscene.globalprobe = device->GetDescriptorIndex(&probes[0].texture, SubresourceType::SRV);
		//}
		//else if (global_dynamic_probe.texture.IsValid())
		//{
		//	shaderscene.globalprobe = device->GetDescriptorIndex(&global_dynamic_probe.texture, SubresourceType::SRV);
		//}
		//else
		{
			shaderscene.globalprobe = -1;
		}

		//shaderscene.TLAS = device->GetDescriptorIndex(&TLAS, SubresourceType::SRV);
		//shaderscene.BVH_counter = device->GetDescriptorIndex(&BVH.primitiveCounterBuffer, SubresourceType::SRV);
		//shaderscene.BVH_nodes = device->GetDescriptorIndex(&BVH.bvhNodeBuffer, SubresourceType::SRV);
		//shaderscene.BVH_primitives = device->GetDescriptorIndex(&BVH.primitiveBuffer, SubresourceType::SRV);

		const geometrics::AABB& bounds = scene_->GetAABB();
		shaderscene.aabb_min = bounds.getMin();
		shaderscene.aabb_max = bounds.getMax();
		shaderscene.aabb_extents.x = abs(shaderscene.aabb_max.x - shaderscene.aabb_min.x);
		shaderscene.aabb_extents.y = abs(shaderscene.aabb_max.y - shaderscene.aabb_min.y);
		shaderscene.aabb_extents.z = abs(shaderscene.aabb_max.z - shaderscene.aabb_min.z);
		shaderscene.aabb_extents_rcp.x = 1.0f / shaderscene.aabb_extents.x;
		shaderscene.aabb_extents_rcp.y = 1.0f / shaderscene.aabb_extents.y;
		shaderscene.aabb_extents_rcp.z = 1.0f / shaderscene.aabb_extents.z;

		shaderscene.ambient = scene_->GetAmbient();
		//shaderscene.ddgi.grid_dimensions = ddgi.grid_dimensions;
		//shaderscene.ddgi.probe_count = ddgi.grid_dimensions.x * ddgi.grid_dimensions.y * ddgi.grid_dimensions.z;
		//shaderscene.ddgi.color_texture_resolution = uint2(ddgi.color_texture.desc.width, ddgi.color_texture.desc.height);
		//shaderscene.ddgi.color_texture_resolution_rcp = float2(1.0f / shaderscene.ddgi.color_texture_resolution.x, 1.0f / shaderscene.ddgi.color_texture_resolution.y);
		//shaderscene.ddgi.depth_texture_resolution = uint2(ddgi.depth_texture.desc.width, ddgi.depth_texture.desc.height);
		//shaderscene.ddgi.depth_texture_resolution_rcp = float2(1.0f / shaderscene.ddgi.depth_texture_resolution.x, 1.0f / shaderscene.ddgi.depth_texture_resolution.y);
		shaderscene.ddgi.color_texture = -1;//shaderscene.ddgi.color_texture = device->GetDescriptorIndex(&ddgi.color_texture, SubresourceType::SRV);
		//shaderscene.ddgi.depth_texture = device->GetDescriptorIndex(&ddgi.depth_texture, SubresourceType::SRV);
		//shaderscene.ddgi.offset_texture = device->GetDescriptorIndex(&ddgi.offset_texture, SubresourceType::SRV);
		//shaderscene.ddgi.grid_min = ddgi.grid_min;
		//shaderscene.ddgi.grid_extents.x = abs(ddgi.grid_max.x - ddgi.grid_min.x);
		//shaderscene.ddgi.grid_extents.y = abs(ddgi.grid_max.y - ddgi.grid_min.y);
		//shaderscene.ddgi.grid_extents.z = abs(ddgi.grid_max.z - ddgi.grid_min.z);
		//shaderscene.ddgi.grid_extents_rcp.x = 1.0f / shaderscene.ddgi.grid_extents.x;
		//shaderscene.ddgi.grid_extents_rcp.y = 1.0f / shaderscene.ddgi.grid_extents.y;
		//shaderscene.ddgi.grid_extents_rcp.z = 1.0f / shaderscene.ddgi.grid_extents.z;
		//shaderscene.ddgi.smooth_backface = ddgi.smooth_backface;
		//shaderscene.ddgi.cell_size.x = shaderscene.ddgi.grid_extents.x / (ddgi.grid_dimensions.x - 1);
		//shaderscene.ddgi.cell_size.y = shaderscene.ddgi.grid_extents.y / (ddgi.grid_dimensions.y - 1);
		//shaderscene.ddgi.cell_size.z = shaderscene.ddgi.grid_extents.z / (ddgi.grid_dimensions.z - 1);
		//shaderscene.ddgi.cell_size_rcp.x = 1.0f / shaderscene.ddgi.cell_size.x;
		//shaderscene.ddgi.cell_size_rcp.y = 1.0f / shaderscene.ddgi.cell_size.y;
		//shaderscene.ddgi.cell_size_rcp.z = 1.0f / shaderscene.ddgi.cell_size.z;
		//shaderscene.ddgi.max_distance = std::max(shaderscene.ddgi.cell_size.x, std::max(shaderscene.ddgi.cell_size.y, shaderscene.ddgi.cell_size.z)) * 1.5f;

		return true;
	}

	bool GSceneDetails::Destroy()
	{
		instanceArraySize = 0;
		geometryArraySize = 0;
		materialArraySize = 0;
		instanceResLookupSize = 0;

		constexpr uint32_t buffer_count = graphics::GraphicsDevice::GetBufferCount();
		instanceBuffer = {};
		geometryBuffer = {};
		materialBuffer = {};
		textureStreamingFeedbackBuffer = {};
		instanceResLookupBuffer = {};
		queryHeap = {};
		queryPredicationBuffer = {};
		for (uint32_t i = 0; i < buffer_count; ++i)
		{
			instanceUploadBuffer[i] = {};
			geometryUploadBuffer[i] = {};
			materialUploadBuffer[i] = {};
			textureStreamingFeedbackBuffer_readback[i] = {};
			queryResultBuffer[i] = {};
			instanceResLookupUploadBuffer[i] = {};
		}

		return true;
	}
}