#include "PluginInterface.h"
#include "Renderer.h"

#include "Shaders/ShaderInterop.h"
#include "Components/GComponents.h"
#include "Utils/JobSystem.h"
#include "Utils/Timer.h"
#include "Utils/Backlog.h"
#include "Utils/EventHandler.h"
#include "Utils/Spinlock.h"
#include "Utils/Profiler.h"
#include "Libs/Math.h"
#include "Libs/PrimitiveHelper.h"
#include "ThirdParty/RectPacker.h"

namespace vz::common
{
	InputLayout			inputLayouts[ILTYPE_COUNT];
	RasterizerState		rasterizers[RSTYPE_COUNT];
	DepthStencilState	depthStencils[DSSTYPE_COUNT];
	BlendState			blendStates[BSTYPE_COUNT];
	Shader				shaders[SHADERTYPE_COUNT];
	GPUBuffer			buffers[BUFFERTYPE_COUNT];
	Sampler				samplers[SAMPLER_COUNT];

	PipelineState		PSO_debug[DEBUGRENDERING_COUNT];
	PipelineState		PSO_mesh[RENDERPASS_COUNT];
	PipelineState		PSO_wireframe;
}

namespace vz::renderer
{
	using namespace primitive;

	struct View
	{
		// User fills these:
		uint8_t layerMask = ~0;
		const Scene* scene = nullptr;
		const CameraComponent* camera = nullptr;
		enum FLAGS
		{
			EMPTY = 0,
			ALLOW_RENDERABLES = 1 << 0,
			ALLOW_LIGHTS = 1 << 1,
			//ALLOW_DECALS = 1 << 2,
			//ALLOW_ENVPROBES = 1 << 3,
			//ALLOW_EMITTERS = 1 << 4,
			//ALLOW_OCCLUSION_CULLING = 1 << 5,
			//ALLOW_SHADOW_ATLAS_PACKING = 1 << 6,

			ALLOW_EVERYTHING = ~0u
		};
		uint32_t flags = EMPTY;

		// vz::renderer::UpdateVisibility() fills these:
		primitive::Frustum frustum;
		std::vector<uint32_t> visibleRenderables;
		//std::vector<uint32_t> visibleDecals;
		//std::vector<uint32_t> visibleEnvProbes;
		//std::vector<uint32_t> visibleEmitters;
		std::vector<uint32_t> visibleLights;
		//rectpacker::State shadowPacker;
		//std::vector<rectpacker::Rect> visibleLightShadowRects;

		std::atomic<uint32_t> renderableCounter;
		std::atomic<uint32_t> lightCounter;

		vz::SpinLock locker;
		float closestRefPlane = std::numeric_limits<float>::max();
		std::atomic_bool volumetricLightRequest{ false };

		void Clear()
		{
			visibleRenderables.clear();
			visibleLights.clear();
			//visibleDecals.clear();
			//visibleEnvProbes.clear();
			//visibleEmitters.clear();

			renderableCounter.store(0);
			lightCounter.store(0);

			closestRefPlane = std::numeric_limits<float>::max();
			volumetricLightRequest.store(false);
		}
		bool IsRequestedVolumetricLights() const
		{
			return volumetricLightRequest.load();
		}
	};

