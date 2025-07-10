#include "Renderer.h"
#include "RenderPath3D_Detail.h"
#include "Font.h"

#include "Utils/Timer.h"
#include "Utils/Backlog.h"

using namespace vz::renderer;
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

	void GSceneDetails::RunGeometryUpdateSystem(jobsystem::context& ctx)
	{
		meshletAllocator.store(0u);

		jobsystem::Dispatch(ctx, (uint32_t)geometryComponents.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			GGeometryComponent& geometry = *geometryComponents[args.jobIndex];
			assert(geometry.geometryIndex == args.jobIndex);

			const std::vector<Primitive>& primitives = geometry.GetPrimitives();
			geometry.meshletCount = 0;

			float tessealation_factor = geometry.GetTessellationFactor();
			for (size_t part_index = 0, n = primitives.size(); part_index < n; ++part_index)
			{
				GPrimBuffers* prim_buffer_ptr = geometry.GetGPrimBuffer(part_index);
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

					// --- mesh shader : TODO ---
					shader_geometry_part.vb_clu = -1;
					shader_geometry_part.vb_bou = -1;

					if (geometry.isMeshletEnabled)
					{
						//shader_geometry_part.meshletOffset = mesh.cluster_ranges[subsetIndex].clusterOffset;
					}
					else
					{
						geometry.meshletCount = triangle_count_to_meshlet_count(primitive.GetNumIndices() / 3u);
					}
					geometry.meshletOffset = meshletAllocator.load();
					meshletAllocator.fetch_add(geometry.meshletCount);

					shader_geometry_part.meshletCount = geometry.meshletCount;
					shader_geometry_part.meshletOffset = geometry.meshletOffset;
					// --------------------------

					shader_geometry_part.aabb_min = primitive.GetAABB()._min;
					shader_geometry_part.aabb_max = primitive.GetAABB()._max;
					shader_geometry_part.uv_range_min = primitive.GetUVRangeMin();
					shader_geometry_part.uv_range_max = primitive.GetUVRangeMax();
					shader_geometry_part.tessellation_factor = tessealation_factor;

					std::memcpy(geometryArrayMapped + geometry.geometryOffset + part_index, &shader_geometry_part, sizeof(ShaderGeometry));

				}
			}

			if (geometry.IsGPUBVHEnabled() && geometry.IsDirtyGPUBVH() && geometry.HasRenderData())
			{
				AddDeferredGeometryGPUBVHUpdate(geometry.GetEntity());
			}
			});
	}
	void GSceneDetails::RunMaterialUpdateSystem(jobsystem::context& ctx)
	{
		isWetmapProcessingRequired = false;

		jobsystem::Dispatch(ctx, (uint32_t)materialComponents.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			GMaterialComponent& material = *materialComponents[args.jobIndex];
			assert(material.materialIndex == args.jobIndex);

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
					const float roughness = material.GetRoughness();
					const float reflectance = material.GetReflectance();
					const float metalness = material.GetMatalness();
					const float refraction = material.GetRefraction();
					const float normalMapStrength = material.GetNormalMapStrength();
					const float parallaxOcclusionMapping = material.GetParallaxOcclusionMapping();
					const float displacementMapping = material.GetDisplacementMapping();
					const XMFLOAT4 subsurfaceScattering = material.GetSubsurfaceScattering();
					const XMFLOAT4 sheenColor = material.GetSheenColor();
					const float transmission = material.GetTransmission();
					const float sheenRoughness = material.GetSheenRoughness();
					const float clearcoat = material.GetClearcoat();
					const float clearcoatRoughness = material.GetClearcoatRoughness();

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

					//if (material.GetShaderType() == MaterialComponent::ShaderType::Water)
					//{
					//	shader_material.sheenColor_saturation = pack_half4(XMFLOAT4(1 - extinctionColor.x, 1 - extinctionColor.y, 1 - extinctionColor.z, saturation));
					//}
					//else
					{
						shader_material.sheenColor_saturation = pack_half4(XMFLOAT4(sheenColor.x, sheenColor.y, sheenColor.z, material.GetSaturate()));
					}

					shader_material.transmission_sheenroughness_clearcoat_clearcoatroughness = pack_half4(transmission, sheenRoughness, clearcoat, clearcoatRoughness);
					float _anisotropy_strength = 0;
					float _anisotropy_rotation_sin = 0;
					float _anisotropy_rotation_cos = 0;
					float _blend_with_terrain_height_rcp = 0;
					//if (shaderType == MaterialComponent::ShaderType::PBR_ANISOTROPIC)
					//{
					//	const float anisotropy_rotation = material.GetAnisotropyRotation();
					//	_anisotropy_strength = clamp(material.GetAnisotropyStrength(), 0.0f, 0.99f);
					//	_anisotropy_rotation_sin = std::sin(anisotropy_rotation);
					//	_anisotropy_rotation_cos = std::cos(anisotropy_rotation);
					//}
					shader_material.aniso_anisosin_anisocos_terrainblend = pack_half4(_anisotropy_strength, _anisotropy_rotation_sin, _anisotropy_rotation_cos, _blend_with_terrain_height_rcp);
					shader_material.shaderType = (uint)material.GetShaderType();

					GRenderableComponent* vol_renderable = material.renderableVolumeMapperRenderable;
					shader_material.volumemapperTargetInstIndex = -1;
					if (vol_renderable)
					{
						shader_material.volumemapperTargetInstIndex = (int)vol_renderable->renderableIndex;
						MaterialComponent::VolumeTextureSlot vol_slot = material.GetVolumeMapperVolumeSlot();
						MaterialComponent::LookupTableSlot lookup_slot = material.GetVolumeMapperLookupSlot();
						shader_material.volumemapperVolumeSlot = SCU32(vol_slot);
						shader_material.volumemapperLookupSlot = SCU32(lookup_slot);

						if (material.GetVolumeTextureVUID(vol_slot) == INVALID_VUID
							|| material.GetLookupTableVUID(lookup_slot) == INVALID_VUID)
						{
							shader_material.volumemapperTargetInstIndex = -1;
						}
					}
					
					shader_material.userdata = 0u;

					shader_material.options_stencilref = 0;

					for (int i = 0; i < TEXTURESLOT_COUNT; ++i)
					{
						GTextureComponent* texture_comp = material.textures[i];
						if (texture_comp)
						{
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
						GVolumeComponent* volume_comp = material.volumeTextures[i];
						if (volume_comp)
						{
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
					for (int i = 0; i < LOOKUPTABLE_COUNT; ++i)
					{
						GTextureComponent* lookup_comp = material.textureLookups[i];
						if (lookup_comp)
						{
							assert(lookup_comp && lookup_comp->GetComponentType() == ComponentType::TEXTURE);
							shader_material.lookup_textures[i].uvset_lodclamp = (lookup_comp->GetUVSet() & 1) | (XMConvertFloatToHalf(lookup_comp->GetLodClamp()) << 1u);
							if (lookup_comp->IsValid())
							{
								shader_material.lookup_textures[i].texture_descriptor = device->GetDescriptorIndex(lookup_comp->GetGPUResource(), SubresourceType::SRV, -1);
							}
							else
							{
								shader_material.lookup_textures[i].texture_descriptor = -1;
							}
							shader_material.lookup_textures[i].sparse_residencymap_descriptor = lookup_comp->GetSparseResidencymapDescriptor();
							shader_material.lookup_textures[i].sparse_feedbackmap_descriptor = lookup_comp->GetSparseFeedbackmapDescriptor();
						}
					}

					if (material.samplerDescriptor < 0)
					{
						shader_material.sampler_descriptor = device->GetDescriptorIndex(&renderer::samplers[SAMPLER_OBJECTSHADER]);
					}
					else
					{
						shader_material.sampler_descriptor = material.samplerDescriptor;
					}

					std::memcpy(dest, &shader_material, sizeof(ShaderMaterial)); // memcpy whole structure into mapped pointer to avoid read from uncached memory
				};

			writeShaderMaterial(materialArrayMapped + material.materialIndex);

			if (textureStreamingFeedbackMapped != nullptr)
			{
				const uint32_t request_packed = textureStreamingFeedbackMapped[material.materialIndex];
				if (request_packed != 0)
				{
					const uint32_t request_uvset0 = request_packed & 0xFFFF;
					const uint32_t request_uvset1 = (request_packed >> 16u) & 0xFFFF;

					for (size_t slot = 0, n = SCU32(MaterialComponent::TextureSlot::TEXTURESLOT_COUNT); slot < n; ++slot)
					{
						GTextureComponent* texture_comp = material.textures[slot];
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
			});
	}
	void GSceneDetails::RunRenderableUpdateSystem(jobsystem::context& ctx)
	{
		size_t num_renderables = renderableComponents.size();
		occlusionResultsObjects.resize(num_renderables);

		// GPUs
		jobsystem::Dispatch(ctx, (uint32_t)num_renderables, SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			GRenderableComponent& renderable = *renderableComponents[args.jobIndex];
			assert(renderable.renderableIndex == args.jobIndex);

			// Update occlusion culling status:
			OcclusionResult& occlusion_result = occlusionResultsObjects[renderable.renderableIndex];
			if (!isFreezeCullingCameraEnabled)
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

			// Note : SceneDetails::RunRenderableUpdateSystem computes raw world matrix and its prev.
			XMFLOAT4X4 world_matrix_prev = matrixRenderables[renderable.renderableIndex];
			XMFLOAT4X4 world_matrix = matrixRenderablesPrev[renderable.renderableIndex];

			renderable.renderFlags = 0u;
			switch (renderable.GetRenderableType())
			{
			case RenderableType::GSPLAT_RENDERABLE:
			case RenderableType::MESH_RENDERABLE:
			{
				// These will only be valid for a single frame:
				GGeometryComponent& geometry = *renderable.geometry;
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

				renderable.materialFilterFlags = 0u;

				// Create GPU instance data:
				ShaderMeshInstance inst;
				inst.Init();

				bool hasBufferEffect = renderable.bufferEffects.size() == num_parts && num_parts > 0;
				// in the case that no primitives but has material (shader) for rendering (volume)
				uint32_t shader_num = renderable.materials.size();
				assert(num_parts == shader_num ? num_parts > 0 : num_parts == 0);

				for (uint32_t shader_index = 0; shader_index < shader_num; ++shader_index)
				{
					const Primitive* part = num_parts > 0 ? &primitives[shader_index] : nullptr;
					const GMaterialComponent& material = *renderable.materials[shader_index];
					if (part)
					{
						if (!part->HasRenderData())
							//renderableShapes.AddPrimitivePart(part, material.GetBaseColor(), world_matrix);
							continue;
					}

					ShaderInstanceResLookup inst_res_lookup;
					inst_res_lookup.Init();
					inst_res_lookup.materialIndex = material.materialIndex;
					if (hasBufferEffect)
					{
						GPrimEffectBuffers& effect_buffers = renderable.bufferEffects[shader_index];
						if (effect_buffers.vbWetmap.IsValid() && material.IsWetmapEnabled())
						{
							inst_res_lookup.vb_wetmap = effect_buffers.vbWetmap.descriptor_srv;
						}
						if (effect_buffers.vbAO.IsValid() && material.IsVertexAOEnabled())
						{
							inst_res_lookup.vb_ao = effect_buffers.vbAO.descriptor_srv;
						}
					}

					renderable.materialFilterFlags |= material.GetFilterMaskFlags();
					renderable.renderFlags |= material.GetRenderFlags();

					sort_bits.bits.tessellation |= material.IsTesellated();
					sort_bits.bits.doublesided |= material.IsDoubleSided();

					sort_bits.bits.shadertype |= 1 << SCU32(material.GetShaderType());
					sort_bits.bits.blendmode |= 1 << SCU32(material.GetBlendMode());
					sort_bits.bits.alphatest |= material.IsAlphaTestEnabled();

					std::memcpy(instanceResLookupMapped + renderable.resLookupOffset + shader_index,
						&inst_res_lookup, sizeof(ShaderInstanceResLookup));
				}

				renderable.sortBits = sort_bits.value;

				// transformRaw is defined in OS itself
				// transform and transformPrev are defined in UNORM space of OS
				// if there is no UNORM-space-defined buffers, 
				//	then transform and transformPrev are the same as transformRaw
				inst.transformRaw.Create(world_matrix);

				XMMATRIX W_inv = XMMatrixInverse(nullptr, XMLoadFloat4x4(&world_matrix));
				XMFLOAT4X4 world_matrix_inv;
				XMStoreFloat4x4(&world_matrix_inv, W_inv);
				inst.transformRawInv.Create(world_matrix_inv);

				if (IsFormatUnorm(geometry.positionFormat) && !geometry.GetGPrimBuffer(0)->soPosW.IsValid())
				{
					// The UNORM correction is only done for the GPU data!
					XMMATRIX R = geometry.GetAABB().getUnormRemapMatrix();
					XMStoreFloat4x4(&world_matrix, R * XMLoadFloat4x4(&world_matrix));
					XMStoreFloat4x4(&world_matrix_prev, R * XMLoadFloat4x4(&world_matrix_prev));
				}
				inst.transform.Create(world_matrix);
				inst.transformPrev.Create(world_matrix_prev);

				XMMATRIX W = XMLoadFloat4x4(&world_matrix);
				XMVECTOR S, R, T;
				XMMatrixDecompose(&S, &R, &T, W);
				float size = std::max(XMVectorGetX(S), std::max(XMVectorGetY(S), XMVectorGetZ(S)));

				const geometrics::AABB& aabb = renderable.GetAABB();

				inst.uid = renderable.GetEntity();
				inst.fadeDistance = renderable.GetFadeDistance();
				inst.flags = renderable.GetFlags();

				//inst.emissive
				inst.color = math::pack_half4(XMFLOAT4(1, 1, 1, 1));
				//inst.lightmap

				// TODO : applying adaptive LOD 
				inst.baseGeometryOffset = geometry.geometryOffset;
				inst.baseGeometryCount = geometry.GetNumParts();
				inst.resLookupIndex = renderable.resLookupOffset;
				inst.geometryOffset = inst.baseGeometryOffset;// inst.baseGeometryOffset + first_part;
				inst.geometryCount = (uint)num_parts;;//last_part - first_part;
				inst.meshletOffset = geometry.meshletOffset;

				// TODO: clipper setting
				inst.clipIndex = -1;

				inst.layerMask = 0u;

				inst.aabbCenter = aabb.getCenter();
				inst.aabbRadius = aabb.getRadius();
				//inst.vb_ao = renderable.vb_ao_srv;
				//inst.vb_wetmap = device->GetDescriptorIndex(&renderable.wetmap, SubresourceType::SRV);

				inst.alphaTest_size = math::pack_half2(XMFLOAT2(0, size));
				//inst.SetUserStencilRef(renderable.userStencilRef);
				XMFLOAT4 rimHighlightColor = renderable.GetRimHighLightColor();
				float rimHighlightFalloff = renderable.GetRimHighLightFalloff();
				inst.rimHighlight = math::pack_half4(XMFLOAT4(rimHighlightColor.x * rimHighlightColor.w, rimHighlightColor.y * rimHighlightColor.w, rimHighlightColor.z * rimHighlightColor.w, rimHighlightFalloff));

				std::memcpy(instanceArrayMapped + renderable.renderableIndex, &inst, sizeof(inst)); // memcpy whole structure into mapped pointer to avoid read from uncached memory
			} break;
			case RenderableType::VOLUME_RENDERABLE:
			{
				const GMaterialComponent& material = *renderable.materials[0];
				GVolumeComponent* volume = material.volumeTextures[SCU32(MaterialComponent::VolumeTextureSlot::VOLUME_MAIN_MAP)];
				assert(volume);
				assert(volume->IsValidVolume());
				GTextureComponent* otf = material.textureLookups[SCU32(MaterialComponent::LookupTableSlot::LOOKUP_OTF)];
				assert(otf);

				renderable.materialFilterFlags |= material.GetFilterMaskFlags();
				renderable.renderFlags |= material.GetRenderFlags();

				union SortBits
				{
					struct
					{
						uint32_t shadertype : SCU32(MaterialComponent::ShaderType::COUNT);
						uint32_t sort_priority : 4;
					} bits;
					uint32_t value;
				};
				static_assert(sizeof(SortBits) == sizeof(uint32_t));

				SortBits sort_bits;
				sort_bits.bits.sort_priority = renderable.sortPriority;

				ShaderMeshInstance inst; // this will be loaded as VolumeInstance
				inst.Init();

				uint3 vol_size = uint3(volume->GetWidth(), volume->GetHeight(), volume->GetDepth());
				vzlog_assert(vol_size.x <= 4096 && vol_size.y <= 4096 && vol_size.z <= 4096, "Volume size must be lessequal than 4096!");
				inst.emissive = uint2(vol_size.x, vol_size.y & 0xFFFF | (vol_size.z & 0xFFFF) << 16);

				const XMUINT3& blocks_size = volume->GetBlocksSize();
				vzlog_assert(blocks_size.x <= 2048 && blocks_size.y <= 2048 && blocks_size.z <= 1024, "# of Volume Blocks must be packked into 32-bits!");
				inst.layerMask = blocks_size.x & 0x7FF | ((blocks_size.x & 0x7FF) << 11) | ((blocks_size.z & 0x3FF) << 22);

				const XMUINT3& block_pitch = volume->GetBlockPitch();
				vzlog_assert(block_pitch.x <= 2048 && block_pitch.y <= 2048 && block_pitch.z <= 1024, "Volume Pitches must be packked into 32-bits!");
				inst.baseGeometryCount = block_pitch.x & 0x7FF | ((block_pitch.x & 0x7FF) << 11) | ((block_pitch.z & 0x3FF) << 22);

				inst.color = math::pack_half4(XMFLOAT4(1, 1, 1, 1));
				//inst.lightmap
				// 
				// common attributes
				inst.uid = renderable.GetEntity();
				inst.fadeDistance = renderable.GetFadeDistance();
				inst.flags = renderable.GetFlags();

				const geometrics::AABB& aabb = renderable.GetAABB();
				inst.aabbCenter = aabb.getCenter();
				inst.aabbRadius = aabb.getRadius();
				XMFLOAT4 rimHighlightColor = renderable.GetRimHighLightColor();
				float rimHighlightFalloff = renderable.GetRimHighLightFalloff();
				inst.rimHighlight = math::pack_half4(XMFLOAT4(rimHighlightColor.x * rimHighlightColor.w, rimHighlightColor.y * rimHighlightColor.w, rimHighlightColor.z * rimHighlightColor.w, rimHighlightFalloff));

				// TODO: clipper setting
				inst.clipIndex = -1;
				//inst.quaternion = ?? decompose of W and assign R to it

				// ----- volume attributes -----
				const XMFLOAT4X4& mat_os2ws = world_matrix;
				XMMATRIX xmat_os2ws = XMLoadFloat4x4(&mat_os2ws);
				XMMATRIX xmat_ws2os = XMMatrixInverse(NULL, xmat_os2ws);
				XMMATRIX xmat_os2vs = XMLoadFloat4x4(&volume->GetMatrixOS2VS());
				XMMATRIX xmat_vs2ts = XMLoadFloat4x4(&volume->GetMatrixVS2TS());
				XMMATRIX xmat_ws2ts = xmat_ws2os * xmat_os2vs * xmat_vs2ts;

				XMFLOAT4X4 mat_ws2ts;
				XMStoreFloat4x4(&mat_ws2ts, xmat_ws2ts);

				inst.transform.Create(mat_ws2ts);
				inst.transformPrev.Create(world_matrix_prev);

				const geometrics::AABB& aabb_vol = volume->ComputeAABB();
				XMFLOAT3 aabb_size = aabb_vol.getWidth();
				XMMATRIX xmat_s = XMMatrixScaling(1.f / aabb_size.x, 1.f / aabb_size.y, 1.f / aabb_size.z);
				XMMATRIX xmat_alignedvbox_ws2bs = xmat_ws2os * xmat_s;
				XMFLOAT4X4 mat_alignedvbox_ws2bs;
				XMStoreFloat4x4(&mat_alignedvbox_ws2bs, xmat_alignedvbox_ws2bs);
				inst.transformRaw.Create(mat_alignedvbox_ws2bs); // TODO... this is supposed to be world_matrix

				inst.resLookupIndex = material.materialIndex; // renderable.resLookupOffset;
				const XMFLOAT2& min_max_stored_v = volume->GetStoredMinMax();
				float value_range = min_max_stored_v.y - min_max_stored_v.x;
				inst.geometryOffset = *(uint*)&value_range;
				float mask_value_range = 255.f;
				inst.geometryCount = *(uint*)&mask_value_range;
				float mask_unormid_otf_map = mask_value_range / (otf->GetHeight() > 1 ? otf->GetHeight() - 1 : 1.f);
				inst.baseGeometryOffset = *(uint*)&mask_unormid_otf_map;

				const Texture vol_blk_texture = volume->GetBlockTexture();
				int texture_volume_blocks = device->GetDescriptorIndex(&vol_blk_texture, SubresourceType::SRV);
				inst.meshletOffset = *(uint*)&texture_volume_blocks;
				const XMFLOAT3& vox_size = volume->GetVoxelSize();
				float sample_dist = std::min(std::min(vox_size.x, vox_size.y), vox_size.z);
				inst.alphaTest_size = *(uint*)&sample_dist;

				renderable.sortBits = sort_bits.value;

				std::memcpy(instanceArrayMapped + renderable.renderableIndex, &inst, sizeof(inst)); // memcpy whole structure into mapped pointer to avoid read from uncached memory
			} break;
			default:
				break;
			}
			});
	}

	bool GSceneDetails::Update(const float dt)
	{
		renderableShapes.Clear();

		font::UpdateAtlas();

		lightComponents = scene_->GetLightComponents();
		geometryComponents = scene_->GetGeometryComponents();
		materialComponents = scene_->GetMaterialComponents();
		renderableComponents = scene_->GetRenderableComponents();
		spriteComponents = scene_->GetRenderableSpriteComponents();
		spriteFontComponents = scene_->GetRenderableSpritefontComponents();
		matrixRenderables = scene_->GetRenderableWorldMatrices();
		matrixRenderablesPrev = scene_->GetRenderableWorldMatricesPrev();

		deltaTime = dt;
		jobsystem::context ctx;

		size_t num_lights = lightComponents.size();
		size_t num_geometries = geometryComponents.size();
		size_t num_materials = materialComponents.size();
		size_t num_renderables = renderableComponents.size();
		size_t num_sprites = spriteComponents.size();
		size_t num_spriteFonts = spriteFontComponents.size();
		
		device = graphics::GetDevice();
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
				for (int i = 0; i < arraysize(textureStreamingFeedbackBuffer_readback); ++i)
				{
					device->CreateBuffer(&desc, nullptr, &textureStreamingFeedbackBuffer_readback[i]);
					device->SetName(&textureStreamingFeedbackBuffer_readback[i], "GSceneDetails::textureStreamingFeedbackBuffer_readback");
				}
			}
			textureStreamingFeedbackMapped = (const uint32_t*)textureStreamingFeedbackBuffer_readback[pingpong_buffer_index].mapped_data;

			// 4. Occlusion culling read: (Non Thread Task)
			//	per each MeshInstance
			if (isOcclusionCullingEnabled && !isFreezeCullingCameraEnabled)
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
			jobsystem::Wait(ctx); // dependencies
			// Scan mesh subset counts and skinning data sizes to allocate GPU geometry data:
			// TODO

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
		geometryArraySize = scene_->GetGeometryPrimitivesAllocatorSize();
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
		instanceResLookupSize = scene_->GetRenderableResLookupAllocatorSize();
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

		RunGeometryUpdateSystem(ctx);
		RunMaterialUpdateSystem(ctx);

		jobsystem::Wait(ctx); // dependencies

		RunRenderableUpdateSystem(ctx);
		//RunCameraUpdateSystem(ctx); .. in render function
		//RunProbeUpdateSystem(ctx); .. future feature
		//RunParticleUpdateSystem(ctx); .. future feature
		//RunSpriteUpdateSystem(ctx); .. future feature
		//RunFontUpdateSystem(ctx); .. future feature

		jobsystem::Wait(ctx); // dependencies

		// Meshlet buffer:
		uint32_t meshletCount = meshletAllocator.load();
		if (meshletBuffer.desc.size < meshletCount * sizeof(ShaderMeshlet))
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(ShaderMeshlet);
			desc.size = desc.stride * meshletCount * 2; // *2 to grow fast
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
			bool success = device->CreateBuffer(&desc, nullptr, &meshletBuffer);
			assert(success);
			device->SetName(&meshletBuffer, "meshletBuffer");
		}

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
		}
		else
		{
			shaderscene.instancebuffer = device->GetDescriptorIndex(&instanceBuffer, SubresourceType::SRV);
			shaderscene.geometrybuffer = device->GetDescriptorIndex(&geometryBuffer, SubresourceType::SRV);
			shaderscene.materialbuffer = device->GetDescriptorIndex(&materialBuffer, SubresourceType::SRV);
			shaderscene.instanceResLookupBuffer = device->GetDescriptorIndex(&instanceResLookupBuffer, SubresourceType::SRV);
		}			
		shaderscene.meshletbuffer = device->GetDescriptorIndex(&meshletBuffer, SubresourceType::SRV);
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
		meshletBuffer = {};
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