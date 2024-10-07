#include "GComponents.h"
#include "Utils/Backlog.h"

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
		isDirty_ = true;
		timeStampSetter_ = TimerNow;
	}
	void GeometryComponent::CopyPrimitives(const std::vector<Primitive>& primitives)
	{
		parts_ = primitives;
		updateAABB();
		isDirty_ = true;
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
			parts.assign((slot + 1) * 2, Primitive()); // * 2 for fast grow-up
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
		isDirty_ = true;
		timeStampSetter_ = TimerNow;
	}
	void GeometryComponent::CopyPrimitive(const Primitive& primitive, const size_t slot)
	{
		tryAssignParts(slot, parts_);
		parts_[slot] = primitive;
		updateAABB();
		isDirty_ = true;
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
	}
}