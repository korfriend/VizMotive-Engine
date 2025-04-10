#include "VzEngineAPIs.h"
#include "Common/Engine_Internal.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"
#include "Utils/SimpleCollision.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_RENDERABLE_COMP(COMP, RET) RenderableComponent* COMP = compfactory::GetRenderableComponent(componentVID_); \
	if (!COMP) {post("RenderableComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	void VzActor::SetVisibleLayerMask(const uint32_t visibleLayerMask, const bool includeDescendants)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetVisibleLayerMask(visibleLayerMask);

		if (includeDescendants)
		{
			std::vector<ActorVID> children = GetChildren();
			for (size_t i = 0, n = children.size(); i < n; ++i)
			{
				VzActor* base_actor = (VzActor*)vzm::GetComponent(children[i]);
				assert(base_actor);
				base_actor->SetVisibleLayerMask(visibleLayerMask, true);
			}
		}
		UpdateTimeStamp();
	}
	void VzActor::SetVisibleLayer(const bool visible, const uint32_t layerBits, const bool includeDescendants)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetVisibleLayer(visible, layerBits);

		if (includeDescendants)
		{
			std::vector<ActorVID> children = GetChildren();
			for (size_t i = 0, n = children.size(); i < n; ++i)
			{
				VzActor* base_actor = (VzActor*)vzm::GetComponent(children[i]);
				assert(base_actor);
				base_actor->SetVisibleLayer(visible, layerBits, true);
			}
		}
		UpdateTimeStamp();
	}
	uint32_t VzActor::GetVisibleLayerMask() const
	{
		GET_RENDERABLE_COMP(renderable, 0u);
		return renderable->GetVisibleLayerMask();
	}
	bool VzActor::IsVisibleWith(const uint32_t layerBits) const
	{
		GET_RENDERABLE_COMP(renderable, false);
		return renderable->IsVisibleWith(layerBits);
	}

	bool VzActor::IsRenderable() const
	{
		GET_RENDERABLE_COMP(renderable, false);
		return renderable->IsMeshRenderable() || renderable->IsVolumeRenderable();
	}
	void VzActor::EnablePickable(const bool enabled, const bool includeDescendants)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->EnablePickable(enabled);
		
		if (includeDescendants)
		{
			std::vector<ActorVID> children = GetChildren();
			for (size_t i = 0, n = children.size(); i < n; ++i)
			{
				VzActor* base_actor = (VzActor*)vzm::GetComponent(children[i]);
				assert(base_actor);
				base_actor->EnablePickable(enabled, true);
			}
		}
		UpdateTimeStamp();
	}
	bool VzActor::IsPickable() const
	{
		GET_RENDERABLE_COMP(renderable, false);
		return renderable->IsPickable();
	}
}

