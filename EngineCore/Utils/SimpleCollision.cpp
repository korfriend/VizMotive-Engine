#include "SimpleCollision.h"
#include "Utils/Backlog.h"
#include "Utils/Jobsystem.h"
#include "Utils/Profiler.h"
#include "Components/Components.h"

#include <cfloat>
#include <algorithm>
#include <cassert>

using namespace vz;
using Primitive = vz::GeometryComponent::Primitive;

namespace vz::GJKcollision
{
	using namespace geometrics;
	/****************************************************************************************
	*  GJK-based collision test                                                               *
	*                                                                                        *
	*  – ConvexShape            : generic convex hull (any number of points)                 *
	*  – TriShape               : lightweight wrapper that turns one mesh triangle into      *
	*                             a ConvexShape                                              *
	*  – GJK_Intersect          : convex–convex Boolean query (no EPA / penetration depth)   *
	*  – GJK_ConvexVsBVH        : fast “convex A  vs  mesh-B’s BVH” collision function.      *
	*                             Returns true on the first triangle hit and gives its index *
	*                                                                                        *
	*  Dependencies:                                                                          *
	*  ─ DirectXMath (XMVector…),                                                            *
	*  ─ your own BVH struct (nodes[], leaf_indices[] etc.),                                 *
	*  ─ vz::geometrics::AABB::intersects() which returns OUTSIDE/INSIDE/INTERSECTS.        *
	*****************************************************************************************/

	// -----------------------------------------------------------------------------
	// Basic convex hull wrapper
	// -----------------------------------------------------------------------------
	struct ConvexShape
	{
		const XMFLOAT3* v = nullptr;  // original local vertices
		uint32_t                 n = 0;

		//------------------------------------------------------------------
		// Fast support: no transform/mul per vertex; SIMD friendly
		//------------------------------------------------------------------
		XMVECTOR Support(FXMVECTOR dir) const
		{
			uint32_t        bestI = 0;
			float           best = -FLT_MAX;

			for (uint32_t i = 0; i < n; ++i)
			{
				// dot in scalar form (SIMD lane 0 only)
				float d = v[i].x * dir.m128_f32[0] +
					v[i].y * dir.m128_f32[1] +
					v[i].z * dir.m128_f32[2];
				if (d > best) { best = d; bestI = i; }
			}
			return XMLoadFloat3(&v[bestI]);
		}
	};

	// helper: Minkowski support A(d) - B(-d)
	inline XMVECTOR Support(const ConvexShape& A,
		const ConvexShape& B,
		FXMVECTOR dir)
	{
		return A.Support(dir) - B.Support(-dir);
	}

	// -----------------------------------------------------------------------------
	// Lightweight triangle wrapper (3 vertices)
	// -----------------------------------------------------------------------------
	struct TriShape : public ConvexShape
	{
		XMFLOAT3 tmp[3];
		TriShape(const XMFLOAT3& p0,
			const XMFLOAT3& p1,
			const XMFLOAT3& p2)
		{
			v = tmp;  n = 3;
			tmp[0] = p0; tmp[1] = p1; tmp[2] = p2;
		}
	};

	// -----------------------------------------------------------------------------
	// Simplex utilities for GJK (1-4 vertices)
	// -----------------------------------------------------------------------------
	struct Simplex
	{
		XMVECTOR pts[4];
		int count = 0;

		void push_front(XMVECTOR p)
		{
			for (int i = count; i > 0; --i) pts[i] = pts[i - 1];
			pts[0] = p;
			if (count < 4) ++count;
		}
	};

	// returns true if origin is inside simplex; otherwise updates dir
	static bool NextSimplex(Simplex& s, XMVECTOR& dir)
	{
		using namespace DirectX;
		switch (s.count)
		{
			// line segment
		case 2:
		{
			const XMVECTOR A = s.pts[0];
			const XMVECTOR B = s.pts[1];
			const XMVECTOR AB = B - A;
			const XMVECTOR AO = -A;

			if (XMVectorGetX(XMVector3Dot(AB, AO)) > 0.0f)
				dir = XMVector3Cross(XMVector3Cross(AB, AO), AB);
			else
			{
				s.pts[0] = A;
				s.count = 1;
				dir = AO;
			}
			break;
		}
		// triangle
		case 3:
		{
			const XMVECTOR A = s.pts[0];
			const XMVECTOR B = s.pts[1];
			const XMVECTOR C = s.pts[2];
			const XMVECTOR AB = B - A;
			const XMVECTOR AC = C - A;
			const XMVECTOR AO = -A;
			const XMVECTOR ABC = XMVector3Cross(AB, AC);

			// region check for AB edge
			if (XMVectorGetX(XMVector3Dot(XMVector3Cross(AB, ABC), AO)) > 0.0f)
			{
				s.pts[2] = s.pts[1];
				s.pts[1] = s.pts[0];
				s.count = 2;
				dir = XMVector3Cross(XMVector3Cross(AB, AO), AB);
				break;
			}
			// region check for AC edge
			if (XMVectorGetX(XMVector3Dot(XMVector3Cross(ABC, AC), AO)) > 0.0f)
			{
				s.pts[1] = s.pts[0];
				s.count = 2;
				dir = XMVector3Cross(XMVector3Cross(AC, AO), AC);
				break;
			}
			// origin above or below triangle?
			if (XMVectorGetX(XMVector3Dot(ABC, AO)) > 0.0f)
				dir = ABC;
			else
			{
				std::swap(s.pts[1], s.pts[2]);
				dir = -ABC;
			}
			break;
		}
		// tetrahedron
		case 4:
		{
			const XMVECTOR A = s.pts[0];
			const XMVECTOR B = s.pts[1];
			const XMVECTOR C = s.pts[2];
			const XMVECTOR D = s.pts[3];
			const XMVECTOR AO = -A;
			const XMVECTOR ABC = XMVector3Cross(B - A, C - A);
			const XMVECTOR ACD = XMVector3Cross(C - A, D - A);
			const XMVECTOR ADB = XMVector3Cross(D - A, B - A);

			if (XMVectorGetX(XMVector3Dot(ABC, AO)) > 0.0f)
			{   // origin over ABC -> drop D
				s.pts[1] = B; s.pts[2] = C; s.count = 3; dir = ABC; break;
			}
			if (XMVectorGetX(XMVector3Dot(ACD, AO)) > 0.0f)
			{   // drop B
				s.pts[1] = C; s.pts[2] = D; s.count = 3; dir = ACD; break;
			}
			if (XMVectorGetX(XMVector3Dot(ADB, AO)) > 0.0f)
			{   // drop C
				s.pts[1] = D; s.pts[2] = B; s.count = 3; dir = ADB; break;
			}
			return true; // origin inside tetrahedron
		}
		}
		return false;
	}

