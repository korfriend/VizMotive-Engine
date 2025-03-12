#pragma once
#include "Components.h"
#include "GBackend/GBackendDevice.h"

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

			FILTER_VOLUME = 1 << 3,
			FILTER_GAUSSIAN_SPLATTING = 1 << 4,

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


		struct GaussianKernelAttribute 
		{
			XMFLOAT4 conic_opacity;
			XMFLOAT4 color_radii;
			XMUINT4 aabb; // bounding box 

			XMFLOAT2 uv; // pixel coords that is output of ndx2pix() func;
			float depth;
			uint32_t padding0;
		};
		struct GaussianSplattingBuffers
		{
			// gaussian kernels
			graphics::GPUBuffer gaussianSHs;
			graphics::GPUBuffer gaussianScale_Opacities;
			graphics::GPUBuffer gaussianQuaterinions;

			bool IsValid() const { return gaussianSHs.IsValid() && gaussianScale_Opacities.IsValid() && gaussianQuaterinions.IsValid(); }

			// ----- inter-processing buffers -----
			// NOTE:
			//	These buffers will be used for storing intermediate results during renderer's GaussianSplattting
			//	Different canvas (renderers) can access these buffers, assuming thread-safe process
			graphics::GPUBuffer touchedTiles_0;	// # of gaussian points
			graphics::GPUBuffer offsetTiles_0;	// # of gaussian points
			graphics::GPUBuffer offsetTilesPing; // # of gaussian points, Ping buffer
			graphics::GPUBuffer offsetTilesPong; // # of gaussian points, Pong buffer

			graphics::GPUBuffer sortKBufferEven; // duplicated key buffer
			graphics::GPUBuffer sortKBufferOdd;
			graphics::GPUBuffer sortVBufferEven; // duplicated value buffer
			graphics::GPUBuffer sortVBufferOdd;

			graphics::GPUBuffer sortHistBuffer;

			// test210 - vertexAttributes
			graphics::GPUBuffer gaussianKernelAttributes; // # of gaussian points
			graphics::GPUBuffer totalSumBufferHost; // # of gaussian points
			graphics::GPUBuffer tileBoundaryBuffer; // # of gaussian points

			// readback buffer
			graphics::GPUBuffer readBackBufferTest;
			const uint32_t* readBackBufferTestMapped = nullptr;
		};

		struct BVHBuffers
		{
			// https://github.com/ToruNiina/lbvh
			// Scene BVH intersection resources:
			graphics::GPUBuffer bvhNodeBuffer;
			graphics::GPUBuffer bvhParentBuffer;
			graphics::GPUBuffer bvhFlagBuffer;
			graphics::GPUBuffer primitiveCounterBuffer;
			graphics::GPUBuffer primitiveIDBuffer;
			graphics::GPUBuffer primitiveBuffer;
			graphics::GPUBuffer primitiveMortonBuffer;
			uint32_t primitiveCapacity = 0;
			bool IsValid() const { return primitiveCounterBuffer.IsValid(); }
		};
		struct GPrimBuffers
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

			BVHBuffers bvhBuffers;
			GaussianSplattingBuffers gaussianSplattingBuffers;

			void Destroy()
			{
				generalBuffer = {};
				streamoutBuffer = {};

				bvhBuffers = {};
				gaussianSplattingBuffers = {};

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

		// ----- BVH -----
		TimeStamp timeStampGPUBVHUpdate = TimerMin;
		bool isBVHEnabled = true;
		bool IsDirtyGPUBVH() const { return TimeDurationCount(timeStampPrimitiveUpdate_, timeStampGPUBVHUpdate) >= 0; }
		// ----- Gaussian Splatting -----
		bool allowGaussianSplatting = false;
		// ----- Meshlet -----
		bool isMeshletEnabled = false;
		uint32_t meshletOffset = ~0u; // base
		uint32_t meshletCount = 0;
		// --------------------

		inline graphics::IndexBufferFormat GetIndexFormat(const size_t slot) const
		{
			return graphics::GetIndexBufferFormat((uint32_t)parts_[slot].GetNumIndices());
		}
		inline size_t GetIndexStride(const size_t slot) const { return GetIndexFormat(slot) == graphics::IndexBufferFormat::UINT32 ? sizeof(uint32_t) : sizeof(uint16_t); }
		GPrimBuffers* GetGPrimBuffer(const size_t slot) { return (GPrimBuffers*)parts_[slot].bufferHandle_.get(); }
		void UpdateRenderData() override;
		void UpdateRenderDataGaussianSplatting();
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
		struct Vertex_TEX32
		{
			float x = 0;
			float y = 0;
			constexpr void FromFULL(const XMFLOAT2& uv, const XMFLOAT2& uv_range_min = XMFLOAT2(0, 0), const XMFLOAT2& uv_range_max = XMFLOAT2(1, 1))
			{
				x = math::InverseLerp(uv_range_min.x, uv_range_max.x, uv.x);
				y = math::InverseLerp(uv_range_min.y, uv_range_max.y, uv.y);
			}
			static constexpr graphics::Format FORMAT = graphics::Format::R32G32_FLOAT;
		};
		struct Vertex_UVS
		{
			Vertex_TEX uv0;
			Vertex_TEX uv1;
			static constexpr graphics::Format FORMAT = graphics::Format::R16G16B16A16_UNORM;
		};
		struct Vertex_UVS32
		{
			Vertex_TEX32 uv0;
			Vertex_TEX32 uv1;
			static constexpr graphics::Format FORMAT = graphics::Format::R32G32B32A32_FLOAT;
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

	struct CORE_EXPORT GTextureInterface
	{
	protected:
		Entity texureEntity_ = 0;
	public:
		GTextureInterface(Entity texureEntity) : texureEntity_(texureEntity) {}
		
		inline int GetSparseResidencymapDescriptor() const;
		inline int GetSparseFeedbackmapDescriptor() const;
		inline const graphics::Texture& GetTexture() const;
		// Allows to set a Texture to the resource from outside
		//	srgb_subresource: you can provide a subresource for SRGB view if the texture is going to be used as SRGB with the GetTextureSRGBSubresource() (optional)
		inline void SetTexture(const graphics::Texture& texture, int srgb_subresource = -1);
		inline const graphics::GPUResource* GetGPUResource() const;
	};

	struct CORE_EXPORT GTextureComponent : TextureComponent, GTextureInterface
	{
	private:
	public:
		GTextureComponent(const Entity entity, const VUID vuid = 0) : TextureComponent(entity, vuid), GTextureInterface(entity) {}

		inline uint32_t GetUVSet() const;
		inline float GetLodClamp() const;

		inline int GetTextureSRGBSubresource() const;
		// Let the streaming system know the required resolution of this resource
		inline void StreamingRequestResolution(uint32_t resolution);
	};

	struct CORE_EXPORT GVolumeComponent : VolumeComponent, GTextureInterface
	{
	private:
		std::vector<uint8_t> volumeMinMaxBlocksData_;
		graphics::Texture volumeMinMaxBlocks_ = {};
		XMUINT3 blockPitch_ = {}; // single block
		XMUINT3 blocksSize_ = {};
		struct GPUBlockBitmask
		{
			graphics::GPUBuffer bitmaskBuffer;
			std::vector<uint32_t> bitmask;
			TimeStamp updateTime = {};
		};
		std::unordered_map<Entity, GPUBlockBitmask> visibleBlockBitmasks_; // for blocks
	public:
		GVolumeComponent(const Entity entity, const VUID vuid = 0) : VolumeComponent(entity, vuid), GTextureInterface(entity) {}

		void UpdateVolumeMinMaxBlocks(const XMUINT3 blockSize);
		const graphics::Texture& GetBlockTexture() const { return volumeMinMaxBlocks_; };
		const XMUINT3& GetBlockPitch() const { return blockPitch_; }
		const XMUINT3& GetBlocksSize() const { return blocksSize_; }

		void UpdateVolumeVisibleBlocksBuffer(const Entity entityVisibleMap);
		// a buffer that contains a bitmask array representing visible blocks
		const graphics::GPUBuffer& GetVisibleBitmaskBuffer(const Entity entityVisibleMap) const;
	};

	// scene 

	struct CORE_EXPORT GRenderableComponent : RenderableComponent
	{
		GRenderableComponent(const Entity entity, const VUID vuid = 0) : RenderableComponent(entity, vuid) {}

		// ----- buffer-based resources -----
		struct GPrimEffectBuffers
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
		std::vector<GPrimEffectBuffers> bufferEffects;	// the same number of GBuffers
		
		// --- these will only be valid for a single frame: (supposed to be updated dynamically) ---
		uint32_t sortPriority = 0; // increase to draw earlier (currently 4 bits will be used)

		// these are for linear array of scene component's array
		uint32_t geometryIndex = ~0u; // current linear array of current rendering scene (used as an offset)
		uint32_t renderableIndex = ~0u;	// current linear array of current rendering scene
		std::vector<uint32_t> materialIndices; // current linear array of current rendering scene

		uint32_t sortBits = 0;
		uint8_t lod = 0;

		uint32_t resLookupIndex = ~0u; // refer to geometryOffset defined in GGeometryComponent

		//----- determined by associated materials -----
		mutable uint32_t materialFilterFlags = 0u;
		mutable uint32_t lightmapIterationCount = 0u;
		mutable uint32_t renderFlags = 0u; // OR-operated MaterialComponent::flags_
	};

	struct PickingIO
	{
	private:
		XMFLOAT2 posPickOnScreen_ = {};
		std::vector<Entity> pickedRenderables_;
		std::vector<XMFLOAT3> pickedPositions_;
		std::vector<int> pickedPritmiveIDs_;	// if no value, then set to -1
		std::vector<int> pickedMaskValueIDs_;	// if no value, then set to -1
	public:
		void SetScreenPos(const XMFLOAT2& pos) { posPickOnScreen_ = pos; }
		const XMFLOAT2& GetScreenPos() const { return posPickOnScreen_; }
		void AddPickingInfo(const Entity entity, const XMFLOAT3& posWS, const int primitiveID, const int maskValue)
		{
			pickedRenderables_.push_back(entity);
			pickedPositions_.push_back(posWS);
			pickedPritmiveIDs_.push_back(primitiveID);
			pickedMaskValueIDs_.push_back(maskValue);
		}
		void Clear()
		{
			pickedRenderables_.clear();
			pickedPositions_.clear();
			pickedPritmiveIDs_.clear();
			pickedMaskValueIDs_.clear();
		}
		const Entity* DataEntities() const { return pickedRenderables_.data(); }
		const XMFLOAT3* DataPositions() const { return pickedPositions_.data(); }
		const int* DataPrimitiveIDs() const { return pickedPritmiveIDs_.data(); }
		const int* DataMaskValues() const { return pickedMaskValueIDs_.data(); }
		size_t NumPickedPositions() const { return pickedPositions_.size(); }
	};

	struct CORE_EXPORT GCameraInterface
	{
	protected:
		Entity cameraEntity_ = 0;

	public:
		GCameraInterface(Entity cameraEntity) : cameraEntity_(cameraEntity) {}

		// temporal attributes for picking process 
		bool isPickingMode = false;
		PickingIO pickingIO;
	};

	struct CORE_EXPORT GCameraComponent : CameraComponent, GCameraInterface
	{
		GCameraComponent(const Entity entity, const VUID vuid = 0) : CameraComponent(entity, vuid), GCameraInterface(entity) {}
	};

	struct CORE_EXPORT GSlicerComponent : SlicerComponent, GCameraInterface
	{
		GSlicerComponent(const Entity entity, const VUID vuid = 0) : SlicerComponent(entity, vuid), GCameraInterface(entity) {}

		void UpdateCurve() override;

		graphics::GPUBuffer curveInterpPointsBuffer;
	};

	struct CORE_EXPORT GLightComponent : LightComponent
	{
		GLightComponent(const Entity entity, const VUID vuid = 0) : LightComponent(entity, vuid) {}

		std::vector<float> cascadeDistances = { 8, 80, 800 };
	};
}

namespace vz::compfactory
{
	inline GMaterialComponent* GetGMaterialComponent(const Entity entity) { return (GMaterialComponent*)GetMaterialComponent(entity); }
	inline GGeometryComponent* GetGGeometryComponent(const Entity entity) { return (GGeometryComponent*)GetGeometryComponent(entity); }
	inline GTextureComponent* GetGTextureComponent(const Entity entity) { return (GTextureComponent*)GetTextureComponent(entity); }
	inline GVolumeComponent* GetGVolumeComponent(const Entity entity) { return (GVolumeComponent*)GetVolumeComponent(entity); }
	inline GRenderableComponent* GetGRenderableComponent(const Entity entity) { return (GRenderableComponent*)GetRenderableComponent(entity); }
	inline GLightComponent* GetGLightComponent(const Entity entity) { return (GLightComponent*)GetLightComponent(entity); }
	inline GCameraComponent* GetGCameraComponent(const Entity entity) { return (GCameraComponent*)GetCameraComponent(entity); }
	inline GMaterialComponent* GetGMaterialComponentByVUID(const VUID vuid) { return (GMaterialComponent*)GetMaterialComponentByVUID(vuid); }
	inline GGeometryComponent* GetGGeometryComponentByVUID(const VUID vuid) { return (GGeometryComponent*)GetGeometryComponentByVUID(vuid); }
	inline GTextureComponent* GetGTextureComponentByVUID(const VUID vuid) { return (GTextureComponent*)GetTextureComponentByVUID(vuid); }
	inline GVolumeComponent* GetGVolumeComponentByVUID(const VUID vuid) { return (GVolumeComponent*)GetVolumeComponentByVUID(vuid); }
	inline GRenderableComponent* GetGRenderableComponentByVUID(const VUID vuid) { return (GRenderableComponent*)GetRenderableComponentByVUID(vuid); }
	inline GLightComponent* GetGLightComponentByVUID(const VUID vuid) { return (GLightComponent*)GetLightComponentByVUID(vuid); }
	inline GCameraComponent* GetGCameraComponentByVUID(const VUID vuid) { return (GCameraComponent*)GetCameraComponentByVUID(vuid); }
}
