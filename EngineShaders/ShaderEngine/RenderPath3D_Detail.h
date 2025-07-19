#pragma once
#include "Renderer.h"

#include "Utils/Timer.h"
#include "Utils/Backlog.h"
#include "Utils/EventHandler.h"
#include "Utils/Profiler.h"
#include "Utils/Helpers.h"
#include "Utils/vzMath.h"
#include "Utils/Geometrics.h"
#include "ThirdParty/RectPacker.h"

using namespace vz::geometrics;

namespace vz::renderer
{
	enum MIPGENFILTER
	{
		MIPGENFILTER_POINT,
		MIPGENFILTER_LINEAR,
		MIPGENFILTER_GAUSSIAN,
	};
	enum DRAWSCENE_FLAGS
	{
		DRAWSCENE_OPAQUE = 1 << 0, // include opaque objects
		DRAWSCENE_TRANSPARENT = 1 << 1, // include transparent objects
		DRAWSCENE_OCCLUSIONCULLING = 1 << 2, // enable skipping objects based on occlusion culling results
		DRAWSCENE_TESSELLATION = 1 << 3, // enable tessellation
		DRAWSCENE_FOREGROUND_ONLY = 1 << 4, // only include objects that are tagged as foreground
	};

	using RenderableType = RenderableComponent::RenderableType;
}

namespace vz::renderer
{
	struct Visibility
	{
		// User fills these:
		uint8_t layerMask = ~0;
		CameraComponent* camera = nullptr;
		enum FLAGS
		{
			EMPTY = 0,
			ALLOW_RENDERABLES = 1 << 0,
			ALLOW_LIGHTS = 1 << 1,
			//ALLOW_DECALS = 1 << 2,
			//ALLOW_ENVPROBES = 1 << 3,
			//ALLOW_EMITTERS = 1 << 4,
			ALLOW_OCCLUSION_CULLING = 1 << 5,
			ALLOW_SHADOW_ATLAS_PACKING = 1 << 6,

			ALLOW_EVERYTHING = ~0u
		};
		uint32_t flags = EMPTY;

		Frustum frustum; // camera's frustum or special purposed frustum

		// TODO: visibleRenderables into visibleMeshRenderables and visibleVolumeRenderables
		//	and use them instead of visibleRenderables
		//	index for scene_Gdetails->renderableComponents
		std::vector<uint32_t> visibleRenderables_All;
		std::vector<uint32_t> visibleRenderables_Mesh;
		std::vector<uint32_t> visibleRenderables_Volume;
		std::vector<uint32_t> visibleRenderables_GSplat;

		//std::vector<uint32_t> visibleDecals;
		//std::vector<uint32_t> visibleEnvProbes;
		//std::vector<uint32_t> visibleEmitters;
		std::vector<uint32_t> visibleLights; // scene_Gdetails->lightComponents
		std::vector<ColliderComponent> visibleColliders;

		rectpacker::State shadowPacker;
		std::vector<rectpacker::Rect> visibleLightShadowRects;

		std::atomic<uint32_t> counterRenderableMesh;
		std::atomic<uint32_t> counterRenderableVolume;
		std::atomic<uint32_t> counterRenderableGSplat;
		std::atomic<uint32_t> counterLight;

		vz::SpinLock locker;
		bool isPlanarReflectionVisible = false;
		float closestReflectionPlane = std::numeric_limits<float>::max();
		std::atomic_bool volumetricLightRequest{ false };
		std::atomic_bool volumetriclight_request{ false };
		std::atomic_bool transparentsVisible{ false };

		void Clear()
		{
			visibleRenderables_All.clear();
			visibleRenderables_Mesh.clear();
			visibleRenderables_Volume.clear();
			visibleRenderables_GSplat.clear();
			visibleLights.clear();
			//visibleDecals.clear();
			//visibleEnvProbes.clear();
			//visibleEmitters.clear();

			counterRenderableMesh.store(0);
			counterRenderableVolume.store(0);
			counterRenderableGSplat.store(0);
			counterLight.store(0);

			closestReflectionPlane = std::numeric_limits<float>::max();
			volumetricLightRequest.store(false);
			transparentsVisible.store(false);
		}
		bool IsRequestedPlanarReflections() const
		{
			return isPlanarReflectionVisible;
		}
		bool IsRequestedVolumetricLights() const
		{
			return volumetricLightRequest.load();
		}
		bool IsTransparentsVisible() const
		{
			return transparentsVisible.load();
		}
	};