	// -----------------------------------------------------------------------------
	// Basic GJK Boolean intersection – convex vs convex
	// -----------------------------------------------------------------------------
	inline bool GJK_Intersect(const ConvexShape& A,
		const ConvexShape& B,
		int  maxIter = 32,
		float tolerance = 1e-6f)
	{
		using namespace DirectX;

		// initial direction = between centers
		XMVECTOR dir = XMVectorSet(1.f, 0.f, 0.f, 0.f);
		Simplex  s;
		s.push_front(Support(A, B, dir));
		dir = -s.pts[0];

		for (int i = 0; i < maxIter; ++i)
		{
			const XMVECTOR p = Support(A, B, dir);
			if (XMVectorGetX(XMVector3Dot(p, dir)) < tolerance) return false; // separated

			s.push_front(p);
			if (NextSimplex(s, dir)) return true; // origin inside simplex
		}
		return false; // iteration limit reached – assume no hit
	}

	// -----------------------------------------------------------------------------
	// Convex vs BVH (returns first triangle index, or -1)
	// -----------------------------------------------------------------------------
	bool GJK_ConvexVsBVH(const ConvexShape& convexA, // yellow (local space)
		const Primitive& meshB,         // salmon
		CXMMATRIX AtoB,          // world→B
		int& hitTri)
	{
		using namespace DirectX;
		hitTri = -1;

		// 0) Pre-transform convex vertices into mesh-B space  (ONE pass)
		std::vector<XMFLOAT3> vertsA_inB(convexA.n);
		for (uint32_t i = 0; i < convexA.n; ++i)
			XMStoreFloat3(&vertsA_inB[i],
				XMVector3Transform(XMLoadFloat3(&convexA.v[i]),
					AtoB)); // local→world→B

		// 1) Wrap them in a ConvexShape with identity matrix
		ConvexShape convexA_inB;
		convexA_inB.v = vertsA_inB.data();
		convexA_inB.n = (uint32_t)vertsA_inB.size();

		// 2) Build a tight AABB + bounding sphere (for plane reject)
		vz::geometrics::AABB aabbHull;
		XMVECTOR cSum = XMVectorZero();
		for (auto& p : vertsA_inB)
		{
			aabbHull._min.x = std::min(aabbHull._min.x, p.x);
			aabbHull._min.y = std::min(aabbHull._min.y, p.y);
			aabbHull._min.z = std::min(aabbHull._min.z, p.z);
			aabbHull._max.x = std::max(aabbHull._max.x, p.x);
			aabbHull._max.y = std::max(aabbHull._max.y, p.y);
			aabbHull._max.z = std::max(aabbHull._max.z, p.z);
			cSum += XMLoadFloat3(&p);
		}
		XMVECTOR center = cSum / float(convexA_inB.n);
		float radiusSq = 0.f;
		for (auto& p : vertsA_inB)
		{
			XMVECTOR d = XMLoadFloat3(&p) - center;
			radiusSq = std::max(radiusSq,
				XMVectorGetX(XMVector3LengthSq(d)));
		}
		float radius = std::sqrt(radiusSq);

		// ---------------------------------------------------------------------
		// 3) Traverse BVH
		// ---------------------------------------------------------------------
		const BVH& bvh = meshB.GetBVH();
		const XMFLOAT3* vtx = meshB.GetVtxPositions().data();
		const uint32_t* idx = meshB.GetIdxPrimives().data();

		uint32_t stack[64]; uint32_t sp = 0;
		stack[sp++] = 0;

		while (sp)
		{
			const auto& node = bvh.nodes[stack[--sp]];

			if (node.aabb.intersects(aabbHull) == vz::geometrics::AABB::OUTSIDE)
				continue;

			if (node.isLeaf())
			{
				for (uint32_t k = 0; k < node.count; ++k)
				{
					uint32_t t = bvh.leaf_indices[node.offset + k];
					const XMFLOAT3& p0 = vtx[idx[3 * t + 0]];
					const XMFLOAT3& p1 = vtx[idx[3 * t + 1]];
					const XMFLOAT3& p2 = vtx[idx[3 * t + 2]];

					// 3-A) fast plane-vs-sphere reject
					XMVECTOR n = XMVector3Cross(XMLoadFloat3(&p1) - XMLoadFloat3(&p0),
						XMLoadFloat3(&p2) - XMLoadFloat3(&p0));
					n = XMVector3Normalize(n);
					float d = -XMVectorGetX(XMVector3Dot(n, XMLoadFloat3(&p0)));
					float dist = XMVectorGetX(XMVector3Dot(n, center)) + d;
					if (dist > radius) continue;                    // clearly outside

					// 3-B) precise GJK (10 iterations)
					TriShape tri(p0, p1, p2);
					if (GJK_Intersect(convexA_inB, tri, /*maxIter=*/10))
					{
						hitTri = int(t); return true;
					}
				}
			}
			else
			{
				stack[sp++] = node.left;
				stack[sp++] = node.left + 1;
			}
		}
		return false;
	}
}

