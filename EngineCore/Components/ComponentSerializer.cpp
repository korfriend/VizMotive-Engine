#include "Components.h"
#include "Common/Archive.h"

namespace vz::component
{
	using namespace vz;
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
	}
}