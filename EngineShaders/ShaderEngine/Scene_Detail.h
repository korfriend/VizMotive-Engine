#pragma once
#include "Renderer.h"

#include "Utils/vzMath.h"
#include "Utils/Geometrics.h"

using namespace vz::geometrics;

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

		GEnvironmentComponent* envrironment = nullptr;
		GProbeComponent globalDynamicProbe;

		std::vector<GGeometryComponent*> geometryComponents; // cached (non enclosing for jobsystem)
		std::vector<GMaterialComponent*> materialComponents; // cached (non enclosing for jobsystem)
		std::vector<GRenderableComponent*> renderableComponents; // cached (non enclosing for jobsystem)
		std::vector<GRenderableComponent*> spriteComponents; // cached (non enclosing for jobsystem)
		std::vector<GRenderableComponent*> spriteFontComponents; // cached (non enclosing for jobsystem)

		std::vector<GLightComponent*> lightComponents; // cached (non enclosing for jobsystem)
		std::vector<GProbeComponent*> probeComponents; // cached (non enclosing for jobsystem)

		std::vector<geometrics::AABB> aabbRenderables;
		std::vector<geometrics::AABB> aabbLights;
		std::vector<geometrics::AABB> aabbProbes;

		std::vector<XMFLOAT4X4> matrixRenderables;
		std::vector<XMFLOAT4X4> matrixRenderablesPrev;
		//const bool occlusionQueryEnabled = false;
		//const bool cameraFreezeCullingEnabled = false;
		bool isWetmapProcessingRequired = false;
		bool isOutlineEnabled = false;

		ShaderScene shaderscene = {};

		bool isAccelerationStructureUpdateRequested = false;
		graphics::RaytracingAccelerationStructure TLAS;
		graphics::GPUBuffer TLAS_instancesUpload[graphics::GraphicsDevice::GetBufferCount()];
		void* TLAS_instancesMapped = nullptr;
		using GPUBVH = GGeometryComponent::BVHBuffers;
		GPUBVH sceneBVH; // this is for non-hardware accelerated raytracing

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
		void RunProbeUpdateSystem(jobsystem::context& ctx);

		void Debug_AddLine(const XMFLOAT3 p0, const XMFLOAT3 p1, const XMFLOAT4 color0, const XMFLOAT4 color1, const bool depthTest) const override;
		void Debug_AddPoint(const XMFLOAT3 p, const XMFLOAT4 color, const bool depthTest) const override;
		void Debug_AddCircle(const XMFLOAT3 p, const float r, const XMFLOAT4 color, const bool depthTest) const override;
	};
}