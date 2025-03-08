#include "SimpleCollision.h"
#include "Utils/Backlog.h"
#include "Utils/Jobsystem.h"
#include "Utils/Profiler.h"
#include "Components/Components.h"

namespace vz::bvhcollision
{
	using namespace vz;
	using Primitive = GeometryComponent::Primitive;

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
			// Plane equation of triangle A: N_A ¡¤ (X - A0) = 0
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
				if (IntersectTriangleTriangle(
					mesh1os2P0, mesh1os2P1, mesh1os2P2,
					mesh2P0, mesh2P1, mesh2P2
				))
				{
					intersectTriMesh1 = triIndex1;
					intersectTriMesh2 = triIndex2;
					return true; // Return true immediately if any intersection is found
				}
			}
		}

		return false; // No intersection
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
			if (!geometry1->IsBusyForBVH())
			{
				vzlog_warning("preparing BVH... (%d)", geometryEntity1);
				jobsystem::context ctx;
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
				vzlog_warning("preparing BVH... (%d)", geometryEntity2);
				jobsystem::context ctx;
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

		XMMATRIX T = XMMatrixMultiply(m1os2ws, m2ws2os);

		auto range = profiler::BeginRangeCPU("Collision Detection");
		bool is_collision = false;
		partIndex1 = partIndex2 = -1;
		triIndex1 = triIndex2 = -1;
		for (size_t i = 0; i < n1; ++i)
		{
			for (size_t j = 0; j < n2; ++j)
			{
				if (IntersectBVH_Recursive(primitives1[i], primitives2[j], T, /*root1=*/0, /*root2=*/0, triIndex1, triIndex2))
				{
					partIndex1 = i;
					partIndex2 = j;
					is_collision = true;
					break; 
				}
			}
		}
		profiler::EndRange(range);
		return is_collision;
	}
}