	struct VisibilityResources
	{
		XMUINT2 tile_count = {};
		graphics::GPUBuffer bins;
		graphics::GPUBuffer binned_tiles;
		graphics::Texture texture_payload_0;
		graphics::Texture texture_payload_1;
		graphics::Texture texture_normals;
		graphics::Texture texture_roughness;

		// You can request any of these extra outputs to be written by VisibilityResolve:
		const graphics::Texture* depthbuffer = nullptr; // depth buffer that matches with post projection
		const graphics::Texture* lineardepth = nullptr; // depth buffer in linear space in [0,1] range
		const graphics::Texture* primitiveID_1_resolved = nullptr; // resolved from MSAA texture_visibility input
		const graphics::Texture* primitiveID_2_resolved = nullptr; // resolved from MSAA texture_visibility input

		inline bool IsValid() const { return bins.IsValid(); }
	};

	struct MIPGEN_OPTIONS
	{
		int arrayIndex = -1;
		const graphics::Texture* gaussian_temp = nullptr;
		bool preserve_coverage = false;
		bool wide_gauss = false;
	};

	struct TemporalAAResources
	{
		mutable int frame = 0;
		graphics::Texture textureTemporal[2];

		bool IsValid() const { return textureTemporal[0].IsValid(); }
		const graphics::Texture* GetCurrent() const { return &textureTemporal[frame % arraysize(textureTemporal)]; }
		const graphics::Texture* GetHistory() const { return &textureTemporal[(frame + 1) % arraysize(textureTemporal)]; }
	};

	struct FSR2Resources
	{
		struct Fsr2Constants
		{
			int32_t   renderSize[2];
			int32_t   displaySize[2];
			uint32_t  lumaMipDimensions[2];
			uint32_t  lumaMipLevelToUse;
			uint32_t  frameIndex;
			float     displaySizeRcp[2];
			float     jitterOffset[2];
			float     deviceToViewDepth[4];
			float     depthClipUVScale[2];
			float     postLockStatusUVScale[2];
			float     reactiveMaskDimRcp[2];
			float     motionVectorScale[2];
			float     downscaleFactor[2];
			float     preExposure;
			float     tanHalfFOV;
			float     motionVectorJitterCancellation[2];
			float     jitterPhaseCount;
			float     lockInitialLifetime;
			float     lockTickDelta;
			float     deltaTime;
			float     dynamicResChangeFactor;
			float     lumaMipRcp;
		};
		mutable Fsr2Constants fsr2_constants = {};
		graphics::Texture adjusted_color;
		graphics::Texture luminance_current;
		graphics::Texture luminance_history;
		graphics::Texture exposure;
		graphics::Texture previous_depth;
		graphics::Texture dilated_depth;
		graphics::Texture dilated_motion;
		graphics::Texture dilated_reactive;
		graphics::Texture disocclusion_mask;
		graphics::Texture lock_status[2];
		graphics::Texture reactive_mask;
		graphics::Texture lanczos_lut;
		graphics::Texture maximum_bias_lut;
		graphics::Texture spd_global_atomic;
		graphics::Texture output_internal[2];

		bool IsValid() const { return adjusted_color.IsValid(); }

		XMFLOAT2 GetJitter() const;
	};

	struct TiledLightResources
	{
		XMUINT2 tileCount = {};
		graphics::GPUBuffer entityTiles; // culled entity indices
	};

	struct GaussianSplattingResources
	{

		XMUINT2 tileCount = {};

		graphics::GPUBuffer tileGaussianRange;

		// --- new version ---
		graphics::GPUBuffer touchedTiles_tiledCounts;
		graphics::GPUBuffer offsetTiles;
		graphics::GPUBuffer indirectBuffer;
		// next step					// tile boundary buffer

