#include "GeometryGenerator.h"
#include "Common/Engine_Internal.h"
#include "Utils/Backlog.h"
#include "Components/Components.h"

namespace vz::geogen
{
	// source from https://github.com/mrdoob/three.js/blob/dev/src/geometries/PolyhedronGeometry.js

	using Primitive = GeometryComponent::Primitive;
	/**
	 * A polyhedron is a solid in three dimensions with flat faces. This class
	 * will take an array of vertices, project them onto a sphere, and then
	 * divide them up to the desired level of detail.
	 *
	 * @augments BufferGeometry
	 */
	class PolyhedronGeometry 
	{
		/**
		 * Constructs a new polyhedron geometry.
		 *
		 * @param {Array<number>} [vertices] - A flat array of vertices describing the base shape.
		 * @param {Array<number>} [indices] - A flat array of indices describing the base shape.
		 * @param {number} [radius=1] - The radius of the shape.
		 * @param {number} [detail=0] - How many levels to subdivide the geometry. The more detail, the smoother the shape.
		 */

		Primitive primtive;

		std::vector<XMFLOAT3> vertices;
		std::vector<uint32_t> indices;
		float radius = 1.f;
		uint32_t detail = 0;

		// helper functions

		void subdivide(const uint32_t detail) {

			// iterate over all faces and apply a subdivision with the given detail value

			for (size_t i = 0, n = indices.size(); i < n; i += 3) {

				// get the vertices of the face
				XMFLOAT3 a = vertices[indices[i + 0]];
				XMFLOAT3 b = vertices[indices[i + 1]];
				XMFLOAT3 c = vertices[indices[i + 2]];

				// perform subdivision

				subdivideFace(a, b, c, detail);

			}

		}

		void subdivideFace(const XMFLOAT3 a, const XMFLOAT3 b, const XMFLOAT3 c, const uint32_t detail) {

			const size_t cols = detail + 1;
			const float cols_rcp = 1.f / (float)cols;

			// we use this multidimensional array as a data structure for creating the subdivision

			std::vector<std::vector<XMFLOAT3>> v(cols + 1);

			// construct all of the vertices for this subdivision

			for (size_t i = 0; i <= cols; i++) {

				float ratio = (float)i * cols_rcp;
				const XMFLOAT3 aj = math::Lerp(a, c, ratio);
				const XMFLOAT3 bj = math::Lerp(b, c, ratio);

				const size_t rows = cols - i;
				const float rows_rcp = 1.f / (float)rows;

				v[i].resize(rows + 1);

				for (size_t j = 0; j <= rows; j++) {

					if (j == 0 && i == cols) {

						v[i][j] = aj;

					}
					else {

						v[i][j] = math::Lerp(aj, bj, (float)j * rows_rcp);

					}

				}

			}

			// construct all of the faces
			std::vector<XMFLOAT3>& positions = primtive.GetMutableVtxPositions();
			positions.reserve(64);

			for (size_t i = 0; i < cols; i++) {

				for (size_t j = 0; j < 2 * (cols - i) - 1; j++) {

					const uint32_t k = (uint32_t)floor(j / 2);

					if (j % 2 == 0) {

						positions.push_back(v[i][k + 1]);
						positions.push_back(v[i + 1][k]);
						positions.push_back(v[i][k]);

					}
					else {

						positions.push_back(v[i][k + 1]);
						positions.push_back(v[i + 1][k + 1]);
						positions.push_back(v[i + 1][k]);

					}

				}

			}

		}

		void applyRadius(const float radius) {

			std::vector<XMFLOAT3>& positions = primtive.GetMutableVtxPositions();

			// iterate over the entire buffer and apply the radius to each vertex

			for (size_t i = 0, n = positions.size(); i < n; i ++) {

				XMVECTOR v = XMLoadFloat3(&positions[i]);
				
				v = XMVector3Normalize(v) * radius;

				XMStoreFloat3(&positions[i], v);
			}

		}

		void generateUVs() {

			std::vector<XMFLOAT3>& positions = primtive.GetMutableVtxPositions();
			std::vector<XMFLOAT2>& uvs = primtive.GetMutableVtxUVSet0();
			uvs.reserve(positions.size());

			for (size_t i = 0, n = positions.size(); i < n; i++) {

				XMFLOAT3 pos = positions[i];

				XMFLOAT2 uv;
				uv.x = azimuth(pos) / 2 / math::PI + 0.5f;
				uv.y = inclination(pos) / math::PI + 0.5f;
				uv.y = 1 - uv.y;
				uvs.push_back(uv);

			}

			correctUVs();

			correctSeam();

		}

