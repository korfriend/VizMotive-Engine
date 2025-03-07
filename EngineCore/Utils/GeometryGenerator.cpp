#include "GeometryGenerator.h"
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

				// B = T ¡¿ N (cross product)
				XMVECTOR vB = XMVector3Cross(vT, vN);

				// N = B ¡¿ T (cross product)
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

		TorusKnotGeometry plolyhedron(radius, tube, tubularSegments, radialSegments, p, q);

		{
			std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());
			geometry->ClearGeometry();
			geometry->AddMovePrimitiveFrom(std::move(plolyhedron.GetPrimitive()));
			geometry->UpdateRenderData();
		}

		return true;
	}

	class BoxGeometry
	{
		/**
		 * Constructs a new box geometry.
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
			// 1. You need three indices to draw a single face
			// 2. A single segment consists of two faces
			// 3. So we need to generate six (2*3) indices per segment
			for (uint32_t iy = 0; iy < gridY; iy++) {
				for (uint32_t ix = 0; ix < gridX; ix++) {
					const uint32_t a = numberOfVertices + ix + gridX1 * iy;
					const uint32_t b = numberOfVertices + ix + gridX1 * (iy + 1);
					const uint32_t c = numberOfVertices + (ix + 1) + gridX1 * (iy + 1);
					const uint32_t d = numberOfVertices + (ix + 1) + gridX1 * iy;

					// Faces with winding order for RHS
					indices.push_back(a);
					indices.push_back(d);
					indices.push_back(b);

					indices.push_back(b);
					indices.push_back(d);
					indices.push_back(c);

					// Increase counter
					groupCount += 6;
				}
			}

			// We keep track of material groups through groupStart and groupCount
			// but we don't store the actual material groups since Primitive doesn't support it
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
			buildPlane('z', 'y', 'x', -1.f, -1.f, depth, height, width, depthSegments, heightSegments, 0, vertices, normals, uvs, indices, numberOfVertices, groupStart); // px
			buildPlane('z', 'y', 'x', 1.f, -1.f, depth, height, -width, depthSegments, heightSegments, 1, vertices, normals, uvs, indices, numberOfVertices, groupStart); // nx
			buildPlane('x', 'z', 'y', 1.f, 1.f, width, depth, height, widthSegments, depthSegments, 2, vertices, normals, uvs, indices, numberOfVertices, groupStart); // py
			buildPlane('x', 'z', 'y', 1.f, -1.f, width, depth, -height, widthSegments, depthSegments, 3, vertices, normals, uvs, indices, numberOfVertices, groupStart); // ny
			buildPlane('x', 'y', 'z', 1.f, -1.f, width, height, depth, widthSegments, heightSegments, 4, vertices, normals, uvs, indices, numberOfVertices, groupStart); // pz
			buildPlane('x', 'y', 'z', -1.f, -1.f, width, height, -depth, widthSegments, heightSegments, 5, vertices, normals, uvs, indices, numberOfVertices, groupStart); // nz

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
		 * Constructs a new tube geometry.
		 *
		 * @param {Path} [path=new QuadraticBezierCurve3(new Vector3(-1,-1,0), new Vector3(-1,1,0), new Vector3(1,1,0))] - A 3D path.
		 * @param {number} [tubularSegments=64] - The number of segments that make up the tube.
		 * @param {number} [radius=1] - The radius of the tube.
		 * @param {number} [radialSegments=8] - The number of segments that make up the cross-section.
		 * @param {boolean} [closed=false] - Whether the tube is closed.
		 */

		Primitive primtive;

		// Path object reference (abstract base class in C++)
		IPath3D* path = nullptr;
		uint32_t tubularSegments = 64;
		float radius = 1.f;
		uint32_t radialSegments = 8;
		bool closed = false;
		bool needToDeletePath = false;

		// exposed internals
		std::vector<XMFLOAT3> tangents;
		std::vector<XMFLOAT3> normals;
		std::vector<XMFLOAT3> binormals;

		// Structure to hold the Frenet frames
		struct FrenetFrames {
			std::vector<XMFLOAT3> tangents;
			std::vector<XMFLOAT3> normals;
			std::vector<XMFLOAT3> binormals;
		};

		// helper functions
		void generateBufferData() {
			std::vector<XMFLOAT3>& positions = primtive.GetMutableVtxPositions();
			std::vector<XMFLOAT3>& vertexNormals = primtive.GetMutableVtxNormals();
			std::vector<XMFLOAT2>& uvs = primtive.GetMutableVtxUVSet0();
			std::vector<uint32_t>& indices = primtive.GetMutableIdxPrimives();

			// Reserve memory for better performance
			const size_t vertexCount = (tubularSegments + 1) * (radialSegments + 1);
			positions.reserve(vertexCount);
			vertexNormals.reserve(vertexCount);
			uvs.reserve(vertexCount);
			indices.reserve(tubularSegments * radialSegments * 6);

			// Generate segments
			for (uint32_t i = 0; i < tubularSegments; i++) {
				generateSegment(i);
			}

			// If the geometry is not closed, generate the last row of vertices and normals
			// at the regular position on the given path
			//
			// If the geometry is closed, duplicate the first row of vertices and normals (uvs will differ)
			generateSegment((closed == false) ? tubularSegments : 0);

			// Generate UVs
			generateUVs();

			// Generate indices
			generateIndices();
		}

		void generateSegment(uint32_t i) {
			std::vector<XMFLOAT3>& positions = primtive.GetMutableVtxPositions();
			std::vector<XMFLOAT3>& vertexNormals = primtive.GetMutableVtxNormals();

			// Get point on path
			XMFLOAT3 P;
			path->GetPointAt((float)i / (float)tubularSegments, P);

			// Retrieve corresponding normal and binormal
			const XMFLOAT3& N = normals[i];
			const XMFLOAT3& B = binormals[i];

			// Generate normals and vertices for the current segment
			for (uint32_t j = 0; j <= radialSegments; j++) {
				const float v = (float)j / (float)radialSegments * math::PI * 2.f;
				const float sin_v = sin(v);
				const float cos_v = -cos(v); // Negative for RHS

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
			std::vector<uint32_t>& indices = primtive.GetMutableIdxPrimives();

			for (uint32_t j = 1; j <= tubularSegments; j++) {
				for (uint32_t i = 1; i <= radialSegments; i++) {
					const uint32_t a = (radialSegments + 1) * (j - 1) + (i - 1);
					const uint32_t b = (radialSegments + 1) * j + (i - 1);
					const uint32_t c = (radialSegments + 1) * j + i;
					const uint32_t d = (radialSegments + 1) * (j - 1) + i;

					// faces with winding order for RHS
					indices.push_back(a);
					indices.push_back(d);
					indices.push_back(b);

					indices.push_back(b);
					indices.push_back(d);
					indices.push_back(c);
				}
			}
		}

		void generateUVs() {
			std::vector<XMFLOAT2>& uvs = primtive.GetMutableVtxUVSet0();

			for (uint32_t i = 0; i <= tubularSegments; i++) {
				for (uint32_t j = 0; j <= radialSegments; j++) {
					XMFLOAT2 uv;
					uv.x = (float)i / (float)tubularSegments;
					uv.y = (float)j / (float)radialSegments;

					uvs.push_back(uv);
				}
			}
		}

		// Compute the Frenet frames for the tube
		FrenetFrames computeFrenetFrames(uint32_t segments, bool closed) {
			FrenetFrames frames;
			frames.tangents.resize(segments);
			frames.normals.resize(segments);
			frames.binormals.resize(segments);

			// Calculate tangent vectors for each segment
			for (uint32_t i = 0; i < segments; i++) {
				const float u = (float)i / (float)(segments - 1);

				// Tangent calculation
				XMFLOAT3 tangent;
				path->GetTangentAt(u, tangent);

				// Normalize the tangent
				XMVECTOR vTangent = XMLoadFloat3(&tangent);
				vTangent = XMVector3Normalize(vTangent);
				XMStoreFloat3(&tangent, vTangent);

				frames.tangents[i] = tangent;
			}

			// Use the first tangent as previous for closed or open path
			XMFLOAT3 vec = XMFLOAT3(0, 1, 0);
			float smallest = 1.f;

			// Find the initial normal
			for (uint32_t i = 0; i < segments; i++) {
				XMVECTOR vT = XMLoadFloat3(&frames.tangents[i]);
				XMVECTOR vVec = XMLoadFloat3(&vec);
				float mag = fabsf(XMVectorGetX(XMVector3Dot(vT, vVec)));

				if (mag < smallest) {
					smallest = mag;
					XMFLOAT3 temp;
					temp.x = 0;
					temp.y = 0;
					temp.z = 1;
					vec = temp;

					// Check if we're still too close
					XMVECTOR vVec2 = XMLoadFloat3(&vec);
					mag = fabsf(XMVectorGetX(XMVector3Dot(vT, vVec2)));

					if (mag < smallest) {
						smallest = mag;
						temp.x = 1;
						temp.y = 0;
						temp.z = 0;
						vec = temp;
					}
				}
			}

			// Calculate initial normal and binormal
			XMVECTOR vInitTangent = XMLoadFloat3(&frames.tangents[0]);
			XMVECTOR vTemp = XMLoadFloat3(&vec);

			// Normal is cross product of tangent and temp vector (for RHS)
			XMVECTOR vInitNormal = XMVector3Cross(vTemp, vInitTangent);
			vInitNormal = XMVector3Normalize(vInitNormal);

			// Binormal is cross product of normal and tangent (for RHS)
			XMVECTOR vInitBinormal = XMVector3Cross(vInitTangent, vInitNormal);
			vInitBinormal = XMVector3Normalize(vInitBinormal);

			XMFLOAT3 initNormal, initBinormal;
			XMStoreFloat3(&initNormal, vInitNormal);
			XMStoreFloat3(&initBinormal, vInitBinormal);

			// Initialize the first frame
			frames.normals[0] = initNormal;
			frames.binormals[0] = initBinormal;

			// Calculate the rest of the frames
			for (uint32_t i = 1; i < segments; i++) {
				// Copy previous frame
				frames.normals[i] = frames.normals[i - 1];
				frames.binormals[i] = frames.binormals[i - 1];

				// Adjust to next tangent using Rotation Minimizing Frames
				XMVECTOR vPrevTangent = XMLoadFloat3(&frames.tangents[i - 1]);
				XMVECTOR vCurrTangent = XMLoadFloat3(&frames.tangents[i]);

				// Dot products for vector projections
				float dot = XMVectorGetX(XMVector3Dot(vPrevTangent, vCurrTangent));

				// Handle almost parallel tangents
				if (fabsf(dot) > 0.99999f) {
					// Just keep the previous frame
					continue;
				}

				// Rotate normal and binormal to align with current tangent
				XMVECTOR vPrevNormal = XMLoadFloat3(&frames.normals[i - 1]);
				XMVECTOR vPrevBinormal = XMLoadFloat3(&frames.binormals[i - 1]);

				// For RHS: use appropriate cross products
				XMVECTOR vNewNormal = XMVector3Cross(vCurrTangent, vPrevBinormal);
				vNewNormal = XMVector3Normalize(vNewNormal);

				XMVECTOR vNewBinormal = XMVector3Cross(vNewNormal, vCurrTangent);
				vNewBinormal = XMVector3Normalize(vNewBinormal);

				XMStoreFloat3(&frames.normals[i], vNewNormal);
				XMStoreFloat3(&frames.binormals[i], vNewBinormal);
			}

			// If closed, calculate frames for the connecting segment
			if (closed) {
				// TODO: Implement additional smoothing for closed paths
			}

			return frames;
		}

	public:
		TubeGeometry(IPath3D* path = nullptr, const uint32_t tubularSegments = 64,
			const float radius = 1.f, const uint32_t radialSegments = 8, const bool closed = false) {

			// If no path provided, create a default path
			if (path == nullptr) {
				// Create default path (using SimplePath3D instead of QuadraticBezierCurve)
				std::vector<XMFLOAT3> pathPoints = {
					{-1.f, -1.f, 0.f},
					{-1.f, 1.f, 0.f},
					{1.f, 1.f, 0.f}
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

			// Compute Frenet frames for the path
			FrenetFrames frames = computeFrenetFrames(tubularSegments, closed);

			// Store the frames
			this->tangents = frames.tangents;
			this->normals = frames.normals;
			this->binormals = frames.binormals;

			// Generate the geometry
			generateBufferData();

			// Set primitive type
			primtive.SetPrimitiveType(GeometryComponent::PrimitiveType::TRIANGLES);
			primtive.ComputeAABB();
		}

		// Cleanup
		~TubeGeometry() {
			// Delete path if we created it in the constructor
			if (needToDeletePath && path != nullptr) {
				delete path;
				path = nullptr;
			}
		}

		Primitive& GetPrimitive() { return primtive; }
	};
}