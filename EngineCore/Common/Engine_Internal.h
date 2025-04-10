#pragma once
#include "Components/GComponents.h"

namespace vz
{
	namespace compfactory
	{
		NameComponent* CreateNameComponent(const Entity entity, const std::string& name);
		TransformComponent* CreateTransformComponent(const Entity entity);
		HierarchyComponent* CreateHierarchyComponent(const Entity entity, const Entity parent = INVALID_ENTITY);
		ColliderComponent* CreateColliderComponent(const Entity entity);
		MaterialComponent* CreateMaterialComponent(const Entity entity);
		GeometryComponent* CreateGeometryComponent(const Entity entity);
		TextureComponent* CreateTextureComponent(const Entity entity);
		VolumeComponent* CreateVolumeComponent(const Entity entity);
		LightComponent* CreateLightComponent(const Entity entity);
		CameraComponent* CreateCameraComponent(const Entity entity);
		SlicerComponent* CreateSlicerComponent(const Entity entity, const bool curvedSlicer = false);
		RenderableComponent* CreateRenderableComponent(const Entity entity);
		SpriteComponent* CreateSpriteComponent(const Entity entity);
		SpriteFontComponent* CreateSpriteFontComponent(const Entity entity);

		size_t Destroy(const Entity entity);
		size_t DestroyAll();
	}
}

namespace vzm
{
	std::recursive_mutex& GetEngineMutex();
	bool IsPendingSubmitCommand();
	void ResetPendingSubmitCommand();
	void CountPendingSubmitCommand();
	size_t GetCountPendingSubmitCommand();
}