namespace vz::bvhcollision
{
	//-------------------------------------
	// Triangle-Triangle Intersection Test Function
	//-------------------------------------
	bool IntersectTriangleTriangle(
		const XMFLOAT3& A0, const XMFLOAT3& A1, const XMFLOAT3& A2,
		const XMFLOAT3& B0, const XMFLOAT3& B1, const XMFLOAT3& B2)
	{
		// Convert to XMVECTOR for internal use:
		XMVECTOR vA0 = XMLoadFloat3(&A0);
		XMVECTOR vA1 = XMLoadFloat3(&A1);
		XMVECTOR vA2 = XMLoadFloat3(&A2);

		XMVECTOR vB0 = XMLoadFloat3(&B0);
		XMVECTOR vB1 = XMLoadFloat3(&B1);
		XMVECTOR vB2 = XMLoadFloat3(&B2);

		//-------------------------------------------------------
		// 1) Check if the planes of the two triangles serve as separating axes
		//
		//   - Normal of triangle A's plane: N_A
		//   - Normal of triangle B's plane: N_B
		//   - If all vertices of one triangle are on the same side of the other triangle's plane, they are separated
		//-------------------------------------------------------
		// Normal of triangle A's plane:
		XMVECTOR eA1 = vA1 - vA0; // A0->A1
		XMVECTOR eA2 = vA2 - vA0; // A0->A2
		XMVECTOR N_A = XMVector3Cross(eA1, eA2);

		// Normal of triangle B's plane:
		XMVECTOR eB1 = vB1 - vB0; // B0->B1
		XMVECTOR eB2 = vB2 - vB0; // B0->B2
		XMVECTOR N_B = XMVector3Cross(eB1, eB2);

		// Check signs of B's vertices against plane A
		{
			// Plane equation of triangle A: N_A · (X - A0) = 0
			XMVECTOR distB0 = XMVector3Dot(N_A, (vB0 - vA0));
			XMVECTOR distB1 = XMVector3Dot(N_A, (vB1 - vA0));
			XMVECTOR distB2 = XMVector3Dot(N_A, (vB2 - vA0));

			// If distB0, distB1, distB2 all have the same sign and are non-zero, triangles are separated
			bool b0_pos = (XMVectorGetX(distB0) > 0.0f);
			bool b1_pos = (XMVectorGetX(distB1) > 0.0f);
			bool b2_pos = (XMVectorGetX(distB2) > 0.0f);

			// All have the same sign & absolute values are not zero => all on one side
			if ((b0_pos == b1_pos) && (b1_pos == b2_pos))
			{
				// Check if all distances are significant (not zero)
				// This handles the degenerate case where all might be zero
				if (std::fabs(XMVectorGetX(distB0)) > 1e-8f &&
					std::fabs(XMVectorGetX(distB1)) > 1e-8f &&
					std::fabs(XMVectorGetX(distB2)) > 1e-8f)
				{
					return false; // Separated
				}
			}
		}

		// Check signs of A's vertices against plane B
		{
			XMVECTOR distA0 = XMVector3Dot(N_B, (vA0 - vB0));
			XMVECTOR distA1 = XMVector3Dot(N_B, (vA1 - vB0));
			XMVECTOR distA2 = XMVector3Dot(N_B, (vA2 - vB0));

			bool a0_pos = (XMVectorGetX(distA0) > 0.0f);
			bool a1_pos = (XMVectorGetX(distA1) > 0.0f);
			bool a2_pos = (XMVectorGetX(distA2) > 0.0f);

			if ((a0_pos == a1_pos) && (a1_pos == a2_pos))
			{
				if (std::fabs(XMVectorGetX(distA0)) > 1e-8f &&
					std::fabs(XMVectorGetX(distA1)) > 1e-8f &&
					std::fabs(XMVectorGetX(distA2)) > 1e-8f)
				{
					return false; // Separated
				}
			}
		}

		//-------------------------------------------------------
		// 2) Check if cross(EdgeA, EdgeB) is a separating axis for all pairs of edges from triangles A and B (SAT)
		//
		//    edgeA x edgeB = potential separating axis (project both polygons and check for overlap)
		//    If any edge pair fails the overlap test -> triangles are separated
		//-------------------------------------------------------
		// Edges of A
		XMVECTOR edgesA[3] = {
			vA1 - vA0,
			vA2 - vA1,
			vA0 - vA2
		};
		// Edges of B
		XMVECTOR edgesB[3] = {
			vB1 - vB0,
			vB2 - vB1,
			vB0 - vB2
		};

		// Utility function (project triangle vertices onto an axis -> find min/max)
		auto ProjectTriOnAxis = [&](const XMVECTOR& axis,
			const XMVECTOR& p0,
			const XMVECTOR& p1,
			const XMVECTOR& p2,
			float& minProj,
			float& maxProj)
			{
				// Normalization is not strictly necessary, but recommended to avoid numerical issues with very small axes
				XMVECTOR n = XMVector3Normalize(axis);
				float d0 = XMVectorGetX(XMVector3Dot(p0, n));
				float d1 = XMVectorGetX(XMVector3Dot(p1, n));
				float d2 = XMVectorGetX(XMVector3Dot(p2, n));
				minProj = std::min({ d0, d1, d2 });
				maxProj = std::max({ d0, d1, d2 });
			};

		// SAT helper: check if two intervals [minA, maxA], [minB, maxB] overlap
		auto OverlapOnAxis = [&](float minA, float maxA, float minB, float maxB)
			{
				if (maxA < minB || maxB < minA) return false;
				return true;
			};

		// Check all edge pairs (3 x 3 = 9 pairs)
		for (int iA = 0; iA < 3; iA++)
		{
			for (int iB = 0; iB < 3; iB++)
			{
				// Separating axis = edgesA[iA] x edgesB[iB]
				XMVECTOR axis = XMVector3Cross(edgesA[iA], edgesB[iB]);

				// Skip if axis is nearly (0,0,0) (parallel edges or zero vector case)
				// as it provides no meaningful separation
				XMVECTOR lenSq = XMVector3LengthSq(axis);
				if (XMVectorGetX(lenSq) < 1e-12f)
				{
					// (Nearly parallel edge pair)
					continue;
				}

				// Project each triangle onto this axis
				float minA, maxA;
				ProjectTriOnAxis(axis, vA0, vA1, vA2, minA, maxA);

				float minB, maxB;
				ProjectTriOnAxis(axis, vB0, vB1, vB2, minB, maxB);

				// If intervals don't overlap, triangles are separated
				if (!OverlapOnAxis(minA, maxA, minB, maxB))
				{
					return false;
				}
			}
		}

		//-------------------------------------------------------
		// If we got here, no separation was found along any potential separating axis -> triangles intersect
		//-------------------------------------------------------
		return true;
	}

