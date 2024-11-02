#pragma once
#include "Components.h"
#include "Common/Backend/GBackendDevice.h"

namespace vz
{
	// Note:
	//	The parameters inside 'G'-components are used by Graphics pipeline and GPGPUs
	//	So, all attributes here are Non-serialized attributes
	//	Most parameters are strongly related to the renderer plugin

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

	// resources

	struct CORE_EXPORT GMaterialComponent : MaterialComponent
	{
		enum MaterialFilterFlags
		{
			// Include nothing:
			FILTER_NONE = 0,

			// Object filtering types:
			FILTER_OPAQUE = 1 << 0,
			FILTER_TRANSPARENT = 1 << 1,
			FILTER_NAVIGATION_MESH = 1 << 2,
			FILTER_OBJECT_ALL = FILTER_OPAQUE | FILTER_TRANSPARENT | FILTER_NAVIGATION_MESH,

			// Other filtering types:
			FILTER_COLLIDER = 1 << 5,

			// Include everything:
			FILTER_ALL = ~0,
		};

		GMaterialComponent(const Entity entity, const VUID vuid = 0) : MaterialComponent(entity, vuid) {}

		// Non-serialized attributes:
		int samplerDescriptor = -1; // optional
		uint8_t userStencilRef = 0;
		graphics::ShadingRate shadingRate = graphics::ShadingRate::RATE_1X1;

		// Create texture resources for GPU
		void UpdateAssociatedTextures() override;
		uint32_t GetFilterMaskFlags() const;
	};

	struct CORE_EXPORT GGeometryComponent : GeometryComponent
	{
		GGeometryComponent(const Entity entity, const VUID vuid = 0) : GeometryComponent(entity, vuid) {}

		struct GBuffers
		{
			uint32_t slot = 0;

			graphics::GPUBuffer generalBuffer; // index buffer + all static vertex buffers
			graphics::GPUBuffer streamoutBuffer; // all dynamic vertex buffers

			BufferView ib;
			BufferView vbPosW;
			BufferView vbNormal;
			BufferView vbTangent;
			BufferView vbUVs;
			BufferView vbColor;

			// 'so' refers to Stream-Output:
			//		useful when the mesh undergoes dynamic changes, 
			//		such as in real-time physics simulations, deformations, or 
			//		when the normals are affected by geometry shaders or other GPU-side processes.
			BufferView soPosW;
			BufferView soNormal;
			BufferView soTangent;
			BufferView soPre;

			void Destroy()
			{
				generalBuffer = {};
				streamoutBuffer = {};

				// buffer views
				ib = {};
				vbPosW = {};
				vbTangent = {};
				vbNormal = {};
				vbUVs = {};
				vbColor = {};

				soPosW = {};
				soNormal = {};
				soTangent = {};
				soPre = {};
			}

		};

		// https://www.nvidia.com/en-us/drivers/bindless-graphics/

		uint32_t geometryOffset = 0; // (including # of parts)

		inline graphics::IndexBufferFormat GetIndexFormat(const size_t slot) const
		{
			return graphics::GetIndexBufferFormat((uint32_t)parts_[slot].GetNumIndices());
		}
		inline size_t GetIndexStride(const size_t slot) const { return GetIndexFormat(slot) == graphics::IndexBufferFormat::UINT32 ? sizeof(uint32_t) : sizeof(uint16_t); }
		GBuffers* GetGBuffer(const size_t slot) { return (GBuffers*)parts_[slot].bufferHandle_.get(); }
		void UpdateRenderData() override;
		void DeleteRenderData() override;
		void UpdateStreamoutRenderData();
		size_t GetMemoryUsageCPU() const override;
		size_t GetMemoryUsageGPU() const override;

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
			static constexpr graphics::Format FORMAT = graphics::Format::R32G32B32_FLOAT;
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
			static constexpr graphics::Format FORMAT = graphics::Format::R32G32B32A32_FLOAT;
		};

		// For a compatibility issue (also binding SRV for extension), use Vertex_POS32W as default
		graphics::Format positionFormat = Vertex_POS32W::FORMAT; // can be modified 

		struct Vertex_TEX
		{
			uint16_t x = 0;
			uint16_t y = 0;

			constexpr void FromFULL(const XMFLOAT2& uv, const XMFLOAT2& uv_range_min = XMFLOAT2(0, 0), const XMFLOAT2& uv_range_max = XMFLOAT2(1, 1))
			{
				x = uint16_t(math::InverseLerp(uv_range_min.x, uv_range_max.x, uv.x) * 65535.0f);
				y = uint16_t(math::InverseLerp(uv_range_min.y, uv_range_max.y, uv.y) * 65535.0f);
			}
			static constexpr graphics::Format FORMAT = graphics::Format::R16G16_UNORM;
		};
		struct Vertex_UVS
		{
			Vertex_TEX uv0;
			Vertex_TEX uv1;
			static constexpr graphics::Format FORMAT = graphics::Format::R16G16B16A16_UNORM;
		};
		struct Vertex_BON
		{
			XMUINT4 packed = XMUINT4(0, 0, 0, 0);