	// must be called after scene->update()
	void UpdateView(View& vis)
	{
		// Perform parallel frustum culling and obtain closest reflector:
		jobsystem::context ctx;
		auto range = profiler::BeginRangeCPU("Frustum Culling");

		assert(vis.scene != nullptr); // User must provide a scene!
		assert(vis.camera != nullptr); // User must provide a camera!

		// The parallel frustum culling is first performed in shared memory, 
		//	then each group writes out it's local list to global memory
		//	The shared memory approach reduces atomics and helps the list to remain
		//	more coherent (less randomly organized compared to original order)
		static const uint32_t groupSize = 64;
		static const size_t sharedmemory_size = (groupSize + 1) * sizeof(uint32_t); // list + counter per group

		// Initialize visible indices:
		vis.Clear();

		// TODO : add frustum culling processs
		//if (!GetFreezeCullingCameraEnabled()) // just for debug
		{
			vis.frustum = vis.camera->GetFrustum();
		}
		//if (!GetOcclusionCullingEnabled() || GetFreezeCullingCameraEnabled())
		//{
		//	vis.flags &= ~View::ALLOW_OCCLUSION_CULLING;
		//}

		
		if (vis.flags & View::ALLOW_LIGHTS)
		{
			// Cull lights:
			const uint32_t light_loop = (uint32_t)vis.scene->GetLightCount();
			vis.visibleLights.resize(light_loop);
			//vis.visibleLightShadowRects.clear();
			//vis.visibleLightShadowRects.resize(light_loop);
			jobsystem::Dispatch(ctx, light_loop, groupSize, [&](jobsystem::JobArgs args) {

				const std::vector<Entity>& light_entities = vis.scene->GetLightEntities();
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
					uint32_t prev_count = vis.lightCounter.fetch_add(group_count);
					for (uint32_t i = 0; i < group_count; ++i)
					{
						vis.visibleLights[prev_count + i] = group_list[i];
					}
				}

				}, sharedmemory_size);
		}

		if (vis.flags & View::ALLOW_RENDERABLES)
		{
			// Cull objects:
			const uint32_t renderable_loop = (uint32_t)vis.scene->GetRenderableCount();
			vis.visibleRenderables.resize(renderable_loop);
			jobsystem::Dispatch(ctx, renderable_loop, groupSize, [&](jobsystem::JobArgs args) {

				const std::vector<Entity>& renderable_entities = vis.scene->GetRenderableEntities();
				Entity entity = renderable_entities[args.jobIndex];
				const RenderableComponent& renderable = *compfactory::GetRenderableComponent(entity);
				assert(!renderable.IsDirty());

				// Setup stream compaction:
				uint32_t& group_count = *(uint32_t*)args.sharedmemory;
				uint32_t* group_list = (uint32_t*)args.sharedmemory + 1;
				if (args.isFirstJobInGroup)
				{
					group_count = 0; // first thread initializes local counter
				}

				const AABB& aabb = renderable.GetAABB();

				if ((aabb.layerMask & vis.layerMask) && vis.frustum.CheckBoxFast(aabb))
				{
					// Local stream compaction:
					group_list[group_count++] = args.jobIndex;

					//Scene::OcclusionResult& occlusion_result = vis.scene->occlusion_results_objects[args.jobIndex];
					//bool occluded = false;
					//if (vis.flags & View::ALLOW_OCCLUSION_CULLING)
					//{
					//	occluded = occlusion_result.IsOccluded();
					//}
					//
					//if (vis.flags & View::ALLOW_OCCLUSION_CULLING)
					//{
					//	if (renderable.IsRenderable() && occlusion_result.occlusionQueries[vis.scene->queryheap_idx] < 0)
					//	{
					//		if (aabb.intersects(vis.camera->Eye))
					//		{
					//			// camera is inside the instance, mark it as visible in this frame:
					//			occlusion_result.occlusionHistory |= 1;
					//		}
					//		else
					//		{
					//			occlusion_result.occlusionQueries[vis.scene->queryheap_idx] = vis.scene->queryAllocator.fetch_add(1); // allocate new occlusion query from heap
					//		}
					//	}
					//}
				}

				// Global stream compaction:
				if (args.isLastJobInGroup && group_count > 0)
				{
					uint32_t prev_count = vis.renderableCounter.fetch_add(group_count);
					for (uint32_t i = 0; i < group_count; ++i)
					{
						vis.visibleRenderables[prev_count + i] = group_list[i];
					}
				}

				}, sharedmemory_size);
		}

		jobsystem::Wait(ctx);

		// finalize stream compaction: (memory safe)
		vis.visibleRenderables.resize((size_t)vis.renderableCounter.load());
		vis.visibleLights.resize((size_t)vis.lightCounter.load());
		