		graphics::GPUBuffer debugBuffer_readback[graphics::GraphicsDevice::GetBufferCount()];
	};

	struct LuminanceResources
	{
		graphics::GPUBuffer luminance;
	};

	struct BloomResources
	{
		graphics::Texture texture_bloom;
		graphics::Texture texture_temp;
	};

	// This will correspond to a single draw call
	//	It's used to render multiple instances of a single mesh
	//	Simply understand this as 'instances' originated from a renderable
	struct InstancedBatch
	{
		uint32_t geometryIndex = ~0u;
		uint32_t renderableIndex = ~0u;
		uint32_t instanceCount = 0;
		uint32_t dataOffset = 0;
		uint8_t userStencilRefOverride = 0;
		bool forceAlphatestForDithering = false;
		uint8_t lod = 0;
		AABB aabb;
	};

	// Direct reference to a renderable instance:
	struct RenderBatch
	{
		uint32_t geometryIndex; // used for sort
		uint32_t instanceIndex;	// renderable index
		uint16_t distance;
		uint8_t camera_mask;
		uint8_t lod_override; // if overriding the base object LOD is needed, specify less than 0xFF in this
		uint32_t sort_bits; // an additional bitmask for sorting only, it should be used to reduce pipeline changes

		inline void Create(uint32_t geometryIndex, uint32_t instanceIndex, float distance, uint32_t sort_bits, uint8_t camera_mask = 0xFF, uint8_t lod_override = 0xFF)
		{
			this->geometryIndex = geometryIndex;
			this->instanceIndex = instanceIndex;
			this->distance = XMConvertFloatToHalf(distance);
			this->sort_bits = sort_bits;
			this->camera_mask = camera_mask;
			this->lod_override = lod_override;
		}
		inline float GetDistance() const
		{
			return XMConvertHalfToFloat(HALF(distance));
		}
		constexpr uint32_t GetGeometryIndex() const
		{
			return geometryIndex;
		}
		constexpr uint32_t GetRenderableIndex() const
		{
			return instanceIndex;
		}

		// opaque sorting
		//	Priority is set to mesh index to have more instancing
		//	distance is second priority (front to back Z-buffering)
		constexpr bool operator<(const RenderBatch& other) const
		{
			union SortKey
			{
				struct
				{
					// The order of members is important here, it means the sort priority (low to high)!
					uint64_t distance : 16;
					uint64_t meshIndex : 16;
					uint64_t sort_bits : 32;
				} bits;
				uint64_t value;
			};
			static_assert(sizeof(SortKey) == sizeof(uint64_t));
			SortKey a = {};
			a.bits.distance = distance;
			a.bits.meshIndex = geometryIndex;
			a.bits.sort_bits = sort_bits;
			SortKey b = {};
			b.bits.distance = other.distance;
			b.bits.meshIndex = other.geometryIndex;
			b.bits.sort_bits = other.sort_bits;
			return a.value < b.value;
		}
		// transparent sorting
		//	Priority is distance for correct alpha blending (back to front rendering)
		//	mesh index is second priority for instancing
		constexpr bool operator>(const RenderBatch& other) const
		{
			union SortKey
			{
				struct
				{
					// The order of members is important here, it means the sort priority (low to high)!
					uint64_t meshIndex : 16;
					uint64_t sort_bits : 32;
					uint64_t distance : 16;
				} bits;
				uint64_t value;
			};
			static_assert(sizeof(SortKey) == sizeof(uint64_t));
			SortKey a = {};
			a.bits.distance = distance;
			a.bits.sort_bits = sort_bits;
			a.bits.meshIndex = geometryIndex;
			SortKey b = {};
			b.bits.distance = other.distance;
			b.bits.sort_bits = other.sort_bits;
			b.bits.meshIndex = other.geometryIndex;
			return a.value > b.value;
		}
	};

	static_assert(sizeof(RenderBatch) == 16ull);

	struct RenderQueue
	{
		std::vector<RenderBatch> batches;