			constexpr void FromFULL(const XMUINT4& boneIndices, const XMFLOAT4& boneWeights)
			{
				// Note:
				//	- Indices are packed at 20 bits which allow indexing >1 million bones per mesh
				//	- Weights are packed at 12 bits which allow 4096 distinct values, this was tweaked to
				//		retain good precision with a high bone count stanford bunny soft body simulation where regular 8 bit weights was not enough
				packed.x = (boneIndices.x & 0xFFFFF) | ((uint32_t(boneWeights.x * 4095) & 0xFFF) << 20u);
				packed.y = (boneIndices.y & 0xFFFFF) | ((uint32_t(boneWeights.y * 4095) & 0xFFF) << 20u);
				packed.z = (boneIndices.z & 0xFFFFF) | ((uint32_t(boneWeights.z * 4095) & 0xFFF) << 20u);
				packed.w = (boneIndices.w & 0xFFFFF) | ((uint32_t(boneWeights.w * 4095) & 0xFFF) << 20u);
			}
			constexpr XMUINT4 GetInd_FULL() const
			{
				return XMUINT4(packed.x & 0xFFFFF, packed.y & 0xFFFFF, packed.z & 0xFFFFF, packed.w & 0xFFFFF);
			}
			constexpr XMFLOAT4 GetWei_FULL() const
			{
				return XMFLOAT4(
					float(packed.x >> 20u) / 4095.0f,
					float(packed.y >> 20u) / 4095.0f,
					float(packed.z >> 20u) / 4095.0f,
					float(packed.w >> 20u) / 4095.0f
				);
			}
		};
		struct Vertex_COL
		{
			uint32_t color = 0;
			static constexpr graphics::Format FORMAT = graphics::Format::R8G8B8A8_UNORM;
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
			static constexpr graphics::Format FORMAT = graphics::Format::R8G8B8A8_SNORM;
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
			static constexpr graphics::Format FORMAT = graphics::Format::R8G8B8A8_SNORM;
		};
	};

	struct CORE_EXPORT GTextureComponent : virtual TextureComponent
	{
	private:
	public:
		GTextureComponent(const Entity entity, const VUID vuid = 0) : TextureComponent(entity, vuid) {}

		inline uint32_t GetUVSet() const;
		inline float GetLodClamp() const;
		inline int GetSparseResidencymapDescriptor() const;
		inline int GetSparseFeedbackmapDescriptor() const;

		inline int GetTextureSRGBSubresource() const;
		inline const graphics::Texture& GetTexture() const;
		// Allows to set a Texture to the resource from outside
		//	srgb_subresource: you can provide a subresource for SRGB view if the texture is going to be used as SRGB with the GetTextureSRGBSubresource() (optional)
		inline void SetTexture(const graphics::Texture& texture, int srgb_subresource = -1);
		// Let the streaming system know the required resolution of this resource
		inline void StreamingRequestResolution(uint32_t resolution);
		inline const graphics::GPUResource* GetGPUResource() const {
			if (!IsValid() || !GetTexture().IsValid())
				return nullptr;
			return &GetTexture();
		}

		//void DeleteRenderData() override;
		//void UpdateRenderData() override;
		//size_t GetMemoryUsageCPU() override;
		//size_t GetMemoryUsageGPU() override;
	};

	//GVolumeComponent
	//	戍式式 Base(only once)
	//	戌式式 TextureComponent(only once)
	//		戌式式 resources(only once)
	//	戍式式 GTextureComponent
	//	戌式式 VolumeComponent
	struct CORE_EXPORT GVolumeTextureComponent : GTextureComponent, VolumeComponent
	{
		GVolumeTextureComponent(const Entity entity, const VUID vuid = 0) : 
			ComponentBase(ComponentType::VOLUMETEXTURE, entity, vuid) 
			, TextureComponent(entity, vuid)
			, GTextureComponent(entity, vuid)
			, VolumeComponent(entity, vuid) {}

		void Serialize(vz::Archive& archive, const uint64_t version) override {
			VolumeComponent::Serialize(archive, version);
		}
	};

	// scene 

	struct CORE_EXPORT GRenderableComponent : RenderableComponent
	{
		GRenderableComponent(const Entity entity, const VUID vuid = 0) : RenderableComponent(entity, vuid) {}

		// ----- buffer-based resources -----
		struct GBufferBasedRes
		{
			graphics::GPUBuffer wetmapBuffer;
			graphics::GPUBuffer AOBuffer;
			mutable bool wetmapCleared = false;

			BufferView vbWetmap;
			BufferView vbAO;

			void Destroy()
			{
				AOBuffer = {};
				wetmapBuffer = {};
				wetmapCleared = false;

				// buffer views
				vbWetmap = {};
				vbAO = {};
			}
		};
		std::vector<GBufferBasedRes> bufferEffects;	// the same number of GBuffers
		
		// --- these will only be valid for a single frame: (supposed to be updated dynamically) ---
		uint32_t sortPriority = 0; // increase to draw earlier (currently 4 bits will be used)

		// these are for linear array of scene component's array
		uint32_t geometryIndex = ~0u; // current linear array of current rendering scene (used as an offset)
		uint32_t renderableIndex = ~0u;	// current linear array of current rendering scene
		std::vector<uint32_t> materialIndices; // current linear array of current rendering scene

		uint32_t sortBits = 0;
		uint8_t lod = 0;

		uint32_t resLookupOffset = ~0u; // refer to geometryOffset defined in GGeometryComponent

		//----- determined by associated materials -----
		mutable uint32_t materialFilterFlags = 0u;
		mutable uint32_t lightmapIterationCount = 0u;
		mutable uint32_t renderFlags = 0u; // OR-operated MaterialComponent::flags_
	};

	struct CORE_EXPORT GCameraComponent : CameraComponent
	{
		GCameraComponent(const Entity entity, const VUID vuid = 0) : CameraComponent(entity, vuid) {}
	};

	struct CORE_EXPORT GLightComponent : LightComponent
	{
		GLightComponent(const Entity entity, const VUID vuid = 0) : LightComponent(entity, vuid) {}

		std::vector<float> cascadeDistances = { 8, 80, 800 };
	};
}