		void correctSeam() {

			std::vector<XMFLOAT2>& uvs = primtive.GetMutableVtxUVSet0();

			// handle case when face straddles the seam, see #3269

			for (size_t i = 0, n = uvs.size(); i < n; i += 3) {

				// uv data of a single face

				const float x0 = uvs[i + 0].x;
				const float x1 = uvs[i + 1].x;
				const float x2 = uvs[i + 2].x;

				const float max = std::max(std::max(x0, x1), x2);
				const float min = std::min(std::min(x0, x1), x2);

				// 0.9 is somewhat arbitrary

				if (max > 0.9 && min < 0.1) {

					if (x0 < 0.2) uvs[i + 0].x += 1;
					if (x1 < 0.2) uvs[i + 1].x += 1;
					if (x2 < 0.2) uvs[i + 2].x += 1;

				}

			}

		}

		void correctUVs() {

			std::vector<XMFLOAT3>& positions = primtive.GetMutableVtxPositions();
			std::vector<XMFLOAT2>& uvs = primtive.GetMutableVtxUVSet0();

			for (size_t i = 0, n = positions.size(); i < n; i += 3) {

				XMFLOAT3 a = positions[i + 0];
				XMFLOAT3 b = positions[i + 1];
				XMFLOAT3 c = positions[i + 2];

				XMFLOAT2 uvA = uvs[i + 0];
				XMFLOAT2 uvB = uvs[i + 1];
				XMFLOAT2 uvC = uvs[i + 2];

				XMFLOAT3 centroid;
				XMStoreFloat3(&centroid, (XMLoadFloat3(&a) + XMLoadFloat3(&b) + XMLoadFloat3(&c)) / 3.f);

				const float azi = azimuth(centroid);

				correctUV(uvA, i + 0, a, azi);
				correctUV(uvB, i + 1, b, azi);
				correctUV(uvC, i + 2, c, azi);

			}

		}

		void correctUV(const XMFLOAT2 uv, const uint32_t stride, const XMFLOAT3 vector, const float azimuth) {

			std::vector<XMFLOAT2>& uvs = primtive.GetMutableVtxUVSet0();

			if ((azimuth < 0) && (uv.x == 1)) {

				uvs[stride].x = uv.x - 1;

			}

			if ((vector.x == 0) && (vector.z == 0)) {

				uvs[stride].x = azimuth / 2 / math::PI + 0.5f;

			}

		}

		// Angle around the Y axis, counter-clockwise when looking from above.

		float azimuth(XMFLOAT3 v) {

			return atan2(v.z, -v.x);

		}


		// Angle above the XZ plane.

		float inclination(XMFLOAT3 v) {

			return atan2(-v.y, sqrt((v.x * v.x) + (v.z * v.z)));

		}

	public:
		PolyhedronGeometry(const std::vector<XMFLOAT3>& vertices, const std::vector<uint32_t>& indices, const float radius = 1, const uint32_t detail = 0) {

			/**
			 * Holds the constructor parameters that have been
			 * used to generate the geometry. Any modification
			 * after instantiation does not change the geometry.
			 */
			this->vertices = vertices;
			this->indices = indices;
			this->radius = radius;
			this->detail = detail;

			//

			subdivide(detail);

			// all vertices should lie on a conceptual sphere with a given radius

			applyRadius(radius);

			// finally, create the uv data

			generateUVs();

			primtive.SetPrimitiveType(GeometryComponent::PrimitiveType::TRIANGLES);
			primtive.FillIndicesFromTriVertices();

			if (detail == 0) {

				primtive.ComputeNormals(GeometryComponent::NormalComputeMethod::COMPUTE_NORMALS_HARD);

			}
			else {

				primtive.ComputeNormals(GeometryComponent::NormalComputeMethod::COMPUTE_NORMALS_SMOOTH_FAST);

			}

			primtive.ComputeAABB();
		}

		Primitive& GetPrimitive() { return primtive; }
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

		PolyhedronGeometry plolyhedron(vertices, indices, radius, detail);

		{
			std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());
			geometry->ClearGeometry();
			geometry->AddMovePrimitiveFrom(std::move(plolyhedron.GetPrimitive()));
			geometry->UpdateRenderData();
		}

		return true;
	}
}