		profiler::EndRange(range); // Frustum Culling
	}

	const Sampler* GetSampler(SAMPLERTYPES id)
	{
		return &common::samplers[id];
	}
	const Shader* GetShader(SHADERTYPE id)
	{
		return &common::shaders[id];
	}
	const InputLayout* GetInputLayout(ILTYPES id)
	{
		return &common::inputLayouts[id];
	}
	const RasterizerState* GetRasterizerState(RSTYPES id)
	{
		return &common::rasterizers[id];
	}
	const DepthStencilState* GetDepthStencilState(DSSTYPES id)
	{
		return &common::depthStencils[id];
	}
	const BlendState* GetBlendState(BSTYPES id)
	{
		return &common::blendStates[id];
	}
	const GPUBuffer* GetBuffer(BUFFERTYPES id)
	{
		return &common::buffers[id];
	}
}

namespace vz
{
	const uint32_t SMALL_SUBTASK_GROUPSIZE = 64u;

	struct GSceneDetails : GScene
	{
		GSceneDetails(Scene* scene) : GScene(scene) {}

		// note all GPU resources (their pointers) are managed by
		//  ComPtr or 
		//  RAII (Resource Acquisition Is Initialization) patterns

		// * This renderer plugin is based on Bindless Graphics 
		//	(https://developer.download.nvidia.com/opengl/tutorials/bindless_graphics.pdf)
		
		float deltaTime = 0.f;
		std::vector<Entity> renderableEntities; // cached (non enclosing for jobsystem)
		std::vector<Entity> lightEntities; // cached (non enclosing for jobsystem)
		std::vector<Entity> geometryEntities; // cached (non enclosing for jobsystem)
		std::vector<Entity> materialEntities; // cached (non enclosing for jobsystem)

		// Separate stream of world matrices:
		std::vector<XMFLOAT4X4> matrixRenderables;
		std::vector<XMFLOAT4X4> matrixRenderablesPrev;

		const bool occlusionQueryEnabled = false;
		const bool cameraFreezeCullingEnabled = false;

		graphics::GraphicsDevice* device;
		// Instances for bindless renderables:
		//	contains in order:
		//		1) renderables (normal meshes)
		size_t instanceArraySize = 0;
		graphics::GPUBuffer instanceUploadBuffer[graphics::GraphicsDevice::GetBufferCount()]; // dynamic GPU-usage
		graphics::GPUBuffer instanceBuffer;	// default GPU-usage
		ShaderMeshInstance* instanceArrayMapped = nullptr; // CPU-access buffer pointer for instanceUploadBuffer[%2]

		// Geometries for bindless visiblity indexing:
		//	contains in order:
		//		1) # of primitive parts
		//		2) emitted particles * 1
		graphics::GPUBuffer geometryUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];
		ShaderGeometry* geometryArrayMapped = nullptr;
		size_t geometryArraySize = 0;
		graphics::GPUBuffer geometryBuffer;
		std::atomic<uint32_t> geometryAllocator{ 0 };

