#pragma once
#include "Components.h"
#include "Common/Backend/GBackendDevice.h"

namespace vz
{
	// Note:
	//	The parameters inside 'G'-components are used by Graphics pipeline and GPGPUs
	//	So, all attributes here are Non-serialized attributes
	
	// resources

	struct CORE_EXPORT GMaterialComponent : MaterialComponent
	{
		GMaterialComponent(const Entity entity, const VUID vuid = 0) : MaterialComponent(entity, vuid) {}

		// Non-serialized attributes:
		int samplerDescriptor = -1; // optional

		// Create texture resources for GPU
		void UpdateAssociatedTextures();
	};

	struct CORE_EXPORT GGeometryComponent : GeometryComponent
	{
		GGeometryComponent(const Entity entity, const VUID vuid = 0) : GeometryComponent(entity, vuid) {}

		// https://www.nvidia.com/en-us/drivers/bindless-graphics/
		uint32_t geometryOffset = 0; // used for bindless graphics 

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
		BufferView vbUVs;
		BufferView vbColor;

		// so refers to Shader Output
		BufferView soPosition; 
		BufferView soNormal;
		BufferView soPrev;
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
		uint32_t sortBits = 0;

		mutable uint32_t lightmapIterationCount = 0;
	};

	struct CORE_EXPORT GLightComponent : LightComponent
	{
		GLightComponent(const Entity entity, const VUID vuid = 0) : LightComponent(entity, vuid) {}

		std::vector<float> cascadeDistances = { 8, 80, 800 };
	};
}
