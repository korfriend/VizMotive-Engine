#include "GeometryGenerator.h"

//#include "CommonInclude.h"
#include "Common/Engine_Internal.h"
#include "Utils/Backlog.h"
#include "Components/Components.h"

namespace vz::geogen
{
	// source from https://github.com/mrdoob/three.js/blob/dev/src/geometries/

	using Primitive = GeometryComponent::Primitive;
	using PrimitiveType = GeometryComponent::PrimitiveType;
	using NormalComputeMethod = GeometryComponent::NormalComputeMethod;

	class PolyhedronGeometry
	{
		/**
		 * Constructs a new polyhedron geometry.
		 *
		 * @param {Array<number>} [vertices=[]] - A flat array of vertices.
		 * @param {Array<number>} [indices=[]] - A flat array of indices.
		 * @param {number} [radius=1] - The radius of the polyhedron.
		 * @param {number} [detail=0] - The level of detail (subdivision).
		 */

		Primitive primitive;

		// Parameters
		std::vector<XMFLOAT3> inputVertices;
		std::vector<uint32_t> inputIndices;
		float radius = 1.f;
		uint32_t detail = 0;

	private:
		// Helper function to push a vertex to the primitive
		void pushVertex(const XMFLOAT3& vertex) {
			primitive.GetMutableVtxPositions().push_back(vertex);
		}

		// Helper function to get a vertex by index
		void getVertexByIndex(uint32_t index, XMFLOAT3& vertex) {
			vertex = inputVertices[index];
		}

		// Angle around the Y axis, counter-clockwise when looking from above
		float azimuth(const XMFLOAT3& vector) {
			return atan2f(vector.z, -vector.x);
		}

		// Angle above the XZ plane
		float inclination(const XMFLOAT3& vector) {
			return atan2f(-vector.y, sqrtf((vector.x * vector.x) + (vector.z * vector.z)));
		}

		// Subdivide a face into more triangles
		void subdivideFace(const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c, uint32_t detail) {
			// If detail is 0, just add the face without subdivision
			if (detail == 0) {
				pushVertex(a);
				pushVertex(b);
				pushVertex(c);
				return;
			}

			const uint32_t cols = detail + 1;

			// We use this multidimensional array as a data structure for creating the subdivision
			std::vector<std::vector<XMFLOAT3>> v(cols + 1);

			// Construct all of the vertices for this subdivision
			for (uint32_t i = 0; i <= cols; i++) {
				v[i].resize(cols - i + 1);

				XMFLOAT3 aj;
				XMFLOAT3 bj;

				// Lerp from a to c
				XMVECTOR vA = XMLoadFloat3(&a);
				XMVECTOR vC = XMLoadFloat3(&c);
				XMStoreFloat3(&aj, XMVectorLerp(vA, vC, (float)i / cols));

				// Lerp from b to c
				XMVECTOR vB = XMLoadFloat3(&b);
				XMStoreFloat3(&bj, XMVectorLerp(vB, vC, (float)i / cols));

				// Create points along the line from aj to bj
				const uint32_t rows = cols - i;

				for (uint32_t j = 0; j <= rows; j++) {
					if (j == 0 && i == cols) {
						v[i][j] = aj;
					}
					else {
						// Lerp from aj to bj
						XMVECTOR vAj = XMLoadFloat3(&aj);
						XMVECTOR vBj = XMLoadFloat3(&bj);
						XMStoreFloat3(&v[i][j], XMVectorLerp(vAj, vBj, (float)j / rows));
					}
				}
			}

			// Construct all of the faces
			for (uint32_t i = 0; i < cols; i++) {
				for (uint32_t j = 0; j < 2 * (cols - i) - 1; j++) {
					const uint32_t k = j / 2;

					if (j % 2 == 0) {
						pushVertex(v[i][k + 1]);
						pushVertex(v[i + 1][k]);
						pushVertex(v[i][k]);
					}
					else {
						pushVertex(v[i][k + 1]);
						pushVertex(v[i + 1][k + 1]);
						pushVertex(v[i + 1][k]);
					}
				}
			}
		}

		// Create the subdivision
		void subdivide(uint32_t detail) {
			XMFLOAT3 a, b, c;

			// Iterate over all faces and apply a subdivision with the given detail value
			for (size_t i = 0; i < inputIndices.size(); i += 3) {
				// Get the vertices of the face
				getVertexByIndex(inputIndices[i + 0], a);
				getVertexByIndex(inputIndices[i + 1], b);
				getVertexByIndex(inputIndices[i + 2], c);

				// Perform subdivision
				subdivideFace(a, b, c, detail);
			}
		}

		// Apply the radius to all vertices
		void applyRadius(float radius) {
			std::vector<XMFLOAT3>& positions = primitive.GetMutableVtxPositions();

			for (size_t i = 0; i < positions.size(); i++) {
				// Normalize and scale
				XMVECTOR v = XMLoadFloat3(&positions[i]);
				v = XMVector3Normalize(v);
				v = XMVectorScale(v, radius);
				XMStoreFloat3(&positions[i], v);
			}
		}

		// Correct UVs for better mapping
		void correctUVs() {
			std::vector<XMFLOAT3>& positions = primitive.GetMutableVtxPositions();
			std::vector<XMFLOAT2>& uvs = primitive.GetMutableVtxUVSet0();

			// Process triangles (3 vertices at a time)
			for (size_t i = 0; i < positions.size(); i += 3) {
				if (i + 2 >= positions.size()) break; // Safety check

				XMFLOAT3 centroid;

				// Calculate centroid of the triangle
				XMVECTOR vA = XMLoadFloat3(&positions[i]);
				XMVECTOR vB = XMLoadFloat3(&positions[i + 1]);
				XMVECTOR vC = XMLoadFloat3(&positions[i + 2]);
				XMVECTOR vCentroid = XMVectorAdd(XMVectorAdd(vA, vB), vC);
				vCentroid = XMVectorDivide(vCentroid, XMVectorReplicate(3.0f));
				XMStoreFloat3(&centroid, vCentroid);

				// Get azimuth of the centroid
				float azi = azimuth(centroid);

				// Correct UVs based on azimuth and pole positions
				for (size_t j = 0; j < 3; j++) {
					// Handle the seam (wrap around)
					if (azi < 0 && uvs[i + j].x == 1.0f) {
						uvs[i + j].x -= 1.0f;
					}

					// Handle pole points
					if (positions[i + j].x == 0 && positions[i + j].z == 0) {
						uvs[i + j].x = azi / (2.0f * XM_PI) + 0.5f;
					}
				}
			}
		}

		// Correct seam in UV coordinates
		void correctSeam() {
			std::vector<XMFLOAT2>& uvs = primitive.GetMutableVtxUVSet0();

			// Process triangles (3 vertices at a time)
			for (size_t i = 0; i < uvs.size(); i += 3) {
				if (i + 2 >= uvs.size()) break; // Safety check

				// Get the three UV x-coordinates
				const float x0 = uvs[i].x;
				const float x1 = uvs[i + 1].x;
				const float x2 = uvs[i + 2].x;

				// Find max and min
				const float max = std::max(std::max(x0, x1), x2);
				const float min = std::min(std::min(x0, x1), x2);

				// If face straddles the seam (part near 0, part near 1)
				if (max > 0.9f && min < 0.1f) {
					if (x0 < 0.2f) uvs[i].x += 1.0f;
					if (x1 < 0.2f) uvs[i + 1].x += 1.0f;
					if (x2 < 0.2f) uvs[i + 2].x += 1.0f;
				}
			}
		}

		// Generate UV coordinates
		void generateUVs() {
			std::vector<XMFLOAT3>& positions = primitive.GetMutableVtxPositions();
			std::vector<XMFLOAT2>& uvs = primitive.GetMutableVtxUVSet0();
			uvs.resize(positions.size());

			// Generate UV coordinates for each vertex
			for (size_t i = 0; i < positions.size(); i++) {
				const float u = azimuth(positions[i]) / (2.0f * XM_PI) + 0.5f;
				const float v = inclination(positions[i]) / XM_PI + 0.5f;
				uvs[i] = XMFLOAT2(u, 1.0f - v);
			}

			correctUVs();
			correctSeam();
		}

