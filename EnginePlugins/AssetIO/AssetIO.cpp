#include "AssetIO.h"

using namespace vz;
Entity CreateNode(
	std::vector<Entity>& actors,
	std::vector<Entity>& cameras, // obj does not include camera
	std::vector<Entity>& lights,
	std::vector<Entity>& geometries,
	std::vector<Entity>& materials,
	std::vector<Entity>& textures, 
	const NodeType ntype, const std::string& name, Entity parent_entity)
{
	Entity entity = compfactory::CreateNameComponent(0u, name)->GetEntity();
	switch (ntype)
	{
	case ACTOR:
		compfactory::CreateTransformComponent(entity);
		compfactory::CreateHierarchyComponent(entity)->SetParent(parent_entity);
		compfactory::CreateRenderableComponent(entity); // empty
		actors.push_back(entity);
		break;
	case CAMERA:
		compfactory::CreateTransformComponent(entity);
		compfactory::CreateHierarchyComponent(entity)->SetParent(parent_entity);
		compfactory::CreateCameraComponent(entity);
		cameras.push_back(entity);
		break;
	case LIGHT:
		compfactory::CreateTransformComponent(entity);
		compfactory::CreateHierarchyComponent(entity)->SetParent(parent_entity);
		compfactory::CreateLightComponent(entity);
		lights.push_back(entity);
		break;
	case GEOMETRY:
		compfactory::CreateGeometryComponent(entity);
		geometries.push_back(entity);
		break;
	case MATERIAL:
		compfactory::CreateMaterialComponent(entity);
		materials.push_back(entity);
		break;
	case TEXTURE:
		compfactory::CreateTextureComponent(entity);
		textures.push_back(entity);
		break;
	}
	return entity;
}