#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_RENDERABLE_COMP(COMP, RET) RenderableComponent* COMP = compfactory::GetRenderableComponent(componentVID_); \
	if (!COMP) {post(type_ + "(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	void VzBaseActor::SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetVisibleMask(layerBits, maskBits);
		UpdateTimeStamp();
	}
}

namespace vzm
{
	bool VzActor::IsRenderable()
	{
		GET_RENDERABLE_COMP(renderable, false);
		return renderable->IsRenderable();
	}
	void VzActor::SetGeometry(const GeometryVID vid)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetGeometry(vid);
		UpdateTimeStamp();
	}
	void VzActor::SetMaterial(const MaterialVID vid, const int slot)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetMaterial(vid, slot);
		UpdateTimeStamp();
	}
	void VzActor::SetMaterials(const std::vector<MaterialVID> vids)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetMaterials(vids);
		UpdateTimeStamp();
	}
	void VzActor::SetCastShadows(const bool enabled)
	{
		assert(0 && "TODO");
		UpdateTimeStamp();
	}
	void VzActor::SetReceiveShadows(const bool enabled)
	{
		assert(0 && "TODO");
		UpdateTimeStamp();
	}
	std::vector<MaterialVID> VzActor::GetMaterials()
	{
		GET_RENDERABLE_COMP(renderable, std::vector<MaterialVID>());
		return renderable->GetMaterials();
	}
	MaterialVID VzActor::GetMaterial(const int slot)
	{
		GET_RENDERABLE_COMP(renderable, INVALID_VID);
		return renderable->GetMaterial(slot);
	}
	GeometryVID VzActor::GetGeometry()
	{
		GET_RENDERABLE_COMP(renderable, INVALID_VID);
		return renderable->GetGeometry();
	}
}