	//-------------------------------------
	// Transform a triangle from mesh1 to mesh2's coordinate system
	//   T : matrix from mesh1 to mesh2
	//-------------------------------------
	static inline void TransformTriangle(
		const XMFLOAT3& mesh1P0, const XMFLOAT3& mesh1P1, const XMFLOAT3& mesh1P2,
		const XMMATRIX& T,
		XMFLOAT3& out1P0, XMFLOAT3& out1P1, XMFLOAT3& out1P2
	)
	{
		XMVECTOR v0 = XMLoadFloat3(&mesh1P0);
		XMVECTOR v1 = XMLoadFloat3(&mesh1P1);
		XMVECTOR v2 = XMLoadFloat3(&mesh1P2);

		// Transform v0, v1, v2 to mesh2 space
		v0 = XMVector3Transform(v0, T);
		v1 = XMVector3Transform(v1, T);
		v2 = XMVector3Transform(v2, T);

		// Store in output parameters
		XMStoreFloat3(&out1P0, v0);
		XMStoreFloat3(&out1P1, v1);
		XMStoreFloat3(&out1P2, v2);
	}

	inline bool SameSide(const XMFLOAT2& p1, const XMFLOAT2& p2, const XMFLOAT2& a, const XMFLOAT2& b)
	{
		float cp1 = (b.x - a.x) * (p1.y - a.y) - (b.y - a.y) * (p1.x - a.x);
		float cp2 = (b.x - a.x) * (p2.y - a.y) - (b.y - a.y) * (p2.x - a.x);
		return (cp1 * cp2 >= 0);
	}

	inline bool PointInTriangle(const XMFLOAT2& p, const XMFLOAT2& a, const XMFLOAT2& b, const XMFLOAT2& c)
	{
		return SameSide(p, a, b, c) &&
			SameSide(p, b, a, c) &&
			SameSide(p, c, a, b);
	}

