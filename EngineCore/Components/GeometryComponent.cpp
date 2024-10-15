#include "GComponents.h"
#include "Utils/Backlog.h"

#include "ThirdParty/mikktspace.h"
#include "ThirdParty/meshoptimizer/meshoptimizer.h"

namespace vz
{
#define MAX_GEOMETRY_PARTS 10000
	void GeometryComponent::MovePrimitives(std::vector<Primitive>& primitives)
	{
		parts_.assign(primitives.size(), Primitive());
		for (size_t i = 0, n = primitives.size(); i < n; ++i)
		{
			Primitive& prim = parts_[i];
			prim.MoveFrom(primitives[i]);
		}
		updateAABB();
		timeStampSetter_ = TimerNow;
	}
	void GeometryComponent::CopyPrimitives(const std::vector<Primitive>& primitives)
	{
		parts_ = primitives;
		updateAABB();
		timeStampSetter_ = TimerNow;
	}

	using Primitive = GeometryComponent::Primitive;
	void tryAssignParts(const size_t slot, std::vector<Primitive>& parts)
	{
		assert(slot < MAX_GEOMETRY_PARTS);
		if (slot >= parts.size()) {
			size_t n = parts.size();
			std::vector<Primitive> parts_tmp(n);
			for (size_t i = 0; i < n; ++i)
			{
				parts_tmp[i].MoveFrom(parts[i]);
			}
			parts.assign(slot + 1, Primitive());
			for (size_t i = 0; i < n; ++i)
			{
				parts_tmp[i].MoveTo(parts[i]);
			}
		}
	}
	void GeometryComponent::MovePrimitive(Primitive& primitive, const size_t slot)
	{
		tryAssignParts(slot, parts_);
		Primitive& prim = parts_[slot];
		prim.MoveFrom(primitive);
		updateAABB();
		timeStampSetter_ = TimerNow;
	}
	void GeometryComponent::CopyPrimitive(const Primitive& primitive, const size_t slot)
	{
		tryAssignParts(slot, parts_);
		parts_[slot] = primitive;
		updateAABB();
		timeStampSetter_ = TimerNow;
	}
	const Primitive* GeometryComponent::GetPrimitive(const size_t slot) const
	{
		if (slot >= parts_.size()) {
			backlog::post("slot is over # of parts!", backlog::LogLevel::Error);
			return nullptr;
		}
		return &parts_[slot];	
	}
	void GeometryComponent::updateAABB()
	{
		for (size_t i = 0, n = parts_.size(); i < n; ++i)
		{
			Primitive& prim = parts_[i];
			primitive::AABB part_aabb = prim.GetAABB();
			aabb_._max = math::Max(aabb_._max, part_aabb._max);
			aabb_._min = math::Min(aabb_._min, part_aabb._min);
		}
		isDirtyAABB_ = false;
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
		const XMFLOAT2& vert = userdata->vertexUVset0[index];
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


	void GGeometryComponent::DeleteRenderData()
	{
		bufferParts.clear();
	}
	void GGeometryComponent::UpdateRenderData()
	{
		DeleteRenderData();

		GraphicsDevice* device = graphics::GetDevice();

		bufferParts.reserve(parts_.size());

		for (size_t i = 0, n = parts_.size(); i < n; ++i)
		{
			Primitive& primitive = parts_[i];

			const std::vector<XMFLOAT3>& vertex_positions = primitive.GetVtxPositions();
			const std::vector<uint32_t>& indices = primitive.GetIdxPrimives();
			std::vector<XMFLOAT3>& vertex_normals = GetModifierVtxNormals(i);
			std::vector<XMFLOAT4>& vertex_tangents = GetModifierVtxTangents(i);
			std::vector<XMFLOAT2>& vertex_uvset_0 = GetModifierVtxUVSet0(i);
			std::vector<XMFLOAT2>& vertex_uvset_1 = GetModifierVtxUVSet1(i);

			// TANGENT computation
			if (primitive.GetPrimitiveType() == PrimitiveType::TRIANGLES && vertex_tangents.empty() 
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
			primitive::AABB aabb = primitive::AABB(_min, _max);
			primitive.SetAABB(aabb);

			if (IsQuantizedPositionsDisabled())
			{
				position_format = vertex_windweights.empty() ? Vertex_POS32::FORMAT : Vertex_POS32W::FORMAT;
			}
			else
			{
				// Determine minimum precision for positions:
				const float target_precision = 1.0f / 1000.0f; // millimeter
				position_format = Vertex_POS16::FORMAT;
				for (size_t i = 0; i < vertex_positions.size(); ++i)
				{
					const XMFLOAT3& pos = vertex_positions[i];
					const uint8_t wind = vertex_windweights.empty() ? 0xFF : vertex_windweights[i];

					Vertex_POS16 v;
					v.FromFULL(aabb, pos, wind);
					XMFLOAT3 p = v.GetPOS(aabb);
					if (
						std::abs(p.x - pos.x) <= target_precision &&
						std::abs(p.y - pos.y) <= target_precision &&
						std::abs(p.z - pos.z) <= target_precision &&
						wind == v.GetWind()
						)
					{
						// success, continue to next vertex with 16 bits
						continue;
					}
					position_format = vertex_windweights.empty() ? Vertex_POS32::FORMAT : Vertex_POS32W::FORMAT; // failed, increase to 32 bits
					break; // since 32 bit is the max, we can bail out
				}

				if (IsFormatUnorm(position_format))
				{
					// This is done to avoid 0 scaling on any axis of the UNORM remap matrix of the AABB
					//	It specifically solves a problem with hardware raytracing which treats AABB with zero axis as invisible
					//	Also there was problem with using float epsilon value, it did not enough precision for raytracing
					constexpr float min_dim = 0.01f;
					if (aabb._max.x - aabb._min.x < min_dim)
					{
						aabb._max.x += min_dim;
						aabb._min.x -= min_dim;
					}
					if (aabb._max.y - aabb._min.y < min_dim)
					{
						aabb._max.y += min_dim;
						aabb._min.y -= min_dim;
					}
					if (aabb._max.z - aabb._min.z < min_dim)
					{
						aabb._max.z += min_dim;
						aabb._min.z -= min_dim;
					}
				}
			}

			// Determine UV range for normalization:
			if (!vertex_uvset_0.empty() || !vertex_uvset_1.empty())
			{
				const XMFLOAT2* uv0_stream = vertex_uvset_0.empty() ? vertex_uvset_1.data() : vertex_uvset_0.data();
				const XMFLOAT2* uv1_stream = vertex_uvset_1.empty() ? vertex_uvset_0.data() : vertex_uvset_1.data();

				uv_range_min = XMFLOAT2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
				uv_range_max = XMFLOAT2(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
				for (size_t i = 0; i < uv_count; ++i)
				{
					uv_range_max = math::Max(uv_range_max, uv0_stream[i]);
					uv_range_max = math::Max(uv_range_max, uv1_stream[i]);
					uv_range_min = math::Min(uv_range_min, uv0_stream[i]);
					uv_range_min = math::Min(uv_range_min, uv1_stream[i]);
				}
			}

			const size_t position_stride = GetFormatStride(position_format);

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
			bd.size =
				AlignTo(vertex_positions.size() * position_stride, alignment) + // position will be first to have 0 offset for flexible alignment!
				AlignTo(indices.size() * GetIndexStride(), alignment) +
				AlignTo(vertex_normals.size() * sizeof(Vertex_NOR), alignment) +
				AlignTo(vertex_tangents.size() * sizeof(Vertex_TAN), alignment) +
				AlignTo(uv_count * sizeof(Vertex_UVS), alignment) +
				AlignTo(vertex_atlas.size() * sizeof(Vertex_TEX), alignment) +
				AlignTo(vertex_colors.size() * sizeof(Vertex_COL), alignment) +
				AlignTo(vertex_boneindices.size() * sizeof(Vertex_BON), alignment) +
				AlignTo(vertex_boneindices2.size() * sizeof(Vertex_BON), alignment)
				;

			auto init_callback = [&](void* dest) {
				uint8_t* buffer_data = (uint8_t*)dest;
				uint64_t buffer_offset = 0ull;

				// vertexBuffer - POSITION + WIND:
				switch (position_format)
				{
				case Vertex_POS16::FORMAT:
				{
					vb_pos_wind.offset = buffer_offset;
					vb_pos_wind.size = vertex_positions.size() * sizeof(Vertex_POS16);
					Vertex_POS16* vertices = (Vertex_POS16*)(buffer_data + buffer_offset);
					buffer_offset += AlignTo(vb_pos_wind.size, alignment);
					for (size_t i = 0; i < vertex_positions.size(); ++i)
					{
						XMFLOAT3 pos = vertex_positions[i];
						const uint8_t wind = vertex_windweights.empty() ? 0xFF : vertex_windweights[i];
						Vertex_POS16 vert;
						vert.FromFULL(aabb, pos, wind);
						std::memcpy(vertices + i, &vert, sizeof(vert));
					}
				}
				break;
				case Vertex_POS32::FORMAT:
				{
					vb_pos_wind.offset = buffer_offset;
					vb_pos_wind.size = vertex_positions.size() * sizeof(Vertex_POS32);
					Vertex_POS32* vertices = (Vertex_POS32*)(buffer_data + buffer_offset);
					buffer_offset += AlignTo(vb_pos_wind.size, alignment);
					for (size_t i = 0; i < vertex_positions.size(); ++i)
					{
						const XMFLOAT3& pos = vertex_positions[i];
						const uint8_t wind = vertex_windweights.empty() ? 0xFF : vertex_windweights[i];
						Vertex_POS32 vert;
						vert.FromFULL(pos);
						std::memcpy(vertices + i, &vert, sizeof(vert));
					}
				}
				break;
				case Vertex_POS32W::FORMAT:
				{
					vb_pos_wind.offset = buffer_offset;
					vb_pos_wind.size = vertex_positions.size() * sizeof(Vertex_POS32W);
					Vertex_POS32W* vertices = (Vertex_POS32W*)(buffer_data + buffer_offset);
					buffer_offset += AlignTo(vb_pos_wind.size, alignment);
					for (size_t i = 0; i < vertex_positions.size(); ++i)
					{
						const XMFLOAT3& pos = vertex_positions[i];
						const uint8_t wind = vertex_windweights.empty() ? 0xFF : vertex_windweights[i];
						Vertex_POS32W vert;
						vert.FromFULL(pos, wind);
						std::memcpy(vertices + i, &vert, sizeof(vert));
					}
				}
				break;
				default:
					assert(0);
					break;
				}

				// Create index buffer GPU data:
				if (GetIndexFormat() == IndexBufferFormat::UINT32)
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
					vb_uvs.size = uv_count * sizeof(Vertex_UVS);
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

				// vertexBuffer - ATLAS
				if (!vertex_atlas.empty())
				{
					vb_atl.offset = buffer_offset;
					vb_atl.size = vertex_atlas.size() * sizeof(Vertex_TEX);
					Vertex_TEX* vertices = (Vertex_TEX*)(buffer_data + buffer_offset);
					buffer_offset += AlignTo(vb_atl.size, alignment);
					for (size_t i = 0; i < vertex_atlas.size(); ++i)
					{
						Vertex_TEX vert;
						vert.FromFULL(vertex_atlas[i]);
						std::memcpy(vertices + i, &vert, sizeof(vert));
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

				// bone reference buffers (skinning, soft body):
				if (!vertex_boneindices.empty())
				{
					vb_bon.offset = buffer_offset;
					const size_t influence_div4 = GetBoneInfluenceDiv4();
					vb_bon.size = (vertex_boneindices.size() + vertex_boneindices2.size()) * sizeof(Vertex_BON);
					Vertex_BON* vertices = (Vertex_BON*)(buffer_data + buffer_offset);
					buffer_offset += AlignTo(vb_bon.size, alignment);
					assert(vertex_boneindices.size() == vertex_boneweights.size()); // must have same number of indices as weights
					assert(vertex_boneindices2.empty() || vertex_boneindices2.size() == vertex_boneindices.size()); // if second influence stream exists, it must be as large as the first
					assert(vertex_boneindices2.size() == vertex_boneweights2.size()); // must have same number of indices as weights
					for (size_t i = 0; i < vertex_boneindices.size(); ++i)
					{
						// Normalize weights:
						//	Note: if multiple influence streams are present,
						//	we have to normalize them together, not separately
						float weights[8] = {};
						weights[0] = vertex_boneweights[i].x;
						weights[1] = vertex_boneweights[i].y;
						weights[2] = vertex_boneweights[i].z;
						weights[3] = vertex_boneweights[i].w;
						if (influence_div4 > 1)
						{
							weights[4] = vertex_boneweights2[i].x;
							weights[5] = vertex_boneweights2[i].y;
							weights[6] = vertex_boneweights2[i].z;
							weights[7] = vertex_boneweights2[i].w;
						}
						float sum = 0;
						for (auto& weight : weights)
						{
							sum += weight;
						}
						if (sum > 0)
						{
							const float norm = 1.0f / sum;
							for (auto& weight : weights)
							{
								weight *= norm;
							}
						}
						// Store back normalized weights:
						vertex_boneweights[i].x = weights[0];
						vertex_boneweights[i].y = weights[1];
						vertex_boneweights[i].z = weights[2];
						vertex_boneweights[i].w = weights[3];
						if (influence_div4 > 1)
						{
							vertex_boneweights2[i].x = weights[4];
							vertex_boneweights2[i].y = weights[5];
							vertex_boneweights2[i].z = weights[6];
							vertex_boneweights2[i].w = weights[7];
						}

						Vertex_BON vert;
						vert.FromFULL(vertex_boneindices[i], vertex_boneweights[i]);
						std::memcpy(vertices + (i * influence_div4 + 0), &vert, sizeof(vert));

						if (influence_div4 > 1)
						{
							vert.FromFULL(vertex_boneindices2[i], vertex_boneweights2[i]);
							std::memcpy(vertices + (i * influence_div4 + 1), &vert, sizeof(vert));
						}
					}
				}

				// morph buffers:
				if (!morph_targets.empty())
				{
					vb_mor.offset = buffer_offset;
					for (MorphTarget& morph : morph_targets)
					{
						if (!morph.vertex_positions.empty())
						{
							morph.offset_pos = (buffer_offset - vb_mor.offset) / morph_stride;
							XMHALF4* vertices = (XMHALF4*)(buffer_data + buffer_offset);
							std::fill(vertices, vertices + vertex_positions.size(), 0);
							if (morph.sparse_indices_positions.empty())
							{
								// flat morphs:
								for (size_t i = 0; i < morph.vertex_positions.size(); ++i)
								{
									XMStoreHalf4(vertices + i, XMLoadFloat3(&morph.vertex_positions[i]));
								}
							}
							else
							{
								// sparse morphs will be flattened for GPU because they will be evaluated in skinning for every vertex:
								for (size_t i = 0; i < morph.sparse_indices_positions.size(); ++i)
								{
									const uint32_t ind = morph.sparse_indices_positions[i];
									XMStoreHalf4(vertices + ind, XMLoadFloat3(&morph.vertex_positions[i]));
								}
							}
							buffer_offset += AlignTo(morph.vertex_positions.size() * sizeof(XMHALF4), alignment);
						}
						if (!morph.vertex_normals.empty())
						{
							morph.offset_nor = (buffer_offset - vb_mor.offset) / morph_stride;
							XMHALF4* vertices = (XMHALF4*)(buffer_data + buffer_offset);
							std::fill(vertices, vertices + vertex_normals.size(), 0);
							if (morph.sparse_indices_normals.empty())
							{
								// flat morphs:
								for (size_t i = 0; i < morph.vertex_normals.size(); ++i)
								{
									XMStoreHalf4(vertices + i, XMLoadFloat3(&morph.vertex_normals[i]));
								}
							}
							else
							{
								// sparse morphs will be flattened for GPU because they will be evaluated in skinning for every vertex:
								for (size_t i = 0; i < morph.sparse_indices_normals.size(); ++i)
								{
									const uint32_t ind = morph.sparse_indices_normals[i];
									XMStoreHalf4(vertices + ind, XMLoadFloat3(&morph.vertex_normals[i]));
								}
							}
							buffer_offset += AlignTo(morph.vertex_normals.size() * sizeof(XMHALF4), alignment);
						}
					}
					vb_mor.size = buffer_offset - vb_mor.offset;
				}

				if (!clusters.empty())
				{
					buffer_offset = AlignTo(buffer_offset, sizeof(ShaderCluster));
					vb_clu.offset = buffer_offset;
					vb_clu.size = clusters.size() * sizeof(ShaderCluster);
					std::memcpy(buffer_data + buffer_offset, clusters.data(), vb_clu.size);
					buffer_offset += AlignTo(vb_clu.size, alignment);
				}
				if (!cluster_bounds.empty())
				{
					buffer_offset = AlignTo(buffer_offset, sizeof(ShaderClusterBounds));
					vb_bou.offset = buffer_offset;
					vb_bou.size = cluster_bounds.size() * sizeof(ShaderClusterBounds);
					std::memcpy(buffer_data + buffer_offset, cluster_bounds.data(), vb_bou.size);
					buffer_offset += AlignTo(vb_bou.size, alignment);
				}
				};

			bool success = device->CreateBuffer2(&bd, init_callback, &generalBuffer);
			assert(success);
			device->SetName(&generalBuffer, "GGeometryComponent::generalBuffer");

			assert(ib.IsValid());
			const Format ib_format = GetIndexFormat() == IndexBufferFormat::UINT32 ? Format::R32_UINT : Format::R16_UINT;
			ib.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, ib.offset, ib.size, &ib_format);
			ib.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, ib.subresource_srv);

			assert(vb_pos_wind.IsValid());
			vb_pos_wind.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_pos_wind.offset, vb_pos_wind.size, &position_format);
			vb_pos_wind.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_pos_wind.subresource_srv);

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
				vb_uvs.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_uvs.offset, vb_uvs.size, &Vertex_UVS::FORMAT);
				vb_uvs.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_uvs.subresource_srv);
			}
			if (vb_atl.IsValid())
			{
				vb_atl.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_atl.offset, vb_atl.size, &Vertex_TEX::FORMAT);
				vb_atl.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_atl.subresource_srv);
			}
			if (vb_col.IsValid())
			{
				vb_col.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_col.offset, vb_col.size, &Vertex_COL::FORMAT);
				vb_col.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_col.subresource_srv);
			}
			if (vb_bon.IsValid())
			{
				vb_bon.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_bon.offset, vb_bon.size);
				vb_bon.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_bon.subresource_srv);
			}
			if (vb_mor.IsValid())
			{
				vb_mor.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_mor.offset, vb_mor.size, &morph_format);
				vb_mor.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_mor.subresource_srv);
			}
			if (vb_clu.IsValid())
			{
				static constexpr uint32_t cluster_stride = sizeof(ShaderCluster);
				vb_clu.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_clu.offset, vb_clu.size, nullptr, &cluster_stride);
				vb_clu.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_clu.subresource_srv);
			}
			if (vb_bou.IsValid())
			{
				static constexpr uint32_t cluster_stride = sizeof(ShaderClusterBounds);
				vb_bou.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_bou.offset, vb_bou.size, nullptr, &cluster_stride);
				vb_bou.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_bou.subresource_srv);
			}

			if (!vertex_boneindices.empty() || !morph_targets.empty())
			{
				CreateStreamoutRenderData();
			}
		}
	}

	/*
	void GGeometryComponent::UpdateStreamoutRenderData()
	{
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
		desc.size =
			AlignTo(vertex_positions.size() * sizeof(Vertex_POS32), alignment) + // pos
			AlignTo(vertex_positions.size() * sizeof(Vertex_POS32), alignment) + // prevpos
			AlignTo(vertex_normals.size() * sizeof(Vertex_NOR), alignment) +
			AlignTo(vertex_tangents.size() * sizeof(Vertex_TAN), alignment)
			;

		bool success = device->CreateBuffer(&desc, nullptr, &streamoutBuffer);
		assert(success);
		device->SetName(&streamoutBuffer, "GGeometryComponent::streamoutBuffer");

		uint64_t buffer_offset = 0ull;

		so_pos.offset = buffer_offset;
		so_pos.size = vertex_positions.size() * sizeof(Vertex_POS32);
		buffer_offset += AlignTo(so_pos.size, alignment);
		so_pos.subresource_srv = device->CreateSubresource(&streamoutBuffer, SubresourceType::SRV, so_pos.offset, so_pos.size, &Vertex_POS32::FORMAT);
		so_pos.subresource_uav = device->CreateSubresource(&streamoutBuffer, SubresourceType::UAV, so_pos.offset, so_pos.size); // UAV can't have RGB32_F format!
		so_pos.descriptor_srv = device->GetDescriptorIndex(&streamoutBuffer, SubresourceType::SRV, so_pos.subresource_srv);
		so_pos.descriptor_uav = device->GetDescriptorIndex(&streamoutBuffer, SubresourceType::UAV, so_pos.subresource_uav);

		so_pre.offset = buffer_offset;
		so_pre.size = so_pos.size;
		buffer_offset += AlignTo(so_pre.size, alignment);
		so_pre.subresource_srv = device->CreateSubresource(&streamoutBuffer, SubresourceType::SRV, so_pre.offset, so_pre.size, &Vertex_POS32::FORMAT);
		so_pre.subresource_uav = device->CreateSubresource(&streamoutBuffer, SubresourceType::UAV, so_pre.offset, so_pre.size); // UAV can't have RGB32_F format!
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

	size_t GGeometryComponent::GetMemoryUsageCPU() const
	{
		size_t size =
			vertex_positions.size() * sizeof(XMFLOAT3) +
			vertex_normals.size() * sizeof(XMFLOAT3) +
			vertex_tangents.size() * sizeof(XMFLOAT4) +
			vertex_uvset_0.size() * sizeof(XMFLOAT2) +
			vertex_uvset_1.size() * sizeof(XMFLOAT2) +
			vertex_boneindices.size() * sizeof(XMUINT4) +
			vertex_boneweights.size() * sizeof(XMFLOAT4) +
			vertex_atlas.size() * sizeof(XMFLOAT2) +
			vertex_colors.size() * sizeof(uint32_t) +
			vertex_windweights.size() * sizeof(uint8_t) +
			indices.size() * sizeof(uint32_t);

		for (const MorphTarget& morph : morph_targets)
		{
			size +=
				morph.vertex_positions.size() * sizeof(XMFLOAT3) +
				morph.vertex_normals.size() * sizeof(XMFLOAT3) +
				morph.sparse_indices_positions.size() * sizeof(uint32_t) +
				morph.sparse_indices_normals.size() * sizeof(uint32_t);
		}

		size += GetMemoryUsageBVH();

		return size;
	}
	size_t GGeometryComponent::GetMemoryUsageGPU() const
	{
		return generalBuffer.desc.size + streamoutBuffer.desc.size;
	}
	size_t GGeometryComponent::GetMemoryUsageBVH() const
	{
		return
			bvh.allocation.capacity() +
			bvh_leaf_aabbs.size() * sizeof(primitive::AABB);
	}

	void GGeometryComponent::CreateRaytracingRenderData()
	{
		GraphicsDevice* device = graphics::GetDevice();

		if (!device->CheckCapability(GraphicsDeviceCapability::RAYTRACING))
			return;

		BLAS_state = GGeometryComponent::BLAS_STATE_NEEDS_REBUILD;

		const uint32_t lod_count = GetLODCount();
		BLASes.resize(lod_count);
		for (uint32_t lod = 0; lod < lod_count; ++lod)
		{
			RaytracingAccelerationStructureDesc desc;
			desc.type = RaytracingAccelerationStructureDesc::Type::BOTTOMLEVEL;

			if (streamoutBuffer.IsValid())
			{
				desc.flags |= RaytracingAccelerationStructureDesc::FLAG_ALLOW_UPDATE;
				desc.flags |= RaytracingAccelerationStructureDesc::FLAG_PREFER_FAST_BUILD;
			}
			else
			{
				desc.flags |= RaytracingAccelerationStructureDesc::FLAG_PREFER_FAST_TRACE;
			}

			uint32_t first_subset = 0;
			uint32_t last_subset = 0;
			GetLODSubsetRange(lod, first_subset, last_subset);
			for (uint32_t subsetIndex = first_subset; subsetIndex < last_subset; ++subsetIndex)
			{
				const GGeometryComponent::MeshSubset& subset = subsets[subsetIndex];
				desc.bottom_level.geometries.emplace_back();
				auto& geometry = desc.bottom_level.geometries.back();
				geometry.type = RaytracingAccelerationStructureDesc::BottomLevel::Geometry::Type::TRIANGLES;
				geometry.triangles.vertex_buffer = generalBuffer;
				geometry.triangles.vertex_byte_offset = vb_pos_wind.offset;
				geometry.triangles.index_buffer = generalBuffer;
				geometry.triangles.index_format = GetIndexFormat();
				geometry.triangles.index_count = subset.indexCount;
				geometry.triangles.index_offset = ib.offset / GetIndexStride() + subset.indexOffset;
				geometry.triangles.vertex_count = (uint32_t)vertex_positions.size();
				if (so_pos.IsValid())
				{
					geometry.triangles.vertex_format = Vertex_POS32::FORMAT;
					geometry.triangles.vertex_stride = sizeof(Vertex_POS32);
				}
				else
				{
					geometry.triangles.vertex_format = position_format == Format::R32G32B32A32_FLOAT ? Format::R32G32B32_FLOAT : position_format;
					geometry.triangles.vertex_stride = GetFormatStride(position_format);
				}
			}

			bool success = device->CreateRaytracingAccelerationStructure(&desc, &BLASes[lod]);
			assert(success);
			device->SetName(&BLASes[lod], std::string("GGeometryComponent::BLAS[LOD" + std::to_string(lod) + "]").c_str());
		}
	}
	void GGeometryComponent::BuildBVH()
	{
		bvh_leaf_aabbs.clear();
		uint32_t first_subset = 0;
		uint32_t last_subset = 0;
		GetLODSubsetRange(0, first_subset, last_subset);
		for (uint32_t subsetIndex = first_subset; subsetIndex < last_subset; ++subsetIndex)
		{
			const GGeometryComponent::MeshSubset& subset = subsets[subsetIndex];
			if (subset.indexCount == 0)
				continue;
			const uint32_t indexOffset = subset.indexOffset;
			const uint32_t triangleCount = subset.indexCount / 3;
			for (uint32_t triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex)
			{
				const uint32_t i0 = indices[indexOffset + triangleIndex * 3 + 0];
				const uint32_t i1 = indices[indexOffset + triangleIndex * 3 + 1];
				const uint32_t i2 = indices[indexOffset + triangleIndex * 3 + 2];
				const XMFLOAT3& p0 = vertex_positions[i0];
				const XMFLOAT3& p1 = vertex_positions[i1];
				const XMFLOAT3& p2 = vertex_positions[i2];
				AABB aabb = primitive::AABB(math::Min(p0, math::Min(p1, p2)), math::Max(p0, math::Max(p1, p2)));
				aabb.layerMask = triangleIndex;
				aabb.userdata = subsetIndex;
				bvh_leaf_aabbs.push_back(aabb);
			}
		}
		bvh.Build(bvh_leaf_aabbs.data(), (uint32_t)bvh_leaf_aabbs.size());
	}
	void GGeometryComponent::ComputeNormals(COMPUTE_NORMALS compute)
	{
		// Start recalculating normals:

		if (compute != COMPUTE_NORMALS_SMOOTH_FAST)
		{
			// Compute hard surface normals:

			// Right now they are always computed even before smooth setting

			vector<uint32_t> newIndexBuffer;
			vector<XMFLOAT3> newPositionsBuffer;
			vector<XMFLOAT3> newNormalsBuffer;
			vector<XMFLOAT2> newUV0Buffer;
			vector<XMFLOAT2> newUV1Buffer;
			vector<XMFLOAT2> newAtlasBuffer;
			vector<XMUINT4> newBoneIndicesBuffer;
			vector<XMFLOAT4> newBoneWeightsBuffer;
			vector<uint32_t> newColorsBuffer;

			for (size_t face = 0; face < indices.size() / 3; face++)
			{
				uint32_t i0 = indices[face * 3 + 0];
				uint32_t i1 = indices[face * 3 + 1];
				uint32_t i2 = indices[face * 3 + 2];

				XMFLOAT3& p0 = vertex_positions[i0];
				XMFLOAT3& p1 = vertex_positions[i1];
				XMFLOAT3& p2 = vertex_positions[i2];

				XMVECTOR U = XMLoadFloat3(&p2) - XMLoadFloat3(&p0);
				XMVECTOR V = XMLoadFloat3(&p1) - XMLoadFloat3(&p0);

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

				if (!vertex_uvset_0.empty())
				{
					newUV0Buffer.push_back(vertex_uvset_0[i0]);
					newUV0Buffer.push_back(vertex_uvset_0[i1]);
					newUV0Buffer.push_back(vertex_uvset_0[i2]);
				}

				if (!vertex_uvset_1.empty())
				{
					newUV1Buffer.push_back(vertex_uvset_1[i0]);
					newUV1Buffer.push_back(vertex_uvset_1[i1]);
					newUV1Buffer.push_back(vertex_uvset_1[i2]);
				}

				if (!vertex_atlas.empty())
				{
					newAtlasBuffer.push_back(vertex_atlas[i0]);
					newAtlasBuffer.push_back(vertex_atlas[i1]);
					newAtlasBuffer.push_back(vertex_atlas[i2]);
				}

				if (!vertex_boneindices.empty())
				{
					newBoneIndicesBuffer.push_back(vertex_boneindices[i0]);
					newBoneIndicesBuffer.push_back(vertex_boneindices[i1]);
					newBoneIndicesBuffer.push_back(vertex_boneindices[i2]);
				}

				if (!vertex_boneweights.empty())
				{
					newBoneWeightsBuffer.push_back(vertex_boneweights[i0]);
					newBoneWeightsBuffer.push_back(vertex_boneweights[i1]);
					newBoneWeightsBuffer.push_back(vertex_boneweights[i2]);
				}

				if (!vertex_colors.empty())
				{
					newColorsBuffer.push_back(vertex_colors[i0]);
					newColorsBuffer.push_back(vertex_colors[i1]);
					newColorsBuffer.push_back(vertex_colors[i2]);
				}

				newIndexBuffer.push_back(static_cast<uint32_t>(newIndexBuffer.size()));
				newIndexBuffer.push_back(static_cast<uint32_t>(newIndexBuffer.size()));
				newIndexBuffer.push_back(static_cast<uint32_t>(newIndexBuffer.size()));
			}

			// For hard surface normals, we created a new mesh in the previous loop through faces, so swap data:
			vertex_positions = newPositionsBuffer;
			vertex_normals = newNormalsBuffer;
			vertex_uvset_0 = newUV0Buffer;
			vertex_uvset_1 = newUV1Buffer;
			vertex_atlas = newAtlasBuffer;
			vertex_colors = newColorsBuffer;
			if (!vertex_boneindices.empty())
			{
				vertex_boneindices = newBoneIndicesBuffer;
			}
			if (!vertex_boneweights.empty())
			{
				vertex_boneweights = newBoneWeightsBuffer;
			}
			indices = newIndexBuffer;
		}

		switch (compute)
		{
		case GGeometryComponent::COMPUTE_NORMALS_HARD:
			break;

		case GGeometryComponent::COMPUTE_NORMALS_SMOOTH:
		{
			// Compute smooth surface normals:

			// 1.) Zero normals, they will be averaged later
			for (size_t i = 0; i < vertex_normals.size(); i++)
			{
				vertex_normals[i] = XMFLOAT3(0, 0, 0);
			}

			// 2.) Find identical vertices by POSITION, accumulate face normals
			for (size_t i = 0; i < vertex_positions.size(); i++)
			{
				XMFLOAT3& v_search_pos = vertex_positions[i];

				for (size_t ind = 0; ind < indices.size() / 3; ++ind)
				{
					uint32_t i0 = indices[ind * 3 + 0];
					uint32_t i1 = indices[ind * 3 + 1];
					uint32_t i2 = indices[ind * 3 + 2];

					XMFLOAT3& v0 = vertex_positions[i0];
					XMFLOAT3& v1 = vertex_positions[i1];
					XMFLOAT3& v2 = vertex_positions[i2];


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
						XMVECTOR U = XMLoadFloat3(&v2) - XMLoadFloat3(&v0);
						XMVECTOR V = XMLoadFloat3(&v1) - XMLoadFloat3(&v0);

						XMVECTOR N = XMVector3Cross(U, V);
						N = XMVector3Normalize(N);

						XMFLOAT3 normal;
						XMStoreFloat3(&normal, N);

						vertex_normals[i].x += normal.x;
						vertex_normals[i].y += normal.y;
						vertex_normals[i].z += normal.z;
					}

				}
			}

			// 3.) Find duplicated vertices by POSITION and UV0 and UV1 and ATLAS and SUBSET and remove them:
			uint32_t first_subset = 0;
			uint32_t last_subset = 0;
			GetLODSubsetRange(0, first_subset, last_subset);
			for (uint32_t subsetIndex = first_subset; subsetIndex < last_subset; ++subsetIndex)
			{
				const GGeometryComponent::MeshSubset& subset = subsets[subsetIndex];
				for (uint32_t i = 0; i < subset.indexCount - 1; i++)
				{
					uint32_t ind0 = indices[subset.indexOffset + (uint32_t)i];
					const XMFLOAT3& p0 = vertex_positions[ind0];
					const XMFLOAT2& u00 = vertex_uvset_0.empty() ? XMFLOAT2(0, 0) : vertex_uvset_0[ind0];
					const XMFLOAT2& u10 = vertex_uvset_1.empty() ? XMFLOAT2(0, 0) : vertex_uvset_1[ind0];
					const XMFLOAT2& at0 = vertex_atlas.empty() ? XMFLOAT2(0, 0) : vertex_atlas[ind0];

					for (uint32_t j = i + 1; j < subset.indexCount; j++)
					{
						uint32_t ind1 = indices[subset.indexOffset + (uint32_t)j];

						if (ind1 == ind0)
						{
							continue;
						}

						const XMFLOAT3& p1 = vertex_positions[ind1];
						const XMFLOAT2& u01 = vertex_uvset_0.empty() ? XMFLOAT2(0, 0) : vertex_uvset_0[ind1];
						const XMFLOAT2& u11 = vertex_uvset_1.empty() ? XMFLOAT2(0, 0) : vertex_uvset_1[ind1];
						const XMFLOAT2& at1 = vertex_atlas.empty() ? XMFLOAT2(0, 0) : vertex_atlas[ind1];

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

						const bool duplicated_atl =
							math::float_equal(at0.x, at1.x) &&
							math::float_equal(at0.y, at1.y);

						if (duplicated_pos && duplicated_uv0 && duplicated_uv1 && duplicated_atl)
						{
							// Erase vertices[ind1] because it is a duplicate:
							if (ind1 < vertex_positions.size())
							{
								vertex_positions.erase(vertex_positions.begin() + ind1);
							}
							if (ind1 < vertex_normals.size())
							{
								vertex_normals.erase(vertex_normals.begin() + ind1);
							}
							if (ind1 < vertex_uvset_0.size())
							{
								vertex_uvset_0.erase(vertex_uvset_0.begin() + ind1);
							}
							if (ind1 < vertex_uvset_1.size())
							{
								vertex_uvset_1.erase(vertex_uvset_1.begin() + ind1);
							}
							if (ind1 < vertex_atlas.size())
							{
								vertex_atlas.erase(vertex_atlas.begin() + ind1);
							}
							if (ind1 < vertex_boneindices.size())
							{
								vertex_boneindices.erase(vertex_boneindices.begin() + ind1);
							}
							if (ind1 < vertex_boneweights.size())
							{
								vertex_boneweights.erase(vertex_boneweights.begin() + ind1);
							}

							// The vertices[ind1] was removed, so each index after that needs to be updated:
							for (auto& index : indices)
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

			}

		}
		break;

		case GGeometryComponent::COMPUTE_NORMALS_SMOOTH_FAST:
		{
			vertex_normals.resize(vertex_positions.size());
			for (size_t i = 0; i < vertex_normals.size(); i++)
			{
				vertex_normals[i] = XMFLOAT3(0, 0, 0);
			}
			uint32_t first_subset = 0;
			uint32_t last_subset = 0;
			GetLODSubsetRange(0, first_subset, last_subset);
			for (uint32_t subsetIndex = first_subset; subsetIndex < last_subset; ++subsetIndex)
			{
				const MeshSubset& subset = subsets[subsetIndex];

				for (uint32_t i = 0; i < subset.indexCount / 3; ++i)
				{
					uint32_t index1 = indices[subset.indexOffset + i * 3 + 0];
					uint32_t index2 = indices[subset.indexOffset + i * 3 + 1];
					uint32_t index3 = indices[subset.indexOffset + i * 3 + 2];

					XMVECTOR side1 = XMLoadFloat3(&vertex_positions[index1]) - XMLoadFloat3(&vertex_positions[index3]);
					XMVECTOR side2 = XMLoadFloat3(&vertex_positions[index1]) - XMLoadFloat3(&vertex_positions[index2]);
					XMVECTOR N = XMVector3Normalize(XMVector3Cross(side1, side2));
					XMFLOAT3 normal;
					XMStoreFloat3(&normal, N);

					vertex_normals[index1].x += normal.x;
					vertex_normals[index1].y += normal.y;
					vertex_normals[index1].z += normal.z;

					vertex_normals[index2].x += normal.x;
					vertex_normals[index2].y += normal.y;
					vertex_normals[index2].z += normal.z;

					vertex_normals[index3].x += normal.x;
					vertex_normals[index3].y += normal.y;
					vertex_normals[index3].z += normal.z;
				}
			}
		}
		break;

		}

		vertex_tangents.clear(); // <- will be recomputed

		CreateRenderData(); // <- normals will be normalized here!
	}
	void GGeometryComponent::FlipCulling()
	{
		for (size_t face = 0; face < indices.size() / 3; face++)
		{
			uint32_t i0 = indices[face * 3 + 0];
			uint32_t i1 = indices[face * 3 + 1];
			uint32_t i2 = indices[face * 3 + 2];

			indices[face * 3 + 0] = i0;
			indices[face * 3 + 1] = i2;
			indices[face * 3 + 2] = i1;
		}

		CreateRenderData();
	}
	void GGeometryComponent::FlipNormals()
	{
		for (auto& normal : vertex_normals)
		{
			normal.x *= -1;
			normal.y *= -1;
			normal.z *= -1;
		}

		CreateRenderData();
	}
	void GGeometryComponent::Recenter()
	{
		XMFLOAT3 center = aabb.getCenter();

		for (auto& pos : vertex_positions)
		{
			pos.x -= center.x;
			pos.y -= center.y;
			pos.z -= center.z;
		}

		CreateRenderData();
	}
	void GGeometryComponent::RecenterToBottom()
	{
		XMFLOAT3 center = aabb.getCenter();
		center.y -= aabb.getHalfWidth().y;

		for (auto& pos : vertex_positions)
		{
			pos.x -= center.x;
			pos.y -= center.y;
			pos.z -= center.z;
		}

		CreateRenderData();
	}
	Sphere GGeometryComponent::GetBoundingSphere() const
	{
		Sphere sphere;
		sphere.center = aabb.getCenter();
		sphere.radius = aabb.getRadius();
		return sphere;
	}
	size_t GGeometryComponent::GetClusterCount() const
	{
		size_t cnt = 0;
		for (auto& x : cluster_ranges)
		{
			cnt = std::max(cnt, size_t(x.clusterOffset + x.clusterCount));
		}
		return cnt;
	}
	size_t GGeometryComponent::CreateSubset()
	{
		int ret = 0;
		const uint32_t lod_count = GetLODCount();
		for (uint32_t lod = 0; lod < lod_count; ++lod)
		{
			uint32_t first_subset = 0;
			uint32_t last_subset = 0;
			GetLODSubsetRange(lod, first_subset, last_subset);
			GGeometryComponent::MeshSubset subset;
			subset.indexOffset = ~0u;
			subset.indexCount = 0;
			for (uint32_t subsetIndex = first_subset; subsetIndex < last_subset; ++subsetIndex)
			{
				subset.indexOffset = std::min(subset.indexOffset, subsets[subsetIndex].indexOffset);
				subset.indexCount = std::max(subset.indexCount, subsets[subsetIndex].indexOffset + subsets[subsetIndex].indexCount);
			}
			subsets.insert(subsets.begin() + last_subset, subset);
			if (lod == 0)
			{
				ret = last_subset;
			}
		}
		if (lod_count > 0)
		{
			subsets_per_lod++;
		}
		CreateRenderData(); // mesh shader needs to rebuild clusters, otherwise wouldn't be needed
		return ret;
	}
	/**/
}