		// Materials for bindless visibility indexing:
		size_t materialArraySize = 0;
		graphics::GPUBuffer materialUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];
		graphics::GPUBuffer materialBuffer;
		graphics::GPUBuffer textureStreamingFeedbackBuffer;	// a sinlge UINT
		graphics::GPUBuffer textureStreamingFeedbackBuffer_readback[graphics::GraphicsDevice::GetBufferCount()];
		const uint32_t* textureStreamingFeedbackMapped = nullptr;
		ShaderMaterial* materialArrayMapped = nullptr;

		// Occlusion query state:
		struct OcclusionResult
		{
			int occlusionQueries[graphics::GraphicsDevice::GetBufferCount()];
			// occlusion result history bitfield (32 bit->32 frame history)
			uint32_t occlusionHistory = ~0u;

			constexpr bool IsOccluded() const
			{
				// Perform a conservative occlusion test:
				// If it is visible in any frames in the history, it is determined visible in this frame
				// But if all queries failed in the history, it is occluded.
				// If it pops up for a frame after occluded, it is visible again for some frames
				return occlusionHistory == 0;
			}
		};
		mutable std::vector<OcclusionResult> occlusionResultsObjects;
		graphics::GPUQueryHeap queryHeap;
		graphics::GPUBuffer queryResultBuffer[graphics::GraphicsDevice::GetBufferCount()];
		graphics::GPUBuffer queryPredicationBuffer;
		uint32_t queryheapIdx = 0;
		mutable std::atomic<uint32_t> queryAllocator{ 0 };

		bool Update(const float dt) override;
		bool Destory() override;

		void RunMeshUpdateSystem(jobsystem::context& ctx);
		void RunMaterialUpdateSystem(jobsystem::context& ctx);
		void RunLightUpdateSystem(jobsystem::context& ctx);
		void RunRenderableUpdateSystem(jobsystem::context& ctx);
	};

	void GSceneDetails::RunMeshUpdateSystem(jobsystem::context& ctx)
	{
		jobsystem::Dispatch(ctx, (uint32_t)geometryEntities.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			Entity entity = geometryEntities[args.jobIndex];
			GGeometryComponent& geometry = *(GGeometryComponent*)compfactory::GetGeometryComponent(entity);

			if (geometry.soPosition.IsValid() && geometry.soPrev.IsValid())
			{
				std::swap(geometry.soPosition, geometry.soPrev);
			}

			if (geometryArrayMapped != nullptr)
			{
				ShaderGeometry shader_geometry;
				shader_geometry.Init();
				shader_geometry.ib = geometry.ib.descriptor_srv;
				if (geometry.soPosition.IsValid())
				{
					shader_geometry.vb_pos = geometry.soPosition.descriptor_srv;
				}
				else
				{
					shader_geometry.vb_pos = geometry.vbPosition.descriptor_srv;
				}
				if (geometry.soNormal.IsValid())
				{
					shader_geometry.vb_nor = geometry.soNormal.descriptor_srv;
				}
				else
				{
					shader_geometry.vb_nor = geometry.vbNormal.descriptor_srv;
				}
				shader_geometry.vb_col = geometry.vbColor.descriptor_srv;
				shader_geometry.vb_uvs = geometry.vbUVs.descriptor_srv;
				shader_geometry.vb_pre = geometry.soPrev.descriptor_srv;
				primitive::AABB aabb = geometry.GetAABB();
				shader_geometry.aabb_min = aabb._min;
				shader_geometry.aabb_max = aabb._max;
				shader_geometry.uv_range_min = shader_geometry.uv_range_min;
				shader_geometry.uv_range_max = shader_geometry.uv_range_max;
				shader_geometry.meshletCount = 0;

				const std::vector<GeometryComponent::Primitive>& primitives = geometry.GetPrimitives();
				uint index_offset_prev = 0;
				for (uint32_t part_index = 0, n = geometry.GetNumParts(); part_index < n; ++part_index)
				{
					const GeometryComponent::Primitive& part_prim = primitives[part_index];

					ShaderGeometry shader_geometry_part = shader_geometry;
					shader_geometry_part.indexOffset = index_offset_prev;
					index_offset_prev = shader_geometry_part.indexCount = part_prim.GetNumVertices();
					//shader_geometry.meshletCount += subsetGeometry.meshletCount;
					std::memcpy(geometryArrayMapped + geometry.geometryOffset + part_index, &shader_geometry_part, sizeof(shader_geometry_part));
				}
			}

		});
	}
	void GSceneDetails::RunMaterialUpdateSystem(jobsystem::context& ctx)
	{
		jobsystem::Dispatch(ctx, (uint32_t)materialEntities.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			Entity entity = materialEntities[args.jobIndex];
			GMaterialComponent& material = *(GMaterialComponent*)compfactory::GetMaterialComponent(entity);
 
			if (material.IsOutlineEnabled())
			{
				//material.engineStencilRef = STENCILREF_OUTLINE;
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

					// will add the material feature..
					const float roughness = 0.f;
					const float reflectance = 0.f;
					const float metalness = 0.f;
					const float refraction = 0.f;
					const float normalMapStrength = 0.f;
					const float parallaxOcclusionMapping = 0.f;
					const float alphaRef = 1.f;
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
						VUID texture_vuid = material.GetTextureVUID(i);
						GTextureComponent* texture_comp = nullptr;
						if (texture_vuid != INVALID_VUID)
						{
							Entity material_entity = compfactory::GetEntityByVUID(texture_vuid);
							texture_comp = (GTextureComponent*)compfactory::GetTextureComponent(material_entity);
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
						VUID texture_vuid = material.GetTextureVUID(slot);
						if (texture_vuid != INVALID_VUID)
						{
							Entity material_entity = compfactory::GetEntityByVUID(texture_vuid);
							GTextureComponent* texture_comp = (GTextureComponent*)compfactory::GetTextureComponent(material_entity);
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
		// GPUs
		jobsystem::Dispatch(ctx, (uint32_t)renderableEntities.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			Entity entity = renderableEntities[args.jobIndex];
			GRenderableComponent& renderable = *(GRenderableComponent*)compfactory::GetRenderableComponent(entity);
			TransformComponent* transform = compfactory::GetTransformComponent(entity);

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

			uint32_t layerMask = ~0;

			if (renderable.IsValid())
			{
				// These will only be valid for a single frame:
				const GGeometryComponent& geometry = *(GGeometryComponent*)compfactory::GetGeometryComponent(renderable.GetGeometry());

				// TODO:
				//	* wetmap looks useful in our rendering purposes
				//	* To allow this, the logic neeeds to be modified
				//		1. wetmap option must be checked in the materials
				//		2. graphics::GPUBuffer GSceneDetails::wetmap
				//if (renderable.IsWetmapEnabled() && !renderable.wetmap.IsValid())
				//{
				//	GPUBufferDesc desc;
				//	desc.size = geometry.vertex_positions.size() * sizeof(uint16_t);
				//	desc.format = Format::R16_UNORM;
				//	desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				//	device->CreateBuffer(&desc, nullptr, &renderable.wetmap);
				//	device->SetName(&renderable.wetmap, "wetmap");
				//	renderable.wetmap_cleared = false;
				//}
				//else if (!renderable.IsWetmapEnabled() && renderable.wetmap.IsValid())
				//{
				//	renderable.wetmap = {};
				//}

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

				uint32_t first_part = 0;
				uint32_t last_part = 0;
				for (uint32_t subsetIndex = first_part; subsetIndex < last_part; ++subsetIndex)
				{
					const GeometryComponent::Primitive* part = geometry.GetPrimitive(subsetIndex);
					Entity material_entity = renderable.GetMaterial(subsetIndex);
					const MaterialComponent* material = compfactory::GetMaterialComponent(material_entity);
					assert(part && material);

					sort_bits.bits.tessellation |= material->IsTesellated();
					sort_bits.bits.doublesided |= material->IsDoubleSided();

					sort_bits.bits.shadertype |= 1 << SCU32(material->GetShaderType());
					sort_bits.bits.blendmode |= 1 << SCU32(material->GetBlendMode());
					sort_bits.bits.alphatest |= material->IsAlphaTestEnabled();

					//int customshader = material->GetCustomShaderID();
					//if (customshader >= 0)
					//{
					//	sort_bits.bits.customshader |= 1 << customshader;
					//}
				}

				renderable.sortBits = sort_bits.value;

				// Create GPU instance data:
				ShaderMeshInstance inst;
				inst.Init();
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

				primitive::AABB aabb = renderable.GetAABB();

				inst.uid = entity;
				inst.baseGeometryOffset = geometry.geometryOffset;
				inst.baseGeometryCount = (uint)geometry.GetNumParts();
				inst.geometryOffset = inst.baseGeometryOffset + first_part;
				inst.geometryCount = last_part - first_part;
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
	void GSceneDetails::RunLightUpdateSystem(jobsystem::context& ctx)
	{
		// GPUs
	}

	bool GSceneDetails::Update(const float dt)
	{
		deltaTime = dt;
		jobsystem::context ctx;

		device = GetGraphicsDevice();

		renderableEntities = scene_->GetRenderableEntities();
		size_t num_renderables = renderableEntities.size();
		
		lightEntities = scene_->GetLightEntities();
		size_t num_lights = lightEntities.size();
		
		materialEntities = scene_->GetMaterialEntities();
		size_t num_materials = materialEntities.size();

		geometryEntities = scene_->GetGeometryEntities();
		size_t num_geometries = geometryEntities.size();

		uint32_t pingpong_buffer_index = device->GetBufferIndex();

		// GPU Setting-up for Update IF necessary
		{
			// 1. dynamic rendering (such as particles and terrain, cloud...) kickoff
			// TODO

			// 2. constant (pingpong) buffers for renderables (Non Thread Task)
			instanceArraySize = num_renderables; // instance is renderable
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
				GGeometryComponent* geometry = (GGeometryComponent*)compfactory::GetGeometryComponent(geometryEntities[args.jobIndex]);
				geometry->geometryOffset = geometryAllocator.fetch_add((uint32_t)geometry->GetNumParts());
				});

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
				device->SetName(&geometryBuffer, "Scene::geometryBuffer");

				// Upload buffer shouldn't be used by shaders with Non-UMA:
				desc.bind_flags = BindFlag::NONE;
				desc.misc_flags = ResourceMiscFlag::NONE;
			}

			desc.usage = Usage::UPLOAD;
			for (int i = 0; i < arraysize(geometryUploadBuffer); ++i)
			{
				device->CreateBuffer(&desc, nullptr, &geometryUploadBuffer[i]);
				device->SetName(&geometryUploadBuffer[i], "Scene::geometryUploadBuffer");
			}
		}
		geometryArrayMapped = (ShaderGeometry*)geometryUploadBuffer[pingpong_buffer_index].mapped_data;

		RunMeshUpdateSystem(ctx);
		RunMaterialUpdateSystem(ctx);

		jobsystem::Wait(ctx); // dependencies

		RunRenderableUpdateSystem(ctx);
		//RunCameraUpdateSystem(ctx); .. in render function
		//RunProbeUpdateSystem(ctx); .. future feature
		RunLightUpdateSystem(ctx);
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

		// content updates...

		return true;
	}

	bool GSceneDetails::Destory()
	{
		return true;
	}

}

namespace vz
{
	struct GRenderPath3DDetails : GRenderPath3D
	{
		GRenderPath3DDetails(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal) : GRenderPath3D(swapChain, rtRenderFinal) {}

		// resources associated with render target buffers and textures

		bool ResizeCanvas() override; // must delete all canvas-related resources and re-create
		bool Render() override;
		bool Destory() override;
	};

	bool GRenderPath3DDetails::ResizeCanvas()
	{
		return true;
	}

	bool GRenderPath3DDetails::Render()
	{
		GraphicsDevice*& device = GetDevice();
		CommandList cmd = device->BeginCommandList();
		//RenderPassImage rp[] = {
		//	RenderPassImage::RenderTarget(&rtOut, RenderPassImage::LoadOp::CLEAR),
		//
		//};
		//graphicsDevice_->RenderPassBegin(rp, arraysize(rp), cmd);
		device->RenderPassBegin(&swapChain_, cmd);
		device->RenderPassEnd(cmd);
		device->SubmitCommandLists();

		return true;
	}

	bool GRenderPath3DDetails::Destory()
	{
		return true;
	}

}

namespace vz
{
	GScene* NewGScene(Scene* scene)
	{
		return new GSceneDetails(scene);
	}

	GRenderPath3D* NewGRenderPath(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
	{
		return new GRenderPath3DDetails(swapChain, rtRenderFinal);
	}

	bool InitRendererShaders()
	{
		Timer timer;

		initializer::SetUpStates();
		initializer::LoadBuffers();

		//static eventhandler::Handle handle2 = eventhandler::Subscribe(eventhandler::EVENT_RELOAD_SHADERS, [](uint64_t userdata) { LoadShaders(); });
		//LoadShaders();

		backlog::post("renderer Initialized (" + std::to_string((int)std::round(timer.elapsed())) + " ms)", backlog::LogLevel::Info);
		//initialized.store(true);
		return true;
	}

}

