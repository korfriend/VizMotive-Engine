#include "Components.h"
#include "Common/Archive.h"
#include "Utils/ECS.h"

namespace vz
{
	using namespace vz::ecs;

	void NameComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
		if (archive.IsReadMode())
		{
			archive >> name;
		}
		else
		{
			archive << name;
		}
	}

	void TransformComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
		if (archive.IsReadMode())
		{
			archive >> isMatrixAutoUpdate_;
			archive >> scale_;
			archive >> rotation_;
			archive >> position_;
			archive >> local_;

			isDirty_ = true;
			if (isMatrixAutoUpdate_)
			{
				UpdateMatrix();
			}
		}
		else
		{
			archive << isMatrixAutoUpdate_; // maybe not needed just for dirtiness, but later might come handy if we have more persistent flags
			archive << scale_;
			archive << rotation_;
			archive << local_;
		}
	}

	void HierarchyComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
		SerializeEntity(archive, parentID, seri);
	}

	void MaterialComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
	}

	void GeometryComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
	}

	void TextureComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
	}

	void RenderableComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
	}

	void LightComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
	}

	void CameraComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
	}
}