		inline void init()
		{
			batches.clear();
		}
		inline void add(uint32_t geometryIndex, uint32_t instanceIndex, float distance, uint32_t sort_bits, uint8_t camera_mask = 0xFF, uint8_t lod_override = 0xFF)
		{
			batches.emplace_back().Create(geometryIndex, instanceIndex, distance, sort_bits, camera_mask, lod_override);
		}
		inline void sort_transparent()
		{
			std::sort(batches.begin(), batches.end(), std::greater<RenderBatch>());
		}
		inline void sort_opaque()
		{
			std::sort(batches.begin(), batches.end(), std::less<RenderBatch>());
		}
		inline bool empty() const
		{
			return batches.empty();
		}
		inline size_t size() const
		{
			return batches.size();
		}
	};

	inline constexpr XMUINT2 GetVisibilityTileCount(XMUINT2 internalResolution)
	{
		return XMUINT2(
			(internalResolution.x + VISIBILITY_BLOCKSIZE - 1) / VISIBILITY_BLOCKSIZE,
			(internalResolution.y + VISIBILITY_BLOCKSIZE - 1) / VISIBILITY_BLOCKSIZE
		);
	}
	inline constexpr XMUINT2 GetEntityCullingTileCount(XMUINT2 internalResolution)
	{
		return XMUINT2(
			(internalResolution.x + TILED_CULLING_BLOCKSIZE - 1) / TILED_CULLING_BLOCKSIZE,
			(internalResolution.y + TILED_CULLING_BLOCKSIZE - 1) / TILED_CULLING_BLOCKSIZE
		);
	}
	inline constexpr XMUINT2 GetGaussianSplattingTileCount(XMUINT2 internalResolution)
	{
		return XMUINT2(
			(internalResolution.x + GSPLAT_TILESIZE - 1) / GSPLAT_TILESIZE,
			(internalResolution.y + GSPLAT_TILESIZE - 1) / GSPLAT_TILESIZE
		);
	}
}

namespace vz::renderer
{
	constexpr float foregroundDepthRange = 0.01f;

	using GPrimBuffers = GGeometryComponent::GPrimBuffers;
	using Primitive = GeometryComponent::Primitive;
	using GPrimEffectBuffers = GRenderableComponent::GPrimEffectBuffers;

	// Note: barrierStack is used for thread-safe and independent barrier system
	extern thread_local std::vector<GPUBarrier> barrierStack;
	void BarrierStackFlush(CommandList cmd);
}

namespace vz::renderer
{
	struct DebugLine
	{
		XMFLOAT3 start = XMFLOAT3(0, 0, 0);
		XMFLOAT3 end = XMFLOAT3(0, 0, 0);
		XMFLOAT4 color_start = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 color_end = XMFLOAT4(1, 1, 1, 1);
	};

	struct DebugPoint
	{
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		float size = 1.0f;
		XMFLOAT4 color = XMFLOAT4(1, 1, 1, 1);
	};

	struct DebugShapeCollection
	{
	private:
		mutable std::vector<DebugLine> renderableLines_;
		mutable std::vector<DebugLine> renderableLines_depth_;
		mutable std::vector<DebugPoint> renderablePoints_;
		mutable std::vector<DebugPoint> renderablePoints_depth_;

		mutable std::vector<std::pair<XMFLOAT4X4, XMFLOAT4>> renderableBoxes_;
		mutable std::vector<std::pair<XMFLOAT4X4, XMFLOAT4>> renderableBoxes_depth_;
		mutable std::vector<std::pair<Sphere, XMFLOAT4>> renderableSpheres_;
		mutable std::vector<std::pair<Sphere, XMFLOAT4>> renderableSpheres_depth_;
		mutable std::vector<std::pair<Capsule, XMFLOAT4>> renderableCapsules_;
		mutable std::vector<std::pair<Capsule, XMFLOAT4>> renderableCapsules_depth_;

		void drawAndClearLines(const CameraComponent& camera, std::vector<DebugLine>& renderableLines, CommandList cmd, bool clearEnabled);
	public:
		float depthLineThicknessPixel = 1.3f;

		static constexpr size_t RENDERABLE_SHAPE_RESERVE = 2048; // for fast growing
		DebugShapeCollection() {
			renderableLines_.reserve(RENDERABLE_SHAPE_RESERVE);
			renderableLines_depth_.reserve(RENDERABLE_SHAPE_RESERVE);
			renderablePoints_.reserve(RENDERABLE_SHAPE_RESERVE);
			renderablePoints_depth_.reserve(RENDERABLE_SHAPE_RESERVE);
		}