	public:
		PolyhedronGeometry(const std::vector<XMFLOAT3>& vertices = {},
			const std::vector<uint32_t>& indices = {},
			float radius = 1.0f,
			uint32_t detail = 0) {

			// Store input parameters
			this->inputVertices = vertices;
			this->inputIndices = indices;
			this->radius = radius;
			this->detail = detail;

			// Clear any existing data
			primitive.GetMutableVtxPositions().clear();
			primitive.GetMutableVtxNormals().clear();
			primitive.GetMutableVtxUVSet0().clear();
			primitive.GetMutableIdxPrimives().clear();

			// Ensure we have valid data
			if (inputVertices.empty() || inputIndices.empty()) {
				// If no data provided, return an empty geometry
				return;
			}

			// The subdivision creates the vertex buffer data
			subdivide(detail);

			// Check if we have vertices after subdivision
			if (primitive.GetMutableVtxPositions().empty()) {
				return;
			}

			// All vertices should lie on a conceptual sphere with a given radius
			applyRadius(radius);

			// Finally, create the uv data
			generateUVs();

			// Generate sequential indices
			std::vector<uint32_t>& idxPrimitives = primitive.GetMutableIdxPrimives();
			const size_t vertexCount = primitive.GetMutableVtxPositions().size();
			idxPrimitives.resize(vertexCount);

			for (size_t i = 0; i < vertexCount; i++) {
				idxPrimitives[i] = static_cast<uint32_t>(i);
			}

			// Set primitive type to triangles
			primitive.SetPrimitiveType(PrimitiveType::TRIANGLES);

			// Generate normals
			if (detail == 0) {
				// For flat normals, we need to compute them per face
				std::vector<XMFLOAT3>& positions = primitive.GetMutableVtxPositions();
				std::vector<XMFLOAT3>& normals = primitive.GetMutableVtxNormals();
				normals.resize(positions.size());

				// Calculate flat normals manually
				for (size_t i = 0; i < positions.size(); i += 3) {
					if (i + 2 >= positions.size()) break; // Safety check

					// Get the three vertices of the face
					XMVECTOR v0 = XMLoadFloat3(&positions[i]);
					XMVECTOR v1 = XMLoadFloat3(&positions[i + 1]);
					XMVECTOR v2 = XMLoadFloat3(&positions[i + 2]);

					// Calculate face normal
					XMVECTOR edge1 = XMVectorSubtract(v1, v0);
					XMVECTOR edge2 = XMVectorSubtract(v2, v0);
					XMVECTOR normal = XMVector3Cross(edge1, edge2);
					normal = XMVector3Normalize(normal);

					// Assign the same normal to all three vertices of the face
					XMFLOAT3 normalFloat;
					XMStoreFloat3(&normalFloat, normal);
					normals[i] = normalFloat;
					normals[i + 1] = normalFloat;
					normals[i + 2] = normalFloat;
				}
			}
			else {
				// For smooth normals - just normalize the positions
				//std::vector<XMFLOAT3>& positions = primitive.GetMutableVtxPositions();
				//std::vector<XMFLOAT3>& normals = primitive.GetMutableVtxNormals();
				//normals.resize(positions.size());
				//
				//for (size_t i = 0; i < positions.size(); i++) {
				//	XMVECTOR v = XMLoadFloat3(&positions[i]);
				//	v = XMVector3Normalize(v);
				//	XMStoreFloat3(&normals[i], v);
				//}
				primitive.ComputeNormals(NormalComputeMethod::COMPUTE_NORMALS_SMOOTH_FAST);
			}


			// Compute bounding box
			primitive.ComputeAABB();
		}

		// Get the primitive
		Primitive& GetPrimitive() { return primitive; }
	};

	bool GenerateIcosahedronGeometry(Entity geometryEntity, const float radius, const uint32_t detail)
	{
		GeometryComponent* geometry = compfactory::GetGeometryComponent(geometryEntity);
		if (geometry == nullptr)
		{
			vzlog_error("Invalid geometryEntity");
			return false;
		}

		const float t = (1.f + sqrt(5.f)) / 2.f;

		const std::vector<XMFLOAT3> vertices = {
			{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
			{0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
			{t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}
		};

		const std::vector<uint32_t> indices = {
			0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
			1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
			3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
			4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1
		};

		PolyhedronGeometry plolyhedron(vertices, indices, radius, std::min(detail, 11u));

		{
			std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());
			geometry->ClearGeometry();
			geometry->AddMovePrimitiveFrom(std::move(plolyhedron.GetPrimitive()));
			geometry->UpdateRenderData();
		}

		return true;
	}

	class TorusKnotGeometry
	{
		/**
		 * Constructs a new torus knot geometry.
		 *
		 * @param {number} [radius=1] - The radius of the torus.
		 * @param {number} [tube=0.4] - The thickness of the tube.
		 * @param {number} [tubularSegments=64] - The number of segments along the tube.
		 * @param {number} [radialSegments=8] - The number of segments around the tube.
		 * @param {number} [p=2] - How many times the geometry winds around its axis of rotational symmetry.
		 * @param {number} [q=3] - How many times the geometry winds around a circle in the interior of the torus.
		 */

		Primitive primitive;

		float radius = 1.f;
		float tube = 0.4f;
		uint32_t tubularSegments = 64;
		uint32_t radialSegments = 8;
		uint32_t p = 2;
		uint32_t q = 3;

	private:
		// This function calculates the current position on the torus curve
		void calculatePositionOnCurve(const float u, const uint32_t p, const uint32_t q, const float radius, XMFLOAT3& position) const {
			const float cu = cosf(u);
			const float su = sinf(u);
			const float quOverP = (float)q / (float)p * u;
			const float cs = cosf(quOverP);

			position.x = radius * (2.f + cs) * 0.5f * cu;
			position.y = radius * (2.f + cs) * su * 0.5f;
			position.z = radius * sinf(quOverP) * 0.5f;
		}

	public:
		TorusKnotGeometry(const float radius = 1.f, const float tube = 0.4f, uint32_t tubularSegments = 64,
			uint32_t radialSegments = 8, const uint32_t p = 2, const uint32_t q = 3) {

			// Floor the segment counts to ensure they're integers, just like in the JS version
			tubularSegments = (uint32_t)floorf((float)tubularSegments);
			radialSegments = (uint32_t)floorf((float)radialSegments);

			// Store parameters
			this->radius = radius;
			this->tube = tube;
			this->tubularSegments = tubularSegments;
			this->radialSegments = radialSegments;
			this->p = p;
			this->q = q;

			// Get buffers
			std::vector<XMFLOAT3>& positions = primitive.GetMutableVtxPositions();
			std::vector<XMFLOAT3>& normals = primitive.GetMutableVtxNormals();
			std::vector<XMFLOAT2>& uvs = primitive.GetMutableVtxUVSet0();
			std::vector<uint32_t>& indices = primitive.GetMutableIdxPrimives();

			// Reserve space for better performance
			positions.reserve((tubularSegments + 1) * (radialSegments + 1));
			normals.reserve((tubularSegments + 1) * (radialSegments + 1));
			uvs.reserve((tubularSegments + 1) * (radialSegments + 1));
			indices.reserve(tubularSegments * radialSegments * 6);

			// Helper variables
			XMFLOAT3 vertex;
			XMFLOAT3 normal;

			XMFLOAT3 P1;
			XMFLOAT3 P2;

			XMFLOAT3 B;
			XMFLOAT3 N;

			// Generate vertices, normals and uvs
			for (uint32_t i = 0; i <= tubularSegments; ++i) {
				// The radian "u" is used to calculate the position on the torus curve of the current tubular segment
				const float u = (float)i / (float)tubularSegments * (float)p * XM_PI * 2.f;

				// Now we calculate two points. P1 is our current position on the curve, P2 is a little farther ahead.
				// These points are used to create a special "coordinate space", which is necessary to calculate the correct vertex positions
				calculatePositionOnCurve(u, p, q, radius, P1);
				calculatePositionOnCurve(u + 0.01f, p, q, radius, P2);

				// Calculate orthonormal basis
				// T = P2 - P1
				XMVECTOR vP1 = XMLoadFloat3(&P1);
				XMVECTOR vP2 = XMLoadFloat3(&P2);
				XMVECTOR vT = XMVectorSubtract(vP2, vP1);

				// N = P2 + P1
				XMVECTOR vN = XMVectorAdd(vP2, vP1);

				// B = T × N (cross product)
				XMVECTOR vB = XMVector3Cross(vT, vN);

				// N = B × T (cross product)
				vN = XMVector3Cross(vB, vT);

				// Normalize B, N. T can be ignored, we don't use it
				vB = XMVector3Normalize(vB);
				vN = XMVector3Normalize(vN);

				XMStoreFloat3(&B, vB);
				XMStoreFloat3(&N, vN);

				for (uint32_t j = 0; j <= radialSegments; ++j) {
					// Now calculate the vertices. They are nothing more than an extrusion of the torus curve.
					// Because we extrude a shape in the xy-plane, there is no need to calculate a z-value.
					const float v = (float)j / (float)radialSegments * XM_PI * 2.f;
					const float cx = -tube * cosf(v);
					const float cy = tube * sinf(v);

					// Now calculate the final vertex position.
					// First we orient the extrusion with our basis vectors, then we add it to the current position on the curve
					vertex.x = P1.x + (cx * N.x + cy * B.x);
					vertex.y = P1.y + (cx * N.y + cy * B.y);
					vertex.z = P1.z + (cx * N.z + cy * B.z);

					positions.push_back(vertex);

					// Normal (P1 is always the center/origin of the extrusion, thus we can use it to calculate the normal)
					// normal = (vertex - P1).normalize()
					XMVECTOR vVertex = XMLoadFloat3(&vertex);
					XMVECTOR vNormal = XMVectorSubtract(vVertex, vP1);
					vNormal = XMVector3Normalize(vNormal);

					XMStoreFloat3(&normal, vNormal);
					normals.push_back(normal);

					// UV
					XMFLOAT2 uv;
					uv.x = (float)i / (float)tubularSegments;
					uv.y = (float)j / (float)radialSegments;
					uvs.push_back(uv);
				}
			}

			// Generate indices
			for (uint32_t j = 1; j <= tubularSegments; j++) {
				for (uint32_t i = 1; i <= radialSegments; i++) {
					// Indices
					const uint32_t a = (radialSegments + 1) * (j - 1) + (i - 1);
					const uint32_t b = (radialSegments + 1) * j + (i - 1);
					const uint32_t c = (radialSegments + 1) * j + i;
					const uint32_t d = (radialSegments + 1) * (j - 1) + i;

					// Faces with original winding order
					indices.push_back(a);
					indices.push_back(b);
					indices.push_back(d);

					indices.push_back(b);
					indices.push_back(c);
					indices.push_back(d);
				}
			}

			// Set primitive type and compute bounding box
			primitive.SetPrimitiveType(PrimitiveType::TRIANGLES);
			primitive.ComputeAABB();
		}

		// Get the primitive
		Primitive& GetPrimitive() { return primitive; }
	};

	bool GenerateTorusKnotGeometry(Entity geometryEntity, const float radius, const float tube, 
		const uint32_t tubularSegments, const uint32_t radialSegments, const uint32_t p, const uint32_t q)
	{
		GeometryComponent* geometry = compfactory::GetGeometryComponent(geometryEntity);
		if (geometry == nullptr)
		{
			vzlog_error("Invalid geometryEntity");
			return false;
		}

		TorusKnotGeometry torusknot(radius, tube, tubularSegments, radialSegments, p, q);

		{
			std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());
			geometry->ClearGeometry();
			geometry->AddMovePrimitiveFrom(std::move(torusknot.GetPrimitive()));
			geometry->UpdateRenderData();
		}

		return true;
	}

