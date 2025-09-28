#pragma once
#include "Renderer.h"

#include "Utils/vzMath.h"
#include "Utils/Geometrics.h"

namespace vz::renderer
{
	using namespace geometrics;
	struct RenderableLine
	{
		XMFLOAT3 start = XMFLOAT3(0, 0, 0);
		XMFLOAT3 end = XMFLOAT3(0, 0, 0);
		XMFLOAT4 color_start = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 color_end = XMFLOAT4(1, 1, 1, 1);
	};

	struct RenderablePoint
	{
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		float size = 1.0f;
		XMFLOAT4 color = XMFLOAT4(1, 1, 1, 1);
	};

	struct DebugTextParams
	{
		std::string text;

		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		int pixel_height = 32;
		float scaling = 1;
		XMFLOAT4 color = XMFLOAT4(1, 1, 1, 1);
		enum FLAGS
		{
			NONE = 0,
			DEPTH_TEST = 1 << 0,		// text can be occluded by geometry
			CAMERA_FACING = 1 << 1,		// text will be rotated to face the camera
			CAMERA_SCALING = 1 << 2,	// text will be always the same size, independent of distance to camera
		};
		uint32_t flags = NONE;
	};

	struct DebugShapeCollection
	{
	private:
		mutable std::vector<RenderableLine> renderableLines_;
		mutable std::vector<RenderableLine> renderableLines_depth_;
		mutable std::vector<RenderablePoint> renderablePoints_;
		mutable std::vector<RenderablePoint> renderablePoints_depth_;

		mutable std::vector<std::pair<XMFLOAT4X4, XMFLOAT4>> renderableBoxes_;
		mutable std::vector<std::pair<XMFLOAT4X4, XMFLOAT4>> renderableBoxes_depth_;
		mutable std::vector<std::pair<Sphere, XMFLOAT4>> renderableSpheres_;
		mutable std::vector<std::pair<Sphere, XMFLOAT4>> renderableSpheres_depth_;
		mutable std::vector<std::pair<Capsule, XMFLOAT4>> renderableCapsules_;
		mutable std::vector<std::pair<Capsule, XMFLOAT4>> renderableCapsules_depth_;

		mutable std::vector<DebugTextParams> renderableTextStorages_;
		// TODO
		//mutable std::vector<const VoxelGrid*> renderableVoxelgrids_;
		//mutable std::vector<const PathQuery*> renderablePathqueries_;
		//mutable std::vector<const TrailRenderer*> renderableTrails_;

		void drawAndClearLines(const CameraComponent& camera, std::vector<RenderableLine>& renderableLines, CommandList cmd, bool clearEnabled);
	public:
		float depthLineThicknessPixel = 1.3f;

		static constexpr size_t RENDERABLE_SHAPE_RESERVE = 2048; // for fast growing
		DebugShapeCollection() {
			renderableLines_.reserve(RENDERABLE_SHAPE_RESERVE);
			renderableLines_depth_.reserve(RENDERABLE_SHAPE_RESERVE);
			renderablePoints_.reserve(RENDERABLE_SHAPE_RESERVE);
			renderablePoints_depth_.reserve(RENDERABLE_SHAPE_RESERVE);
		}

		void AddDrawLine(const RenderableLine& line, bool depth) const
		{
			if (depth)
				renderableLines_depth_.push_back(line);
			else
				renderableLines_.push_back(line);
		}
		void DrawLines(const CameraComponent& camera, CommandList cmd, bool clearEnabled = true);

		void AddPrimitivePart(const GeometryComponent::Primitive& part, const XMFLOAT4& baseColor, const XMFLOAT4X4& world);

		void AddDebugText(); // TODO

		void DrawDebugTextStorage(const CameraComponent& camera, CommandList cmd, bool clearEnabled);

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
}