		void AddDrawLine(const DebugLine& line, bool depth) const
		{
			if (depth)
				renderableLines_depth_.push_back(line);
			else
				renderableLines_.push_back(line);
		}
		void DrawLines(const CameraComponent& camera, CommandList cmd, bool clearEnabled = true);

		void AddPrimitivePart(const GeometryComponent::Primitive& part, const XMFLOAT4& baseColor, const XMFLOAT4X4& world);

		void Clear() const
		{
			// *this = RenderableShapeCollection(); // not recommend this due to inefficient memory footprint
			renderableLines_.clear();
			renderableLines_depth_.clear();
			renderablePoints_.clear();
			renderablePoints_depth_.clear();
			renderableBoxes_.clear();
			renderableBoxes_depth_.clear();
			renderableSpheres_.clear();
			renderableSpheres_depth_.clear();
			renderableCapsules_.clear();
			renderableCapsules_depth_.clear();
		}
	};

	struct GSceneDetails;
	struct GRenderPath3DDetails : GRenderPath3D
	{
		GRenderPath3DDetails(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
			: GRenderPath3D(swapChain, rtRenderFinal)
		{
			device = GetDevice();
		}
		virtual ~GRenderPath3DDetails() = default;

		// NOTE: "static thread_local" technique!!!
		//	the parameter must be initialized after the "static thread_local" declaration!
		inline static thread_local RenderQueue renderQueue;
		inline static thread_local RenderQueue renderQueue_transparent;

		mutable bool firstFrame = true;

		// this needs to be renderpath option NOR renderer engine option
		//  visibilityShadingInCS is good to a complex scene rather than a simple scene (e.g., axis view)
		bool visibilityShadingInCS = false; 
		
		GSceneDetails* scene_Gdetails = nullptr;

		FrameCB frameCB = {};
		// separate graphics pipelines for the combination of special rendering effects
		renderer::Visibility visMain;
		renderer::Visibility visReflection;

		// auxiliary cameras for special rendering effects
		LayeredMaskComponent layeredmaskReflection = LayeredMaskComponent(0);
		CameraComponent cameraReflection = CameraComponent(0);
		CameraComponent cameraReflectionPrevious = CameraComponent(0);
		CameraComponent cameraPrevious = CameraComponent(0);

		// resources associated with render target buffers and textures
		TemporalAAResources temporalAAResources; // dynamic allocation
		FSR2Resources fsr2Resources;
		TiledLightResources tiledLightResources;
		//TiledLightResources tiledLightResources_planarReflection; // dynamic allocation
		LuminanceResources luminanceResources; // dynamic allocation
		BloomResources bloomResources;
		// Gaussian Splatting 
		GaussianSplattingResources gaussianSplattingResources;

		graphics::Texture rtShadingRate; // UINT8 shading rate per tile

		// aliased (rtPostprocess, rtPrimitiveID)

		graphics::Texture debugUAV; // debug UAV can be used by some shaders...
		graphics::Texture rtPostprocess; // ping-pong with main scene RT in post-process chain


		// temporal rt textures ... we need to reduce these textures (reuse others!!)
		//graphics::Texture rtDvrDepth; // aliased to rtPrimitiveID_2
		//graphics::Texture rtCounter; // aliased to rtPrimitiveID_1 ??

		graphics::Texture rtMain;
		graphics::Texture rtMain_render; // can be MSAA
		graphics::Texture rtPrimitiveID_1;	// aliasing to rtPostprocess
		graphics::Texture rtPrimitiveID_2;	// aliasing to rtDvrProcess
		graphics::Texture rtPrimitiveID_1_render; // can be MSAA
		graphics::Texture rtPrimitiveID_2_render; // can be MSAA
		graphics::Texture rtPrimitiveID_debug; // test

		graphics::Texture depthBufferMain; // used for depth-testing, can be MSAA
		graphics::Texture rtLinearDepth; // linear depth result + mipchain (max filter)
		graphics::Texture depthBuffer_Copy; // used for shader resource, single sample
		graphics::Texture depthBuffer_Copy1; // used for disocclusion check

		graphics::Texture rtParticleDistortion_render = {};
		graphics::Texture rtParticleDistortion = {};

		graphics::Texture rtSceneCopy; // contains the rendered scene that can be fed into transparent pass for distortion effect
		graphics::Texture rtSceneCopy_tmp; // temporary for gaussian mipchain

		graphics::Texture rtOutlineSource; // linear depth but only the regions which have outline stencil

		graphics::Texture distortion_overlay; // optional full screen distortion from an asset

		graphics::Texture shadowMapAtlas;
		graphics::Texture shadowMapAtlas_Transparent;
		int maxShadowResolution_2D = 1024;
		int maxShadowResolution_cube = 256;

		mutable const graphics::Texture* lastPostprocessRT = &rtPostprocess;

		//graphics::Texture reprojectedDepth; // prev frame depth reprojected into current, and downsampled for meshlet occlusion culling
		
		VisibilityResources visibilityResources;	// dynamic allocation

		// ---------- GRenderPath3D's internal impl.: -----------------
		//  * functions with an input 'CommandList' are to be implemented here, otherwise, implement 'renderer::' namespace

		// call renderer::UpdatePerFrameData to update :
		//	1. visMain
		//	2. frameCB
		void UpdateVisibility(Visibility& vis);
		void UpdatePerFrameData(Scene& scene, const Visibility& vis, FrameCB& frameCB, float dt);
		void UpdateProcess(const float dt);
		// Updates the GPU state according to the previously called UpdatePerFrameData()
		void UpdateRenderData(const renderer::Visibility& vis, const FrameCB& frameCB, CommandList cmd);
		void UpdateRenderDataAsync(const renderer::Visibility& vis, const FrameCB& frameCB, CommandList cmd);

		void RefreshLightmaps(const Scene& scene, CommandList cmd);
		void RefreshWetmaps(const Visibility& vis, CommandList cmd);

		void TextureStreamingReadbackCopy(const Scene& scene, graphics::CommandList cmd);

		void GenerateMipChain(const Texture& texture, MIPGENFILTER filter, CommandList cmd, const MIPGEN_OPTIONS& options);

		// Compress a texture into Block Compressed format
		//	textureSrc	: source uncompressed texture
		//	textureBC	: destination compressed texture, must be a supported BC format (BC1/BC3/BC4/BC5/BC6H_UFLOAT)
		//	Currently this will handle simple Texture2D with mip levels, and additionally BC6H cubemap
		void BlockCompress(const graphics::Texture& textureSrc, graphics::Texture& textureBC, graphics::CommandList cmd, uint32_t dstSliceOffset = 0);
		// Updates the per camera constant buffer need to call for each different camera that is used when calling DrawScene() and the like
		//	cameraPrevious : camera from previous frame, used for reprojection effects.
		//	cameraReflection : camera that renders planar reflection
		void BindCameraCB(const CameraComponent& camera, const CameraComponent& cameraPrevious, const CameraComponent& cameraReflection, CommandList cmd);
		void BindCommonResources(CommandList cmd);

		void OcclusionCulling_Reset(const Visibility& vis, CommandList cmd);
		void OcclusionCulling_Render(const CameraComponent& camera, const Visibility& vis, CommandList cmd);
		void OcclusionCulling_Resolve(const Visibility& vis, CommandList cmd);

		ForwardEntityMaskCB ForwardEntityCullingCPU(const Visibility& vis, const AABB& batch_aabb, RENDERPASS renderPass);

		void CreateTiledLightResources(TiledLightResources& res, XMUINT2 resolution);
		void ComputeTiledLightCulling(const TiledLightResources& res, const Visibility& vis, const Texture& debugUAV, CommandList cmd);
		void CreateGaussianResources(GaussianSplattingResources& res, XMUINT2 resolution);
		void CreateVisibilityResources(VisibilityResources& res, XMUINT2 resolution);

		void Visibility_Prepare(const VisibilityResources& res, const Texture& input_primitiveID_1, const Texture& input_primitiveID_2, CommandList cmd); // input_primitiveID can be MSAA
		// SURFACE need to be checked whether it requires FORWARD or DEFERRED
		void Visibility_Surface(const VisibilityResources& res, const Texture& output, CommandList cmd);
		void Visibility_Surface_Reduced(const VisibilityResources& res, CommandList cmd);
		void Visibility_Shade(const VisibilityResources& res, const Texture& output, CommandList cmd);

		// render based on raster-graphics pipeline
		void DrawScene(const Visibility& vis, RENDERPASS renderPass, CommandList cmd, uint32_t flags);
		// Draw the sprites and fonts from the scene
		void DrawSpritesAndFonts(const CameraComponent& camera, bool distortion, CommandList cmd);
		// Draw simple light visualizer geometries
		void DrawLightVisualizers(const Visibility& vis, graphics::CommandList cmd);
		// Draw debug world. You must also enable what parts to draw, eg. SetToDrawGridHelper, etc, see implementation for details what can be enabled.
		void DrawDebugWorld(
			const Scene& scene,
			const CameraComponent& camera,
			CommandList cmd
		);
		void DrawShadowmaps(const Visibility& vis, CommandList cmd);

		// rendering process that includes render-pipelines and compute shaders
		void RenderMeshes(const Visibility& vis, const RenderQueue& renderQueue, RENDERPASS renderPass, uint32_t filterMask, CommandList cmd, uint32_t flags = 0, uint32_t camera_count = 1);
		void RenderTransparents(CommandList cmd);

		// compute shaders-based rendering
		void RenderSlicerMeshes(CommandList cmd);
		void RenderGaussianSplatting(CommandList cmd);
		void RenderDirectVolumes(CommandList cmd);
		void RenderPostprocessChain(CommandList cmd);

		bool RenderProcess();
		bool SlicerProcess();
		void Compose(CommandList cmd);

		// ---------- Post Processings ----------
		void ProcessDeferredResourceRequests(CommandList cmd);

		void Postprocess_Blur_Gaussian(
			const Texture& input,
			const Texture& temp,
			const Texture& output,
			CommandList cmd,
			int mip_src,
			int mip_dst,
			bool wide
		);

		void Postprocess_TemporalAA(
			const TemporalAAResources& res,
			const Texture& input,
			CommandList cmd
		);

		void Postprocess_Tonemap(
			const Texture& input,
			const Texture& output,
			CommandList cmd,
			float exposure,
			float brightness,
			float contrast,
			float saturation,
			bool dither,
			const Texture* texture_colorgradinglut,
			const Texture* texture_distortion,
			const GPUBuffer* buffer_luminance,
			const Texture* texture_bloom,
			ColorSpace display_colorspace,
			Tonemap tonemap,
			const Texture* texture_distortion_overlay,
			float hdr_calibration
		);

		void Postprocess_Downsample4x(
			const Texture& input,
			const Texture& output,
			CommandList cmd
		);

		// ---------- GRenderPath3D's interfaces: -----------------
		bool ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight) override; // must delete all canvas-related resources and re-create
		bool ResizeCanvasSlicer(uint32_t canvasWidth, uint32_t canvasHeight); // must delete all canvas-related resources and re-create
		bool Render(const float dt) override;
		bool Destroy() override;