	class BoxGeometry
	{
		/**
		 * Constructs a new box geometry for Right-Handed System (RHS).
		 *
		 * @param {number} [width=1] - Width of the box.
		 * @param {number} [height=1] - Height of the box.
		 * @param {number} [depth=1] - Depth of the box.
		 * @param {number} [widthSegments=1] - Number of width segments.
		 * @param {number} [heightSegments=1] - Number of height segments.
		 * @param {number} [depthSegments=1] - Number of depth segments.
		 */

		Primitive primitive;

		// Parameters
		float width = 1.f;
		float height = 1.f;
		float depth = 1.f;
		uint32_t widthSegments = 1;
		uint32_t heightSegments = 1;
		uint32_t depthSegments = 1;

	private:
		// Helper function to build each side of the box
		void buildPlane(char u, char v, char w, float udir, float vdir,
			float width, float height, float depth,
			uint32_t gridX, uint32_t gridY, uint32_t materialIndex,
			std::vector<XMFLOAT3>& vertices,
			std::vector<XMFLOAT3>& normals,
			std::vector<XMFLOAT2>& uvs,
			std::vector<uint32_t>& indices,
			uint32_t& numberOfVertices,
			uint32_t& groupStart) {

			const float segmentWidth = width / gridX;
			const float segmentHeight = height / gridY;

			const float widthHalf = width / 2.f;
			const float heightHalf = height / 2.f;
			const float depthHalf = depth / 2.f;

			const uint32_t gridX1 = gridX + 1;
			const uint32_t gridY1 = gridY + 1;

			uint32_t vertexCounter = 0;
			uint32_t groupCount = 0;

			XMFLOAT3 vector(0.f, 0.f, 0.f);

			// Generate vertices, normals and uvs
			for (uint32_t iy = 0; iy < gridY1; iy++) {
				const float y = iy * segmentHeight - heightHalf;

				for (uint32_t ix = 0; ix < gridX1; ix++) {
					const float x = ix * segmentWidth - widthHalf;

					// Set values to correct vector component
					if (u == 'x') vector.x = x * udir;
					else if (u == 'y') vector.y = x * udir;
					else if (u == 'z') vector.z = x * udir;

					if (v == 'x') vector.x = y * vdir;
					else if (v == 'y') vector.y = y * vdir;
					else if (v == 'z') vector.z = y * vdir;

					if (w == 'x') vector.x = depthHalf;
					else if (w == 'y') vector.y = depthHalf;
					else if (w == 'z') vector.z = depthHalf;

					// Add to vertices
					vertices.push_back(vector);

					// Set up normal vector
					XMFLOAT3 normal(0.f, 0.f, 0.f);

					if (w == 'x') normal.x = depth > 0.f ? 1.f : -1.f;
					else if (w == 'y') normal.y = depth > 0.f ? 1.f : -1.f;
					else if (w == 'z') normal.z = depth > 0.f ? 1.f : -1.f;

					// Add to normals
					normals.push_back(normal);

					// Add UVs
					XMFLOAT2 uv(
						(float)ix / (float)gridX,
						1.f - ((float)iy / (float)gridY)
					);
					uvs.push_back(uv);

					// Increment counter
					vertexCounter++;
				}
			}

			// Indices
			// For RHS, we need clockwise winding order when viewed from outside
			for (uint32_t iy = 0; iy < gridY; iy++) {
				for (uint32_t ix = 0; ix < gridX; ix++) {
					const uint32_t a = numberOfVertices + ix + gridX1 * iy;
					const uint32_t b = numberOfVertices + ix + gridX1 * (iy + 1);
					const uint32_t c = numberOfVertices + (ix + 1) + gridX1 * (iy + 1);
					const uint32_t d = numberOfVertices + (ix + 1) + gridX1 * iy;

					// Faces with clockwise winding order for RHS
					indices.push_back(a);
					indices.push_back(b);
					indices.push_back(d);

					indices.push_back(b);
					indices.push_back(c);
					indices.push_back(d);

					// Increase counter
					groupCount += 6;
				}
			}

			// We keep track of material groups through groupStart and groupCount
			groupStart += groupCount;

			// Update total number of vertices
			numberOfVertices += vertexCounter;
		}

	public:
		BoxGeometry(float width = 1.f, float height = 1.f, float depth = 1.f,
			uint32_t widthSegments = 1, uint32_t heightSegments = 1, uint32_t depthSegments = 1) {

			// Store parameters
			this->width = width;
			this->height = height;
			this->depth = depth;
			this->widthSegments = widthSegments;
			this->heightSegments = heightSegments;
			this->depthSegments = depthSegments;

			// Ensure segments are integers
			widthSegments = (uint32_t)floor(widthSegments);
			heightSegments = (uint32_t)floor(heightSegments);
			depthSegments = (uint32_t)floor(depthSegments);

			// Initialize buffers
			std::vector<XMFLOAT3> vertices;
			std::vector<XMFLOAT3> normals;
			std::vector<XMFLOAT2> uvs;
			std::vector<uint32_t> indices;

			// Helper variables
			uint32_t numberOfVertices = 0;
			uint32_t groupStart = 0;

			// Build each side of the box geometry
			// For RHS: +X is right, +Y is up, +Z is forward (towards viewer)

			// +X face (right)
			buildPlane('z', 'y', 'x', -1.f, -1.f, depth, height, width, depthSegments, heightSegments, 0, vertices, normals, uvs, indices, numberOfVertices, groupStart);

			// -X face (left)
			buildPlane('z', 'y', 'x', 1.f, -1.f, depth, height, -width, depthSegments, heightSegments, 1, vertices, normals, uvs, indices, numberOfVertices, groupStart);

			// +Y face (top)
			buildPlane('x', 'z', 'y', 1.f, 1.f, width, depth, height, widthSegments, depthSegments, 2, vertices, normals, uvs, indices, numberOfVertices, groupStart);

			// -Y face (bottom)
			buildPlane('x', 'z', 'y', 1.f, -1.f, width, depth, -height, widthSegments, depthSegments, 3, vertices, normals, uvs, indices, numberOfVertices, groupStart);

			// +Z face (front - towards viewer)
			buildPlane('x', 'y', 'z', 1.f, -1.f, width, height, depth, widthSegments, heightSegments, 4, vertices, normals, uvs, indices, numberOfVertices, groupStart);

			// -Z face (back - away from viewer)
			buildPlane('x', 'y', 'z', -1.f, -1.f, width, height, -depth, widthSegments, heightSegments, 5, vertices, normals, uvs, indices, numberOfVertices, groupStart);

			// Set geometry data
			std::vector<XMFLOAT3>& vtxPositions = primitive.GetMutableVtxPositions();
			std::vector<XMFLOAT3>& vtxNormals = primitive.GetMutableVtxNormals();
			std::vector<XMFLOAT2>& vtxUVs = primitive.GetMutableVtxUVSet0();
			std::vector<uint32_t>& idxPrimitives = primitive.GetMutableIdxPrimives();

			// Copy vertex data
			vtxPositions = vertices;
			vtxNormals = normals;
			vtxUVs = uvs;
			idxPrimitives = indices;

			// Set primitive type to triangles
			primitive.SetPrimitiveType(PrimitiveType::TRIANGLES);

			// Compute bounding box
			primitive.ComputeAABB();
		}

		// Get the primitive
		Primitive& GetPrimitive() { return primitive; }
	};
	
	bool GenerateBoxGeometry(Entity geometryEntity, const float width, const float height, const float depth, const uint32_t widthSegments, const uint32_t heightSegments, const uint32_t depthSegments)
	{
		GeometryComponent* geometry = compfactory::GetGeometryComponent(geometryEntity);
		if (geometry == nullptr)
		{
			vzlog_error("Invalid geometryEntity");
			return false;
		}

		BoxGeometry box(width, height, depth, widthSegments, heightSegments, depthSegments);

		{
			std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());
			geometry->ClearGeometry();
			geometry->AddMovePrimitiveFrom(std::move(box.GetPrimitive()));
			geometry->UpdateRenderData();
		}

