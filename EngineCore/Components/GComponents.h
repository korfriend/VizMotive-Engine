#pragma once
#include "Components.h"
#include "Common/Backend/GBackendDevice.h"

namespace vz
{
	// Note:
	//	The parameters inside 'G'-components are used by Graphics pipeline and GPGPUs
	//	So, all attributes here are Non-serialized attributes
	//	Most parameters are strongly related to the renderer plugin
	
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

		// Create texture resources for GPU
		void UpdateAssociatedTextures();
		uint32_t GetFilterMaskFlags() const;
	};

	struct CORE_EXPORT GGeometryComponent : GeometryComponent
	{
		GGeometryComponent(const Entity entity, const VUID vuid = 0) : GeometryComponent(entity, vuid) {}

		// https://www.nvidia.com/en-us/drivers/bindless-graphics/

		uint32_t geometryOffset = 0; // (including # of parts)

		struct GeometryPartBuffer
		{
			graphics::GPUBuffer generalBuffer; // index buffer + all static vertex buffers
			graphics::GPUBuffer streamoutBuffer; // all dynamic vertex buffers

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
			BufferView vbPosition;
			BufferView vbNormal;
			BufferView vbTangent;
			BufferView vbUVs;
			BufferView vbColor;

			// so refers to Shader Output
			BufferView soPosition;
			BufferView soNormal;
			BufferView soPrev;

			void Destroy()
			{
				generalBuffer = {};
				streamoutBuffer = {};

				// buffer views
				ib = {};
				vbPosition = {};
				vbTangent = {};
				vbNormal = {};
				vbUVs = {};
				vbColor = {};

				soPosition = {};
				soNormal = {};
				soPrev = {};
			}
		};
		std::vector<GeometryPartBuffer> bufferParts;

		void UpdateRenderData() override;
		void DeleteRenderData() override;

		// CreateRaytracingRenderData
		
		// These will be added in GeometryComponent (override)
		// BuildBVH
		// ComputeNormals
		// FlipCulling
		// FlipNormals
		// Recenter
		// RecenterToBottom
		// GetBoundingSphere
		// FlipNormals, FlipCulling
		// Recenter (Pivot)
		// RecenterToBottom
		// GetBoundingSphere
		// GetClusterCount
		// CreateSubset
		// GetMemory...
	};

	struct CORE_EXPORT GTextureComponent : TextureComponent
	{
	private:
	public:
		GTextureComponent(const Entity entity, const VUID vuid = 0);

		uint32_t GetUVSet() const;
		float GetLodClamp() const;
		int GetSparseResidencymapDescriptor() const;
		int GetSparseFeedbackmapDescriptor() const;

		int GetTextureSRGBSubresource() const;
		const graphics::Texture& GetTexture() const;
		// Allows to set a Texture to the resource from outside
		//	srgb_subresource: you can provide a subresource for SRGB view if the texture is going to be used as SRGB with the GetTextureSRGBSubresource() (optional)
		void SetTexture(const graphics::Texture& texture, int srgb_subresource = -1);
		// Let the streaming system know the required resolution of this resource
		void StreamingRequestResolution(uint32_t resolution);
		const graphics::GPUResource* GetGPUResource() const
		{
			if (!IsValid() || !GetTexture().IsValid())
				return nullptr;
			return &GetTexture();
		}
	};

	// scene 

	struct CORE_EXPORT GRenderableComponent : RenderableComponent
	{
		GRenderableComponent(const Entity entity, const VUID vuid = 0) : RenderableComponent(entity, vuid) {}

		uint32_t sortPriority = 0; // increase to draw earlier (currently 4 bits will be used)

		// these will only be valid for a single frame: (supposed to be updated dynamically)
		uint32_t geometryIndex = ~0u;	// bindless
		uint32_t sortBits = 0;

		//----- determined by associated materials -----
		std::vector<graphics::GPUBuffer> vbWetmaps; // for each primitive part
		mutable bool wetmapCleared = false;
		mutable uint32_t materialFilterFlags = 0u;
		mutable uint32_t lightmapIterationCount = 0u;
	};

	struct CORE_EXPORT GCameraComponent : CameraComponent
	{
		GCameraComponent(const Entity entity, const VUID vuid = 0) : CameraComponent(entity, vuid) {}

		graphics::Viewport viewport;
		graphics::Rect scissor;
	};

	struct CORE_EXPORT GLightComponent : LightComponent
	{
		GLightComponent(const Entity entity, const VUID vuid = 0) : LightComponent(entity, vuid) {}

		std::vector<float> cascadeDistances = { 8, 80, 800 };
	};
}
