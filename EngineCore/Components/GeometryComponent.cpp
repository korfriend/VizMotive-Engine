#include "GComponents.h"
#include "Common/Engine_Internal.h"
#include "Utils/Backlog.h"
#include "Utils/Timer.h"

#include "ThirdParty/mikktspace.h"
#include "ThirdParty/meshoptimizer/meshoptimizer.h"

namespace vz
{
	using Primitive = GeometryComponent::Primitive;

#define MAX_GEOMETRY_PARTS 32 // ShaderInterop.h's `#define MAXPARTS 32`
	void GeometryComponent::MovePrimitivesFrom(std::vector<Primitive>&& primitives)
	{
		waiter_->waitForFree();

		parts_ = std::move(primitives);
		//parts_.assign(primitives.size(), Primitive());
		for (size_t i = 0, n = parts_.size(); i < n; ++i)
		{
			Primitive& prim = parts_[i];
			//prim.MoveFrom(primitives[i]);
			prim.recentBelongingGeometry_ = entity_;
		}
		isDirty_ = true;
		timeStampSetter_ = TimerNow;
	}
	void GeometryComponent::CopyPrimitivesFrom(const std::vector<Primitive>& primitives)
	{
		waiter_->waitForFree();

		parts_ = primitives;
		for (size_t i = 0, n = parts_.size(); i < n; ++i)
		{
			Primitive& prim = parts_[i];
			prim.recentBelongingGeometry_ = entity_;
		}
		isDirty_ = true;
		timeStampSetter_ = TimerNow;
	}

	void tryAssignParts(const size_t slot, std::vector<Primitive>& parts)
	{
		assert(slot < MAX_GEOMETRY_PARTS);
		if (slot >= parts.size()) {
			size_t n = parts.size();
			std::vector<Primitive> parts_tmp(n);
			for (size_t i = 0; i < n; ++i)
			{
				parts_tmp[i].MoveFrom(std::move(parts[i]));
			}
			parts.assign(slot + 1, Primitive());
			for (size_t i = 0; i < n; ++i)
			{
				parts_tmp[i].MoveTo(parts[i]);
			}
		}
	}
	void GeometryComponent::MovePrimitiveFrom(Primitive&& primitive, const size_t slot)
	{
		waiter_->waitForFree();

		tryAssignParts(slot, parts_);
		Primitive& prim = parts_[slot];
		prim.MoveFrom(std::move(primitive));
		prim.recentBelongingGeometry_ = entity_;
		isDirty_ = true;
		timeStampSetter_ = TimerNow;
	}
	void GeometryComponent::CopyPrimitiveFrom(const Primitive& primitive, const size_t slot)
	{
		waiter_->waitForFree();

		tryAssignParts(slot, parts_);
		parts_[slot] = primitive;
		Primitive& prim = parts_[slot];
		prim.recentBelongingGeometry_ = entity_;
		isDirty_ = true;
		timeStampSetter_ = TimerNow;
	}
	void GeometryComponent::AddMovePrimitiveFrom(Primitive&& primitive)
	{
		waiter_->waitForFree();

		parts_.push_back(std::move(primitive));
		parts_.back().recentBelongingGeometry_ = entity_;
		isDirty_ = true;
		timeStampSetter_ = TimerNow;
	}
	void GeometryComponent::AddCopyPrimitiveFrom(const Primitive& primitive)
	{
		parts_.push_back(primitive);
		parts_.back().recentBelongingGeometry_ = entity_;
		isDirty_ = true;
		timeStampSetter_ = TimerNow;
	}

	void GeometryComponent::ClearGeometry()
	{
		waiter_->waitForFree();

		parts_.clear();
		DeleteRenderData();

		isDirty_ = true;
		hasBVH_ = false;
		aabb_ = geometrics::AABB();
	}
	const Primitive* GeometryComponent::GetPrimitive(const size_t slot) const
	{
		if (slot >= parts_.size()) {
			backlog::post("slot is over # of parts!", backlog::LogLevel::Error);
			return nullptr;
		}
		return &parts_[slot];
	}

	Primitive* GeometryComponent::GetMutablePrimitive(const size_t slot)
	{
		if (slot >= parts_.size()) {
			backlog::post("slot is over # of parts!", backlog::LogLevel::Error);
			return nullptr;
		}
		waiter_->waitForFree();
		return &parts_[slot];
	}

}

namespace vz
{
	void GeometryComponent::update()
	{
		size_t subset_count = 0;
		aabb_ = {};
		for (size_t i = 0, n = parts_.size(); i < n; ++i)
		{
			Primitive& prim = parts_[i];

			prim.updateGpuEssentials();	// update prim.aabb_

			size_t count = prim.subsets_.size();
			if (count == 0)
			{
				prim.subsets_ = { {0, (uint32_t)prim.indexPrimitives_.size()} };
				count = 1;
			}
			if (subset_count > 0) vzlog_assert(subset_count == count, "Each part MUST have the same LODs!");
			subset_count = count;

			aabb_._max = math::Max(aabb_._max, prim.aabb_._max);
			aabb_._min = math::Min(aabb_._min, prim.aabb_._min);
		}

		// TODO: UPDATE 'partLODs_'
		partLODs_ = subset_count;

		timeStampPrimitiveUpdate_ = TimerNow;
		hasBVH_ = false;
		isDirty_ = false;
	}
}

namespace vz
{
	using namespace graphics;

	struct MikkTSpaceUserdata
	{
		const std::vector<XMFLOAT3>& vertexPositions;
		std::vector<XMFLOAT3>& vertexNormals;
		std::vector<XMFLOAT4>& vertexTangents;
		std::vector<XMFLOAT2>& vertexUVset;

		MikkTSpaceUserdata(const std::vector<XMFLOAT3>& vtxPositions,
			std::vector<XMFLOAT3>& vtxNormals, std::vector<XMFLOAT4>& vtxTangents, std::vector<XMFLOAT2>& vtxUVset) :
			vertexPositions(vtxPositions),
			vertexNormals(vtxNormals),
			vertexTangents(vtxTangents),
			vertexUVset(vtxUVset) {}

		const uint32_t* indicesLOD0 = nullptr;
		int faceCountLOD0 = 0;
	};
	int get_num_faces(const SMikkTSpaceContext* context)
	{
		const MikkTSpaceUserdata* userdata = static_cast<const MikkTSpaceUserdata*>(context->m_pUserData);
		return userdata->faceCountLOD0;
	}
	int get_num_vertices_of_face(const SMikkTSpaceContext* context, const int iFace)
	{
		return 3;
	}
	int get_vertex_index(const SMikkTSpaceContext* context, int iFace, int iVert)
	{
		const MikkTSpaceUserdata* userdata = static_cast<const MikkTSpaceUserdata*>(context->m_pUserData);
		int face_size = get_num_vertices_of_face(context, iFace);
		int indices_index = iFace * face_size + iVert;
		int index = int(userdata->indicesLOD0[indices_index]);
		return index;
	}
	void get_position(const SMikkTSpaceContext* context, float* outpos, const int iFace, const int iVert)
	{
		const MikkTSpaceUserdata* userdata = static_cast<const MikkTSpaceUserdata*>(context->m_pUserData);
		int index = get_vertex_index(context, iFace, iVert);
		const XMFLOAT3& vert = userdata->vertexPositions[index];
		outpos[0] = vert.x;
		outpos[1] = vert.y;
		outpos[2] = vert.z;
	}
	void get_normal(const SMikkTSpaceContext* context, float* outnormal, const int iFace, const int iVert)
	{
		const MikkTSpaceUserdata* userdata = static_cast<const MikkTSpaceUserdata*>(context->m_pUserData);
		int index = get_vertex_index(context, iFace, iVert);
		const XMFLOAT3& vert = userdata->vertexNormals[index];
		outnormal[0] = vert.x;
		outnormal[1] = vert.y;
		outnormal[2] = vert.z;
	}
	void get_tex_coords(const SMikkTSpaceContext* context, float* outuv, const int iFace, const int iVert)
	{
		const MikkTSpaceUserdata* userdata = static_cast<const MikkTSpaceUserdata*>(context->m_pUserData);
		int index = get_vertex_index(context, iFace, iVert);
		const XMFLOAT2& vert = userdata->vertexUVset[index];
		outuv[0] = vert.x;
		outuv[1] = vert.y;
	}
	void set_tspace_basic(const SMikkTSpaceContext* context, const float* tangentu, const float fSign, const int iFace, const int iVert)
	{
		const MikkTSpaceUserdata* userdata = static_cast<const MikkTSpaceUserdata*>(context->m_pUserData);
		auto index = get_vertex_index(context, iFace, iVert);
		XMFLOAT4& vert = userdata->vertexTangents[index];
		vert.x = tangentu[0];
		vert.y = tangentu[1];
		vert.z = tangentu[2];
		vert.w = fSign;
	}

