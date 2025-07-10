#include "AssetIO.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "Utils/vzMath.h"
#include "Utils/Helpers.h"
#include "Utils/Backlog.h"

#include <unordered_map>

using namespace std;
using namespace vz;

struct membuf : std::streambuf
{
	membuf(char* begin, char* end) {
		this->setg(begin, begin, end);
	}
};

// Custom material file reader:
class MaterialFileReader : public tinyobj::MaterialReader {
public:
	explicit MaterialFileReader(const std::string& mtl_basedir)
		: m_mtlBaseDir(mtl_basedir) {}
	virtual ~MaterialFileReader() {}
	virtual bool operator()(const std::string& matId,
		std::vector<tinyobj::material_t>* materials,
		std::map<std::string, int>* matMap, std::string* err)
	{
		std::string filepath;

		if (!m_mtlBaseDir.empty()) {
			filepath = std::string(m_mtlBaseDir) + matId;
		}
		else {
			filepath = matId;
		}

		vector<uint8_t> filedata;
		if (!helper::FileRead(filepath, filedata))
		{
			std::string ss;
			ss += "WARN: Material file [ " + filepath + " ] not found.\n";
			if (err) {
				(*err) += ss;
			}
			return false;
		}

		membuf sbuf((char*)filedata.data(), (char*)filedata.data() + filedata.size());
		std::istream matIStream(&sbuf);

		std::string warning;
		LoadMtl(matMap, materials, &matIStream, &warning);

		if (!warning.empty()) {
			if (err) {
				(*err) += warning;
			}
		}

		return true;
	}

private:
	std::string m_mtlBaseDir;
};

// Transform the data from OBJ space to engine-space:
static const bool transform_to_LH = false;