	inline bool TriTriOverlap2D(
		const XMFLOAT2& A0, const XMFLOAT2& A1, const XMFLOAT2& A2,
		const XMFLOAT2& B0, const XMFLOAT2& B1, const XMFLOAT2& B2)
	{
		// A의 꼭짓점 중 하나라도 B 안에?
		if (PointInTriangle(A0, B0, B1, B2)) return true;
		if (PointInTriangle(A1, B0, B1, B2)) return true;
		if (PointInTriangle(A2, B0, B1, B2)) return true;

		// B의 꼭짓점 중 하나라도 A 안에?
		if (PointInTriangle(B0, A0, A1, A2)) return true;
		if (PointInTriangle(B1, A0, A1, A2)) return true;
		if (PointInTriangle(B2, A0, A1, A2)) return true;

		// Edge 교차 체크 (총 3x3 = 9개 쌍)
		auto EdgeIntersect = [](const XMFLOAT2& a1, const XMFLOAT2& a2, const XMFLOAT2& b1, const XMFLOAT2& b2) -> bool
			{
				auto det = [](float a, float b, float c, float d) -> float {
					return a * d - b * c;
					};

				float dx1 = a2.x - a1.x, dy1 = a2.y - a1.y;
				float dx2 = b2.x - b1.x, dy2 = b2.y - b1.y;
				float delta = det(dx1, dy1, dx2, dy2);

				if (std::fabs(delta) < 1e-6f)
					return false; // 평행

				float s = det(b1.x - a1.x, b1.y - a1.y, dx2, dy2) / delta;
				float t = det(b1.x - a1.x, b1.y - a1.y, dx1, dy1) / delta;

				return (s >= 0 && s <= 1 && t >= 0 && t <= 1);
			};

		const XMFLOAT2* A[3] = { &A0, &A1, &A2 };
		const XMFLOAT2* B[3] = { &B0, &B1, &B2 };
		for (int i = 0; i < 3; ++i)
		{
			const XMFLOAT2& a1 = *A[i];
			const XMFLOAT2& a2 = *A[(i + 1) % 3];
			for (int j = 0; j < 3; ++j)
			{
				const XMFLOAT2& b1 = *B[j];
				const XMFLOAT2& b2 = *B[(j + 1) % 3];
				if (EdgeIntersect(a1, a2, b1, b2))
					return true;
			}
		}

		return false;
	}

	inline bool TriTriIntersectFast(const XMFLOAT3& A0, const XMFLOAT3& A1, const XMFLOAT3& A2,
		const XMFLOAT3& B0, const XMFLOAT3& B1, const XMFLOAT3& B2)
	{
		using namespace DirectX;

		const XMVECTOR a0 = XMLoadFloat3(&A0);
		const XMVECTOR a1 = XMLoadFloat3(&A1);
		const XMVECTOR a2 = XMLoadFloat3(&A2);
		const XMVECTOR b0 = XMLoadFloat3(&B0);
		const XMVECTOR b1 = XMLoadFloat3(&B1);
		const XMVECTOR b2 = XMLoadFloat3(&B2);

		// --- 단계 0 : 삼각형 평면의 서명(부호) – 전체 3 DOT 비교만으로 분리 가능성 탐색
		XMVECTOR nA = XMVector3Cross(a1 - a0, a2 - a0);
		XMVECTOR dB0 = XMVector3Dot(nA, b0 - a0);
		XMVECTOR dB1 = XMVector3Dot(nA, b1 - a0);
		XMVECTOR dB2 = XMVector3Dot(nA, b2 - a0);
		int signB = (XMVectorGetX(dB0) > 0) + (XMVectorGetX(dB1) > 0) + (XMVectorGetX(dB2) > 0);
		if (signB == 0 || signB == 3)   // B 가 A 의 한쪽 면에 있음 → 분리
			return false;

		XMVECTOR nB = XMVector3Cross(b1 - b0, b2 - b0);
		XMVECTOR dA0 = XMVector3Dot(nB, a0 - b0);
		XMVECTOR dA1 = XMVector3Dot(nB, a1 - b0);
		XMVECTOR dA2 = XMVector3Dot(nB, a2 - b0);
		int signA = (XMVectorGetX(dA0) > 0) + (XMVectorGetX(dA1) > 0) + (XMVectorGetX(dA2) > 0);
		if (signA == 0 || signA == 3)
			return false;

		// 2D 삼각형 교차 (Baraff 1990) – 3 edge 반-평면 모두 통과하면 교차
		auto overlap2D = [](const XMVECTOR& v0, const XMVECTOR& v1, const XMVECTOR& v2,
			const XMVECTOR& u0, const XMVECTOR& u1, const XMVECTOR& u2)->bool
			{
				// edge (v0→v1)
				XMVECTOR e = v1 - v0;
				XMVECTOR n = XMVectorSet(-XMVectorGetZ(e), XMVectorGetY(e), 0, 0); // 2D perp
				float v0d = XMVectorGetX(XMVector2Dot(n, v0));
				float v2d = XMVectorGetX(XMVector2Dot(n, v2));
				float u0d = XMVectorGetX(XMVector2Dot(n, u0));
				float u1d = XMVectorGetX(XMVector2Dot(n, u1));
				float u2d = XMVectorGetX(XMVector2Dot(n, u2));
				if (((u0d - v0d) > 0 && (u1d - v0d) > 0 && (u2d - v0d) > 0) ||
					((u0d - v0d) < 0 && (u1d - v0d) < 0 && (u2d - v0d) < 0))
					return false;
				return true;   // 실제 구현은 3 edge 모두 체크
			};

		// --- 단계 1 : 교차직선 L = nA × nB
		XMVECTOR dir = XMVector3Cross(nA, nB);
		// 두 평면이 거의 평행하면 dir ≈ 0 → coplanar 특례
		if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-12f)
			goto coplanar;