		return true;
	}

	class SphereGeometry
	{
		/**
		 * Constructs a new sphere geometry.
		 *
		 * @param {number} [radius=1] - The radius of the sphere.
		 * @param {number} [widthSegments=32] - The number of segments horizontally.
		 * @param {number} [heightSegments=16] - The number of segments vertically.
		 * @param {number} [phiStart=0] - The starting angle in the horizontal plane.
		 * @param {number} [phiLength=2*PI] - The angle size in the horizontal plane.
		 * @param {number} [thetaStart=0] - The starting angle in the vertical plane.
		 * @param {number} [thetaLength=PI] - The angle size in the vertical plane.
		 */

		Primitive primitive;

		// Parameters
		float radius = 1.0f;
		uint32_t widthSegments = 32;
		uint32_t heightSegments = 16;
		float phiStart = 0.0f;
		float phiLength = XM_2PI;
		float thetaStart = 0.0f;
		float thetaLength = XM_PI;

	private:
		// Build the geometry data
		void buildGeometry() {
			// Clear any existing data
			primitive.GetMutableVtxPositions().clear();
			primitive.GetMutableVtxNormals().clear();
			primitive.GetMutableVtxUVSet0().clear();
			primitive.GetMutableIdxPrimives().clear();

			// Ensure parameters are valid
			widthSegments = std::max(3u, widthSegments);
			heightSegments = std::max(2u, heightSegments);
			const float thetaEnd = std::min(thetaStart + thetaLength, XM_PI);

			// Temporary storage for building the mesh
			std::vector<std::vector<uint32_t>> grid;
			uint32_t index = 0;

			// Vertex position and normal
			XMFLOAT3 vertex;
			XMFLOAT3 normal;

			// Generate vertices, normals and uvs
			for (uint32_t iy = 0; iy <= heightSegments; iy++) {
				std::vector<uint32_t> verticesRow;

				const float v = static_cast<float>(iy) / heightSegments;

				// Special case for the poles
				float uOffset = 0.0f;

				if (iy == 0 && thetaStart == 0.0f) {
					uOffset = 0.5f / widthSegments;
				}
				else if (iy == heightSegments && thetaEnd == XM_PI) {
					uOffset = -0.5f / widthSegments;
				}

				for (uint32_t ix = 0; ix <= widthSegments; ix++) {
					const float u = static_cast<float>(ix) / widthSegments;

					// Calculate vertex position
					vertex.x = -radius * cosf(phiStart + u * phiLength) * sinf(thetaStart + v * thetaLength);
					vertex.y = radius * cosf(thetaStart + v * thetaLength);
					vertex.z = radius * sinf(phiStart + u * phiLength) * sinf(thetaStart + v * thetaLength);

					// Add vertex position
					primitive.GetMutableVtxPositions().push_back(vertex);

					// Calculate and add normal (normalized position vector)
					XMVECTOR normalVec = XMVectorSet(vertex.x, vertex.y, vertex.z, 0.0f);
					normalVec = XMVector3Normalize(normalVec);
					XMStoreFloat3(&normal, normalVec);
					primitive.GetMutableVtxNormals().push_back(normal);

					// Add UV coordinate
					primitive.GetMutableVtxUVSet0().push_back(XMFLOAT2(u + uOffset, 1.0f - v));

					verticesRow.push_back(index++);
				}

				grid.push_back(verticesRow);
			}

			// Generate indices for triangles
			for (uint32_t iy = 0; iy < heightSegments; iy++) {
				for (uint32_t ix = 0; ix < widthSegments; ix++) {
					const uint32_t a = grid[iy][ix + 1];
					const uint32_t b = grid[iy][ix];
					const uint32_t c = grid[iy + 1][ix];
					const uint32_t d = grid[iy + 1][ix + 1];

					// Create two triangles for each quad in the grid
					if (iy != 0 || thetaStart > 0.0f) {
						primitive.GetMutableIdxPrimives().push_back(a);
						primitive.GetMutableIdxPrimives().push_back(b);
						primitive.GetMutableIdxPrimives().push_back(d);
					}

					if (iy != heightSegments - 1 || thetaEnd < XM_PI) {
						primitive.GetMutableIdxPrimives().push_back(b);
						primitive.GetMutableIdxPrimives().push_back(c);
						primitive.GetMutableIdxPrimives().push_back(d);
					}
				}
			}

			// Set primitive type
			primitive.SetPrimitiveType(PrimitiveType::TRIANGLES);

			// Compute bounding box
			primitive.ComputeAABB();
		}

	public:
		SphereGeometry(float radius = 1.0f,
			uint32_t widthSegments = 32,
			uint32_t heightSegments = 16,
			float phiStart = 0.0f,
			float phiLength = XM_2PI,
			float thetaStart = 0.0f,
			float thetaLength = XM_PI) {

			// Store input parameters
			this->radius = radius;
			this->widthSegments = widthSegments;
			this->heightSegments = heightSegments;
			this->phiStart = phiStart;
			this->phiLength = phiLength;
			this->thetaStart = thetaStart;
			this->thetaLength = thetaLength;

			// Build the geometry
			buildGeometry();
		}

		// Get the primitive
		Primitive& GetPrimitive() { return primitive; }

		// Copy constructor
		SphereGeometry(const SphereGeometry& source) {
			this->radius = source.radius;
			this->widthSegments = source.widthSegments;
			this->heightSegments = source.heightSegments;
			this->phiStart = source.phiStart;
			this->phiLength = source.phiLength;
			this->thetaStart = source.thetaStart;
			this->thetaLength = source.thetaLength;

			this->primitive = source.primitive;
		}
	};
	
	bool GenerateSphereGeometry(Entity geometryEntity, const float radius, const uint32_t widthSegments, const uint32_t heightSegments, const float phiStart, const float phiLength, const float thetaStart, const float thetaLength)
	{
		if (widthSegments < 3 || widthSegments < 2)
		{
			vzlog_warning("SphereGeometry: widthSegments >= 3 && widthSegments >= 2");
			return false;
		}
		return true;
	}

	class LatheGeometry
	{
		/**
		 * Constructs a new lathe geometry.
		 *
		 * @param {Array<Vector2>} [points=[Vector2(0, -0.5), Vector2(0.5, 0), Vector2(0, 0.5)]] - An array of Vector2s.
		 * @param {number} [segments=12] - The number of circumference segments.
		 * @param {number} [phiStart=0] - The starting angle in radians.
		 * @param {number} [phiLength=Math.PI * 2] - The radian length of the lathe.
		 */

		Primitive primitive;

		// Parameters
		std::vector<XMFLOAT2> points;
		uint32_t segments = 12;
		float phiStart = 0.f;
		float phiLength = XM_2PI;

	public:
		LatheGeometry(const std::vector<XMFLOAT2>& inputPoints = {
			XMFLOAT2(0.f, -0.5f), XMFLOAT2(0.5f, 0.f), XMFLOAT2(0.f, 0.5f)
			},
			uint32_t inputSegments = 12,
			float inputPhiStart = 0.f,
			float inputPhiLength = XM_2PI) {

			// Store parameters
			this->points = inputPoints;
			this->segments = (uint32_t)floorf((float)inputSegments); // Ensure segments is an integer
			this->phiStart = inputPhiStart;
			this->phiLength = std::min(std::max(inputPhiLength, 0.f), XM_2PI); // Clamp phiLength to [0, 2PI]

			// Check if there are enough points
			if (points.size() < 2) {
				// Need at least two points to create a shape
				return;
			}

			// Get buffers
			std::vector<XMFLOAT3>& positions = primitive.GetMutableVtxPositions();
			std::vector<XMFLOAT3>& normals = primitive.GetMutableVtxNormals();
			std::vector<XMFLOAT2>& uvs = primitive.GetMutableVtxUVSet0();
			std::vector<uint32_t>& indices = primitive.GetMutableIdxPrimives();

			// Clear and reserve space for better performance
			positions.clear();
			normals.clear();
			uvs.clear();
			indices.clear();

			positions.reserve((segments + 1) * points.size());
			normals.reserve((segments + 1) * points.size());
			uvs.reserve((segments + 1) * points.size());
			indices.reserve(segments * (points.size() - 1) * 6);

			// Helper variables
			const float inverseSegments = 1.0f / segments;

			// Pre-compute normals for initial "meridian"
			std::vector<XMFLOAT3> initNormals(points.size());
			XMFLOAT3 normal = XMFLOAT3(0.f, 0.f, 0.f);
			XMFLOAT3 curNormal = XMFLOAT3(0.f, 0.f, 0.f);
			XMFLOAT3 prevNormal = XMFLOAT3(0.f, 0.f, 0.f);
			float dx = 0.f;
			float dy = 0.f;

			for (size_t j = 0; j < points.size(); j++) {
				if (j == 0) {
					// Special handling for 1st vertex on path
					if (points.size() > 1) {
						dx = points[j + 1].x - points[j].x;
						dy = points[j + 1].y - points[j].y;

						normal.x = dy * 1.0f;
						normal.y = -dx;
						normal.z = dy * 0.0f;

						prevNormal = normal;

						// Normalize normal
						XMVECTOR vNormal = XMLoadFloat3(&normal);
						vNormal = XMVector3Normalize(vNormal);
						XMStoreFloat3(&normal, vNormal);

						initNormals[j] = normal;
					}
				}
				else if (j == points.size() - 1) {
					// Special handling for last Vertex on path
					initNormals[j] = prevNormal;
				}
				else {
					// Default handling for all vertices in between
					dx = points[j + 1].x - points[j].x;
					dy = points[j + 1].y - points[j].y;

					normal.x = dy * 1.0f;
					normal.y = -dx;
					normal.z = dy * 0.0f;

					curNormal = normal;

					normal.x += prevNormal.x;
					normal.y += prevNormal.y;
					normal.z += prevNormal.z;

					// Normalize normal
					XMVECTOR vNormal = XMLoadFloat3(&normal);
					vNormal = XMVector3Normalize(vNormal);
					XMStoreFloat3(&normal, vNormal);

					initNormals[j] = normal;
					prevNormal = curNormal;
				}
			}

			// Generate vertices, uvs and normals
			for (uint32_t i = 0; i <= segments; i++) {
				const float phi = phiStart + i * inverseSegments * phiLength;
				const float sin_phi = sinf(phi);
				const float cos_phi = cosf(phi);

				for (size_t j = 0; j < points.size(); j++) {
					// Vertex
					XMFLOAT3 vertex;
					vertex.x = points[j].x * sin_phi;
					vertex.y = points[j].y;
					vertex.z = points[j].x * cos_phi;

					positions.push_back(vertex);

					// UV
					XMFLOAT2 uv;
					uv.x = (float)i / segments;
					uv.y = (float)j / (points.size() - 1);

					uvs.push_back(uv);

					// Normal
					XMFLOAT3 vertNormal;
					vertNormal.x = initNormals[j].x * sin_phi;
					vertNormal.y = initNormals[j].y;
					vertNormal.z = initNormals[j].x * cos_phi;

					normals.push_back(vertNormal);
				}
			}

			// Indices
			for (uint32_t i = 0; i < segments; i++) {
				for (size_t j = 0; j < (points.size() - 1); j++) {
					const uint32_t base = (uint32_t)(j + i * points.size());

					const uint32_t a = base;
					const uint32_t b = base + (uint32_t)points.size();
					const uint32_t c = base + (uint32_t)points.size() + 1;
					const uint32_t d = base + 1;

					// Faces
					indices.push_back(a);
					indices.push_back(b);
					indices.push_back(d);

					indices.push_back(c);
					indices.push_back(d);
					indices.push_back(b);
				}
			}

			// Set primitive type and compute bounding box
			primitive.SetPrimitiveType(PrimitiveType::TRIANGLES);
			primitive.ComputeAABB();
		}

		// Get the primitive
		Primitive& GetPrimitive() { return primitive; }
	};

	bool GenerateCapsuleGeometry(Entity geometryEntity, const float radius, const float length, const uint32_t capSegments, const uint32_t radialSegments)
	{
		return true;
	}

	// IPath3D - Interface for 3D path operations
	class IPath3D {
	public:
		virtual ~IPath3D() = default;

		// Get a point at position u (0 to 1) along the path
		virtual void GetPointAt(float u, XMFLOAT3& point) const = 0;

		// Get a tangent vector at position u (0 to 1) along the path
		virtual void GetTangentAt(float u, XMFLOAT3& tangent) const = 0;

		// Get the total length of the path
		virtual float GetLength() const = 0;

		// Get the number of control points on the path
		virtual size_t GetPointCount() const = 0;
	};
	// Simple path implementation using std::vector<XMFLOAT3>
	class SimplePath3D : public IPath3D {
	private:
		std::vector<XMFLOAT3> points;
		bool closed = false;
		mutable float cachedLength = -1.f;

	public:
		SimplePath3D() = default;

		// Create a path from a vector of points
		SimplePath3D(const std::vector<XMFLOAT3>& controlPoints, bool isClosed = false)
			: points(controlPoints), closed(isClosed) {
		}

		// Add a control point
		void AddPoint(const XMFLOAT3& point) {
			points.push_back(point);
			cachedLength = -1.f; // Length needs to be recalculated
		}

		// Set a control point
		void SetPoint(size_t index, const XMFLOAT3& point) {
			if (index < points.size()) {
				points[index] = point;
				cachedLength = -1.f; // Length needs to be recalculated
			}
		}

		// Get a control point
		const XMFLOAT3& GetPoint(size_t index) const {
			return points[index];
		}

		// Set whether the path is closed
		void SetClosed(bool isClosed) {
			closed = isClosed;
			cachedLength = -1.f;
		}

		// Get whether the path is closed
		bool IsClosed() const {
			return closed;
		}

		// Get the number of control points (IPath3D interface implementation)
		size_t GetPointCount() const override {
			return points.size();
		}

		// Helper function to calculate distance between two points
		float DistanceBetween(const XMFLOAT3& p1, const XMFLOAT3& p2) const {
			XMVECTOR v1 = XMLoadFloat3(&p1);
			XMVECTOR v2 = XMLoadFloat3(&p2);
			XMVECTOR vDiff = XMVectorSubtract(v1, v2);
			return XMVectorGetX(XMVector3Length(vDiff));
		}

		// Calculate path length (IPath3D interface implementation)
		float GetLength() const override {
			// Return cached length if available
			if (cachedLength >= 0.f) {
				return cachedLength;
			}

			if (points.size() < 2) {
				return 0.f;
			}

			// Sum distances between control points
			float length = 0.f;
			const size_t count = points.size();
			const size_t lastIdx = closed ? count : count - 1;

			for (size_t i = 0; i < lastIdx; i++) {
				length += DistanceBetween(points[i], points[(i + 1) % count]);
			}

			cachedLength = length;
			return length;
		}

		// Calculate point on the path (IPath3D interface implementation)
		void GetPointAt(float u, XMFLOAT3& point) const override {
			if (points.size() < 2) {
				point = points.size() == 1 ? points[0] : XMFLOAT3(0, 0, 0);
				return;
			}

			// Clamp u to range 0-1
			u = std::max(0.f, std::min(1.f, u));

			const size_t count = points.size();

			if (closed) {
				// Closed path
				const float totalDist = u * GetLength();
				float accumulatedDist = 0.f;

				for (size_t i = 0; i < count; i++) {
					const size_t nextIdx = (i + 1) % count;
					const float segmentDist = DistanceBetween(points[i], points[nextIdx]);

					if (accumulatedDist + segmentDist >= totalDist) {
						// Calculate relative position in current segment
						const float segmentU = (totalDist - accumulatedDist) / segmentDist;

						// Linear interpolation
						XMVECTOR v0 = XMLoadFloat3(&points[i]);
						XMVECTOR v1 = XMLoadFloat3(&points[nextIdx]);
						XMVECTOR vResult = XMVectorLerp(v0, v1, segmentU);

						XMStoreFloat3(&point, vResult);
						return;
					}

					accumulatedDist += segmentDist;
				}

				// If we get here, return the first point
				point = points[0];
			}
			else {
				// Open path
				if (u <= 0.f) {
					point = points[0];
					return;
				}

				if (u >= 1.f) {
					point = points[count - 1];
					return;
				}

				// Find segment corresponding to u in the entire path
				const float totalSegments = static_cast<float>(count - 1);
				const float scaledIndex = u * totalSegments;
				const size_t index = static_cast<size_t>(scaledIndex);
				const float segmentU = scaledIndex - static_cast<float>(index);

				// Linear interpolation within the segment
				XMVECTOR v0 = XMLoadFloat3(&points[index]);
				XMVECTOR v1 = XMLoadFloat3(&points[index + 1]);
				XMVECTOR vResult = XMVectorLerp(v0, v1, segmentU);

				XMStoreFloat3(&point, vResult);
			}
		}

		// Calculate tangent vector (IPath3D interface implementation)
		void GetTangentAt(float u, XMFLOAT3& tangent) const override {
			if (points.size() < 2) {
				tangent = { 1.f, 0.f, 0.f }; // Return default value
				return;
			}

			// Clamp u to range 0-1
			u = std::max(0.f, std::min(1.f, u));

			const size_t count = points.size();

			if (closed) {
				// Closed path
				const float totalDist = u * GetLength();
				float accumulatedDist = 0.f;

				for (size_t i = 0; i < count; i++) {
					const size_t nextIdx = (i + 1) % count;
					const float segmentDist = DistanceBetween(points[i], points[nextIdx]);

					if (accumulatedDist + segmentDist >= totalDist) {
						// Calculate tangent for current segment
						XMVECTOR v0 = XMLoadFloat3(&points[i]);
						XMVECTOR v1 = XMLoadFloat3(&points[nextIdx]);
						XMVECTOR vDirection = XMVectorSubtract(v1, v0);
						vDirection = XMVector3Normalize(vDirection);

						XMStoreFloat3(&tangent, vDirection);
						return;
					}

					accumulatedDist += segmentDist;
				}

				// If we get here, return tangent of first segment
				XMVECTOR v0 = XMLoadFloat3(&points[0]);
				XMVECTOR v1 = XMLoadFloat3(&points[1]);
				XMVECTOR vDirection = XMVectorSubtract(v1, v0);
				vDirection = XMVector3Normalize(vDirection);

				XMStoreFloat3(&tangent, vDirection);
			}
			else {
				// Open path
				if (u <= 0.f) {
					// Tangent of first segment
					XMVECTOR v0 = XMLoadFloat3(&points[0]);
					XMVECTOR v1 = XMLoadFloat3(&points[1]);
					XMVECTOR vDirection = XMVectorSubtract(v1, v0);
					vDirection = XMVector3Normalize(vDirection);

					XMStoreFloat3(&tangent, vDirection);
					return;
				}

				if (u >= 1.f) {
					// Tangent of last segment
					XMVECTOR v0 = XMLoadFloat3(&points[count - 2]);
					XMVECTOR v1 = XMLoadFloat3(&points[count - 1]);
					XMVECTOR vDirection = XMVectorSubtract(v1, v0);
					vDirection = XMVector3Normalize(vDirection);

					XMStoreFloat3(&tangent, vDirection);
					return;
				}

				// Find segment corresponding to u in the entire path
				const float totalSegments = static_cast<float>(count - 1);
				const float scaledIndex = u * totalSegments;
				const size_t index = static_cast<size_t>(scaledIndex);

				// Calculate segment tangent
				XMVECTOR v0 = XMLoadFloat3(&points[index]);
				XMVECTOR v1 = XMLoadFloat3(&points[index + 1]);
				XMVECTOR vDirection = XMVectorSubtract(v1, v0);
				vDirection = XMVector3Normalize(vDirection);

				XMStoreFloat3(&tangent, vDirection);
			}
		}
	};

	class TubeGeometry
	{
		/**
		 * Constructs a new tube geometry around a 3D curve path.
		 *
		 * @param {Path} [path=new QuadraticBezierCurve3((-1,-1,0), (-1,1,0), (1,1,0))] - The path to create a tube around.
		 * @param {number} [tubularSegments=64] - The number of segments around the tube.
		 * @param {number} [radius=1] - The radius of the tube.
		 * @param {number} [radialSegments=8] - The number of segments around the circumference of the tube.
		 * @param {boolean} [closed=false] - Whether the tube is closed.
		 * @param {boolean} [addCaps=false] - Whether to add caps to the tube ends.
		 */

		Primitive primitive;

		// Parameters
		IPath3D* path = nullptr;
		uint32_t tubularSegments = 64;
		float radius = 1.0f;
		uint32_t radialSegments = 8;
		bool closed = false;
		bool flatCaps = false;
		bool addCaps = false;
		bool needToDeletePath = false;

		// Exposed internals
		std::vector<XMFLOAT3> tangents;
		std::vector<XMFLOAT3> normals;
		std::vector<XMFLOAT3> binormals;

		// Structure to hold the Frenet frames
		struct FrenetFrames {
			std::vector<XMFLOAT3> tangents;
			std::vector<XMFLOAT3> normals;
			std::vector<XMFLOAT3> binormals;
		};

	private:
		// Generate buffer data
		void generateBufferData() {
			std::vector<XMFLOAT3>& positions = primitive.GetMutableVtxPositions();
			std::vector<XMFLOAT3>& vertexNormals = primitive.GetMutableVtxNormals();
			std::vector<XMFLOAT2>& uvs = primitive.GetMutableVtxUVSet0();
			std::vector<uint32_t>& indices = primitive.GetMutableIdxPrimives();

			// Calculate vertex count including potential caps
			size_t tubeVertexCount = (tubularSegments + 1) * (radialSegments + 1);
			size_t capsVertexCount = 0;

			// If we're adding caps and the tube is not closed, we need additional vertices
			if (addCaps && !closed) {
				// Each cap requires radialSegments + 1 vertices for the rim 
				// plus 1 vertex for the center point
				capsVertexCount = 2 * ((radialSegments + 1) + 1);
			}

			// Reserve memory for better performance
			positions.reserve(tubeVertexCount + capsVertexCount);
			vertexNormals.reserve(tubeVertexCount + capsVertexCount);
			uvs.reserve(tubeVertexCount + capsVertexCount);

			// Calculate indices count including potential caps
			size_t tubeIndicesCount = tubularSegments * radialSegments * 6;
			size_t capsIndicesCount = 0;

			if (addCaps && !closed) {
				// Each cap consists of radialSegments triangles
				capsIndicesCount = 2 * (radialSegments * 3);
			}

			indices.reserve(tubeIndicesCount + capsIndicesCount);

			// Generate tube segments
			for (uint32_t i = 0; i < tubularSegments; i++) {
				generateSegment(i);
			}

			// If the geometry is not closed, generate the last row of vertices and normals
			// at the regular position on the given path
			//
			// If the geometry is closed, duplicate the first row of vertices and normals (uvs will differ)
			generateSegment((closed == false) ? tubularSegments : 0);

			// Generate UVs for the tube
			generateUVs();

			// Generate indices for the tube
			generateIndices();

			// Generate caps if requested and tube is not closed
			if (addCaps && !closed) {
				if (flatCaps)
					generateFlatCaps();
				else
					generateHemisphereCaps();
			}
		}

		void generateSegment(uint32_t i) {
			std::vector<XMFLOAT3>& positions = primitive.GetMutableVtxPositions();
			std::vector<XMFLOAT3>& vertexNormals = primitive.GetMutableVtxNormals();

			// Get point on path
			XMFLOAT3 P;
			path->GetPointAt((float)i / (float)tubularSegments, P);

			// Retrieve corresponding normal and binormal
			const XMFLOAT3& N = normals[i];
			const XMFLOAT3& B = binormals[i];

			// Generate normals and vertices for the current segment
			for (uint32_t j = 0; j <= radialSegments; j++) {
				const float v = (float)j / (float)radialSegments * math::PI * 2.0f;

				// Using sin and -cos for Right-Handed System (RHS)
				const float sin_v = sin(v);
				const float cos_v = -cos(v);

				// Normal
				XMFLOAT3 normal;
				normal.x = (cos_v * N.x + sin_v * B.x);
				normal.y = (cos_v * N.y + sin_v * B.y);
				normal.z = (cos_v * N.z + sin_v * B.z);

				// Normalize normal
				XMVECTOR vNormal = XMLoadFloat3(&normal);
				vNormal = XMVector3Normalize(vNormal);
				XMStoreFloat3(&normal, vNormal);

				vertexNormals.push_back(normal);

				// Vertex
				XMFLOAT3 vertex;
				vertex.x = P.x + radius * normal.x;
				vertex.y = P.y + radius * normal.y;
				vertex.z = P.z + radius * normal.z;

				positions.push_back(vertex);
			}
		}

		void generateIndices() {
			std::vector<uint32_t>& indices = primitive.GetMutableIdxPrimives();

			for (uint32_t j = 1; j <= tubularSegments; j++) {
				for (uint32_t i = 1; i <= radialSegments; i++) {
					const uint32_t a = (radialSegments + 1) * (j - 1) + (i - 1);
					const uint32_t b = (radialSegments + 1) * j + (i - 1);
					const uint32_t c = (radialSegments + 1) * j + i;
					const uint32_t d = (radialSegments + 1) * (j - 1) + i;

					// Faces with winding order for RHS (Counter-Clockwise)
					indices.push_back(a);
					indices.push_back(b);
					indices.push_back(d);

					indices.push_back(d);
					indices.push_back(b);
					indices.push_back(c);
				}
			}
		}

		void generateUVs() {
			std::vector<XMFLOAT2>& uvs = primitive.GetMutableVtxUVSet0();

			for (uint32_t i = 0; i <= tubularSegments; i++) {
				for (uint32_t j = 0; j <= radialSegments; j++) {
					XMFLOAT2 uv;
					uv.x = (float)i / (float)tubularSegments;
					uv.y = (float)j / (float)radialSegments;

					uvs.push_back(uv);
				}
			}
		}

		// New method to generate caps for the tube ends
		void generateFlatCaps() {
			std::vector<XMFLOAT3>& positions = primitive.GetMutableVtxPositions();
			std::vector<XMFLOAT3>& vertexNormals = primitive.GetMutableVtxNormals();
			std::vector<XMFLOAT2>& uvs = primitive.GetMutableVtxUVSet0();
			std::vector<uint32_t>& indices = primitive.GetMutableIdxPrimives();

			// Get the starting vertex index for caps
			uint32_t startVertexIndex = static_cast<uint32_t>(positions.size());

			// Generate caps for both ends
			for (uint32_t capIndex = 0; capIndex < 2; capIndex++) {
				// For capIndex = 0: start cap, capIndex = 1: end cap

				// Get path position and tangent at the cap location
				XMFLOAT3 centerPoint;
				XMFLOAT3 tangent;

				if (capIndex == 0) {
					// Start cap (path position at u=0)
					path->GetPointAt(0.0f, centerPoint);
					path->GetTangentAt(0.0f, tangent);
				}
				else {
					// End cap (path position at u=1)
					path->GetPointAt(1.0f, centerPoint);
					path->GetTangentAt(1.0f, tangent);

					// Flip tangent for end cap to point outward
					tangent.x = -tangent.x;
					tangent.y = -tangent.y;
					tangent.z = -tangent.z;
				}

				// Normalize tangent to use as cap normal
				XMVECTOR vTangent = XMLoadFloat3(&tangent);
				vTangent = XMVector3Normalize(vTangent);
				XMStoreFloat3(&tangent, vTangent);

				// Get current normal and binormal
				const XMFLOAT3& N = normals[capIndex == 0 ? 0 : tubularSegments];
				const XMFLOAT3& B = binormals[capIndex == 0 ? 0 : tubularSegments];

				// Add center vertex of the cap
				positions.push_back(centerPoint);
				vertexNormals.push_back(tangent);
				uvs.push_back(XMFLOAT2(0.5f, 0.5f)); // Center of cap UV

				uint32_t centerVertexIndex = startVertexIndex;
				startVertexIndex++;

				// Add vertices for the cap rim
				for (uint32_t j = 0; j <= radialSegments; j++) {
					const float v = (float)j / (float)radialSegments * math::PI * 2.0f;

					// Using sin and -cos for Right-Handed System (RHS)
					const float sin_v = sin(v);
					const float cos_v = -cos(v);

					// Calculate normal for the rim vertices (same as tube vertices)
					XMFLOAT3 normal;
					normal.x = (cos_v * N.x + sin_v * B.x);
					normal.y = (cos_v * N.y + sin_v * B.y);
					normal.z = (cos_v * N.z + sin_v * B.z);

					// Normalize normal
					XMVECTOR vNormal = XMLoadFloat3(&normal);
					vNormal = XMVector3Normalize(vNormal);
					XMStoreFloat3(&normal, vNormal);

					// Calculate rim vertex position
					XMFLOAT3 vertex;
					vertex.x = centerPoint.x + radius * normal.x;
					vertex.y = centerPoint.y + radius * normal.y;
					vertex.z = centerPoint.z + radius * normal.z;

					// Add rim vertex with the cap normal
					positions.push_back(vertex);
					vertexNormals.push_back(tangent); // All vertices use the same normal (along tangent)

					// UV coordinates for rim vertices
					float u_cap = 0.5f + 0.5f * cos_v;
					float v_cap = 0.5f + 0.5f * sin_v;
					uvs.push_back(XMFLOAT2(u_cap, v_cap));

					// Create triangles connecting center to rim
					if (j < radialSegments) {
						if (capIndex == 0) {
							// Start cap - counter-clockwise winding for RHS
							indices.push_back(centerVertexIndex);
							indices.push_back(startVertexIndex + j);
							indices.push_back(startVertexIndex + j + 1);
						}
						else {
							// End cap - clockwise winding for RHS (to face outward)
							indices.push_back(centerVertexIndex);
							indices.push_back(startVertexIndex + j + 1);
							indices.push_back(startVertexIndex + j);
						}
					}
				}

				// Update startVertexIndex for the next cap
				startVertexIndex += radialSegments + 1;
			}
		}

		// New method to generate hemispherical caps for the tube ends
		void generateHemisphereCaps() {
			// Reference to mesh buffers
			std::vector<XMFLOAT3>& positions = primitive.GetMutableVtxPositions();
			std::vector<XMFLOAT3>& vertexNormals = primitive.GetMutableVtxNormals();
			std::vector<XMFLOAT2>& uvs = primitive.GetMutableVtxUVSet0();
			std::vector<uint32_t>& indices = primitive.GetMutableIdxPrimives();

			// Iterate for start cap and end cap
			for (uint32_t capIndex = 0; capIndex < 2; capIndex++) {
				// Calculate cap position and tangent
				XMFLOAT3 centerPoint;
				XMFLOAT3 tangent;
				if (capIndex == 0) {
					// Start cap (u = 0)
					path->GetPointAt(0.0f, centerPoint);
					path->GetTangentAt(0.0f, tangent);
				}
				else {
					// End cap (u = 1): Invert tangent for outward orientation
					path->GetPointAt(1.0f, centerPoint);
					path->GetTangentAt(1.0f, tangent);
					tangent.x = -tangent.x;
					tangent.y = -tangent.y;
					tangent.z = -tangent.z;
				}
				// Normalize tangent
				XMVECTOR vTangent = XMLoadFloat3(&tangent);
				vTangent = XMVector3Normalize(vTangent);
				XMStoreFloat3(&tangent, vTangent);

				// Tube frame: N(normal), B(binormal)
				const XMFLOAT3& N = normals[capIndex == 0 ? 0 : tubularSegments];
				const XMFLOAT3& B = binormals[capIndex == 0 ? 0 : tubularSegments];

				// Hemisphere subdivision parameters (vertical: stacks, horizontal: slices)
				uint32_t stacks = radialSegments;  // 0: apex, stacks: rim
				uint32_t slices = radialSegments;  // 0 ~ slices: 360 degree division

				// Current cap vertex start index
				uint32_t startVertexIndex = static_cast<uint32_t>(positions.size());

				// === Vertex Generation ===
				// r == 0: apex - set in opposite direction to existing tangent
				// r == 1 ~ stacks: vertices for each ring (invert the sign of tangent term when calculating sphere coordinates)
				for (uint32_t r = 0; r <= stacks; r++) {
					// theta: 0 (apex) ~ π/2 (rim)
					float theta = (float)r / (float)stacks * XM_PIDIV2;
					float sinTheta = sin(theta);
					float cosTheta = cos(theta);

					if (r == 0) {
						// apex: reverse sphere direction -> centerPoint - radius * tangent
						XMFLOAT3 offset = { -tangent.x * radius, -tangent.y * radius, -tangent.z * radius };
						XMFLOAT3 pos = { centerPoint.x + offset.x, centerPoint.y + offset.y, centerPoint.z + offset.z };
						positions.push_back(pos);

						// Normal: normalize offset
						XMVECTOR vOff = XMLoadFloat3(&offset);
						vOff = XMVector3Normalize(vOff);
						XMFLOAT3 norm;
						XMStoreFloat3(&norm, vOff);
						vertexNormals.push_back(norm);

						// UV: UV for apex (modify as needed)
						uvs.push_back(XMFLOAT2(0.5f, 0.0f));
					}
					else {
						// r > 0: Generate vertices for each ring (phi: 0~2π)
						for (uint32_t slice = 0; slice <= slices; slice++) {
							float phi = (float)slice / (float)slices * 2.0f * math::PI;
							float cosPhi = cos(phi);
							float sinPhi = sin(phi);

							// Original formula: 
							// offset = radius * ( sinTheta*cosPhi * N + sinTheta*sinPhi * B + cosTheta * tangent )
							// Here, we flip the sign of the tangent term to invert the sphere direction
							XMFLOAT3 offset;
							offset.x = radius * (sinTheta * cosPhi * N.x + sinTheta * sinPhi * B.x - cosTheta * tangent.x);
							offset.y = radius * (sinTheta * cosPhi * N.y + sinTheta * sinPhi * B.y - cosTheta * tangent.y);
							offset.z = radius * (sinTheta * cosPhi * N.z + sinTheta * sinPhi * B.z - cosTheta * tangent.z);

							XMFLOAT3 pos = { centerPoint.x + offset.x, centerPoint.y + offset.y, centerPoint.z + offset.z };
							positions.push_back(pos);

							// Calculate normal: normalize offset
							XMVECTOR vOff = XMLoadFloat3(&offset);
							vOff = XMVector3Normalize(vOff);
							XMFLOAT3 norm;
							XMStoreFloat3(&norm, vOff);
							vertexNormals.push_back(norm);

							// Simple spherical UV mapping
							float u = phi / (2.0f * math::PI);
							float v = theta / XM_PIDIV2;
							uvs.push_back(XMFLOAT2(u, v));
						}
					}
				}

				// === Index Generation ===
				// First: Create triangle fan between apex and first ring
				uint32_t apexIndex = startVertexIndex;       // r = 0
				uint32_t firstRingStart = startVertexIndex + 1;  // r = 1 start index

				// Adjust winding order to match RHS
				for (uint32_t slice = 0; slice < slices; slice++) {
					if (capIndex == 0) {
						// Start cap: reverse original winding order
						indices.push_back(apexIndex);
						indices.push_back(firstRingStart + slice + 1);
						indices.push_back(firstRingStart + slice);
					}
					else {
						// End cap: opposite winding order
						indices.push_back(apexIndex);
						indices.push_back(firstRingStart + slice);
						indices.push_back(firstRingStart + slice + 1);
					}
				}

				// Second: Connect adjacent rings with quads (two triangles)
				for (uint32_t r = 1; r < stacks; r++) {
					uint32_t ringStart = startVertexIndex + 1 + (r - 1) * (slices + 1);
					uint32_t nextRingStart = ringStart + (slices + 1);
					for (uint32_t slice = 0; slice < slices; slice++) {
						if (capIndex == 0) {
							// Start cap: reverse winding order
							indices.push_back(ringStart + slice);
							indices.push_back(ringStart + slice + 1);
							indices.push_back(nextRingStart + slice);

							indices.push_back(ringStart + slice + 1);
							indices.push_back(nextRingStart + slice + 1);
							indices.push_back(nextRingStart + slice);
						}
						else {
							// End cap: opposite winding order
							indices.push_back(ringStart + slice);
							indices.push_back(nextRingStart + slice);
							indices.push_back(ringStart + slice + 1);

							indices.push_back(ringStart + slice + 1);
							indices.push_back(nextRingStart + slice);
							indices.push_back(nextRingStart + slice + 1);
						}
					}
				}
			}
		}

		// Compute the Frenet frames for the tube
		FrenetFrames computeFrenetFrames(uint32_t segments, bool closed) {
			FrenetFrames frames;
			frames.tangents.resize(segments + 1); // +1 to handle the case where closed is false
			frames.normals.resize(segments + 1);
			frames.binormals.resize(segments + 1);

			// Calculate tangent vectors for each segment
			for (uint32_t i = 0; i <= segments; i++) {
				const float u = (closed && i == segments) ? 0.0f : (float)i / (float)segments;

				// Tangent calculation
				XMFLOAT3 tangent;
				path->GetTangentAt(u, tangent);

				// Normalize the tangent
				XMVECTOR vTangent = XMLoadFloat3(&tangent);
				vTangent = XMVector3Normalize(vTangent);
				XMStoreFloat3(&tangent, vTangent);

				frames.tangents[i] = tangent;
			}

			// Find a suitable vector for generating the initial normal
			XMFLOAT3 vec = XMFLOAT3(0.0f, 1.0f, 0.0f);
			float smallest = 1.0f;

			// Find the initial normal - look for a vector that's not parallel to any tangent
			for (uint32_t i = 0; i <= segments; i++) {
				XMVECTOR vT = XMLoadFloat3(&frames.tangents[i]);
				XMVECTOR vVec = XMLoadFloat3(&vec);
				float mag = fabsf(XMVectorGetX(XMVector3Dot(vT, vVec)));

				if (mag < smallest) {
					smallest = mag;
					XMFLOAT3 temp = XMFLOAT3(0.0f, 0.0f, 1.0f);
					vec = temp;

					// Check if we're still too close
					XMVECTOR vVec2 = XMLoadFloat3(&vec);
					mag = fabsf(XMVectorGetX(XMVector3Dot(vT, vVec2)));

					if (mag < smallest) {
						smallest = mag;
						temp = XMFLOAT3(1.0f, 0.0f, 0.0f);
						vec = temp;
					}
				}
			}

			// Calculate initial normal and binormal
			XMVECTOR vInitTangent = XMLoadFloat3(&frames.tangents[0]);
			XMVECTOR vTemp = XMLoadFloat3(&vec);

			// For RHS: normal = cross(temp, tangent)
			XMVECTOR vInitNormal = XMVector3Cross(vTemp, vInitTangent);
			vInitNormal = XMVector3Normalize(vInitNormal);

			// For RHS: binormal = cross(tangent, normal)
			XMVECTOR vInitBinormal = XMVector3Cross(vInitTangent, vInitNormal);
			vInitBinormal = XMVector3Normalize(vInitBinormal);

			XMFLOAT3 initNormal, initBinormal;
			XMStoreFloat3(&initNormal, vInitNormal);
			XMStoreFloat3(&initBinormal, vInitBinormal);

			// Initialize the first frame
			frames.normals[0] = initNormal;
			frames.binormals[0] = initBinormal;

			// Calculate the rest of the frames using Parallel Transport method
			for (uint32_t i = 1; i <= segments; i++) {
				XMVECTOR vPrevTangent = XMLoadFloat3(&frames.tangents[i - 1]);
				XMVECTOR vCurrTangent = XMLoadFloat3(&frames.tangents[i]);

				// Dot product between current and previous tangents
				float dot = XMVectorGetX(XMVector3Dot(vPrevTangent, vCurrTangent));

				// If tangents are nearly parallel, just copy the previous frame
				if (fabsf(dot) > 0.99999f) {
					frames.normals[i] = frames.normals[i - 1];
					frames.binormals[i] = frames.binormals[i - 1];
					continue;
				}

				// Get rotation axis (cross product) and angle (acos of dot product)
				XMVECTOR vRotAxis = XMVector3Cross(vPrevTangent, vCurrTangent);
				vRotAxis = XMVector3Normalize(vRotAxis);
				float angle = acosf(std::min(1.0f, std::max(-1.0f, dot)));

				// Create rotation matrix around the rotation axis
				XMMATRIX rotMatrix = XMMatrixRotationAxis(vRotAxis, angle);

				// Rotate previous normal and binormal
				XMVECTOR vPrevNormal = XMLoadFloat3(&frames.normals[i - 1]);
				XMVECTOR vPrevBinormal = XMLoadFloat3(&frames.binormals[i - 1]);

				XMVECTOR vNewNormal = XMVector3Transform(vPrevNormal, rotMatrix);
				XMVECTOR vNewBinormal = XMVector3Transform(vPrevBinormal, rotMatrix);

				// Ensure orthogonality for RHS
				vNewNormal = XMVector3Normalize(vNewNormal);

				// Ensure binormal is perpendicular to tangent and normal (for RHS)
				vNewBinormal = XMVector3Cross(vCurrTangent, vNewNormal);
				vNewBinormal = XMVector3Normalize(vNewBinormal);

				// Double-check normal is perpendicular to tangent and binormal
				vNewNormal = XMVector3Cross(vNewBinormal, vCurrTangent);
				vNewNormal = XMVector3Normalize(vNewNormal);

				XMStoreFloat3(&frames.normals[i], vNewNormal);
				XMStoreFloat3(&frames.binormals[i], vNewBinormal);
			}

			// Handle closed path by smoothing the connection
			if (closed) {
				// For a closed path, we need to adjust the last frame to match the first
				// Calculate rotation to align last frame with first frame
				XMVECTOR vFirstTangent = XMLoadFloat3(&frames.tangents[0]);
				XMVECTOR vLastTangent = XMLoadFloat3(&frames.tangents[segments]);

				// Blend normals and binormals for a smooth transition
				XMVECTOR vFirstNormal = XMLoadFloat3(&frames.normals[0]);
				XMVECTOR vLastNormal = XMLoadFloat3(&frames.normals[segments]);

				XMVECTOR vFirstBinormal = XMLoadFloat3(&frames.binormals[0]);
				XMVECTOR vLastBinormal = XMLoadFloat3(&frames.binormals[segments]);

				// Blend factor (can adjust for smoother transition)
				const float blend = 0.5f;

				XMVECTOR vBlendedNormal = XMVectorLerp(vFirstNormal, vLastNormal, blend);
				vBlendedNormal = XMVector3Normalize(vBlendedNormal);

				XMVECTOR vBlendedBinormal = XMVectorLerp(vFirstBinormal, vLastBinormal, blend);
				vBlendedBinormal = XMVector3Normalize(vBlendedBinormal);

				// Update the first and last frames
				XMStoreFloat3(&frames.normals[0], vBlendedNormal);
				XMStoreFloat3(&frames.normals[segments], vBlendedNormal);

				XMStoreFloat3(&frames.binormals[0], vBlendedBinormal);
				XMStoreFloat3(&frames.binormals[segments], vBlendedBinormal);
			}

			return frames;
		}

	public:
		TubeGeometry(IPath3D* path = nullptr, const uint32_t tubularSegments = 64,
			const float radius = 1.0f, const uint32_t radialSegments = 8,
			const bool closed = false, const bool addCaps = false, const bool flatCaps = false) {

			// If no path provided, create a default path
			if (path == nullptr) {
				// Create default path (using SimplePath3D instead of QuadraticBezierCurve)
				std::vector<XMFLOAT3> pathPoints = {
					{-1.0f, -1.0f, 0.0f},
					{-1.0f, 1.0f, 0.0f},
					{1.0f, 1.0f, 0.0f}
				};
				path = new SimplePath3D(pathPoints);
				needToDeletePath = true;
			}

			/**
			 * Holds the constructor parameters that have been
			 * used to generate the geometry. Any modification
			 * after instantiation does not change the geometry.
			 */
			this->path = path;
			this->tubularSegments = tubularSegments;
			this->radius = radius;
			this->radialSegments = radialSegments;
			this->closed = closed;
			this->addCaps = addCaps;
			this->flatCaps = flatCaps;

			// Note: Caps are only added if the tube is not closed
			if (closed) {
				this->addCaps = false;  // No need for caps if the tube is closed
			}

			// Compute Frenet frames for the path
			FrenetFrames frames = computeFrenetFrames(tubularSegments, closed);

			// Store the frames
			this->tangents = frames.tangents;
			this->normals = frames.normals;
			this->binormals = frames.binormals;

			// Generate the geometry
			generateBufferData();

			// Set primitive type
			primitive.SetPrimitiveType(PrimitiveType::TRIANGLES);
			primitive.ComputeAABB();
		}

		// Copy constructor
		TubeGeometry(const TubeGeometry& source) {
			this->path = source.path;
			this->tubularSegments = source.tubularSegments;
			this->radius = source.radius;
			this->radialSegments = source.radialSegments;
			this->closed = source.closed;
			this->addCaps = source.addCaps;
			this->needToDeletePath = false; // Don't delete the path in copy

			this->tangents = source.tangents;
			this->normals = source.normals;
			this->binormals = source.binormals;

			this->primitive = source.primitive;
		}

		// Cleanup
		~TubeGeometry() {
			// Delete path if we created it in the constructor
			if (needToDeletePath && path != nullptr) {
				delete path;
				path = nullptr;
			}
		}

		Primitive& GetPrimitive() { return primitive; }
	};

	bool GenerateTubeGeometry(Entity geometryEntity, const std::vector<XMFLOAT3>& path, uint32_t tubularSegments, const float radius, uint32_t radialSegments, const bool closed)
	{
		if (path.size() < 2)
		{
			return false;
		}

		GeometryComponent* geometry = compfactory::GetGeometryComponent(geometryEntity);
		if (geometry == nullptr)
		{
			vzlog_error("Invalid geometryEntity");
			return false;
		}

		SimplePath3D ipath(path, closed);

		TubeGeometry tube(&ipath, tubularSegments, radius, radialSegments, closed, true, false);

		{
			std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());
			geometry->ClearGeometry();
			geometry->AddMovePrimitiveFrom(std::move(tube.GetPrimitive()));
			geometry->UpdateRenderData();
		}

		return true;
	}
}