		const Texture& GetLastProcessRT() const override { return *lastPostprocessRT; }
	};

	struct GSceneDetails : GScene
	{
		GSceneDetails(Scene* scene) : GScene(scene) {}
		virtual ~GSceneDetails() = default;

		// note all GPU resources (their pointers) are managed by
		//  ComPtr or 
		//  RAII (Resource Acquisition Is Initialization) patterns

		// * This renderer plugin is based on Bindless Graphics 
		//	(https://developer.download.nvidia.com/opengl/tutorials/bindless_graphics.pdf)

		// cached attributes of Scene Components
		//	supposed to be safe in a single frame
		float deltaTime = 0.f;
		const Scene* GetScene() const { return scene_; }
		
		std::vector<GLightComponent*> lightComponents; // cached (non enclosing for jobsystem)
		std::vector<GGeometryComponent*> geometryComponents; // cached (non enclosing for jobsystem)
		std::vector<GMaterialComponent*> materialComponents; // cached (non enclosing for jobsystem)
		std::vector<GRenderableComponent*> renderableComponents; // cached (non enclosing for jobsystem)
		std::vector<GRenderableComponent*> spriteComponents; // cached (non enclosing for jobsystem)
		std::vector<GRenderableComponent*> spriteFontComponents; // cached (non enclosing for jobsystem)