		if (!overlap2D(a0, a1, a2, b0, b1, b2)) return false;
		if (!overlap2D(b0, b1, b2, a0, a1, a2)) return false;
		return true;

	coplanar:

		// 1. 법선 벡터 선택
		XMVECTOR n = XMVector3Cross(a1 - a0, a2 - a0); // Triangle A의 법선

		XMFLOAT3 nf;
		XMStoreFloat3(&nf, XMVectorAbs(n));

		int i0, i1; // 투영할 축 (ex: i0 = x, i1 = y)

		if (nf.x > nf.y && nf.x > nf.z) { i0 = 1; i1 = 2; } // X가 가장 크면 YZ 평면으로
		else if (nf.y > nf.z) { i0 = 0; i1 = 2; } // Y가 가장 크면 XZ 평면으로
		else { i0 = 0; i1 = 1; } // Z가 가장 크면 XY 평면으로

		// 2. 2D 좌표로 변환하는 헬퍼
		auto to2D = [&](const XMVECTOR& v) -> XMFLOAT2 {
			XMFLOAT3 vf; XMStoreFloat3(&vf, v);
			return { (&vf.x)[i0], (&vf.x)[i1] };
			};

		// 3. A 삼각형
		XMFLOAT2 a2d[3] = {
			to2D(a0), to2D(a1), to2D(a2)
		};
		//    B 삼각형
		XMFLOAT2 b2d[3] = {
			to2D(b0), to2D(b1), to2D(b2)
		};

		// 4. 2D 삼각형 교차 함수 사용
		if (!TriTriOverlap2D(a2d[0], a2d[1], a2d[2], b2d[0], b2d[1], b2d[2]))
			return false; // 교차 없음