	void Primitive::updateGpuEssentials()
	{
		std::vector<XMFLOAT3>& vertex_positions = vertexPositions_;
		std::vector<uint32_t>& indices = indexPrimitives_;
		std::vector<XMFLOAT3>& vertex_normals = vertexNormals_;
		std::vector<XMFLOAT4>& vertex_tangents = vertexTangents_;
		std::vector<XMFLOAT2>& vertex_uvset_0 = vertexUVset0_;
		std::vector<XMFLOAT2>& vertex_uvset_1 = vertexUVset1_;

		if (vertex_tangents.size() != vertex_positions.size())
		{
			vertex_tangents.clear();
		}
		if (vertex_uvset_0.size() != vertex_positions.size())
		{
			vertex_uvset_0.clear();
		}
		if (vertex_uvset_1.size() != vertex_positions.size())
		{
			vertex_uvset_1.clear();
		}
		if (vertex_normals.size() != vertex_positions.size())
		{
			vertex_normals.clear();
		}

		// TANGENT computation (must be computed for quantized normals)
		if (ptype_ == PrimitiveType::TRIANGLES && vertex_tangents.empty()
			&& !vertex_uvset_0.empty() && !vertex_normals.empty())
		{
			// Generate tangents if not found:
			vertex_tangents.resize(vertex_positions.size());

			// MikkTSpace tangent generation:
			MikkTSpaceUserdata userdata(vertex_positions, vertex_normals, vertex_tangents, vertex_uvset_0);
			userdata.indicesLOD0 = indices.data();
			userdata.faceCountLOD0 = indices.size() / 3;

			SMikkTSpaceInterface iface = {};
			iface.m_getNumFaces = get_num_faces;
			iface.m_getNumVerticesOfFace = get_num_vertices_of_face;
			iface.m_getNormal = get_normal;
			iface.m_getPosition = get_position;
			iface.m_getTexCoord = get_tex_coords;
			iface.m_setTSpaceBasic = set_tspace_basic;
			SMikkTSpaceContext context = {};
			context.m_pInterface = &iface;
			context.m_pUserData = &userdata;
			tbool mikktspace_result = genTangSpaceDefault(&context);
			assert(mikktspace_result == 1);
		}

		const size_t uv_count = std::max(vertex_uvset_0.size(), vertex_uvset_1.size());

		// Bounds computation:
		XMFLOAT3 _min = XMFLOAT3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		XMFLOAT3 _max = XMFLOAT3(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
		for (size_t i = 0; i < vertex_positions.size(); ++i)
		{
			const XMFLOAT3& pos = vertex_positions[i];
			_min = math::Min(_min, pos);
			_max = math::Max(_max, pos);
		}
		aabb_ = geometrics::AABB(_min, _max);

		// Determine UV range for normalization:
		uvStride_ = sizeof(GGeometryComponent::Vertex_UVS);
		//uvFormat_ = GGeometryComponent::Vertex_UVS::FORMAT;
		useFullPrecisionUV_ = false;
		if (!vertex_uvset_0.empty() || !vertex_uvset_1.empty())
		{
			const XMFLOAT2* uv0_stream = vertex_uvset_0.empty() ? vertex_uvset_1.data() : vertex_uvset_0.data();
			const XMFLOAT2* uv1_stream = vertex_uvset_1.empty() ? vertex_uvset_0.data() : vertex_uvset_1.data();

			uvRangeMin_ = XMFLOAT2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
			uvRangeMax_ = XMFLOAT2(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
			for (size_t i = 0; i < uv_count; ++i)
			{
				uvRangeMax_ = math::Max(uvRangeMax_, uv0_stream[i]);
				uvRangeMax_ = math::Max(uvRangeMax_, uv1_stream[i]);
				uvRangeMin_ = math::Min(uvRangeMin_, uv0_stream[i]);
				uvRangeMin_ = math::Min(uvRangeMin_, uv1_stream[i]);
			}
			if (std::abs(uvRangeMax_.x - uvRangeMin_.x) > 65536 || std::abs(uvRangeMax_.y - uvRangeMin_.y) > 65536)
			{
				// The bounding box of UVs is too large, fall back to full precision UVs:
				uvStride_ = sizeof(GGeometryComponent::Vertex_UVS32);
				//uvFormat_ = GGeometryComponent::Vertex_UVS32::FORMAT;
				useFullPrecisionUV_ = true;
			}
		}
	}

	bool isMeshConvex2(const std::vector<XMFLOAT3>& positions,
		const std::vector<uint32_t>& indices,
		float eps = 1e-6f)
	{
		using namespace DirectX;
		const size_t vCount = positions.size();
		const size_t fCount = indices.size() / 3;

		if (vCount < 4 || fCount < 4)      // need at least a tetrahedron
			return false;

		/*-----------------------------------------------------------
		* 1. Compute mesh centroid (approximate)
		*----------------------------------------------------------*/
		XMVECTOR centroid = XMVectorZero();
		for (const auto& p : positions)
			centroid += XMLoadFloat3(&p);
		centroid /= float(vCount);

		/*-----------------------------------------------------------
		* 2. For every face plane …
		*----------------------------------------------------------*/
		for (size_t f = 0; f < fCount; ++f)
		{
			const XMFLOAT3& p0 = positions[indices[3 * f + 0]];
			const XMFLOAT3& p1 = positions[indices[3 * f + 1]];
			const XMFLOAT3& p2 = positions[indices[3 * f + 2]];

			const XMVECTOR v0 = XMLoadFloat3(&p0);
			const XMVECTOR v1 = XMLoadFloat3(&p1);
			const XMVECTOR v2 = XMLoadFloat3(&p2);

			/* Face normal (unnormalised) */
			XMVECTOR n = XMVector3Cross(v1 - v0, v2 - v0);

			/* If area is ~0 the mesh is degenerate */
			if (XMVector3Equal(n, XMVectorZero()))
				return false;

			/* Make normal point outwards */
			if (XMVectorGetX(XMVector3Dot(n, centroid - v0)) > 0.0f)
				n = -n;   // flip

			/* Plane D param  (n·X + d = 0) */
			const float d = -XMVectorGetX(XMVector3Dot(n, v0));

			/*-------------------------------------------------------
			* 3. All other vertices must be inside/ on plane
			*------------------------------------------------------*/
			for (size_t vi = 0; vi < vCount; ++vi)
			{
				/* Skip the face’s own vertices – not necessary but faster */
				if (vi == indices[3 * f] ||
					vi == indices[3 * f + 1] ||
					vi == indices[3 * f + 2])
					continue;

				const XMVECTOR pv = XMLoadFloat3(&positions[vi]);
				const float    dist = XMVectorGetX(XMVector3Dot(n, pv)) + d;

				/* Any vertex strictly in front of plane → non-convex */
				if (dist > eps)
					return false;
			}
		}
		return true;    /* Every face passed – mesh is convex */
	}

	bool isMeshConvex(const std::vector<XMFLOAT3>& positions,
		const std::vector<uint32_t>& indices,
		float eps = 1e-6f)
	{
		using namespace DirectX;

		const size_t vCount = positions.size();
		const size_t fCount = indices.size() / 3;
		if (vCount < 4 || fCount < 4) return false;          // need at least a tetrahedron

		// -------------------------------------------------------------------------
		// For every face, make sure all *other* vertices lie on (or behind) that
		// plane.  We do not care which side is “front” because we look at both signs.
		// -------------------------------------------------------------------------
		for (size_t f = 0; f < fCount; ++f)
		{
			const XMFLOAT3& p0 = positions[indices[3 * f + 0]];
			const XMFLOAT3& p1 = positions[indices[3 * f + 1]];
			const XMFLOAT3& p2 = positions[indices[3 * f + 2]];

			const XMVECTOR v0 = XMLoadFloat3(&p0);
			const XMVECTOR v1 = XMLoadFloat3(&p1);
			const XMVECTOR v2 = XMLoadFloat3(&p2);

			// Face normal (unnormalised) and length-check
			const XMVECTOR n = XMVector3Cross(v1 - v0, v2 - v0);
			const float    nLen2 = XMVectorGetX(XMVector3LengthSq(n));
			if (nLen2 < 1e-12f) return false;                // degenerate / zero-area face

			// Plane equation: n • X + d = 0
			const float d = -XMVectorGetX(XMVector3Dot(n, v0));

			float minDist = FLT_MAX;
			float maxDist = -FLT_MAX;

			// Check every vertex (except the three of this face)
			for (size_t vi = 0; vi < vCount; ++vi)
			{
				if (vi == indices[3 * f] ||
					vi == indices[3 * f + 1] ||
					vi == indices[3 * f + 2])
					continue;

				const float dist =
					XMVectorGetX(XMVector3Dot(n, XMLoadFloat3(&positions[vi]))) + d;

				minDist = std::min(minDist, dist);
				maxDist = std::max(maxDist, dist);

				// Early exit: if vertices exist on *both* sides outside the band
				if (minDist < -eps && maxDist > eps)
					return false;            // non-convex
			}
		}
		return true;                         // all faces passed
	}

	void Primitive::updateBVH(const bool enabled)
	{
		//vzlog_assert(ptype_ == PrimitiveType::TRIANGLES, "BVH is allowed only for triangle mesh (no stripe)");

		if (!enabled)
		{
			bvhLeafAabbs_.clear();
		}
		else if (!bvh_.IsValid())
		{
			uint32_t index_count = indexPrimitives_.size();
			if (index_count == 0)
				return;

			switch (ptype_)
			{
			case PrimitiveType::TRIANGLES:
			{
				Timer timer;

				const uint32_t triangle_count = index_count / 3;
				for (uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index)
				{
					const uint32_t i0 = indexPrimitives_[triangle_index * 3 + 0];
					const uint32_t i1 = indexPrimitives_[triangle_index * 3 + 1];
					const uint32_t i2 = indexPrimitives_[triangle_index * 3 + 2];
					const XMFLOAT3& p0 = vertexPositions_[i0];
					const XMFLOAT3& p1 = vertexPositions_[i1];
					const XMFLOAT3& p2 = vertexPositions_[i2];
					geometrics::AABB aabb = geometrics::AABB(math::Min(p0, math::Min(p1, p2)), math::Max(p0, math::Max(p1, p2)));
					aabb.layerMask = triangle_index;
					//aabb.userdata = part_index;
					bvhLeafAabbs_.push_back(aabb);
				}
				bvh_.maxLeafPrimitives = 2;
				bvh_.Build(bvhLeafAabbs_.data(), (uint32_t)bvhLeafAabbs_.size());

				if (triangle_count < 100)
				{
					XMVECTOR ext = XMLoadFloat3(&aabb_._max) - XMLoadFloat3(&aabb_._min);
					float diag = XMVectorGetX(XMVector3Length(ext));
					const float userScale = 1e-5f;
					isConvex = isMeshConvex(vertexPositions_, indexPrimitives_, diag * userScale);
					if (isConvex)
						backlog::postThreadSafe("Simple Convex Shape");
				}
				else
					isConvex = false;

				backlog::postThreadSafe("CPUBVH updated (" + std::to_string((int)std::round(timer.elapsed())) + " ms)" + " # of tris: " + std::to_string(triangle_count));
			} 
			break;
			case PrimitiveType::LINES:
			{
				Timer timer;

				const uint32_t line_count = index_count / 2;
				for (uint32_t line_index = 0; line_index < line_count; ++line_index)
				{
					const uint32_t i0 = indexPrimitives_[line_index * 2 + 0];
					const uint32_t i1 = indexPrimitives_[line_index * 2 + 1];
					const XMFLOAT3& p0 = vertexPositions_[i0];
					const XMFLOAT3& p1 = vertexPositions_[i1];
					geometrics::AABB aabb = geometrics::AABB(math::Min(p0, p1), math::Max(p0, p1));
					aabb.layerMask = line_index;
					//aabb.userdata = part_index;
					bvhLeafAabbs_.push_back(aabb);
				}
				bvh_.maxLeafPrimitives = 2;
				bvh_.Build(bvhLeafAabbs_.data(), (uint32_t)bvhLeafAabbs_.size());

				backlog::postThreadSafe("CPUBVH updated (" + std::to_string((int)std::round(timer.elapsed())) + " ms)" + " # of tris: " + std::to_string(line_count));
			}
			default:
				backlog::postThreadSafe("Invalid Primitive Type for CPUBVH!", backlog::LogLevel::Warn);
				return;
			}
		}
		else
		{
			backlog::postThreadSafe("CPUBVH is already updated. so ignore the update request!", backlog::LogLevel::Warn);
		}
	}

	size_t Primitive::GetMemoryUsageCPU() const
	{
		size_t footage_bytes = 0;
		footage_bytes += vertexPositions_.size() * sizeof(XMFLOAT3);
		footage_bytes += vertexNormals_.size() * sizeof(XMFLOAT3);
		footage_bytes += vertexTangents_.size() * sizeof(XMFLOAT4);
		footage_bytes += vertexUVset0_.size() * sizeof(XMFLOAT2);
		footage_bytes += vertexUVset1_.size() * sizeof(XMFLOAT2);
		footage_bytes += vertexColors_.size() * sizeof(uint32_t);
		footage_bytes += indexPrimitives_.size() * sizeof(uint32_t);

		// TODO 
		// BVH, SH, ...

		return footage_bytes;
	}

#define AUTO_RENDER_DATA GeometryComponent* geometry = compfactory::GetGeometryComponent(recentBelongingGeometry_);\
	if (geometry && autoUpdateRenderData) geometry->UpdateRenderData();

	void Primitive::FillIndicesFromTriVertices()
	{
		if (ptype_ != PrimitiveType::TRIANGLES)
		{
			vzlog_error("FillIndicesFromTriVertices is allowed for Triangular mesh");
			return;
		}

		size_t n = vertexPositions_.size();
		if (n < 3 || n % 3 != 0)
		{
			vzlog_error("Invalid triangles");
			return;
		}

		indexPrimitives_.resize(n);
		for (size_t i = 0; i < n; ++i)
		{
			indexPrimitives_[i] = i;
		}
	}

	void Primitive::ComputeNormals(NormalComputeMethod computeMode)
	{
		// Start recalculating normals:

		if (computeMode != NormalComputeMethod::COMPUTE_NORMALS_SMOOTH_FAST)
		{
			// Compute hard surface normals:

			// Right now they are always computed even before smooth setting

			std::vector<uint32_t> newIndexBuffer;
			std::vector<XMFLOAT3> newPositionsBuffer;
			std::vector<XMFLOAT3> newNormalsBuffer;
			std::vector<XMFLOAT2> newUV0Buffer;
			std::vector<XMFLOAT2> newUV1Buffer;
			std::vector<XMFLOAT2> newAtlasBuffer;
			std::vector<XMUINT4> newBoneIndicesBuffer;
			std::vector<XMFLOAT4> newBoneWeightsBuffer;
			std::vector<uint32_t> newColorsBuffer;

			for (size_t face = 0; face < indexPrimitives_.size() / 3; face++)
			{
				uint32_t i0 = indexPrimitives_[face * 3 + 0];
				uint32_t i1 = indexPrimitives_[face * 3 + 1];
				uint32_t i2 = indexPrimitives_[face * 3 + 2];

				XMFLOAT3& p0 = vertexPositions_[i0];
				XMFLOAT3& p1 = vertexPositions_[i1];
				XMFLOAT3& p2 = vertexPositions_[i2];

				XMVECTOR U = XMLoadFloat3(&p1) - XMLoadFloat3(&p0);
				XMVECTOR V = XMLoadFloat3(&p2) - XMLoadFloat3(&p0);

				XMVECTOR N = XMVector3Cross(U, V);
				N = XMVector3Normalize(N);

				XMFLOAT3 normal;
				XMStoreFloat3(&normal, N);

				newPositionsBuffer.push_back(p0);
				newPositionsBuffer.push_back(p1);
				newPositionsBuffer.push_back(p2);

				newNormalsBuffer.push_back(normal);
				newNormalsBuffer.push_back(normal);
				newNormalsBuffer.push_back(normal);

				if (!vertexUVset0_.empty())
				{
					newUV0Buffer.push_back(vertexUVset0_[i0]);
					newUV0Buffer.push_back(vertexUVset0_[i1]);
					newUV0Buffer.push_back(vertexUVset0_[i2]);
				}

				if (!vertexUVset1_.empty())
				{
					newUV1Buffer.push_back(vertexUVset1_[i0]);
					newUV1Buffer.push_back(vertexUVset1_[i1]);
					newUV1Buffer.push_back(vertexUVset1_[i2]);
				}

				if (!vertexColors_.empty())
				{
					newColorsBuffer.push_back(vertexColors_[i0]);
					newColorsBuffer.push_back(vertexColors_[i1]);
					newColorsBuffer.push_back(vertexColors_[i2]);
				}

				newIndexBuffer.push_back(static_cast<uint32_t>(newIndexBuffer.size()));
				newIndexBuffer.push_back(static_cast<uint32_t>(newIndexBuffer.size()));
				newIndexBuffer.push_back(static_cast<uint32_t>(newIndexBuffer.size()));
			}

			// For hard surface normals, we created a new mesh in the previous loop through faces, so swap data:
			vertexPositions_ = newPositionsBuffer;
			vertexNormals_ = newNormalsBuffer;
			vertexUVset0_ = newUV0Buffer;
			vertexUVset1_ = newUV1Buffer;
			vertexColors_ = newColorsBuffer;
			indexPrimitives_ = newIndexBuffer;
		}

		switch (computeMode)
		{
		case NormalComputeMethod::COMPUTE_NORMALS_HARD:
			break;

		case NormalComputeMethod::COMPUTE_NORMALS_SMOOTH:
		{
			// Compute smooth surface normals:

			// 1.) Zero normals, they will be averaged later
			for (size_t i = 0; i < vertexNormals_.size(); i++)
			{
				vertexNormals_[i] = XMFLOAT3(0, 0, 0);
			}

			std::vector<size_t> vtx_counter(vertexPositions_.size(), 0);
			// 2.) Find identical vertices by POSITION, accumulate face normals
			for (size_t i = 0; i < vertexPositions_.size(); i++)
			{
				XMFLOAT3& v_search_pos = vertexPositions_[i];

				for (size_t ind = 0; ind < indexPrimitives_.size() / 3; ++ind)
				{
					uint32_t i0 = indexPrimitives_[ind * 3 + 0];
					uint32_t i1 = indexPrimitives_[ind * 3 + 1];
					uint32_t i2 = indexPrimitives_[ind * 3 + 2];

					XMFLOAT3& v0 = vertexPositions_[i0];
					XMFLOAT3& v1 = vertexPositions_[i1];
					XMFLOAT3& v2 = vertexPositions_[i2];


					bool match_pos0 =
						math::float_equal(v_search_pos.x, v0.x) &&
						math::float_equal(v_search_pos.y, v0.y) &&
						math::float_equal(v_search_pos.z, v0.z);

					bool match_pos1 =
						math::float_equal(v_search_pos.x, v1.x) &&
						math::float_equal(v_search_pos.y, v1.y) &&
						math::float_equal(v_search_pos.z, v1.z);

					bool match_pos2 =
						math::float_equal(v_search_pos.x, v2.x) &&
						math::float_equal(v_search_pos.y, v2.y) &&
						math::float_equal(v_search_pos.z, v2.z);

					if (match_pos0 || match_pos1 || match_pos2)
					{
						XMVECTOR U = XMLoadFloat3(&v1) - XMLoadFloat3(&v0);
						XMVECTOR V = XMLoadFloat3(&v2) - XMLoadFloat3(&v0);

						XMVECTOR N = XMVector3Cross(U, V);
						N = XMVector3Normalize(N);

						XMFLOAT3 normal;
						XMStoreFloat3(&normal, N);

						vertexNormals_[i].x += normal.x;
						vertexNormals_[i].y += normal.y;
						vertexNormals_[i].z += normal.z;
						vtx_counter[i]++;
					}

				}
			}

			// 3.) Find duplicated vertices by POSITION and UV0 and UV1 and remove them:
			uint32_t first_subset = 0;
			uint32_t last_subset = 0;

			for (uint32_t i = 0; i < indexPrimitives_.size() - 1; i++)
			{
				uint32_t ind0 = indexPrimitives_[i];
				const XMFLOAT3& p0 = vertexPositions_[ind0];
				const XMFLOAT2& u00 = vertexUVset0_.empty() ? XMFLOAT2(0, 0) : vertexUVset0_[ind0];
				const XMFLOAT2& u10 = vertexUVset1_.empty() ? XMFLOAT2(0, 0) : vertexUVset1_[ind0];

				for (uint32_t j = 1; j < indexPrimitives_.size(); j++)
				{
					uint32_t ind1 = indexPrimitives_[j];

					if (ind1 == ind0)
					{
						continue;
					}

					const XMFLOAT3& p1 = vertexPositions_[ind1];
					const XMFLOAT2& u01 = vertexUVset0_.empty() ? XMFLOAT2(0, 0) : vertexUVset0_[ind1];
					const XMFLOAT2& u11 = vertexUVset1_.empty() ? XMFLOAT2(0, 0) : vertexUVset1_[ind1];

					const bool duplicated_pos =
						math::float_equal(p0.x, p1.x) &&
						math::float_equal(p0.y, p1.y) &&
						math::float_equal(p0.z, p1.z);

					const bool duplicated_uv0 =
						math::float_equal(u00.x, u01.x) &&
						math::float_equal(u00.y, u01.y);

					const bool duplicated_uv1 =
						math::float_equal(u10.x, u11.x) &&
						math::float_equal(u10.y, u11.y);

					if (duplicated_pos && duplicated_uv0 && duplicated_uv1)
					{
						// Erase vertices[ind1] because it is a duplicate:
						if (ind1 < vertexPositions_.size())
						{
							vertexPositions_.erase(vertexPositions_.begin() + ind1);
						}
						if (ind1 < vertexNormals_.size())
						{
							vertexNormals_.erase(vertexNormals_.begin() + ind1);
						}
						if (ind1 < vertexUVset0_.size())
						{
							vertexUVset0_.erase(vertexUVset0_.begin() + ind1);
						}
						if (ind1 < vertexUVset1_.size())
						{
							vertexUVset1_.erase(vertexUVset1_.begin() + ind1);
						}
						if (ind1 < vtx_counter.size())
						{
							vtx_counter.erase(vtx_counter.begin() + ind1);
						}

						// The vertices[ind1] was removed, so each index after that needs to be updated:
						for (auto& index : indexPrimitives_)
						{
							if (index > ind1 && index > 0)
							{
								index--;
							}
							else if (index == ind1)
							{
								index = ind0;
							}
						}

					}
				}
			}

			// Normalize //
			for (size_t i = 0; i < vertexNormals_.size(); i++)
			{
				float num_rcp = 1.f / (float)vtx_counter[i];
				XMFLOAT3& n = vertexNormals_[i];
				n.x *= num_rcp;
				n.y *= num_rcp;
				n.z *= num_rcp;
			}
		}
		break;

		case NormalComputeMethod::COMPUTE_NORMALS_SMOOTH_FAST:
		{

			std::vector<size_t> vtx_counter(vertexPositions_.size(), 0);

			vertexNormals_.resize(vertexPositions_.size());
			for (size_t i = 0; i < vertexNormals_.size(); i++)
			{
				vertexNormals_[i] = XMFLOAT3(0, 0, 0);
			}

			uint32_t num_prims = indexPrimitives_.size() / 3;
			for (uint32_t i = 0; i < num_prims; ++i)
			{
				uint32_t index1 = indexPrimitives_[i * 3 + 0];
				uint32_t index2 = indexPrimitives_[i * 3 + 1];
				uint32_t index3 = indexPrimitives_[i * 3 + 2];

				XMVECTOR side1 = XMLoadFloat3(&vertexPositions_[index2]) - XMLoadFloat3(&vertexPositions_[index1]);
				XMVECTOR side2 = XMLoadFloat3(&vertexPositions_[index3]) - XMLoadFloat3(&vertexPositions_[index1]);
				XMVECTOR N = XMVector3Normalize(XMVector3Cross(side1, side2));
				XMFLOAT3 normal;
				XMStoreFloat3(&normal, N);

				vertexNormals_[index1].x += normal.x;
				vertexNormals_[index1].y += normal.y;
				vertexNormals_[index1].z += normal.z;
				vtx_counter[index1]++;

				vertexNormals_[index2].x += normal.x;
				vertexNormals_[index2].y += normal.y;
				vertexNormals_[index2].z += normal.z;
				vtx_counter[index2]++;

				vertexNormals_[index3].x += normal.x;
				vertexNormals_[index3].y += normal.y;
				vertexNormals_[index3].z += normal.z;
				vtx_counter[index3]++;
			}


			// Normalize //
			for (size_t i = 0; i < vertexNormals_.size(); i++)
			{
				float num_rcp = 1.f / (float)vtx_counter[i];
				XMFLOAT3& n = vertexNormals_[i];
				n.x *= num_rcp;
				n.y *= num_rcp;
				n.z *= num_rcp;
			}
		}
		break;

		}

		vertexTangents_.clear(); // <- will be recomputed

		AUTO_RENDER_DATA;
	}
	void Primitive::ComputeAABB()
	{
		geometrics::AABB aabb;
		for (size_t i = 0, n = vertexPositions_.size(); i < n; ++i)
		{
			XMFLOAT3& p = vertexPositions_[i];
			aabb._min = math::Min(aabb._min, p);
			aabb._max = math::Max(aabb._max, p);
		}
		aabb_ = aabb;
	}
	void Primitive::FlipCulling()
	{
		for (size_t face = 0; face < indexPrimitives_.size() / 3; face++)
		{
			uint32_t i0 = indexPrimitives_[face * 3 + 0];
			uint32_t i1 = indexPrimitives_[face * 3 + 1];
			uint32_t i2 = indexPrimitives_[face * 3 + 2];

			indexPrimitives_[face * 3 + 0] = i0;
			indexPrimitives_[face * 3 + 1] = i2;
			indexPrimitives_[face * 3 + 2] = i1;
		}

		AUTO_RENDER_DATA;
	}
	void Primitive::FlipNormals()
	{
		for (auto& normal : vertexNormals_)
		{
			normal.x *= -1;
			normal.y *= -1;
			normal.z *= -1;
		}

		AUTO_RENDER_DATA;
	}
	void Primitive::ReoriginToCenter()
	{
		XMFLOAT3 center = aabb_.getCenter();

		for (auto& pos : vertexPositions_)
		{
			pos.x -= center.x;
			pos.y -= center.y;
			pos.z -= center.z;
		}

		AUTO_RENDER_DATA;
	}
	void Primitive::ReoriginToBottom()
	{
		XMFLOAT3 center = aabb_.getCenter();
		center.y -= aabb_.getHalfWidth().y;

		for (auto& pos : vertexPositions_)
		{
			pos.x -= center.x;
			pos.y -= center.y;
			pos.z -= center.z;
		}

		AUTO_RENDER_DATA;
	}
	//size_t Primitive::CreateSubset()
	//{
	//	int ret = 0;
	//	const uint32_t lod_count = GetLODCount();
	//	for (uint32_t lod = 0; lod < lod_count; ++lod)
	//	{
	//		uint32_t first_subset = 0;
	//		uint32_t last_subset = 0;
	//		GetLODSubsetRange(lod, first_subset, last_subset);
	//		GGeometryComponent::MeshSubset subset;
	//		subset.indexOffset = ~0u;
	//		subset.indexCount = 0;
	//		for (uint32_t subsetIndex = first_subset; subsetIndex < last_subset; ++subsetIndex)
	//		{
	//			subset.indexOffset = std::min(subset.indexOffset, subsets[subsetIndex].indexOffset);
	//			subset.indexCount = std::max(subset.indexCount, subsets[subsetIndex].indexOffset + subsets[subsetIndex].indexCount);
	//		}
	//		subsets.insert(subsets.begin() + last_subset, subset);
	//		if (lod == 0)
	//		{
	//			ret = last_subset;
	//		}
	//	}
	//	if (lod_count > 0)
	//	{
	//		subsets_per_lod++;
	//	}
	//	CreateRenderData(); // mesh shader needs to rebuild clusters, otherwise wouldn't be needed
	//	return ret;
	//}
}

namespace vz
{
	void GeometryComponent::UpdateBVH(const bool enabled)
	{
		std::lock_guard<std::mutex> lock(*mutex_);
		bool expected = true;
		if (!waiter_->freeTestAndSetWait())
		{
			return;
		}
		//if (!waiter_->isFree())
		//{
		//	return;
		//}
		//waiter_->setWait();
		
		for (Primitive& prim : parts_)
		{
			switch (prim.GetPrimitiveType())
			{
			case PrimitiveType::TRIANGLES:
			case PrimitiveType::LINES:
				prim.updateBVH(enabled);
				break;
			default:
				break;
			}
		}
		hasBVH_ = enabled;
		timeStampSetter_ = TimerNow;
		timeStampBVHUpdate_ = TimerNow;
		waiter_->setFree();
	}
}

using uint = uint32_t;
using float3 = XMFLOAT3;
using uint3 = XMUINT3;
static const uint MESHLET_VERTEX_COUNT = 64u;
static const uint MESHLET_TRIANGLE_COUNT = 124u;
// the following structures that defines meshlet clusters MUST BE same to the structures defined in ShaderInteropXXX.h
struct ShaderClusterTriangle
{
	uint raw;
	void Init(uint i0, uint i1, uint i2, uint flags = 0u)
	{
		raw = 0;
		raw |= i0 & 0xFF;
		raw |= (i1 & 0xFF) << 8u;
		raw |= (i2 & 0xFF) << 16u;
		raw |= (flags & 0xFF) << 24u;
	}
	uint i0() { return raw & 0xFF; }
	uint i1() { return (raw >> 8u) & 0xFF; }
	uint i2() { return (raw >> 16u) & 0xFF; }
	uint3 tri() { return uint3(i0(), i1(), i2()); }
	uint flags() { return raw >> 24u; }
};
struct alignas(16) ShaderCluster
{
	uint triangleCount;
	uint vertexCount;
	uint padding0;
	uint padding1;

	uint vertices[MESHLET_VERTEX_COUNT];
	ShaderClusterTriangle triangles[MESHLET_TRIANGLE_COUNT];
};
struct alignas(16) ShaderSphere
{
	float3 center;
	float radius;
};
struct alignas(16) ShaderClusterBounds
{
	ShaderSphere sphere;

	float3 cone_axis;
	float cone_cutoff;
};

namespace vz
{
	using GPrimBuffers = GGeometryComponent::GPrimBuffers;

	void GGeometryComponent::DeleteRenderData()
	{
		std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());

		for (size_t i = 0, n = parts_.size(); i < n; ++i)
		{
			Primitive& primitive = parts_[i];
			primitive.bufferHandle_.reset();	// calls GPrimBuffers::Destroy()
		}

		hasRenderData_ = false;
	}

	// general buffer
	void GGeometryComponent::UpdateRenderData()
	{
		std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());

		DeleteRenderData();

		if (isDirty_)
		{
			update();
		}

		GraphicsDevice* device = graphics::GetDevice();

		const size_t position_stride = GetFormatStride(positionFormat);

		hasRenderData_ = false;
		if (parts_.size() == 0)
		{
			return;
		}

		// general buffer creation
		for (size_t part_index = 0, n = parts_.size(); part_index < n; ++part_index)
		{
			Primitive& primitive = parts_[part_index];
			switch (primitive.GetPrimitiveType())
			{
			case PrimitiveType::POINTS:
			case PrimitiveType::LINE_STRIP:
				continue;
			default:
			case PrimitiveType::LINES: 
			{
				break;
			}
			}

			if (!parts_[part_index].IsValid())
			{
				continue;
			}
			primitive.bufferHandle_ = std::make_shared<GPrimBuffers>();
			GPrimBuffers& part_buffers = *(GPrimBuffers*)primitive.bufferHandle_.get();
			part_buffers.busyUpdate = true;
			part_buffers.slot = part_index;
			
			GPUBufferDesc bd;
			if (device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
			{
				// In UMA mode, it is better to create UPLOAD buffer, this avoids one copy from UPLOAD to DEFAULT
				bd.usage = Usage::UPLOAD;
			}
			else
			{
				bd.usage = Usage::DEFAULT;
			}
			bd.bind_flags = BindFlag::VERTEX_BUFFER | BindFlag::INDEX_BUFFER | BindFlag::SHADER_RESOURCE;
			bd.misc_flags = ResourceMiscFlag::BUFFER_RAW | ResourceMiscFlag::TYPED_FORMAT_CASTING | ResourceMiscFlag::NO_DEFAULT_DESCRIPTORS;
			if (device->CheckCapability(GraphicsDeviceCapability::RAYTRACING))
			{
				bd.misc_flags |= ResourceMiscFlag::RAY_TRACING;
			}
			const uint64_t alignment = device->GetMinOffsetAlignment(&bd);

			const std::vector<XMFLOAT3>& vertex_positions = primitive.vertexPositions_;
			const std::vector<uint32_t>& indices = primitive.indexPrimitives_;
			const std::vector<XMFLOAT3>& vertex_normals = primitive.vertexNormals_;
			const std::vector<XMFLOAT4>& vertex_tangents = primitive.vertexTangents_;
			const std::vector<XMFLOAT2>& vertex_uvset_0 = primitive.vertexUVset0_;
			const std::vector<XMFLOAT2>& vertex_uvset_1 = primitive.vertexUVset1_;
			const std::vector<uint32_t>& vertex_colors = primitive.vertexColors_;

			const size_t uv_count = std::max(vertex_uvset_0.size(), vertex_uvset_1.size());

			bd.size =
				AlignTo(vertex_positions.size() * position_stride, alignment) + // position will be first to have 0 offset for flexible alignment!
				AlignTo(indices.size() * GetIndexStride(part_index), alignment) +
				AlignTo(vertex_normals.size() * sizeof(Vertex_NOR), alignment) +
				AlignTo(vertex_tangents.size() * sizeof(Vertex_TAN), alignment) +
				AlignTo(uv_count * primitive.uvStride_, alignment) +
				AlignTo(vertex_colors.size() * sizeof(Vertex_COL), alignment)
				;

			GPUBuffer& generalBuffer = part_buffers.generalBuffer;
			BufferView& ib = part_buffers.ib;
			BufferView& vb_pos = part_buffers.vbPosW;
			BufferView& vb_nor = part_buffers.vbNormal;
			BufferView& vb_tan = part_buffers.vbTangent;
			BufferView& vb_uvs = part_buffers.vbUVs;
			BufferView& vb_col = part_buffers.vbColor;
			const XMFLOAT2& uv_range_min = primitive.GetUVRangeMin();
			const XMFLOAT2& uv_range_max = primitive.GetUVRangeMax();

			auto init_callback = [&](void* dest) {
				uint8_t* buffer_data = (uint8_t*)dest;
				uint64_t buffer_offset = 0ull;

				// vertexBuffer - POSITION:
				switch (positionFormat)
				{
				case Vertex_POS32::FORMAT:
				{
					vb_pos.offset = buffer_offset;
					vb_pos.size = vertex_positions.size() * sizeof(Vertex_POS32);
					Vertex_POS32* vertices = (Vertex_POS32*)(buffer_data + buffer_offset);
					buffer_offset += AlignTo(vb_pos.size, alignment);
					for (size_t i = 0; i < vertex_positions.size(); ++i)
					{
						const XMFLOAT3& pos = vertex_positions[i];
						Vertex_POS32 vert;
						vert.FromFULL(pos);
						std::memcpy(vertices + i, &vert, sizeof(vert));
					}
				}
				break;
				case Vertex_POS32W::FORMAT:
				{
					vb_pos.offset = buffer_offset;
					vb_pos.size = vertex_positions.size() * sizeof(Vertex_POS32W);
					Vertex_POS32W* vertices = (Vertex_POS32W*)(buffer_data + buffer_offset);
					buffer_offset += AlignTo(vb_pos.size, alignment);
					for (size_t i = 0; i < vertex_positions.size(); ++i)
					{
						const XMFLOAT3& pos = vertex_positions[i];
						// something special?? e.g., density or probability for volume-geometric processing
						const uint8_t weight = 0; // vertex_weights.empty() ? 0xFF : vertex_weights[i];
						Vertex_POS32W vert;
						vert.FromFULL(pos, weight);
						std::memcpy(vertices + i, &vert, sizeof(vert));
					}
				}
				break;
				default:
					assert(0);
					break;
				}

				// Create index buffer GPU data:
				if (GetIndexFormat(part_index) == IndexBufferFormat::UINT32)
				{
					ib.offset = buffer_offset;
					ib.size = indices.size() * sizeof(uint32_t);
					uint32_t* indexdata = (uint32_t*)(buffer_data + buffer_offset);
					buffer_offset += AlignTo(ib.size, alignment);
					std::memcpy(indexdata, indices.data(), ib.size);
				}
				else
				{
					ib.offset = buffer_offset;
					ib.size = indices.size() * sizeof(uint16_t);
					uint16_t* indexdata = (uint16_t*)(buffer_data + buffer_offset);
					buffer_offset += AlignTo(ib.size, alignment);
					for (size_t i = 0; i < indices.size(); ++i)
					{
						std::memcpy(indexdata + i, &indices[i], sizeof(uint16_t));
					}
				}

				// vertexBuffer - NORMALS:
				if (!vertex_normals.empty())
				{
					vb_nor.offset = buffer_offset;
					vb_nor.size = vertex_normals.size() * sizeof(Vertex_NOR);
					Vertex_NOR* vertices = (Vertex_NOR*)(buffer_data + buffer_offset);
					buffer_offset += AlignTo(vb_nor.size, alignment);
					for (size_t i = 0; i < vertex_normals.size(); ++i)
					{
						Vertex_NOR vert;
						vert.FromFULL(vertex_normals[i]);
						std::memcpy(vertices + i, &vert, sizeof(vert));
					}
				}

				// vertexBuffer - TANGENTS
				if (!vertex_tangents.empty())
				{
					vb_tan.offset = buffer_offset;
					vb_tan.size = vertex_tangents.size() * sizeof(Vertex_TAN);
					Vertex_TAN* vertices = (Vertex_TAN*)(buffer_data + buffer_offset);
					buffer_offset += AlignTo(vb_tan.size, alignment);
					for (size_t i = 0; i < vertex_tangents.size(); ++i)
					{
						Vertex_TAN vert;
						vert.FromFULL(vertex_tangents[i]);
						std::memcpy(vertices + i, &vert, sizeof(vert));
					}
				}

				// vertexBuffer - UV SETS
				if (!vertex_uvset_0.empty() || !vertex_uvset_1.empty())
				{
					const XMFLOAT2* uv0_stream = vertex_uvset_0.empty() ? vertex_uvset_1.data() : vertex_uvset_0.data();
					const XMFLOAT2* uv1_stream = vertex_uvset_1.empty() ? vertex_uvset_0.data() : vertex_uvset_1.data();

					vb_uvs.offset = buffer_offset;
					vb_uvs.size = uv_count * primitive.uvStride_;
					if (primitive.uvStride_ == sizeof(Vertex_UVS))
					{
						Vertex_UVS* vertices = (Vertex_UVS*)(buffer_data + buffer_offset);
						buffer_offset += AlignTo(vb_uvs.size, alignment);
						for (size_t i = 0; i < uv_count; ++i)
						{
							Vertex_UVS vert;
							vert.uv0.FromFULL(uv0_stream[i], uv_range_min, uv_range_max);
							vert.uv1.FromFULL(uv1_stream[i], uv_range_min, uv_range_max);
							std::memcpy(vertices + i, &vert, sizeof(vert));
						}
					}

					else
					{
						Vertex_UVS32* vertices = (Vertex_UVS32*)(buffer_data + buffer_offset);
						buffer_offset += AlignTo(vb_uvs.size, alignment);
						for (size_t i = 0; i < uv_count; ++i)
						{
							Vertex_UVS32 vert;
							vert.uv0.FromFULL(uv0_stream[i], uv_range_min, uv_range_max);
							vert.uv1.FromFULL(uv1_stream[i], uv_range_min, uv_range_max);
							std::memcpy(vertices + i, &vert, sizeof(vert));
						}
					}
				}

				// vertexBuffer - COLORS
				if (!vertex_colors.empty())
				{
					vb_col.offset = buffer_offset;
					vb_col.size = vertex_colors.size() * sizeof(Vertex_COL);
					Vertex_COL* vertices = (Vertex_COL*)(buffer_data + buffer_offset);
					buffer_offset += AlignTo(vb_col.size, alignment);
					for (size_t i = 0; i < vertex_colors.size(); ++i)
					{
						Vertex_COL vert;
						vert.color = vertex_colors[i];
						std::memcpy(vertices + i, &vert, sizeof(vert));
					}
				}
			};

			bool success = device->CreateBuffer2(&bd, init_callback, &part_buffers.generalBuffer);
			assert(success);
			device->SetName(&part_buffers.generalBuffer, "GGeometryComponent::bufferHandle_::generalBuffer");

			assert(ib.IsValid());
			const Format ib_format = GetIndexFormat(part_index) == IndexBufferFormat::UINT32 ? Format::R32_UINT : Format::R16_UINT;
			ib.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, ib.offset, ib.size, &ib_format);
			ib.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, ib.subresource_srv);

			assert(vb_pos.IsValid());
			vb_pos.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_pos.offset, vb_pos.size, &positionFormat);
			vb_pos.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_pos.subresource_srv);

			if (vb_nor.IsValid())
			{
				vb_nor.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_nor.offset, vb_nor.size, &Vertex_NOR::FORMAT);
				vb_nor.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_nor.subresource_srv);
			}
			if (vb_tan.IsValid())
			{
				vb_tan.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_tan.offset, vb_tan.size, &Vertex_TAN::FORMAT);
				vb_tan.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_tan.subresource_srv);
			}
			if (vb_uvs.IsValid())
			{
				vb_uvs.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_uvs.offset, vb_uvs.size, 
					primitive.useFullPrecisionUV_ ? &GGeometryComponent::Vertex_UVS32::FORMAT : &GGeometryComponent::Vertex_UVS::FORMAT);
				vb_uvs.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_uvs.subresource_srv);
			}
			if (vb_col.IsValid())
			{
				vb_col.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_col.offset, vb_col.size, &Vertex_COL::FORMAT);
				vb_col.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_col.subresource_srv);
			}

			part_buffers.busyUpdate = false;
			hasRenderData_ = true;
		}

		std::vector<ShaderCluster> clusters;
		std::vector<ShaderClusterBounds> cluster_bounds;

		if (isMeshletEnabled)
		{
			// TODO
			/*
			const size_t max_vertices = MESHLET_VERTEX_COUNT;
			const size_t max_triangles = MESHLET_TRIANGLE_COUNT;
			const float cone_weight = 0.5f;

			const uint32_t lod_count = GetLODCount();
			for (uint32_t lod = 0; lod < lod_count; ++lod)
			{
				uint32_t first_subset = 0;
				uint32_t last_subset = 0;
				GetLODSubsetRange(lod, first_subset, last_subset);
				for (uint32_t subsetIndex = first_subset; subsetIndex < last_subset; ++subsetIndex)
				{
					const MeshSubset& subset = subsets[subsetIndex];
					SubsetClusterRange& meshlet_range = cluster_ranges.emplace_back();

					if (subset.indexCount == 0)
						continue;

					size_t max_meshlets = meshopt_buildMeshletsBound(subset.indexCount, max_vertices, max_triangles);
					std::vector<meshopt_Meshlet> meshopt_meshlets(max_meshlets);
					std::vector<unsigned int> meshlet_vertices(max_meshlets * max_vertices);
					std::vector<unsigned char> meshlet_triangles(max_meshlets * max_triangles * 3);

					size_t meshlet_count = meshopt_buildMeshlets(
						meshopt_meshlets.data(),
						meshlet_vertices.data(),
						meshlet_triangles.data(),
						&indices[subset.indexOffset],
						subset.indexCount,
						&vertex_positions[0].x,
						vertex_positions.size(),
						sizeof(XMFLOAT3),
						max_vertices,
						max_triangles,
						cone_weight
					);

					clusters.reserve(clusters.size() + meshlet_count);
					cluster_bounds.reserve(cluster_bounds.size() + meshlet_count);

					meshlet_range.clusterOffset = (uint32_t)clusters.size();
					meshlet_range.clusterCount = (uint32_t)meshlet_count;

					const meshopt_Meshlet& last = meshopt_meshlets[meshlet_count - 1];

					meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
					meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
					meshopt_meshlets.resize(meshlet_count);

					for (size_t i = 0; i < meshopt_meshlets.size(); ++i)
					{
						const meshopt_Meshlet& meshlet = meshopt_meshlets[i];
						meshopt_optimizeMeshlet(
							&meshlet_vertices[meshlet.vertex_offset],
							&meshlet_triangles[meshlet.triangle_offset],
							meshlet.triangle_count,
							meshlet.vertex_count
						);

						meshopt_Bounds bounds = meshopt_computeMeshletBounds(
							&meshlet_vertices[meshlet.vertex_offset],
							&meshlet_triangles[meshlet.triangle_offset],
							meshlet.triangle_count,
							&vertex_positions[0].x,
							vertex_positions.size(),
							sizeof(XMFLOAT3)
						);

						ShaderClusterBounds& clusterbound = cluster_bounds.emplace_back();
						clusterbound.sphere.center.x = bounds.center[0];
						clusterbound.sphere.center.y = bounds.center[1];
						clusterbound.sphere.center.z = bounds.center[2];
						clusterbound.sphere.radius = bounds.radius;
						clusterbound.cone_axis.x = -bounds.cone_axis[0];
						clusterbound.cone_axis.y = -bounds.cone_axis[1];
						clusterbound.cone_axis.z = -bounds.cone_axis[2];
						clusterbound.cone_cutoff = bounds.cone_cutoff;

						ShaderCluster& cluster = clusters.emplace_back();
						cluster.vertexCount = meshlet.vertex_count;
						cluster.triangleCount = meshlet.triangle_count;
						for (size_t tri = 0; tri < meshlet.triangle_count; ++tri)
						{
							cluster.triangles[tri].init(
								meshlet_triangles[meshlet.triangle_offset + tri * 3 + 0],
								meshlet_triangles[meshlet.triangle_offset + tri * 3 + 1],
								meshlet_triangles[meshlet.triangle_offset + tri * 3 + 2]
							);
						}
						for (size_t vert = 0; vert < meshlet.vertex_count; ++vert)
						{
							cluster.vertices[vert] = meshlet_vertices[meshlet.vertex_offset + vert];
						}
					}
				}
			}

			bd.size = AlignTo(bd.size, sizeof(ShaderCluster));
			bd.size = AlignTo(bd.size + clusters.size() * sizeof(ShaderCluster), alignment);

			bd.size = AlignTo(bd.size, sizeof(ShaderClusterBounds));
			bd.size = AlignTo(bd.size + cluster_bounds.size() * sizeof(ShaderClusterBounds), alignment);
			/**/
		}

		// safe check
		if (hasRenderData_)
		{
			for (size_t part_index = 0, n = parts_.size(); part_index < n; ++part_index)
			{
				Primitive& primitive = parts_[part_index];
				GPrimBuffers& part_buffers = *(GPrimBuffers*)primitive.bufferHandle_.get();
				vzlog_assert(!part_buffers.busyUpdate, "GPrimBuffers::busyUpdate must be FALSE! at the end of UpdateRenderData");
			}
		}

		timeStampSetter_ = TimerNow;
	}
	
	void GGeometryComponent::UpdateCustomRenderData(const std::function<void(graphics::GraphicsDevice* device)>& task)
	{
		std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());

		DeleteRenderData();

		if (isDirty_)
		{
			update();
		}

		const size_t position_stride = GetFormatStride(positionFormat);

		hasRenderData_ = false;
		if (parts_.size() == 0)
		{
			return;
		}

		// general buffer creation
		for (size_t part_index = 0, n = parts_.size(); part_index < n; ++part_index)
		{
			Primitive& primitive = parts_[part_index];
			if (!parts_[part_index].IsValid())
			{
				continue;
			}
			primitive.bufferHandle_ = std::make_shared<GPrimBuffers>();
		}
		
		task(graphics::GetDevice());

		hasRenderData_ = true;

		timeStampSetter_ = TimerNow;
	}

	void GGeometryComponent::UpdateStreamoutRenderData()
	{
		if (!hasRenderData_) 
		{
			UpdateRenderData();
		}

		GraphicsDevice* device = graphics::GetDevice();

		GPUBufferDesc desc;
		desc.usage = Usage::DEFAULT;
		desc.bind_flags = BindFlag::VERTEX_BUFFER | BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
		desc.misc_flags = ResourceMiscFlag::BUFFER_RAW | ResourceMiscFlag::TYPED_FORMAT_CASTING | ResourceMiscFlag::NO_DEFAULT_DESCRIPTORS;
		if (device->CheckCapability(GraphicsDeviceCapability::RAYTRACING))
		{
			desc.misc_flags |= ResourceMiscFlag::RAY_TRACING;
		}

		const uint64_t alignment = device->GetMinOffsetAlignment(&desc) * sizeof(Vertex_POS32); // additional alignment for RGB32F
		

		for (size_t i = 0, n = parts_.size(); i < n; ++i)
		{
			Primitive& primitive = parts_[i];
			switch (primitive.GetPrimitiveType())
			{
			case PrimitiveType::POINTS:
			case PrimitiveType::LINE_STRIP:
			case PrimitiveType::LINES:
				continue;
			default:
				break;
			}

			primitive.bufferHandle_ = std::make_shared<GPrimBuffers>();
			GPrimBuffers& part_buffers = *(GPrimBuffers*)primitive.bufferHandle_.get();

			const std::vector<XMFLOAT3>& vertex_positions = primitive.vertexPositions_;
			const std::vector<uint32_t>& indices = primitive.indexPrimitives_;
			const std::vector<XMFLOAT3>& vertex_normals = primitive.vertexNormals_;
			const std::vector<XMFLOAT4>& vertex_tangents = primitive.vertexTangents_;

			desc.size =
				AlignTo(vertex_positions.size() * sizeof(Vertex_POS32W), alignment) + // pos
				AlignTo(vertex_positions.size() * sizeof(Vertex_POS32W), alignment) + // prevpos
				AlignTo(vertex_normals.size() * sizeof(Vertex_NOR), alignment) +
				AlignTo(vertex_tangents.size() * sizeof(Vertex_TAN), alignment)
				;

			GPUBuffer& streamoutBuffer = part_buffers.streamoutBuffer;
			BufferView& vb_nor = part_buffers.vbNormal;
			BufferView& vb_tan = part_buffers.vbTangent;
			BufferView& so_pos = part_buffers.soPosW;
			BufferView& so_pre = part_buffers.soPre;
			BufferView& so_nor = part_buffers.soNormal;
			BufferView& so_tan = part_buffers.soTangent;

			bool success = device->CreateBuffer(&desc, nullptr, &streamoutBuffer);
			assert(success);
			device->SetName(&streamoutBuffer, "GGeometryComponent::streamoutBuffer");

			uint64_t buffer_offset = 0ull;

			so_pos.offset = buffer_offset;
			so_pos.size = vertex_positions.size() * sizeof(Vertex_POS32W);
			buffer_offset += AlignTo(so_pos.size, alignment);
			so_pos.subresource_srv = device->CreateSubresource(&streamoutBuffer, SubresourceType::SRV, so_pos.offset, so_pos.size, &Vertex_POS32W::FORMAT);
			so_pos.subresource_uav = device->CreateSubresource(&streamoutBuffer, SubresourceType::UAV, so_pos.offset, so_pos.size, &Vertex_POS32W::FORMAT); // UAV can't have RGB32_F format!
			so_pos.descriptor_srv = device->GetDescriptorIndex(&streamoutBuffer, SubresourceType::SRV, so_pos.subresource_srv);
			so_pos.descriptor_uav = device->GetDescriptorIndex(&streamoutBuffer, SubresourceType::UAV, so_pos.subresource_uav);

			so_pre.offset = buffer_offset;
			so_pre.size = so_pos.size;
			buffer_offset += AlignTo(so_pre.size, alignment);
			so_pre.subresource_srv = device->CreateSubresource(&streamoutBuffer, SubresourceType::SRV, so_pre.offset, so_pre.size, &Vertex_POS32W::FORMAT);
			so_pre.subresource_uav = device->CreateSubresource(&streamoutBuffer, SubresourceType::UAV, so_pre.offset, so_pre.size, &Vertex_POS32W::FORMAT); // UAV can't have RGB32_F format!
			so_pre.descriptor_srv = device->GetDescriptorIndex(&streamoutBuffer, SubresourceType::SRV, so_pre.subresource_srv);
			so_pre.descriptor_uav = device->GetDescriptorIndex(&streamoutBuffer, SubresourceType::UAV, so_pre.subresource_uav);

			if (vb_nor.IsValid())
			{
				so_nor.offset = buffer_offset;
				so_nor.size = vb_nor.size;
				buffer_offset += AlignTo(so_nor.size, alignment);
				so_nor.subresource_srv = device->CreateSubresource(&streamoutBuffer, SubresourceType::SRV, so_nor.offset, so_nor.size, &Vertex_NOR::FORMAT);
				so_nor.subresource_uav = device->CreateSubresource(&streamoutBuffer, SubresourceType::UAV, so_nor.offset, so_nor.size, &Vertex_NOR::FORMAT);
				so_nor.descriptor_srv = device->GetDescriptorIndex(&streamoutBuffer, SubresourceType::SRV, so_nor.subresource_srv);
				so_nor.descriptor_uav = device->GetDescriptorIndex(&streamoutBuffer, SubresourceType::UAV, so_nor.subresource_uav);
			}

			if (vb_tan.IsValid())
			{
				so_tan.offset = buffer_offset;
				so_tan.size = vb_tan.size;
				buffer_offset += AlignTo(so_tan.size, alignment);
				so_tan.subresource_srv = device->CreateSubresource(&streamoutBuffer, SubresourceType::SRV, so_tan.offset, so_tan.size, &Vertex_TAN::FORMAT);
				so_tan.subresource_uav = device->CreateSubresource(&streamoutBuffer, SubresourceType::UAV, so_tan.offset, so_tan.size, &Vertex_TAN::FORMAT);
				so_tan.descriptor_srv = device->GetDescriptorIndex(&streamoutBuffer, SubresourceType::SRV, so_tan.subresource_srv);
				so_tan.descriptor_uav = device->GetDescriptorIndex(&streamoutBuffer, SubresourceType::UAV, so_tan.subresource_uav);
			}
		}
	}

	size_t GGeometryComponent::GetMemoryUsageCPU() const
	{
		size_t size = 0;
		for (size_t i = 0, n = parts_.size(); i < n; ++i)
		{
			const Primitive& primitive = parts_[i];

			size += primitive.GetMemoryUsageCPU();

			//size += GetMemoryUsageBVH();
		}
		return size;
	}
	size_t GGeometryComponent::GetMemoryUsageGPU() const
	{
		size_t size = 0;
		for (size_t i = 0, n = parts_.size(); i < n; ++i)
		{
			const Primitive& primitive = parts_[i];
			const GPrimBuffers* part_buffers = (GPrimBuffers*)primitive.bufferHandle_.get();
			if (part_buffers == nullptr)
			{
				continue;
			}
			size += part_buffers->generalBuffer.desc.size + part_buffers->streamoutBuffer.desc.size;
		}
		return size;
	}
	//size_t GGeometryComponent::GetMemoryUsageBVH() const
	//{
	//	return
	//		bvh.allocation.capacity() +
	//		bvh_leaf_aabbs.size() * sizeof(primitive::AABB);
	//}
	//void GGeometryComponent::BuildBVH()
	//{
	//	bvh_leaf_aabbs.clear();
	//	uint32_t first_subset = 0;
	//	uint32_t last_subset = 0;
	//	GetLODSubsetRange(0, first_subset, last_subset);
	//	for (uint32_t subsetIndex = first_subset; subsetIndex < last_subset; ++subsetIndex)
	//	{
	//		const GGeometryComponent::MeshSubset& subset = subsets[subsetIndex];
	//		if (subset.indexCount == 0)
	//			continue;
	//		const uint32_t indexOffset = subset.indexOffset;
	//		const uint32_t triangleCount = subset.indexCount / 3;
	//		for (uint32_t triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex)
	//		{
	//			const uint32_t i0 = indices[indexOffset + triangleIndex * 3 + 0];
	//			const uint32_t i1 = indices[indexOffset + triangleIndex * 3 + 1];
	//			const uint32_t i2 = indices[indexOffset + triangleIndex * 3 + 2];
	//			const XMFLOAT3& p0 = vertex_positions[i0];
	//			const XMFLOAT3& p1 = vertex_positions[i1];
	//			const XMFLOAT3& p2 = vertex_positions[i2];
	//			AABB aabb = primitive::AABB(math::Min(p0, math::Min(p1, p2)), math::Max(p0, math::Max(p1, p2)));
	//			aabb.layerMask = triangleIndex;
	//			aabb.userdata = subsetIndex;
	//			bvh_leaf_aabbs.push_back(aabb);
	//		}
	//	}
	//	bvh.Build(bvh_leaf_aabbs.data(), (uint32_t)bvh_leaf_aabbs.size());
	//}
}