		std::vector<geometrics::AABB> aabbRenderables;
		std::vector<geometrics::AABB> aabbLights;
		
		std::vector<XMFLOAT4X4> matrixRenderables;
		std::vector<XMFLOAT4X4> matrixRenderablesPrev;
		//const bool occlusionQueryEnabled = false;
		//const bool cameraFreezeCullingEnabled = false;
		bool isWetmapProcessingRequired = false;
		bool isOutlineEnabled = false;

		ShaderScene shaderscene = {};

		graphics::GraphicsDevice* device = nullptr;
		// Instances (parts) for bindless renderables:
		//	contains in order:
		//		1) renderables (general meshes and volumes)
		size_t instanceArraySize = 0;
		graphics::GPUBuffer instanceUploadBuffer[graphics::GraphicsDevice::GetBufferCount()]; // dynamic GPU-usage
		graphics::GPUBuffer instanceBuffer = {};	// default GPU-usage
		ShaderMeshInstance* instanceArrayMapped = nullptr; // CPU-access buffer pointer for instanceUploadBuffer[%2]

		// Geometries for bindless view indexing:
		//	contains in order:
		//		1) # of primitive parts
		//		2) emitted particles * 1
		size_t geometryArraySize = 0;
		graphics::GPUBuffer geometryUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];
		graphics::GPUBuffer geometryBuffer = {};	// not same to the geometryEntities, reorganized using geometryArraySize
		ShaderGeometry* geometryArrayMapped = nullptr;