		return true;  // 교차 있음
	}


	//-------------------------------------
	// When both nodes are leaves => Perform triangle-triangle intersection test
	//-------------------------------------
	bool IntersectLeafLeaf(
		const Primitive& mesh1,
		const Primitive& mesh2,
		const geometrics::BVH::Node& node1, const geometrics::BVH::Node& node2,
		const XMMATRIX& T,
		int& intersectTriMesh1,
		int& intersectTriMesh2
	)
	{
		const XMFLOAT3* positions1 = mesh1.GetVtxPositions().data();
		const XMFLOAT3* positions2 = mesh2.GetVtxPositions().data();
		const uint32_t* indices1 = mesh1.GetIdxPrimives().data();
		const uint32_t* indices2 = mesh2.GetIdxPrimives().data();
		const geometrics::BVH& bvh1 = mesh1.GetBVH();
		const geometrics::BVH& bvh2 = mesh2.GetBVH();
		// node1 contains triangles from mesh1,
		// node2 contains triangles from mesh2
		// Return true if any intersection is found
		for (uint32_t i = 0; i < node1.count; ++i)
		{
			uint32_t triIndex1 = bvh1.leaf_indices[node1.offset + i];

			const XMFLOAT3& mesh1P0 = positions1[indices1[3 * triIndex1 + 0]];
			const XMFLOAT3& mesh1P1 = positions1[indices1[3 * triIndex1 + 1]];
			const XMFLOAT3& mesh1P2 = positions1[indices1[3 * triIndex1 + 2]];

			// Transform mesh1's triangle to mesh2's coordinate system:
			XMFLOAT3 mesh1os2P0, mesh1os2P1, mesh1os2P2;
			TransformTriangle(mesh1P0, mesh1P1, mesh1P2, T, mesh1os2P0, mesh1os2P1, mesh1os2P2);

			for (uint32_t j = 0; j < node2.count; ++j)
			{
				uint32_t triIndex2 = bvh2.leaf_indices[node2.offset + j];
				const XMFLOAT3& mesh2P0 = positions2[indices2[3 * triIndex2 + 0]];
				const XMFLOAT3& mesh2P1 = positions2[indices2[3 * triIndex2 + 1]];
				const XMFLOAT3& mesh2P2 = positions2[indices2[3 * triIndex2 + 2]];

				// Now perform triangle-triangle intersection test
				// both triangles are in mesh2's space
				// or use IntersectTriangleTriangle
				if (TriTriIntersectFast(mesh1os2P0, mesh1os2P1, mesh1os2P2,
					mesh2P0, mesh2P1, mesh2P2))
				{
					intersectTriMesh1 = triIndex1; 
					intersectTriMesh2 = triIndex2; 
					return true; // Return true immediately if any intersection is found
				}
			}
		}

		return false; // No intersection
	}

	bool IntersectBVH_LoopStack(
		const Primitive& meshA,
		const Primitive& meshB,
		CXMMATRIX        AtoB,                 // meshA → meshB
		int& triIdxA,
		int& triIdxB)
	{
		const auto& bvhA = meshA.GetBVH();
		const auto& bvhB = meshB.GetBVH();

		struct Pair { uint32_t a, b; };
		std::vector<Pair> stack;
		stack.reserve(256);
		stack.push_back({ 0, 0 });               // root–root

		uint64_t cntAABB = 0;
		uint64_t cntTriTri = 0;

		while (!stack.empty())
		{
			const auto [ia, ib] = stack.back();
			stack.pop_back();

			const auto& nodeA = bvhA.nodes[ia];
			const auto& nodeB = bvhB.nodes[ib];

			// 1) Transform nodeA to B space only once
			geometrics::AABB aabbAinB = nodeA.aabb.transform(AtoB);

			// 2) AABB intersection test
			if (!aabbAinB.intersects(nodeB.aabb))
				continue;

			// 3) Both are leaves ==> triangle intersection
			if (nodeA.isLeaf() && nodeB.isLeaf())
			{
				++cntTriTri;
				if (IntersectLeafLeaf(meshA, meshB, nodeA, nodeB,
					AtoB, triIdxA, triIdxB))
					return true;
				continue;
			}

			// 4) Decide which side to branch first
			const bool splitA =
				!nodeA.isLeaf() &&
				(nodeB.isLeaf() ||
					nodeA.aabb.getArea() > nodeB.aabb.getArea());

			if (splitA)
			{
				stack.push_back({ nodeA.left,     ib });
				stack.push_back({ nodeA.left + 1, ib });
			}
			else if (!nodeB.isLeaf())
			{
				stack.push_back({ ia, nodeB.left });
				stack.push_back({ ia, nodeB.left + 1 });
			}
		}

		return false;
	}


	//-------------------------------------
	// Recursive BVH Intersection Test Function
	//
	//  nodeIdx1: Index of mesh1.bvh.nodes[]
	//  nodeIdx2: Index of mesh2.bvh.nodes[]
	//-------------------------------------
	bool IntersectBVH_Recursive(
		const Primitive& mesh1,
		const Primitive& mesh2,
		const XMMATRIX& T, // mesh1 -> mesh2
		const uint32_t nodeIdx1,
		const uint32_t nodeIdx2,
		int& intersectTriMesh1,
		int& intersectTriMesh2
	)
	{
		const geometrics::BVH& newBvh1 = mesh1.GetBVH();
		const geometrics::BVH& newBvh2 = mesh2.GetBVH();
		// getting BVH nodes of each mesh 
		const auto& node1 = newBvh1.nodes[nodeIdx1];
		const auto& node2 = newBvh2.nodes[nodeIdx2];

		// 1) transform node1's AABB to mesh2's object space by T
		geometrics::AABB aabb1_in_mesh2 = node1.aabb.transform(T);

		// 2) intersection test between the transformed AABB and mesh2's AABB 
		//    unless intersecting each other, no need to further test
		if (aabb1_in_mesh2.intersects(node2.aabb) == geometrics::AABB::OUTSIDE)
		{
			return false;
		}

		// 3) if both nodes are leaf, test the intersection between their triangles
		if (node1.isLeaf() && node2.isLeaf())
		{
			return IntersectLeafLeaf(mesh1, mesh2, node1, node2, T, intersectTriMesh1, intersectTriMesh2);
		}
		// 4) if not, test the AABB intersection between node and the other node's children recursively
		else if (!node1.isLeaf() && node2.isLeaf())
		{
			// node1: inner node, node2: leaf
			// check node1's left and right children
			if (IntersectBVH_Recursive(mesh1, mesh2, T, node1.left, nodeIdx2, intersectTriMesh1, intersectTriMesh2)) return true;
			if (IntersectBVH_Recursive(mesh1, mesh2, T, node1.left + 1, nodeIdx2, intersectTriMesh1, intersectTriMesh2)) return true;
		}
		else if (node1.isLeaf() && !node2.isLeaf())
		{
			// node2: inner node, node1: leaf
			// check node2's left and right children
			if (IntersectBVH_Recursive(mesh1, mesh2, T, nodeIdx1, node2.left, intersectTriMesh1, intersectTriMesh2))     return true;
			if (IntersectBVH_Recursive(mesh1, mesh2, T, nodeIdx1, node2.left + 1, intersectTriMesh1, intersectTriMesh2)) return true;
		}
		else
		{
			// both are inner nodes
			// check the combination of both nodes' children
			if (IntersectBVH_Recursive(mesh1, mesh2, T, node1.left, node2.left, intersectTriMesh1, intersectTriMesh2))     return true;
			if (IntersectBVH_Recursive(mesh1, mesh2, T, node1.left, node2.left + 1, intersectTriMesh1, intersectTriMesh2)) return true;
			if (IntersectBVH_Recursive(mesh1, mesh2, T, node1.left + 1, node2.left, intersectTriMesh1, intersectTriMesh2))     return true;
			if (IntersectBVH_Recursive(mesh1, mesh2, T, node1.left + 1, node2.left + 1, intersectTriMesh1, intersectTriMesh2)) return true;
		}

		// no intersection
		return false;
	}

	bool CollisionPairwiseCheck(const Entity geometryEntity1, const Entity transformEntity1, const Entity geometryEntity2, const Entity transformEntity2,
		int& partIndex1, int& triIndex1, int& partIndex2, int& triIndex2)
	{
		TransformComponent* transform1 = compfactory::GetTransformComponent(transformEntity1);
		TransformComponent* transform2 = compfactory::GetTransformComponent(transformEntity2);
		GeometryComponent* geometry1 = compfactory::GetGeometryComponent(geometryEntity1);
		GeometryComponent* geometry2 = compfactory::GetGeometryComponent(geometryEntity2);
		if (!(transform1 && transform2 && geometry1 && geometry2))
		{
			vzlog_error("Invalid Component!");
			return false;
		}

		const std::vector<Primitive>& primitives1 = geometry1->GetPrimitives();
		const std::vector<Primitive>& primitives2 = geometry2->GetPrimitives();
		const size_t n1 = primitives1.size();
		const size_t n2 = primitives2.size();
		if (n1 == 0 || n2 == 0)
		{
			vzlog_error("Invalid Geometry! (having no Primitive)");
			return false;
		}

		size_t count_has_bvh = 0;
		if (!geometry1->HasBVH() || geometry1->IsDirtyBVH())
		{
			//vzlog_warning("Scene update is required for BVH of geometry (%d)", geometryEntity1);
			if (!geometry1->IsBusyForBVH())
			{
				vzlog_warning("preparing BVH... (%llu)", geometryEntity1);
				static jobsystem::context ctx; // Must be declared static to prevent context overflow, which could lead to thread access violations
				jobsystem::Execute(ctx, [geometryEntity1](jobsystem::JobArgs args) {
					GeometryComponent* geometry1 = compfactory::GetGeometryComponent(geometryEntity1);
					geometry1->UpdateBVH(true);
					});
			}
		}
		else
		{
			count_has_bvh++;
		}

		if (!geometry2->HasBVH() || geometry2->IsDirtyBVH())
		{
			if (!geometry1->IsBusyForBVH())
			{
				vzlog_warning("preparing BVH... (%llu)", geometryEntity2);
				static jobsystem::context ctx; // Must be declared static to prevent context overflow, which could lead to thread access violations
				jobsystem::Execute(ctx, [geometryEntity2](jobsystem::JobArgs args) {
					GeometryComponent* geometry2 = compfactory::GetGeometryComponent(geometryEntity2);
					geometry2->UpdateBVH(true);
					});
			}
		}
		else
		{
			count_has_bvh++;
		}
		if (count_has_bvh < 2)
		{
			return false;
		}
		
		XMMATRIX m1os2ws = XMLoadFloat4x4(&transform1->GetWorldMatrix());
		XMMATRIX m2os2ws = XMLoadFloat4x4(&transform2->GetWorldMatrix());
		XMMATRIX m2ws2os = XMMatrixInverse(nullptr, m2os2ws);
		XMMATRIX m1ws2os = XMMatrixInverse(nullptr, m1os2ws);

		XMMATRIX T = XMMatrixMultiply(m1os2ws, m2ws2os); // os1 to os2
		XMMATRIX T_inv = XMMatrixMultiply(m2os2ws, m1ws2os); // // os2 to os1

		auto range = profiler::BeginRangeCPU("Collision Detection");
		bool is_collision = false;
		partIndex1 = partIndex2 = -1;
		triIndex1 = triIndex2 = -1;

		using namespace GJKcollision;
		for (size_t i = 0; i < n1; ++i)
		{
			const Primitive& prim1 = primitives1[i];
			int is_convex_1 = (int)prim1.IsConvexShape();

			for (size_t j = 0; j < n2; ++j)
			{
				const Primitive& prim2 = primitives2[j];
				int is_convex_2 = (int)prim2.IsConvexShape();
				switch (is_convex_1 + is_convex_2)
				{
				case 1:
					if (is_convex_1)
					{
						ConvexShape convex_shape;
						convex_shape.n = prim1.GetNumVertices();
						convex_shape.v = (const XMFLOAT3*)prim1.GetVtxPositions().data();
						int tri_idx;
						if (GJK_ConvexVsBVH(convex_shape, prim2, T, tri_idx))
						{ 
							partIndex1 = i;
							partIndex2 = j;
							i = n1; j = n2;
							is_collision = true;
						}
					}
					else
					{
						ConvexShape convex_shape;
						convex_shape.n = prim2.GetNumVertices();
						convex_shape.v = (const XMFLOAT3*)prim2.GetVtxPositions().data();
						int tri_idx;
						if (GJK_ConvexVsBVH(convex_shape, prim1, T_inv, tri_idx))
						{
							partIndex1 = i;
							partIndex2 = j;
							i = n1; j = n2;
							is_collision = true;
						}
					}
					break;
				case 2:
					//GJKcollision::GJK_Intersect()
					break;
				default: // 0
					if (IntersectBVH_LoopStack(primitives1[i], primitives2[j], T, triIndex1, triIndex2))
						//if (IntersectBVH_Recursive(primitives1[i], primitives2[j], T, /*root1=*/0, /*root2=*/0, triIndex1, triIndex2))
					{
						partIndex1 = i;
						partIndex2 = j;
						is_collision = true;
						i = n1; j = n2;
						//break;
					}
					break;
				}
			}
		}
		profiler::EndRange(range);
		return is_collision;
	}
}