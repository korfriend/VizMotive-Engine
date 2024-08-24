#pragma once
#include "../Common/CommonInclude.h"
#include "../Common/Archive.h"
#include "../Utils/ECS.h"

#include "../HighAPIs/VzEnums.h"
using namespace vzenum;

#include <string>

namespace vz::component
{
	// ECS components
	// 
	// node 

	struct NameComponent
	{
		std::string name;

		inline void operator=(const std::string& str) { name = str; }
		inline void operator=(std::string&& str) { name = std::move(str); }
		inline bool operator==(const std::string& str) const { return name.compare(str) == 0; }

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct TransformComponent
	{
		bool isMatrixAutoUpdate = true;
		XMFLOAT3 scale = XMFLOAT3(1, 1, 1);
		XMFLOAT4 rotation = XMFLOAT4(0, 0, 0, 1);	// this is a quaternion
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		XMFLOAT4X4 local = vz::math::IDENTITY_MATRIX;

		// Non-serialized attributes:

		// The world matrix can be computed from local scale, rotation, translation
		//	- by calling UpdateTransform()
		//	- or by calling SetDirty() and letting the TransformUpdateSystem handle the updating
		XMFLOAT4X4 world = vz::math::IDENTITY_MATRIX;

		XMFLOAT3 GetPosition() const;
		XMFLOAT4 GetRotation() const;
		XMFLOAT3 GetScale() const;
		XMFLOAT3 GetForward() const;
		XMFLOAT3 GetUp() const;
		XMFLOAT3 GetRight() const;

		void SetPosition();
		void SetScale(const XMFLOAT3 scale);
		void SetRotation(const XMFLOAT3 rotAngles, const EULER_ORDER order = EULER_ORDER::XYZ);
		void SetQuaternion(const XMFLOAT4 q, const EULER_ORDER order = EULER_ORDER::XYZ);

		void UpdateMatrix();

		// Apply the world matrix to the local space. This overwrites scale, rotation, position
		void ApplyTransform();

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct HierarchyComponent
	{
		vz::ecs::Entity parentID = vz::ecs::INVALID_ENTITY;

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	// resources

	struct MaterialComponent
	{
		

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct GeometryComponent
	{
		struct Primitive {
			VertexBuffer* vertices = nullptr;
			IndexBuffer* indices = nullptr;
			Aabb aabb; // object-space bounding box
			UvMap uvmap; // mapping from each glTF UV set to either UV0 or UV1 (8 bytes)
			MorphTargetBuffer* morphTargetBuffer = nullptr;
			uint32_t morphTargetOffset;
			std::vector<int> slotIndices;

			PrimitiveType ptype = PrimitiveType::TRIANGLES;
		};

		std::vector<VzPrimitive> primitives_;

		std::vector<char> cacheVB;
		std::vector<char> cacheIB;
		std::vector<char> cacheMTB;
	public:
		bool isSystem = false;
		gltfio::FilamentAsset* assetOwner = nullptr; // has ownership
		filament::Aabb aabb;
		void Set(const std::vector<VzPrimitive>& primitives);
		std::vector<VzPrimitive>* Get();



		vz::vector<XMFLOAT3> vertex_positions;
		vz::vector<XMFLOAT3> vertex_normals;
		vz::vector<XMFLOAT4> vertex_tangents;
		vz::vector<XMFLOAT2> vertex_uvset_0;
		vz::vector<XMFLOAT2> vertex_uvset_1;
		vz::vector<XMUINT4> vertex_boneindices;
		vz::vector<XMFLOAT4> vertex_boneweights;
		vz::vector<XMFLOAT2> vertex_atlas;
		vz::vector<uint32_t> vertex_colors;
		vz::vector<uint8_t> vertex_windweights;
		vz::vector<uint32_t> indices;

		struct MeshSubset
		{
			wi::ecs::Entity materialID = wi::ecs::INVALID_ENTITY;
			uint32_t indexOffset = 0;
			uint32_t indexCount = 0;

			// Non-serialized attributes:
			uint32_t materialIndex = 0;
		};
		wi::vector<MeshSubset> subsets;

		float tessellationFactor = 0.0f;
		wi::ecs::Entity armatureID = wi::ecs::INVALID_ENTITY;

		struct MorphTarget
		{
			wi::vector<XMFLOAT3> vertex_positions;
			wi::vector<XMFLOAT3> vertex_normals;
			wi::vector<uint32_t> sparse_indices_positions; // optional, these can be used to target vertices indirectly
			wi::vector<uint32_t> sparse_indices_normals; // optional, these can be used to target vertices indirectly
			float weight = 0;

			// Non-serialized attributes:
			uint64_t offset_pos = ~0ull;
			uint64_t offset_nor = ~0ull;
		};
		wi::vector<MorphTarget> morph_targets;

		uint32_t subsets_per_lod = 0; // this needs to be specified if there are multiple LOD levels

		// Non-serialized attributes:
		vz::primitive::AABB aabb;
		vz::graphics::GPUBuffer generalBuffer; // index buffer + all static vertex buffers
		vz::graphics::GPUBuffer streamoutBuffer; // all dynamic vertex buffers
		struct BufferView
		{
			uint64_t offset = ~0ull;
			uint64_t size = 0ull;
			int subresource_srv = -1;
			int descriptor_srv = -1;
			int subresource_uav = -1;
			int descriptor_uav = -1;

			constexpr bool IsValid() const
			{
				return offset != ~0ull;
			}
		};
		BufferView ib;
		BufferView vb_pos_wind;
		BufferView vb_nor;
		BufferView vb_tan;
		BufferView vb_uvs;
		BufferView vb_atl;
		BufferView vb_col;
		BufferView vb_bon;
		BufferView vb_mor;
		BufferView so_pos;
		BufferView so_nor;
		BufferView so_tan;
		BufferView so_pre;
		uint32_t geometryOffset = 0;
		uint32_t meshletCount = 0;
		uint32_t active_morph_count = 0;
		uint32_t morphGPUOffset = 0;
		XMFLOAT2 uv_range_min = XMFLOAT2(0, 0);
		XMFLOAT2 uv_range_max = XMFLOAT2(1, 1);

		wi::vector<wi::graphics::RaytracingAccelerationStructure> BLASes; // one BLAS per LOD
		enum BLAS_STATE
		{
			BLAS_STATE_NEEDS_REBUILD,
			BLAS_STATE_NEEDS_REFIT,
			BLAS_STATE_COMPLETE,
		};
		mutable BLAS_STATE BLAS_state = BLAS_STATE_NEEDS_REBUILD;

		wi::vector<wi::primitive::AABB> bvh_leaf_aabbs;
		wi::BVH bvh;

		inline void SetRenderable(bool value) { if (value) { _flags |= RENDERABLE; } else { _flags &= ~RENDERABLE; } }
		inline void SetDoubleSided(bool value) { if (value) { _flags |= DOUBLE_SIDED; } else { _flags &= ~DOUBLE_SIDED; } }
		inline void SetDoubleSidedShadow(bool value) { if (value) { _flags |= DOUBLE_SIDED_SHADOW; } else { _flags &= ~DOUBLE_SIDED_SHADOW; } }
		inline void SetDynamic(bool value) { if (value) { _flags |= DYNAMIC; } else { _flags &= ~DYNAMIC; } }

		// Enable disable CPU-side BVH acceleration structure
		//	true: BVH will be built immediately if it doesn't exist yet
		//	false: BVH will be deleted immediately if it exists
		inline void SetBVHEnabled(bool value) { if (value) { _flags |= BVH_ENABLED; if (!bvh.IsValid()) { BuildBVH(); } } else { _flags &= ~BVH_ENABLED; bvh = {}; bvh_leaf_aabbs.clear(); } }

		// Disable quantization of position GPU data. You can use this if you notice inaccuracy in positions.
		//	This should be enabled for connecting meshes like terrain chunks if their AABB is not consistent with each other
		inline void SetQuantizedPositionsDisabled(bool value) { if (value) { _flags |= QUANTIZED_POSITIONS_DISABLED; } else { _flags &= ~QUANTIZED_POSITIONS_DISABLED; } }

		inline bool IsRenderable() const { return _flags & RENDERABLE; }
		inline bool IsDoubleSided() const { return _flags & DOUBLE_SIDED; }
		inline bool IsDoubleSidedShadow() const { return _flags & DOUBLE_SIDED_SHADOW; }
		inline bool IsDynamic() const { return _flags & DYNAMIC; }
		inline bool IsBVHEnabled() const { return _flags & BVH_ENABLED; }
		inline bool IsQuantizedPositionsDisabled() const { return _flags & QUANTIZED_POSITIONS_DISABLED; }

		inline float GetTessellationFactor() const { return tessellationFactor; }
		inline wi::graphics::IndexBufferFormat GetIndexFormat() const { return wi::graphics::GetIndexBufferFormat((uint32_t)vertex_positions.size()); }
		inline size_t GetIndexStride() const { return GetIndexFormat() == wi::graphics::IndexBufferFormat::UINT32 ? sizeof(uint32_t) : sizeof(uint16_t); }
		inline bool IsSkinned() const { return armatureID != wi::ecs::INVALID_ENTITY; }
		inline uint32_t GetLODCount() const { return subsets_per_lod == 0 ? 1 : ((uint32_t)subsets.size() / subsets_per_lod); }
		inline void GetLODSubsetRange(uint32_t lod, uint32_t& first_subset, uint32_t& last_subset) const
		{
			first_subset = 0;
			last_subset = (uint32_t)subsets.size();
			if (subsets_per_lod > 0)
			{
				lod = std::min(lod, GetLODCount() - 1);
				first_subset = subsets_per_lod * lod;
				last_subset = first_subset + subsets_per_lod;
			}
		}

		// Deletes all GPU resources
		void DeleteRenderData();

		// Recreates GPU resources for index/vertex buffers
		void CreateRenderData();
		void CreateStreamoutRenderData();
		void CreateRaytracingRenderData();

		// Rebuilds CPU-side BVH acceleration structure
		void BuildBVH();

		size_t GetMemoryUsageCPU() const;
		size_t GetMemoryUsageGPU() const;
		size_t GetMemoryUsageBVH() const;

		enum COMPUTE_NORMALS
		{
			COMPUTE_NORMALS_HARD,		// hard face normals, can result in additional vertices generated
			COMPUTE_NORMALS_SMOOTH,		// smooth per vertex normals, this can remove/simplify geometry, but slow
			COMPUTE_NORMALS_SMOOTH_FAST	// average normals, vertex count will be unchanged, fast
		};
		void ComputeNormals(COMPUTE_NORMALS compute);
		void FlipCulling();
		void FlipNormals();
		void Recenter();
		void RecenterToBottom();
		wi::primitive::Sphere GetBoundingSphere() const;

		void Serialize(wi::Archive& archive, wi::ecs::EntitySerializer& seri);

		struct Vertex_POS10
		{
			uint32_t x : 10;
			uint32_t y : 10;
			uint32_t z : 10;
			uint32_t w : 2;

			constexpr void FromFULL(const wi::primitive::AABB& aabb, XMFLOAT3 pos, uint8_t wind)
			{
				pos = wi::math::InverseLerp(aabb._min, aabb._max, pos); // UNORM remap
				x = uint32_t(saturate(pos.x) * 1023.0f);
				y = uint32_t(saturate(pos.y) * 1023.0f);
				z = uint32_t(saturate(pos.z) * 1023.0f);
				w = uint32_t((float(wind) / 255.0f) * 3);
			}
			inline XMVECTOR LoadPOS(const wi::primitive::AABB& aabb) const
			{
				XMFLOAT3 v = GetPOS(aabb);
				return XMLoadFloat3(&v);
			}
			constexpr XMFLOAT3 GetPOS(const wi::primitive::AABB& aabb) const
			{
				XMFLOAT3 v = XMFLOAT3(
					float(x) / 1023.0f,
					float(y) / 1023.0f,
					float(z) / 1023.0f
				);
				return wi::math::Lerp(aabb._min, aabb._max, v);
			}
			constexpr uint8_t GetWind() const
			{
				return uint8_t((float(w) / 3.0f) * 255);
			}
			static constexpr wi::graphics::Format FORMAT = wi::graphics::Format::R10G10B10A2_UNORM;
		};
		struct Vertex_POS16
		{
			uint16_t x = 0;
			uint16_t y = 0;
			uint16_t z = 0;
			uint16_t w = 0;

			constexpr void FromFULL(const wi::primitive::AABB& aabb, XMFLOAT3 pos, uint8_t wind)
			{
				pos = wi::math::InverseLerp(aabb._min, aabb._max, pos); // UNORM remap
				x = uint16_t(pos.x * 65535.0f);
				y = uint16_t(pos.y * 65535.0f);
				z = uint16_t(pos.z * 65535.0f);
				w = uint16_t((float(wind) / 255.0f) * 65535.0f);
			}
			inline XMVECTOR LoadPOS(const wi::primitive::AABB& aabb) const
			{
				XMFLOAT3 v = GetPOS(aabb);
				return XMLoadFloat3(&v);
			}
			constexpr XMFLOAT3 GetPOS(const wi::primitive::AABB& aabb) const
			{
				XMFLOAT3 v = XMFLOAT3(
					float(x) / 65535.0f,
					float(y) / 65535.0f,
					float(z) / 65535.0f
				);
				return wi::math::Lerp(aabb._min, aabb._max, v);
			}
			constexpr uint8_t GetWind() const
			{
				return uint8_t((float(w) / 65535.0f) * 255);
			}
			static constexpr wi::graphics::Format FORMAT = wi::graphics::Format::R16G16B16A16_UNORM;
		};
		struct Vertex_POS32
		{
			float x = 0;
			float y = 0;
			float z = 0;

			constexpr void FromFULL(const XMFLOAT3& pos)
			{
				x = pos.x;
				y = pos.y;
				z = pos.z;
			}
			inline XMVECTOR LoadPOS() const
			{
				return XMVectorSet(x, y, z, 1);
			}
			constexpr XMFLOAT3 GetPOS() const
			{
				return XMFLOAT3(x, y, z);
			}
			static constexpr wi::graphics::Format FORMAT = wi::graphics::Format::R32G32B32_FLOAT;
		};
		struct Vertex_POS32W
		{
			float x = 0;
			float y = 0;
			float z = 0;
			float w = 0;

			constexpr void FromFULL(const XMFLOAT3& pos, uint8_t wind)
			{
				x = pos.x;
				y = pos.y;
				z = pos.z;
				w = float(wind) / 255.0f;
			}
			inline XMVECTOR LoadPOS() const
			{
				return XMVectorSet(x, y, z, 1);
			}
			constexpr XMFLOAT3 GetPOS() const
			{
				return XMFLOAT3(x, y, z);
			}
			constexpr uint8_t GetWind() const
			{
				return uint8_t(w * 255);
			}
			static constexpr wi::graphics::Format FORMAT = wi::graphics::Format::R32G32B32A32_FLOAT;
		};
		wi::graphics::Format position_format = Vertex_POS16::FORMAT; // CreateRenderData() will choose the appropriate format

		struct Vertex_TEX
		{
			uint16_t x = 0;
			uint16_t y = 0;

			constexpr void FromFULL(const XMFLOAT2& uv, const XMFLOAT2& uv_range_min = XMFLOAT2(0, 0), const XMFLOAT2& uv_range_max = XMFLOAT2(1, 1))
			{
				x = uint16_t(wi::math::InverseLerp(uv_range_min.x, uv_range_max.x, uv.x) * 65535.0f);
				y = uint16_t(wi::math::InverseLerp(uv_range_min.y, uv_range_max.y, uv.y) * 65535.0f);
			}
			static constexpr wi::graphics::Format FORMAT = wi::graphics::Format::R16G16_UNORM;
		};
		struct Vertex_UVS
		{
			Vertex_TEX uv0;
			Vertex_TEX uv1;
			static constexpr wi::graphics::Format FORMAT = wi::graphics::Format::R16G16B16A16_UNORM;
		};
		struct Vertex_BON
		{
			uint16_t ind0 = 0;
			uint16_t ind1 = 0;
			uint16_t ind2 = 0;
			uint16_t ind3 = 0;

			uint16_t wei0 = 0;
			uint16_t wei1 = 1;
			uint16_t wei2 = 2;
			uint16_t wei3 = 3;

			constexpr void FromFULL(const XMUINT4& boneIndices, const XMFLOAT4& boneWeights)
			{
				ind0 = uint16_t(boneIndices.x);
				ind1 = uint16_t(boneIndices.y);
				ind2 = uint16_t(boneIndices.z);
				ind3 = uint16_t(boneIndices.w);

				wei0 = uint16_t(boneWeights.x * 65535.0f);
				wei1 = uint16_t(boneWeights.y * 65535.0f);
				wei2 = uint16_t(boneWeights.z * 65535.0f);
				wei3 = uint16_t(boneWeights.w * 65535.0f);
			}
			constexpr XMUINT4 GetInd_FULL() const
			{
				return XMUINT4(ind0, ind1, ind2, ind3);
			}
			constexpr XMFLOAT4 GetWei_FULL() const
			{
				return XMFLOAT4(
					float(wei0) / 65535.0f,
					float(wei1) / 65535.0f,
					float(wei2) / 65535.0f,
					float(wei3) / 65535.0f
				);
			}
		};
		struct Vertex_COL
		{
			uint32_t color = 0;
			static constexpr wi::graphics::Format FORMAT = wi::graphics::Format::R8G8B8A8_UNORM;
		};
		struct Vertex_NOR
		{
			int8_t x = 0;
			int8_t y = 0;
			int8_t z = 0;
			int8_t w = 0;

			void FromFULL(const XMFLOAT3& nor)
			{
				XMVECTOR N = XMLoadFloat3(&nor);
				N = XMVector3Normalize(N);
				XMFLOAT3 n;
				XMStoreFloat3(&n, N);

				x = int8_t(n.x * 127.5f);
				y = int8_t(n.y * 127.5f);
				z = int8_t(n.z * 127.5f);
				w = 0;
			}
			inline XMFLOAT3 GetNOR() const
			{
				return XMFLOAT3(
					float(x) / 127.5f,
					float(y) / 127.5f,
					float(z) / 127.5f
				);
			}
			inline XMVECTOR LoadNOR() const
			{
				return XMVectorSet(
					float(x) / 127.5f,
					float(y) / 127.5f,
					float(z) / 127.5f,
					0
				);
			}
			static constexpr wi::graphics::Format FORMAT = wi::graphics::Format::R8G8B8A8_SNORM;
		};
		struct Vertex_TAN
		{
			int8_t x = 0;
			int8_t y = 0;
			int8_t z = 0;
			int8_t w = 0;

			void FromFULL(const XMFLOAT4& tan)
			{
				XMVECTOR T = XMLoadFloat4(&tan);
				T = XMVector3Normalize(T);
				XMFLOAT4 t;
				XMStoreFloat4(&t, T);
				t.w = tan.w;

				x = int8_t(t.x * 127.5f);
				y = int8_t(t.y * 127.5f);
				z = int8_t(t.z * 127.5f);
				w = int8_t(t.w * 127.5f);
			}
			inline XMFLOAT4 GetTAN() const
			{
				return XMFLOAT4(
					float(x) / 127.5f,
					float(y) / 127.5f,
					float(z) / 127.5f,
					float(w) / 127.5f
				);
			}
			static constexpr wi::graphics::Format FORMAT = wi::graphics::Format::R8G8B8A8_SNORM;
		};

	};

	struct TextureComponent
	{

	};

	// scene 

	struct RenderableComponent
	{
		enum FLAGS
		{
			EMPTY = 0,
			RENDERABLE = 1 << 0,
			CAST_SHADOW = 1 << 1,
			DYNAMIC = 1 << 2,
			_DEPRECATED_IMPOSTOR_PLACEMENT = 1 << 3,
			REQUEST_PLANAR_REFLECTION = 1 << 4,
			LIGHTMAP_RENDER_REQUEST = 1 << 5,
			LIGHTMAP_DISABLE_BLOCK_COMPRESSION = 1 << 6,
			FOREGROUND = 1 << 7,
			NOT_VISIBLE_IN_MAIN_CAMERA = 1 << 8,
			NOT_VISIBLE_IN_REFLECTIONS = 1 << 9,
		};
		uint32_t _flags = RENDERABLE | CAST_SHADOW;

		wi::ecs::Entity meshID = wi::ecs::INVALID_ENTITY;
		uint32_t cascadeMask = 0; // which shadow cascades to skip from lowest detail to highest detail (0: skip none, 1: skip first, etc...)
		uint32_t filterMask = 0;
		XMFLOAT4 color = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 emissiveColor = XMFLOAT4(1, 1, 1, 1);
		uint8_t userStencilRef = 0;
		float lod_distance_multiplier = 1;
		float draw_distance = std::numeric_limits<float>::max(); // object will begin to fade out at this distance to camera
		uint32_t lightmapWidth = 0;
		uint32_t lightmapHeight = 0;
		wi::vector<uint8_t> lightmapTextureData;
		uint32_t sort_priority = 0; // increase to draw earlier (currently 4 bits will be used)
		wi::vector<uint8_t> vertex_ao;
		float alphaRef = 1;

		// Non-serialized attributes:
		uint32_t filterMaskDynamic = 0;

		wi::graphics::Texture lightmap;
		mutable uint32_t lightmapIterationCount = 0;
		wi::graphics::GPUBuffer vb_ao;
		int vb_ao_srv = -1;

		XMFLOAT3 center = XMFLOAT3(0, 0, 0);
		float radius = 0;
		float fadeDistance = 0;

		uint32_t lod = 0;

		// these will only be valid for a single frame:
		uint32_t mesh_index = ~0u;
		uint32_t sort_bits = 0;

		inline void SetRenderable(bool value) { if (value) { _flags |= RENDERABLE; } else { _flags &= ~RENDERABLE; } }
		inline void SetCastShadow(bool value) { if (value) { _flags |= CAST_SHADOW; } else { _flags &= ~CAST_SHADOW; } }
		inline void SetDynamic(bool value) { if (value) { _flags |= DYNAMIC; } else { _flags &= ~DYNAMIC; } }
		inline void SetRequestPlanarReflection(bool value) { if (value) { _flags |= REQUEST_PLANAR_REFLECTION; } else { _flags &= ~REQUEST_PLANAR_REFLECTION; } }
		inline void SetLightmapRenderRequest(bool value) { if (value) { _flags |= LIGHTMAP_RENDER_REQUEST; } else { _flags &= ~LIGHTMAP_RENDER_REQUEST; } }
		inline void SetLightmapDisableBlockCompression(bool value) { if (value) { _flags |= LIGHTMAP_DISABLE_BLOCK_COMPRESSION; } else { _flags &= ~LIGHTMAP_DISABLE_BLOCK_COMPRESSION; } }
		// Foreground object will be rendered in front of regular objects
		inline void SetForeground(bool value) { if (value) { _flags |= FOREGROUND; } else { _flags &= ~FOREGROUND; } }
		// With this you can disable object rendering for main camera (DRAWSCENE_MAINCAMERA)
		inline void SetNotVisibleInMainCamera(bool value) { if (value) { _flags |= NOT_VISIBLE_IN_MAIN_CAMERA; } else { _flags &= ~NOT_VISIBLE_IN_MAIN_CAMERA; } }
		// With this you can disable object rendering for reflections
		inline void SetNotVisibleInReflections(bool value) { if (value) { _flags |= NOT_VISIBLE_IN_REFLECTIONS; } else { _flags &= ~NOT_VISIBLE_IN_REFLECTIONS; } }

		inline bool IsRenderable() const { return (_flags & RENDERABLE) && (GetTransparency() < 0.99f); }
		inline bool IsCastingShadow() const { return _flags & CAST_SHADOW; }
		inline bool IsDynamic() const { return _flags & DYNAMIC; }
		inline bool IsRequestPlanarReflection() const { return _flags & REQUEST_PLANAR_REFLECTION; }
		inline bool IsLightmapRenderRequested() const { return _flags & LIGHTMAP_RENDER_REQUEST; }
		inline bool IsLightmapDisableBlockCompression() const { return _flags & LIGHTMAP_DISABLE_BLOCK_COMPRESSION; }
		inline bool IsForeground() const { return _flags & FOREGROUND; }
		inline bool IsNotVisibleInMainCamera() const { return _flags & NOT_VISIBLE_IN_MAIN_CAMERA; }
		inline bool IsNotVisibleInReflections() const { return _flags & NOT_VISIBLE_IN_REFLECTIONS; }

		inline float GetTransparency() const { return 1 - color.w; }
		inline uint32_t GetFilterMask() const { return filterMask | filterMaskDynamic; }

		// User stencil value can be in range [0, 15]
		//	Values greater than 0 can be used to override userStencilRef of MaterialComponent
		inline void SetUserStencilRef(uint8_t value)
		{
			assert(value < 16);
			userStencilRef = value & 0x0F;
		}

		void ClearLightmap();
		void SaveLightmap(); // not thread safe if LIGHTMAP_BLOCK_COMPRESSION is enabled!
		void CompressLightmap(); // not thread safe if LIGHTMAP_BLOCK_COMPRESSION is enabled!

		void Serialize(wi::Archive& archive, wi::ecs::EntitySerializer& seri);

		void CreateRenderData();
		void DeleteRenderData();
		struct Vertex_AO
		{
			uint8_t value = 0;
			static constexpr wi::graphics::Format FORMAT = wi::graphics::Format::R8_UNORM;
		};
	};

	struct LightComponent
	{
		enum FLAGS
		{
			EMPTY = 0,
			CAST_SHADOW = 1 << 0,
			VOLUMETRICS = 1 << 1,
			VISUALIZER = 1 << 2,
			LIGHTMAPONLY_STATIC = 1 << 3,
			VOLUMETRICCLOUDS = 1 << 4,
		};
		uint32_t _flags = EMPTY;

		enum LightType
		{
			DIRECTIONAL = ENTITY_TYPE_DIRECTIONALLIGHT,
			POINT = ENTITY_TYPE_POINTLIGHT,
			SPOT = ENTITY_TYPE_SPOTLIGHT,
			//SPHERE = ENTITY_TYPE_SPHERELIGHT,
			//DISC = ENTITY_TYPE_DISCLIGHT,
			//RECTANGLE = ENTITY_TYPE_RECTANGLELIGHT,
			//TUBE = ENTITY_TYPE_TUBELIGHT,
			LIGHTTYPE_COUNT,
			ENUM_FORCE_UINT32 = 0xFFFFFFFF,
		};
		LightType type = POINT;

		XMFLOAT3 color = XMFLOAT3(1, 1, 1);
		float intensity = 1.0f; // Brightness of light in. The units that this is defined in depend on the type of light. Point and spot lights use luminous intensity in candela (lm/sr) while directional lights use illuminance in lux (lm/m2). https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual
		float range = 10.0f;
		float outerConeAngle = XM_PIDIV4;
		float innerConeAngle = 0; // default value is 0, means only outer cone angle is used
		float radius = 0.025f;
		float length = 0;

		wi::vector<float> cascade_distances = { 8,80,800 };
		wi::vector<std::string> lensFlareNames;

		int forced_shadow_resolution = -1; // -1: disabled, greater: fixed shadow map resolution

		// Non-serialized attributes:
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		XMFLOAT3 direction = XMFLOAT3(0, 1, 0);
		XMFLOAT4 rotation = XMFLOAT4(0, 0, 0, 1);
		XMFLOAT3 scale = XMFLOAT3(1, 1, 1);
		mutable int occlusionquery = -1;

		wi::vector<wi::Resource> lensFlareRimTextures;

		inline void SetCastShadow(bool value) { if (value) { _flags |= CAST_SHADOW; } else { _flags &= ~CAST_SHADOW; } }
		inline void SetVolumetricsEnabled(bool value) { if (value) { _flags |= VOLUMETRICS; } else { _flags &= ~VOLUMETRICS; } }
		inline void SetVisualizerEnabled(bool value) { if (value) { _flags |= VISUALIZER; } else { _flags &= ~VISUALIZER; } }
		inline void SetStatic(bool value) { if (value) { _flags |= LIGHTMAPONLY_STATIC; } else { _flags &= ~LIGHTMAPONLY_STATIC; } }
		inline void SetVolumetricCloudsEnabled(bool value) { if (value) { _flags |= VOLUMETRICCLOUDS; } else { _flags &= ~VOLUMETRICCLOUDS; } }

		inline bool IsCastingShadow() const { return _flags & CAST_SHADOW; }
		inline bool IsVolumetricsEnabled() const { return _flags & VOLUMETRICS; }
		inline bool IsVisualizerEnabled() const { return _flags & VISUALIZER; }
		inline bool IsStatic() const { return _flags & LIGHTMAPONLY_STATIC; }
		inline bool IsVolumetricCloudsEnabled() const { return _flags & VOLUMETRICCLOUDS; }

		inline float GetRange() const
		{
			float retval = range;
			retval = std::max(0.001f, retval);
			retval = std::min(retval, 65504.0f); // clamp to 16-bit float max value
			return retval;
		}

		inline void SetType(LightType val) { type = val; }
		inline LightType GetType() const { return type; }

		// Set energy amount with non physical light units (from before version 0.70.0):
		inline void BackCompatSetEnergy(float energy)
		{
			switch (type)
			{
			case wi::scene::LightComponent::POINT:
				intensity = energy * 20;
				break;
			case wi::scene::LightComponent::SPOT:
				intensity = energy * 200;
				break;
			default:
				break;
			}
		}

		void Serialize(wi::Archive& archive, wi::ecs::EntitySerializer& seri);
	};

	struct CameraComponent
	{
		enum FLAGS
		{
			EMPTY = 0,
			DIRTY = 1 << 0,
			CUSTOM_PROJECTION = 1 << 1,
		};
		uint32_t _flags = EMPTY;

		float width = 0.0f;
		float height = 0.0f;
		float zNearP = 0.1f;
		float zFarP = 5000.0f;
		float fov = XM_PI / 3.0f;
		float focal_length = 1;
		float aperture_size = 0;
		XMFLOAT2 aperture_shape = XMFLOAT2(1, 1);

		// Non-serialized attributes:
		XMFLOAT3 Eye = XMFLOAT3(0, 0, 0);
		XMFLOAT3 At = XMFLOAT3(0, 0, 1);
		XMFLOAT3 Up = XMFLOAT3(0, 1, 0);
		XMFLOAT3X3 rotationMatrix;
		XMFLOAT4X4 View, Projection, VP;
		wi::primitive::Frustum frustum;
		XMFLOAT4X4 InvView, InvProjection, InvVP;
		XMFLOAT2 jitter;
		XMFLOAT4 clipPlane = XMFLOAT4(0, 0, 0, 0); // default: no clip plane
		XMFLOAT4 clipPlaneOriginal = XMFLOAT4(0, 0, 0, 0); // not reversed clip plane
		wi::Canvas canvas;
		wi::graphics::Rect scissor;
		uint32_t sample_count = 1;
		int texture_primitiveID_index = -1;
		int texture_depth_index = -1;
		int texture_lineardepth_index = -1;
		int texture_velocity_index = -1;
		int texture_normal_index = -1;
		int texture_roughness_index = -1;
		int texture_reflection_index = -1;
		int texture_reflection_depth_index = -1;
		int texture_refraction_index = -1;
		int texture_waterriples_index = -1;
		int texture_ao_index = -1;
		int texture_ssr_index = -1;
		int texture_ssgi_index = -1;
		int texture_rtshadow_index = -1;
		int texture_rtdiffuse_index = -1;
		int texture_surfelgi_index = -1;
		int buffer_entitytiles_index = -1;
		int texture_vxgi_diffuse_index = -1;
		int texture_vxgi_specular_index = -1;
		uint shadercamera_options = SHADERCAMERA_OPTION_NONE;

		void CreatePerspective(float newWidth, float newHeight, float newNear, float newFar, float newFOV = XM_PI / 3.0f);
		void UpdateCamera();
		void TransformCamera(const XMMATRIX& W);
		void TransformCamera(const TransformComponent& transform) { TransformCamera(XMLoadFloat4x4(&transform.world)); }
		void Reflect(const XMFLOAT4& plane = XMFLOAT4(0, 1, 0, 0));

		inline XMVECTOR GetEye() const { return XMLoadFloat3(&Eye); }
		inline XMVECTOR GetAt() const { return XMLoadFloat3(&At); }
		inline XMVECTOR GetUp() const { return XMLoadFloat3(&Up); }
		inline XMVECTOR GetRight() const { return XMVector3Cross(GetAt(), GetUp()); }
		inline XMMATRIX GetView() const { return XMLoadFloat4x4(&View); }
		inline XMMATRIX GetInvView() const { return XMLoadFloat4x4(&InvView); }
		inline XMMATRIX GetProjection() const { return XMLoadFloat4x4(&Projection); }
		inline XMMATRIX GetInvProjection() const { return XMLoadFloat4x4(&InvProjection); }
		inline XMMATRIX GetViewProjection() const { return XMLoadFloat4x4(&VP); }
		inline XMMATRIX GetInvViewProjection() const { return XMLoadFloat4x4(&InvVP); }

		inline void SetDirty(bool value = true) { if (value) { _flags |= DIRTY; } else { _flags &= ~DIRTY; } }
		inline void SetCustomProjectionEnabled(bool value = true) { if (value) { _flags |= CUSTOM_PROJECTION; } else { _flags &= ~CUSTOM_PROJECTION; } }
		inline bool IsDirty() const { return _flags & DIRTY; }
		inline bool IsCustomProjectionEnabled() const { return _flags & CUSTOM_PROJECTION; }

		void Lerp(const CameraComponent& a, const CameraComponent& b, float t);

		void Serialize(wi::Archive& archive, wi::ecs::EntitySerializer& seri);
	};
}
