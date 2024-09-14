#include "GComponents.h"
#include "Utils/Backlog.h"

namespace vz
{
	void GeometryComponent::MovePrimitives(const std::vector<Primitive>& primitives)
	{
		parts_.assign(primitives.size(), Primitive());
		for (size_t i = 0, n = primitives.size(); i < n; ++i)
		{
			Primitive& prim = parts_[i];
			prim.MoveFrom(primitives[i]);
		}
		updateAABB();
		isDirty_ = true;
	}
	void GeometryComponent::CopyPrimitives(const std::vector<Primitive>& primitives)
	{
		parts_ = primitives;
		updateAABB();
		isDirty_ = true;
	}
	void GeometryComponent::MovePrimitive(const Primitive& primitive, const size_t slot)
	{
		if (slot >= parts_.size()) {
			backlog::post("slot is over # of parts!", backlog::LogLevel::Error);
			return;
		}
		Primitive& prim = parts_[slot];
		prim.MoveFrom(primitive);
		updateAABB();
		isDirty_ = true;
	}
	void GeometryComponent::CopyPrimitive(const Primitive& primitive, const size_t slot)
	{
		if (slot >= parts_.size()) {
			backlog::post("slot is over # of parts!", backlog::LogLevel::Error);
			return;
		}
		parts_[slot] = primitive;
		updateAABB();
		isDirty_ = true;
	}
	bool GeometryComponent::GetPrimitive(const size_t slot, Primitive& primitive)	
	{
		if (slot >= parts_.size()) {
			backlog::post("slot is over # of parts!", backlog::LogLevel::Error);
			return false;
		}
		primitive = parts_[slot];
		updateAABB();
		return true; 
	}
	void GeometryComponent::updateAABB()
	{
		for (size_t i = 0, n = parts_.size(); i < n; ++i)
		{
			Primitive& prim = parts_[i];
			geometry::AABB part_aabb = prim.GetAABB();
			aabb_._max = math::Max(aabb_._max, part_aabb._max);
			aabb_._min = math::Min(aabb_._min, part_aabb._min);
		}
	}
}