Entity ImportModel_OBJ(const std::string& fileName)
{
	std::string directory = helper::GetDirectoryFromPath(fileName);
	std::string name = helper::GetFileNameFromPath(fileName);

	tinyobj::attrib_t obj_attrib;
	std::vector<tinyobj::shape_t> obj_shapes;
	std::vector<tinyobj::material_t> obj_materials;
	std::string obj_errors;

	vector<uint8_t> filedata;
	bool success = helper::FileRead(fileName, filedata);

	if (success)
	{
		membuf sbuf((char*)filedata.data(), (char*)filedata.data() + filedata.size());
		std::istream in(&sbuf);
		MaterialFileReader matFileReader(directory);
		success = tinyobj::LoadObj(&obj_attrib, &obj_shapes, &obj_materials, &obj_errors, &in, &matFileReader, true);
	}
	else
	{
		obj_errors = "Failed to read file: " + fileName;
	}

	if (!obj_errors.empty())
	{
		backlog::post(obj_errors, backlog::LogLevel::Warn);
	}

	Entity root_entity = INVALID_ENTITY;

	// entity list to configure node-style components
	if (success)
	{
		// root actor //
		root_entity = compfactory::MakeNodeActor(name);

		std::vector<Entity> materials;

		// Load material library:
		for (auto& obj_material : obj_materials)
		{
			Entity material_entity = compfactory::MakeResMaterial(obj_material.name);
			materials.push_back(material_entity);
			MaterialComponent& material = *compfactory::GetMaterialComponent(material_entity);
			material.SetShaderType(MaterialComponent::ShaderType::PBR);
			material.SetBaseColor(XMFLOAT4(obj_material.diffuse[0], obj_material.diffuse[1], obj_material.diffuse[2], 1));

			// textures //
			auto registerMaterial = [&material, &directory](const std::string& obj_mat_name, const MaterialComponent::TextureSlot slot)
				{
					std::vector<Entity> dummy;
					if (obj_mat_name == "") return;
					std::string mat_filename = directory + obj_mat_name;
					if (helper::FileExists(mat_filename))
					{
						Entity texture_entity = compfactory::MakeResTexture(obj_mat_name);
						TextureComponent& texture = *compfactory::GetTextureComponent(texture_entity);
						if (!texture.LoadImageFile(mat_filename))
						{
							compfactory::RemoveEntity(texture_entity);
						}
						else
						{
							material.SetTexture(texture_entity, slot);
						}
					}
				};
			registerMaterial(obj_material.diffuse_texname, MaterialComponent::TextureSlot::BASECOLORMAP);
			registerMaterial(obj_material.displacement_texname, MaterialComponent::TextureSlot::DISPLACEMENTMAP);

			material.SetEmissiveColor({ obj_material.emission[0], obj_material.emission[1], obj_material.emission[2],
				std::max(obj_material.emission[0], std::max(obj_material.emission[1], obj_material.emission[2]))
				});

			//material.refractionIndex = obj_material.ior;
			material.SetMatalness(obj_material.metallic);
			registerMaterial(obj_material.normal_texname, MaterialComponent::TextureSlot::NORMALMAP);
			registerMaterial(obj_material.specular_texname, MaterialComponent::TextureSlot::SURFACEMAP);
			material.SetRoughness(obj_material.roughness);

			if (material.GetTextureVUID(MaterialComponent::TextureSlot::NORMALMAP) == INVALID_VUID)
			{
				registerMaterial(obj_material.bump_texname, MaterialComponent::TextureSlot::NORMALMAP);
			}
			if (material.GetTextureVUID(MaterialComponent::TextureSlot::SURFACEMAP) == INVALID_VUID)
			{
				registerMaterial(obj_material.specular_highlight_texname, MaterialComponent::TextureSlot::NORMALMAP);
			}
		}

		if (materials.empty())
		{
			// Create default material if nothing was found:
			materials.push_back(compfactory::MakeResMaterial("OBJImport_defaultMaterial::" + name));
		}

		// Load actors, meshes:
		for (auto& shape : obj_shapes)
		{
			Entity actor_entity = compfactory::MakeNodeStaticMeshActor(shape.name, root_entity);

			Entity mesh_entity = compfactory::MakeResGeometry(shape.name + "_mesh");
			
			RenderableComponent& renderable = *compfactory::GetRenderableComponent(actor_entity);
			renderable.SetGeometry(mesh_entity);

			GeometryComponent& mesh = *compfactory::GetGeometryComponent(mesh_entity);

			unordered_map<int, int> registered_materialIndices = {};
			unordered_map<size_t, uint32_t> unique_vertices = {};

			GeometryComponent::Primitive* mutable_primitive = nullptr;
			std::vector<XMFLOAT3>* vertex_positions = nullptr;
			std::vector<XMFLOAT3>* vertex_normals = nullptr;
			std::vector<XMFLOAT2>* vertex_uvSet0 = nullptr;
			std::vector<uint32_t>* indices = nullptr;
			for (size_t i = 0; i < shape.mesh.indices.size(); i += 3)
			{
				tinyobj::index_t reordered_indices[] = {
					shape.mesh.indices[i + 0],
					shape.mesh.indices[i + 1],
					shape.mesh.indices[i + 2],
				};

				// todo: option param would be better
				bool flipCulling = false;
				if (flipCulling)
				{
					reordered_indices[1] = shape.mesh.indices[i + 2];
					reordered_indices[2] = shape.mesh.indices[i + 1];
				}

				for (auto& index : reordered_indices)
				{
					XMFLOAT3 pos = XMFLOAT3(
						obj_attrib.vertices[index.vertex_index * 3 + 0],
						obj_attrib.vertices[index.vertex_index * 3 + 1],
						obj_attrib.vertices[index.vertex_index * 3 + 2]
					);

					XMFLOAT3 nor = XMFLOAT3(0, 0, 0);
					if (!obj_attrib.normals.empty())
					{
						nor = XMFLOAT3(
							obj_attrib.normals[index.normal_index * 3 + 0],
							obj_attrib.normals[index.normal_index * 3 + 1],
							obj_attrib.normals[index.normal_index * 3 + 2]
						);
					}

					XMFLOAT2 tex = XMFLOAT2(0, 0);
					if (index.texcoord_index >= 0 && !obj_attrib.texcoords.empty())
					{
						tex = XMFLOAT2(
							obj_attrib.texcoords[index.texcoord_index * 2 + 0],
							1 - obj_attrib.texcoords[index.texcoord_index * 2 + 1]
						);
					}

					int material_index = std::max(0, shape.mesh.material_ids[i / 3]); // this indexes the material library
					if (registered_materialIndices.count(material_index) == 0)
					//if (!registered_materialIndices.contains(material_index))	// c++ 20
					{
						size_t part_index = mesh.GetNumParts();
						registered_materialIndices[material_index] = (int)part_index;
						GeometryComponent::Primitive empty_primitive;
						mesh.CopyPrimitiveFrom(empty_primitive, part_index);
						renderable.SetMaterial(materials[material_index], part_index);
						//mesh.subsets.back().indexOffset = (uint32_t)mesh.indices.size();
						mutable_primitive = mesh.GetMutablePrimitive(part_index);
						mutable_primitive->SetPrimitiveType(GeometryComponent::PrimitiveType::TRIANGLES);

						vertex_positions = &mutable_primitive->GetMutableVtxPositions();
						vertex_normals = &mutable_primitive->GetMutableVtxNormals();
						vertex_uvSet0 = &mutable_primitive->GetMutableVtxUVSet0();
						indices = &mutable_primitive->GetMutableIdxPrimives();
					}
					assert(mutable_primitive);

					if (transform_to_LH)
					{
						pos.z *= -1;
						nor.z *= -1;
					}

					// eliminate duplicate vertices by means of hashing:
					size_t vertex_hash = 0;
					helper::hash_combine(vertex_hash, index.vertex_index);
					helper::hash_combine(vertex_hash, index.normal_index);
					helper::hash_combine(vertex_hash, index.texcoord_index);
					helper::hash_combine(vertex_hash, material_index);

					if (unique_vertices.count(vertex_hash) == 0)
					//if (!unique_vertices.contains(vertex_hash)) c++ 20
					{
						unique_vertices[vertex_hash] = (uint32_t)vertex_positions->size();
						vertex_positions->push_back(pos);
						vertex_normals->push_back(nor);
						vertex_uvSet0->push_back(tex);
					}
					indices->push_back(unique_vertices[vertex_hash]);
				}
			}
			mesh.UpdateRenderData();
		}

	}
	else
	{
		backlog::post("OBJ import failed! Check backlog for errors!", backlog::LogLevel::Error);
	}

	return root_entity;
}