		// Materials for bindless view indexing:
		size_t materialArraySize = 0;
		graphics::GPUBuffer materialUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];
		graphics::GPUBuffer materialBuffer = {};
		ShaderMaterial* materialArrayMapped = nullptr;

		graphics::GPUBuffer textureStreamingFeedbackBuffer;	// a sinlge UINT
		graphics::GPUBuffer textureStreamingFeedbackBuffer_readback[graphics::GraphicsDevice::GetBufferCount()];
		const uint32_t* textureStreamingFeedbackMapped = nullptr;

		// Material-index lookup corresponding to each geometry of a renderable
		size_t instanceResLookupSize = 0;
		graphics::GPUBuffer instanceResLookupUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];
		graphics::GPUBuffer instanceResLookupBuffer = {};
		ShaderInstanceResLookup* instanceResLookupMapped = nullptr;

		// Meshlets for 
		//  1. MeshShader or 
		//  2. substitute data structure for reducing PritmiveID texture size:
		graphics::GPUBuffer meshletBuffer = {};
		std::atomic<uint32_t> meshletAllocator{ 0 };	// meshlet refers to a triangle or meshlet

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
		graphics::GPUBuffer queryPredicationBuffer = {};
		uint32_t queryheapIdx = 0;
		mutable std::atomic<uint32_t> queryAllocator{ 0 };

		DebugShapeCollection debugShapes; // dynamic allocation

		bool Update(const float dt) override;
		bool Destroy() override;

		void RunGeometryUpdateSystem(jobsystem::context& ctx);
		void RunMaterialUpdateSystem(jobsystem::context& ctx);
		void RunRenderableUpdateSystem(jobsystem::context& ctx); // note a single renderable can generate multiple mesh instances

		void Debug_AddLine(const XMFLOAT3 p0, const XMFLOAT3 p1, const XMFLOAT4 color0, const XMFLOAT4 color1, const bool depthTest) const override; 
		void Debug_AddPoint(const XMFLOAT3 p, const XMFLOAT4 color, const bool depthTest) const override;
		void Debug_AddCircle(const XMFLOAT3 p, const float r, const XMFLOAT4 color, const bool depthTest) const override;
	};
}