namespace vzm
{
	void VzStaticMeshActor::SetGeometry(const GeometryVID vid)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetGeometry(vid);
		UpdateTimeStamp();
	}
	void VzStaticMeshActor::SetMaterial(const MaterialVID vid, const int slot)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetMaterial(vid, slot);
		UpdateTimeStamp();
	}
	void VzStaticMeshActor::SetMaterials(const std::vector<MaterialVID> vids)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetMaterials(vids);
		UpdateTimeStamp();
	}
	void VzStaticMeshActor::EnableCastShadows(const bool enabled)
	{
		assert(0 && "TODO");
		UpdateTimeStamp();
	}
	void VzStaticMeshActor::EnableReceiveShadows(const bool enabled)
	{
		assert(0 && "TODO");
		UpdateTimeStamp();
	}

	void VzStaticMeshActor::EnableSlicerSolidFill(const bool enabled)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->EnableSlicerSolidFill(enabled);
		UpdateTimeStamp();
	}

	void VzStaticMeshActor::EnableClipper(const bool clipBoxEnabled, const bool clipPlaneEnabled)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->EnableClipper(clipBoxEnabled, clipPlaneEnabled);
		UpdateTimeStamp();
	}

	void VzStaticMeshActor::EnableOutline(const bool enabled)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->EnableOutline(enabled);
		UpdateTimeStamp();
	}
	void VzStaticMeshActor::EnableUndercut(const bool enabled)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->EnableUndercut(enabled);
		UpdateTimeStamp();
	}

	void VzStaticMeshActor::SetClipPlane(const vfloat4& clipPlane)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetClipPlane(*(XMFLOAT4*)&clipPlane);
		UpdateTimeStamp();
	}
	void VzStaticMeshActor::SetClipBox(const vfloat4x4& clipBox)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetClipBox(*(XMFLOAT4X4*)&clipBox);
		UpdateTimeStamp();
	}
	bool VzStaticMeshActor::IsClipperEnabled(bool* clipBoxEnabled, bool* clipPlaneEnabled) const
	{
		GET_RENDERABLE_COMP(renderable, false);
		bool box_clipped = renderable->IsBoxClipperEnabled();
		bool plane_clipped = renderable->IsPlaneClipperEnabled();
		if (clipBoxEnabled) *clipBoxEnabled = box_clipped;
		if (clipPlaneEnabled) *clipPlaneEnabled = plane_clipped;
		return box_clipped || plane_clipped;
	}

	void VzStaticMeshActor::SetOutineThickness(const float v)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetOutineThickness(v);
		UpdateTimeStamp();
	}
	void VzStaticMeshActor::SetOutineColor(const vfloat3 v)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetOutineColor(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}
	void VzStaticMeshActor::SetOutineThreshold(const float v)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetOutineThreshold(v);
		UpdateTimeStamp();
	}
	void VzStaticMeshActor::SetUndercutDirection(const vfloat3 v)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetUndercutDirection(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}
	void VzStaticMeshActor::SetUndercutColor(const vfloat3 v)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetUndercutColor(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}

	bool VzStaticMeshActor::CollisionCheck(const ActorVID targetActorVID, int* partIndexSrc, int* partIndexTarget, int* triIndexSrc, int* triIndexTarget) const
	{
		GET_RENDERABLE_COMP(renderable, false);
		RenderableComponent* renderable_target = compfactory::GetRenderableComponent(targetActorVID);
		if (renderable_target == nullptr)
		{
			vzlog_error("Invalid Target Actor!");
			return false;
		}
		int partIndexSrc_v, partIndexTarget_v, triIndexSrc_v, triIndexTarget_v;
		bool detected = bvhcollision::CollisionPairwiseCheck(renderable->GetGeometry(), componentVID_, renderable_target->GetGeometry(), targetActorVID, partIndexSrc_v, triIndexSrc_v, partIndexTarget_v, triIndexTarget_v);
		if (partIndexSrc) *partIndexSrc = partIndexSrc_v;
		if (partIndexTarget) *partIndexTarget = partIndexTarget_v;
		if (triIndexSrc) *triIndexSrc = triIndexSrc_v;
		if (triIndexTarget) *triIndexTarget = triIndexTarget_v;
		return detected;
	}

	void VzStaticMeshActor::DebugRender(const std::string& debugScript)
	{

	}

	std::vector<MaterialVID> VzStaticMeshActor::GetMaterials() const
	{
		GET_RENDERABLE_COMP(renderable, std::vector<MaterialVID>());
		return renderable->GetMaterials();
	}
	MaterialVID VzStaticMeshActor::GetMaterial(const int slot) const
	{
		GET_RENDERABLE_COMP(renderable, INVALID_VID);
		return renderable->GetMaterial(slot);
	}
	GeometryVID VzStaticMeshActor::GetGeometry() const
	{
		GET_RENDERABLE_COMP(renderable, INVALID_VID);
		return renderable->GetGeometry();
	}

	void VzStaticMeshActor::AssignCollider()
	{
		if (compfactory::ContainColliderComponent(componentVID_))
		{
			vzlog_warning("%s already has collider!", GetName().c_str());
			return;
		}
		compfactory::CreateColliderComponent(componentVID_);
	}

	bool VzStaticMeshActor::HasCollider() const
	{
		return compfactory::ContainColliderComponent(componentVID_);
	}

	bool VzStaticMeshActor::ColliderCollisionCheck(const ActorVID targetActorVID) const
	{
		assert(0 && "TODO");
		return false;
	}
}