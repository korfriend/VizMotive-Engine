#pragma once
#include "Math.h"

#include <limits>
#include <cassert>
#include <functional>

namespace vz::geometrics
{
	struct Sphere;
	struct Ray;
	struct AABB;
	struct Capsule;
	struct Plane;

	struct AABB
	{
		enum INTERSECTION_TYPE
		{
			OUTSIDE,
			INTERSECTS,
			INSIDE,
		};

		XMFLOAT3 _min;
		uint32_t layerMask = ~0u;
		XMFLOAT3 _max;
		uint32_t userdata = 0;

		AABB(
			const XMFLOAT3& _min = XMFLOAT3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()),
			const XMFLOAT3& _max = XMFLOAT3(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest())
		) : _min(_min), _max(_max) {}
		inline void createFromHalfWidth(const XMFLOAT3& center, const XMFLOAT3& halfwidth);
		inline AABB transform(const XMMATRIX& mat) const;
		inline AABB transform(const XMFLOAT4X4& mat) const;
		inline XMFLOAT3 getCenter() const;
		inline XMFLOAT3 getWidth() const;
		inline XMFLOAT3 getHalfWidth() const;
		inline XMMATRIX getAsBoxMatrix() const;
		inline XMMATRIX getUnormRemapMatrix() const;
		inline float getArea() const;
		inline float getRadius() const;
		inline INTERSECTION_TYPE intersects2D(const AABB& b) const;
		inline INTERSECTION_TYPE intersects(const AABB& b) const;
		inline bool intersects(const XMFLOAT3& p) const;
		inline bool intersects(const Ray& ray) const;
		inline bool intersects(const Sphere& sphere) const;
		inline bool intersects(const BoundingFrustum& frustum) const;
		inline AABB operator* (float a);
		inline static AABB Merge(const AABB& a, const AABB& b);

		constexpr XMFLOAT3 getMin() const { return _min; }
		constexpr XMFLOAT3 getMax() const { return _max; }
		constexpr XMFLOAT3 corner(int index) const
		{
			switch (index)
			{
			case 0: return _min;
			case 1: return XMFLOAT3(_min.x, _max.y, _min.z);
			case 2: return XMFLOAT3(_min.x, _max.y, _max.z);
			case 3: return XMFLOAT3(_min.x, _min.y, _max.z);
			case 4: return XMFLOAT3(_max.x, _min.y, _min.z);
			case 5: return XMFLOAT3(_max.x, _max.y, _min.z);
			case 6: return _max;
			case 7: return XMFLOAT3(_max.x, _min.y, _max.z);
			}
			assert(0);
			return XMFLOAT3(0, 0, 0);
		}
		constexpr bool IsValid() const
		{
			if (_min.x > _max.x || _min.y > _max.y || _min.z > _max.z)
				return false;
			return true;
		}

		//void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};
	struct Sphere
	{
		XMFLOAT3 center;
		float radius;
		Sphere() :center(XMFLOAT3(0, 0, 0)), radius(0) {}
		Sphere(const XMFLOAT3& c, float r) :center(c), radius(r)
		{
			assert(radius >= 0);
		}
		inline bool intersects(const XMVECTOR& P) const;
		inline bool intersects(const XMFLOAT3& P) const;
		inline bool intersects(const AABB& b) const;
		inline bool intersects(const Sphere& b) const;
		inline bool intersects(const Sphere& b, float& dist) const;
		inline bool intersects(const Sphere& b, float& dist, XMFLOAT3& direction) const;
		inline bool intersects(const Capsule& b) const;
		inline bool intersects(const Capsule& b, float& dist) const;
		inline bool intersects(const Capsule& b, float& dist, XMFLOAT3& direction) const;
		inline bool intersects(const Plane& b) const;
		inline bool intersects(const Plane& b, float& dist) const;
		inline bool intersects(const Plane& b, float& dist, XMFLOAT3& direction) const;
		inline bool intersects(const Ray& b) const;
		inline bool intersects(const Ray& b, float& dist) const;
		inline bool intersects(const Ray& b, float& dist, XMFLOAT3& direction) const;

		// Construct a matrix that will orient to position according to surface normal:
		inline XMFLOAT4X4 GetPlacementOrientation(const XMFLOAT3& position, const XMFLOAT3& normal) const;
	};
	struct Capsule
	{
		XMFLOAT3 base = XMFLOAT3(0, 0, 0);
		XMFLOAT3 tip = XMFLOAT3(0, 0, 0);
		float radius = 0;
		Capsule() = default;
		Capsule(const XMFLOAT3& base, const XMFLOAT3& tip, float radius) :base(base), tip(tip), radius(radius)
		{
			assert(radius >= 0);
		}
		Capsule(XMVECTOR base, XMVECTOR tip, float radius) :radius(radius)
		{
			assert(radius >= 0);
			XMStoreFloat3(&this->base, base);
			XMStoreFloat3(&this->tip, tip);
		}
		Capsule(const Sphere& sphere, float height) :
			base(XMFLOAT3(sphere.center.x, sphere.center.y - sphere.radius, sphere.center.z)),
			tip(XMFLOAT3(base.x, base.y + height, base.z)),
			radius(sphere.radius)
		{
			assert(radius >= 0);
		}
		inline AABB getAABB() const
		{
			XMFLOAT3 halfWidth = XMFLOAT3(radius, radius, radius);
			AABB base_aabb;
			base_aabb.createFromHalfWidth(base, halfWidth);
			AABB tip_aabb;
			tip_aabb.createFromHalfWidth(tip, halfWidth);
			AABB result = AABB::Merge(base_aabb, tip_aabb);
			assert(result.IsValid());
			return result;
		}
		inline bool intersects(const Capsule& b, XMFLOAT3& position, XMFLOAT3& incident_normal, float& penetration_depth) const;
		inline bool intersects(const Sphere& b) const;
		inline bool intersects(const Sphere& b, float& dist) const;
		inline bool intersects(const Sphere& b, float& dist, XMFLOAT3& direction) const;
		inline bool intersects(const Plane& b) const;
		inline bool intersects(const Plane& b, float& dist) const;
		inline bool intersects(const Plane& b, float& dist, XMFLOAT3& direction) const;
		inline bool intersects(const Ray& b) const;
		inline bool intersects(const Ray& b, float& dist) const;
		inline bool intersects(const Ray& b, float& dist, XMFLOAT3& direction) const;
		inline bool intersects(const XMFLOAT3& point) const;

		// Construct a matrix that will orient to position according to surface normal:
		inline XMFLOAT4X4 GetPlacementOrientation(const XMFLOAT3& position, const XMFLOAT3& normal) const;
	};
	struct Plane
	{
		XMFLOAT3 origin = {};
		XMFLOAT3 normal = {};
		XMFLOAT4X4 projection = vz::math::IDENTITY_MATRIX;

		inline bool intersects(const Sphere& b) const;
		inline bool intersects(const Sphere& b, float& dist) const;
		inline bool intersects(const Sphere& b, float& dist, XMFLOAT3& direction) const;
		inline bool intersects(const Capsule& b) const;
		inline bool intersects(const Capsule& b, float& dist) const;
		inline bool intersects(const Capsule& b, float& dist, XMFLOAT3& direction) const;
		inline bool intersects(const Ray& b) const;
		inline bool intersects(const Ray& b, float& dist) const;
		inline bool intersects(const Ray& b, float& dist, XMFLOAT3& direction) const;
	};
	struct Ray
	{
		XMFLOAT3 origin;
		float TMin = 0;
		XMFLOAT3 direction;
		float TMax = std::numeric_limits<float>::max();
		XMFLOAT3 direction_inverse;

		Ray(const XMFLOAT3& newOrigin = XMFLOAT3(0, 0, 0), const XMFLOAT3& newDirection = XMFLOAT3(0, 0, 1), float newTMin = 0, float newTMax = std::numeric_limits<float>::max()) :
			Ray(XMLoadFloat3(&newOrigin), XMLoadFloat3(&newDirection), newTMin, newTMax)
		{}
		Ray(const XMVECTOR& newOrigin, const XMVECTOR& newDirection, float newTMin = 0, float newTMax = std::numeric_limits<float>::max())
		{
			XMStoreFloat3(&origin, newOrigin);
			XMStoreFloat3(&direction, newDirection);
			XMStoreFloat3(&direction_inverse, XMVectorReciprocal(newDirection));
			TMin = newTMin;
			TMax = newTMax;
		}
		inline bool intersects(const AABB& b) const;
		inline bool intersects(const Sphere& b) const;
		inline bool intersects(const Sphere& b, float& dist) const;
		inline bool intersects(const Sphere& b, float& dist, XMFLOAT3& direction) const;
		inline bool intersects(const Capsule& b) const;
		inline bool intersects(const Capsule& b, float& dist) const;
		inline bool intersects(const Capsule& b, float& dist, XMFLOAT3& direction) const;
		inline bool intersects(const Plane& b) const;
		inline bool intersects(const Plane& b, float& dist) const;
		inline bool intersects(const Plane& b, float& dist, XMFLOAT3& direction) const;

		inline void CreateFromPoints(const XMFLOAT3& a, const XMFLOAT3& b);

		// Construct a matrix that will orient to position according to surface normal:
		inline XMFLOAT4X4 GetPlacementOrientation(const XMFLOAT3& position, const XMFLOAT3& normal) const;
	};

	struct Frustum
	{
		XMFLOAT4 planes[6];

		inline void Create(const XMMATRIX& viewProjection);

		inline bool CheckPoint(const XMFLOAT3&) const;
		inline bool CheckSphere(const XMFLOAT3&, float) const;

		enum BoxFrustumIntersect
		{
			BOX_FRUSTUM_OUTSIDE,
			BOX_FRUSTUM_INTERSECTS,
			BOX_FRUSTUM_INSIDE,
		};
		inline BoxFrustumIntersect CheckBox(const AABB& box) const;
		inline bool CheckBoxFast(const AABB& box) const;

		inline const XMFLOAT4& getNearPlane() const;
		inline const XMFLOAT4& getFarPlane() const;
		inline const XMFLOAT4& getLeftPlane() const;
		inline const XMFLOAT4& getRightPlane() const;
		inline const XMFLOAT4& getTopPlane() const;
		inline const XMFLOAT4& getBottomPlane() const;
	};

	class Hitbox2D
	{
	public:
		XMFLOAT2 pos;
		XMFLOAT2 siz;

		Hitbox2D() :pos(XMFLOAT2(0, 0)), siz(XMFLOAT2(0, 0)) {}
		Hitbox2D(const XMFLOAT2& newPos, const XMFLOAT2 newSiz) :pos(newPos), siz(newSiz) {}
		~Hitbox2D() {};

		inline bool intersects(const XMFLOAT2& b) const;
		inline bool intersects(const Hitbox2D& b) const;
	};

}

namespace vz::geometrics
{
	void AABB::createFromHalfWidth(const XMFLOAT3& center, const XMFLOAT3& halfwidth)
	{
		_min = XMFLOAT3(center.x - halfwidth.x, center.y - halfwidth.y, center.z - halfwidth.z);
		_max = XMFLOAT3(center.x + halfwidth.x, center.y + halfwidth.y, center.z + halfwidth.z);
	}
	AABB AABB::transform(const XMMATRIX& mat) const
	{
		const XMVECTOR vcorners[8] = {
			XMVector3Transform(XMLoadFloat3(&_min), mat),
			XMVector3Transform(XMVectorSet(_min.x, _max.y, _min.z, 1), mat),
			XMVector3Transform(XMVectorSet(_min.x, _max.y, _max.z, 1), mat),
			XMVector3Transform(XMVectorSet(_min.x, _min.y, _max.z, 1), mat),
			XMVector3Transform(XMVectorSet(_max.x, _min.y, _min.z, 1), mat),
			XMVector3Transform(XMVectorSet(_max.x, _max.y, _min.z, 1), mat),
			XMVector3Transform(XMLoadFloat3(&_max), mat),
			XMVector3Transform(XMVectorSet(_max.x, _min.y, _max.z, 1), mat),
		};
		XMVECTOR vmin = vcorners[0];
		XMVECTOR vmax = vcorners[0];
		vmin = XMVectorMin(vmin, vcorners[1]);
		vmax = XMVectorMax(vmax, vcorners[1]);
		vmin = XMVectorMin(vmin, vcorners[2]);
		vmax = XMVectorMax(vmax, vcorners[2]);
		vmin = XMVectorMin(vmin, vcorners[3]);
		vmax = XMVectorMax(vmax, vcorners[3]);
		vmin = XMVectorMin(vmin, vcorners[4]);
		vmax = XMVectorMax(vmax, vcorners[4]);
		vmin = XMVectorMin(vmin, vcorners[5]);
		vmax = XMVectorMax(vmax, vcorners[5]);
		vmin = XMVectorMin(vmin, vcorners[6]);
		vmax = XMVectorMax(vmax, vcorners[6]);
		vmin = XMVectorMin(vmin, vcorners[7]);
		vmax = XMVectorMax(vmax, vcorners[7]);

		XMFLOAT3 min, max;
		XMStoreFloat3(&min, vmin);
		XMStoreFloat3(&max, vmax);
		return AABB(min, max);
	}
	AABB AABB::transform(const XMFLOAT4X4& mat) const
	{
		return transform(XMLoadFloat4x4(&mat));
	}
	XMFLOAT3 AABB::getCenter() const
	{
		return XMFLOAT3((_min.x + _max.x) * 0.5f, (_min.y + _max.y) * 0.5f, (_min.z + _max.z) * 0.5f);
	}
	XMFLOAT3 AABB::getWidth() const
	{
		return XMFLOAT3(abs(_max.x - _min.x), abs(_max.y - _min.y), abs(_max.z - _min.z));
	}
	XMFLOAT3 AABB::getHalfWidth() const
	{
		XMFLOAT3 center = getCenter();
		return XMFLOAT3(abs(_max.x - center.x), abs(_max.y - center.y), abs(_max.z - center.z));
	}
	XMMATRIX AABB::AABB::getAsBoxMatrix() const
	{
		XMFLOAT3 ext = getHalfWidth();
		XMMATRIX sca = XMMatrixScaling(ext.x, ext.y, ext.z);
		XMFLOAT3 pos = getCenter();
		XMMATRIX tra = XMMatrixTranslation(pos.x, pos.y, pos.z);

		return sca * tra;
	}
	XMMATRIX AABB::AABB::getUnormRemapMatrix() const
	{
		return
			XMMatrixScaling(_max.x - _min.x, _max.y - _min.y, _max.z - _min.z) *
			XMMatrixTranslation(_min.x, _min.y, _min.z)
			;
	}
	float AABB::getArea() const
	{
		XMFLOAT3 _min = getMin();
		XMFLOAT3 _max = getMax();
		return (_max.x - _min.x) * (_max.y - _min.y) * (_max.z - _min.z);
	}
	float AABB::getRadius() const
	{
		XMFLOAT3 abc = getHalfWidth();
		return std::sqrt(std::pow(std::sqrt(std::pow(abc.x, 2.0f) + std::pow(abc.y, 2.0f)), 2.0f) + std::pow(abc.z, 2.0f));
	}
	AABB::INTERSECTION_TYPE AABB::intersects(const AABB& b) const
	{
		if (!IsValid() || !b.IsValid())
			return OUTSIDE;

		XMFLOAT3 aMin = getMin(), aMax = getMax();
		XMFLOAT3 bMin = b.getMin(), bMax = b.getMax();

		if (bMin.x >= aMin.x && bMax.x <= aMax.x &&
			bMin.y >= aMin.y && bMax.y <= aMax.y &&
			bMin.z >= aMin.z && bMax.z <= aMax.z)
		{
			return INSIDE;
		}

		if (aMax.x < bMin.x || aMin.x > bMax.x)
			return OUTSIDE;
		if (aMax.y < bMin.y || aMin.y > bMax.y)
			return OUTSIDE;
		if (aMax.z < bMin.z || aMin.z > bMax.z)
			return OUTSIDE;

		return INTERSECTS;
	}
	AABB::INTERSECTION_TYPE AABB::intersects2D(const AABB& b) const
	{
		if (!IsValid() || !b.IsValid())
			return OUTSIDE;

		XMFLOAT3 aMin = getMin(), aMax = getMax();
		XMFLOAT3 bMin = b.getMin(), bMax = b.getMax();

		if (bMin.x >= aMin.x && bMax.x <= aMax.x &&
			bMin.y >= aMin.y && bMax.y <= aMax.y)
		{
			return INSIDE;
		}

		if (aMax.x < bMin.x || aMin.x > bMax.x)
			return OUTSIDE;
		if (aMax.y < bMin.y || aMin.y > bMax.y)
			return OUTSIDE;

		return INTERSECTS;
	}
	bool AABB::intersects(const XMFLOAT3& p) const
	{
		if (!IsValid())
			return false;
		if (p.x > _max.x) return false;
		if (p.x < _min.x) return false;
		if (p.y > _max.y) return false;
		if (p.y < _min.y) return false;
		if (p.z > _max.z) return false;
		if (p.z < _min.z) return false;
		return true;
	}
	bool AABB::intersects(const Ray& ray) const
	{
		if (!IsValid())
			return false;
		if (intersects(ray.origin))
			return true;

		XMFLOAT3 MIN = getMin();
		XMFLOAT3 MAX = getMax();

		float tx1 = (MIN.x - ray.origin.x) * ray.direction_inverse.x;
		float tx2 = (MAX.x - ray.origin.x) * ray.direction_inverse.x;

		float tmin = std::min(tx1, tx2);
		float tmax = std::max(tx1, tx2);
		if (ray.TMax < tmin || ray.TMin > tmax)
			return false;

		float ty1 = (MIN.y - ray.origin.y) * ray.direction_inverse.y;
		float ty2 = (MAX.y - ray.origin.y) * ray.direction_inverse.y;

		tmin = std::max(tmin, std::min(ty1, ty2));
		tmax = std::min(tmax, std::max(ty1, ty2));
		if (ray.TMax < tmin || ray.TMin > tmax)
			return false;

		float tz1 = (MIN.z - ray.origin.z) * ray.direction_inverse.z;
		float tz2 = (MAX.z - ray.origin.z) * ray.direction_inverse.z;

		tmin = std::max(tmin, std::min(tz1, tz2));
		tmax = std::min(tmax, std::max(tz1, tz2));
		if (ray.TMax < tmin || ray.TMin > tmax)
			return false;

		return tmax >= tmin;
	}
	bool AABB::intersects(const Sphere& sphere) const
	{
		if (!IsValid())
			return false;
		return sphere.intersects(*this);
	}
	bool AABB::intersects(const BoundingFrustum& frustum) const
	{
		if (!IsValid())
			return false;
		BoundingBox bb = BoundingBox(getCenter(), getHalfWidth());
		bool intersection = frustum.Intersects(bb);
		return intersection;
	}
	AABB AABB::operator* (float a)
	{
		XMFLOAT3 min = getMin();
		XMFLOAT3 max = getMax();
		min.x *= a;
		min.y *= a;
		min.z *= a;
		max.x *= a;
		max.y *= a;
		max.z *= a;
		return AABB(min, max);
	}
	AABB AABB::Merge(const AABB& a, const AABB& b)
	{
		return AABB(vz::math::Min(a.getMin(), b.getMin()), vz::math::Max(a.getMax(), b.getMax()));
	}

	bool Sphere::intersects(const XMVECTOR& P) const
	{
		float distsq = vz::math::DistanceSquared(XMLoadFloat3(&center), P);
		float radiussq = radius * radius;
		return distsq < radiussq;
	}
	bool Sphere::intersects(const XMFLOAT3& P) const
	{
		float distsq = vz::math::DistanceSquared(center, P);
		float radiussq = radius * radius;
		return distsq < radiussq;
	}
	bool Sphere::intersects(const AABB& b) const
	{
		if (!b.IsValid())
			return false;
		XMFLOAT3 min = b.getMin();
		XMFLOAT3 max = b.getMax();
		XMFLOAT3 closestPointInAabb = vz::math::Min(vz::math::Max(center, min), max);
		float distanceSquared = vz::math::DistanceSquared(closestPointInAabb, center);
		return distanceSquared < (radius * radius);
	}
	bool Sphere::intersects(const Sphere& b)const
	{
		float dist = 0;
		return intersects(b, dist);
	}
	bool Sphere::intersects(const Sphere& b, float& dist) const
	{
		dist = vz::math::Distance(center, b.center);
		dist = dist - radius - b.radius;
		return dist < 0;
	}
	bool Sphere::intersects(const Sphere& b, float& dist, XMFLOAT3& direction) const
	{
		XMVECTOR A = XMLoadFloat3(&center);
		XMVECTOR B = XMLoadFloat3(&b.center);
		XMVECTOR DIR = A - B;
		XMVECTOR DIST = XMVector3Length(DIR);
		DIR = DIR / DIST;
		XMStoreFloat3(&direction, DIR);
		dist = XMVectorGetX(DIST);
		dist = dist - radius - b.radius;
		return dist < 0;
	}
	bool Sphere::intersects(const Capsule& b) const
	{
		float dist = 0;
		return intersects(b, dist);
	}
	bool Sphere::intersects(const Capsule& b, float& dist) const
	{
		XMVECTOR A = XMLoadFloat3(&b.base);
		XMVECTOR B = XMLoadFloat3(&b.tip);
		XMVECTOR N = XMVector3Normalize(A - B);
		A -= N * b.radius;
		B += N * b.radius;
		XMVECTOR C = XMLoadFloat3(&center);
		dist = vz::math::GetPointSegmentDistance(C, A, B);
		dist = dist - radius - b.radius;
		return dist < 0;
	}
	bool Sphere::intersects(const Capsule& b, float& dist, XMFLOAT3& direction) const
	{
		XMVECTOR A = XMLoadFloat3(&b.base);
		XMVECTOR B = XMLoadFloat3(&b.tip);
		XMVECTOR N = XMVector3Normalize(A - B);
		A -= N * b.radius;
		B += N * b.radius;
		XMVECTOR C = XMLoadFloat3(&center);
		XMVECTOR D = C - vz::math::ClosestPointOnLineSegment(A, B, C);
		dist = XMVectorGetX(XMVector3Length(D));
		D /= dist;
		XMStoreFloat3(&direction, D);
		dist = dist - radius - b.radius;
		return dist < 0;
	}
	bool Sphere::intersects(const Plane& b) const
	{
		return b.intersects(*this);
	}
	bool Sphere::intersects(const Plane& b, float& dist) const
	{
		return b.intersects(*this, dist);
	}
	bool Sphere::intersects(const Plane& b, float& dist, XMFLOAT3& direction) const
	{
		return b.intersects(*this, dist, direction);
	}
	bool Sphere::intersects(const Ray& b) const
	{
		float dist;
		XMFLOAT3 direction;
		return intersects(b, dist, direction);
	}
	bool Sphere::intersects(const Ray& b, float& dist) const
	{
		XMFLOAT3 direction;
		return intersects(b, dist, direction);
	}
	bool Sphere::intersects(const Ray& b, float& dist, XMFLOAT3& direction) const
	{
		XMVECTOR C = XMLoadFloat3(&center);
		XMVECTOR O = XMLoadFloat3(&b.origin);
		XMVECTOR D = XMLoadFloat3(&b.direction);
		XMVECTOR OC = O - C;
		float B = XMVectorGetX(XMVector3Dot(OC, D));
		float c = XMVectorGetX(XMVector3Dot(OC, OC)) - radius * radius;
		float discr = B * B - c;
		if (discr > 0)
		{
			float discrSq = std::sqrt(discr);

			float t = (-B - discrSq);
			if (t<b.TMax && t>b.TMin)
			{
				XMVECTOR P = O + D * t;
				XMVECTOR N = XMVector3Normalize(P - C);
				dist = t;
				XMStoreFloat3(&direction, N);
				return true;
			}

			t = (-B + discrSq);
			if (t<b.TMax && t>b.TMin)
			{
				XMVECTOR P = O + D * t;
				XMVECTOR N = XMVector3Normalize(P - C);
				dist = t;
				XMStoreFloat3(&direction, N);
			}
		}
		return false;
	}
	XMFLOAT4X4 Sphere::GetPlacementOrientation(const XMFLOAT3& position, const XMFLOAT3& normal) const
	{
		XMVECTOR N = XMLoadFloat3(&normal);
		XMVECTOR P = XMLoadFloat3(&position);
		XMVECTOR E = XMLoadFloat3(&center) - P;
		XMVECTOR T = XMVector3Normalize(XMVector3Cross(N, P - E));
		XMVECTOR B = XMVector3Normalize(XMVector3Cross(T, N));
		XMMATRIX M = { T, N, B, P };
		XMFLOAT4X4 orientation;
		XMStoreFloat4x4(&orientation, M);
		return orientation;
	}



	bool Capsule::intersects(const Capsule& other, XMFLOAT3& position, XMFLOAT3& incident_normal, float& penetration_depth) const
	{
		if (getAABB().intersects(other.getAABB()) == AABB::INTERSECTION_TYPE::OUTSIDE)
			return false;

		XMVECTOR a_Base = XMLoadFloat3(&base);
		XMVECTOR a_Tip = XMLoadFloat3(&tip);
		XMVECTOR a_Radius = XMVectorReplicate(radius);
		XMVECTOR a_Normal = XMVector3Normalize(a_Tip - a_Base);
		XMVECTOR a_LineEndOffset = a_Normal * a_Radius;
		XMVECTOR a_A = a_Base + a_LineEndOffset;
		XMVECTOR a_B = a_Tip - a_LineEndOffset;

		XMVECTOR b_Base = XMLoadFloat3(&other.base);
		XMVECTOR b_Tip = XMLoadFloat3(&other.tip);
		XMVECTOR b_Radius = XMVectorReplicate(other.radius);
		XMVECTOR b_Normal = XMVector3Normalize(b_Tip - b_Base);
		XMVECTOR b_LineEndOffset = b_Normal * b_Radius;
		XMVECTOR b_A = b_Base + b_LineEndOffset;
		XMVECTOR b_B = b_Tip - b_LineEndOffset;

		// Vectors between line endpoints:
		XMVECTOR v0 = b_A - a_A;
		XMVECTOR v1 = b_B - a_A;
		XMVECTOR v2 = b_A - a_B;
		XMVECTOR v3 = b_B - a_B;

		// Distances (squared) between line endpoints:
		float d0 = XMVectorGetX(XMVector3LengthSq(v0));
		float d1 = XMVectorGetX(XMVector3LengthSq(v1));
		float d2 = XMVectorGetX(XMVector3LengthSq(v2));
		float d3 = XMVectorGetX(XMVector3LengthSq(v3));

		// Select best potential endpoint on capsule A:
		XMVECTOR bestA;
		if (d2 < d0 || d2 < d1 || d3 < d0 || d3 < d1)
		{
			bestA = a_B;
		}
		else
		{
			bestA = a_A;
		}

		// Select point on capsule B line segment nearest to best potential endpoint on A capsule:
		XMVECTOR bestB = vz::math::ClosestPointOnLineSegment(b_A, b_B, bestA);

		// Now do the same for capsule A segment:
		bestA = vz::math::ClosestPointOnLineSegment(a_A, a_B, bestB);

		// Finally, sphere collision:
		XMVECTOR N = bestA - bestB;
		XMVECTOR len = XMVector3Length(N);
		N /= len;
		float dist = XMVectorGetX(len);

		XMStoreFloat3(&position, bestA - N * radius);
		XMStoreFloat3(&incident_normal, N);
		penetration_depth = radius + other.radius - dist;

		return penetration_depth > 0;
	}
	bool Capsule::intersects(const Sphere& b) const
	{
		return b.intersects(*this);
	}
	bool Capsule::intersects(const Sphere& b, float& dist) const
	{
		bool intersects = b.intersects(*this, dist);
		dist = -dist;
		return intersects;
	}
	bool Capsule::intersects(const Sphere& b, float& dist, XMFLOAT3& direction) const
	{
		bool intersects = b.intersects(*this, dist, direction);
		dist = -dist;
		direction.x *= -1;
		direction.y *= -1;
		direction.z *= -1;
		return intersects;
	}
	bool Capsule::intersects(const Plane& b) const
	{
		return b.intersects(*this);
	}
	bool Capsule::intersects(const Plane& b, float& dist) const
	{
		bool intersects = b.intersects(*this, dist);
		dist = -dist;
		return intersects;
	}
	bool Capsule::intersects(const Plane& b, float& dist, XMFLOAT3& direction) const
	{
		bool intersects = b.intersects(*this, dist, direction);
		dist = -dist;
		return intersects;
	}
	bool Capsule::intersects(const Ray& ray) const
	{
		float dist;
		XMFLOAT3 direction;
		return intersects(ray, dist, direction);
	}
	bool Capsule::intersects(const Ray& ray, float& dist) const
	{
		XMFLOAT3 direction;
		return intersects(ray, dist, direction);
	}
	bool Capsule::intersects(const Ray& ray, float& dist, XMFLOAT3& direction) const
	{
		XMVECTOR A = XMLoadFloat3(&base);
		XMVECTOR B = XMLoadFloat3(&tip);
		XMVECTOR L = XMVector3Normalize(A - B);
		A -= L * radius;
		B += L * radius;
		XMVECTOR O = XMLoadFloat3(&ray.origin);
		XMVECTOR D = XMLoadFloat3(&ray.direction);
		XMVECTOR C = XMVector3Normalize(XMVector3Cross(L, A - O));
		XMVECTOR N = XMVector3Cross(L, C);
		XMVECTOR Plane = XMPlaneFromPointNormal(A, N);
		XMVECTOR I = XMPlaneIntersectLine(Plane, O, O + D * ray.TMax);
		XMVECTOR P = vz::math::ClosestPointOnLineSegment(A, B, I);

		Sphere sphere;
		XMStoreFloat3(&sphere.center, P);
		sphere.radius = radius;
		return sphere.intersects(ray, dist, direction);
	}
	bool Capsule::intersects(const XMFLOAT3& point) const
	{
		XMVECTOR Base = XMLoadFloat3(&base);
		XMVECTOR Tip = XMLoadFloat3(&tip);
		XMVECTOR Radius = XMVectorReplicate(radius);
		XMVECTOR Normal = XMVector3Normalize(Tip - Base);
		XMVECTOR LineEndOffset = Normal * Radius;
		XMVECTOR A = Base + LineEndOffset;
		XMVECTOR B = Tip - LineEndOffset;

		XMVECTOR P = XMLoadFloat3(&point);

		XMVECTOR C = vz::math::ClosestPointOnLineSegment(A, B, P);

		return XMVectorGetX(XMVector3Length(P - C)) <= radius;
	}
	XMFLOAT4X4 Capsule::GetPlacementOrientation(const XMFLOAT3& position, const XMFLOAT3& normal) const
	{
		const XMVECTOR Base = XMLoadFloat3(&base);
		const XMVECTOR Tip = XMLoadFloat3(&tip);
		const XMVECTOR Axis = XMVector3Normalize(Tip - Base);
		XMVECTOR N = XMLoadFloat3(&normal);
		XMVECTOR P = XMLoadFloat3(&position);
		XMVECTOR E = Axis;
		XMVECTOR T = XMVector3Normalize(XMVector3Cross(N, P - E));
		XMVECTOR Binorm = XMVector3Normalize(XMVector3Cross(T, N));
		XMMATRIX M = { T, N, Binorm, P };
		XMFLOAT4X4 orientation;
		XMStoreFloat4x4(&orientation, M);
		return orientation;
	}



	bool Plane::intersects(const Sphere& b) const
	{
		float dist;
		XMFLOAT3 direction;
		return intersects(b, dist, direction);
	}
	bool Plane::intersects(const Sphere& b, float& dist) const
	{
		XMFLOAT3 direction;
		return intersects(b, dist, direction);
	}
	bool Plane::intersects(const Sphere& b, float& dist, XMFLOAT3& direction) const
	{
		XMVECTOR C = XMLoadFloat3(&b.center);
		dist = vz::math::GetPlanePointDistance(XMLoadFloat3(&origin), XMLoadFloat3(&normal), C);
		direction = normal;
		if (dist < 0)
		{
			direction.x *= -1;
			direction.y *= -1;
			direction.z *= -1;
			dist = std::abs(dist);
		}
		dist = dist - b.radius;
		if (dist < 0)
		{
			XMMATRIX planeProjection = XMLoadFloat4x4(&projection);
			XMVECTOR clipSpacePos = XMVector3Transform(C, planeProjection);
			XMVECTOR uvw = clipSpacePos * XMVectorSet(0.5f, -0.5f, 0.5f, 1) + XMVectorSet(0.5f, 0.5f, 0.5f, 0);
			XMVECTOR uvw_sat = XMVectorSaturate(uvw);
			XMVECTOR uvw_diff = XMVectorAbs(uvw - uvw_sat);
			if (XMVectorGetX(uvw_diff) > std::numeric_limits<float>::epsilon())
				dist = 0; // force no collision
			else if (XMVectorGetY(uvw_diff) > std::numeric_limits<float>::epsilon())
				dist = 0; // force no collision
			else if (XMVectorGetZ(uvw_diff) > std::numeric_limits<float>::epsilon())
				dist = 0; // force no collision
		}
		return dist < 0;
	}
	bool Plane::intersects(const Capsule& b) const
	{
		float dist;
		XMFLOAT3 direction;
		return intersects(b, dist, direction);
	}
	bool Plane::intersects(const Capsule& b, float& dist) const
	{
		XMFLOAT3 direction;
		return intersects(b, dist, direction);
	}
	bool Plane::intersects(const Capsule& b, float& dist, XMFLOAT3& direction) const
	{
		direction = normal;
		dist = 0;

		XMVECTOR N = XMLoadFloat3(&normal);
		XMVECTOR O = XMLoadFloat3(&origin);

		XMVECTOR A = XMLoadFloat3(&b.base);
		XMVECTOR B = XMLoadFloat3(&b.tip);
		XMVECTOR D = XMVector3Normalize(A - B);
		A -= D * b.radius;
		B += D * b.radius;

		XMVECTOR C;
		if (std::abs(XMVectorGetX(XMVector3Dot(N, D))) < std::numeric_limits<float>::epsilon())
		{
			// parallel line-plane, take any point on capsule segment
			C = A;
		}
		else
		{
			// trace point on plane by capsule line and compute closest point on capsule to intersection point
			XMVECTOR t = XMVector3Dot(N, (A - O) / XMVectorAbs(XMVector3Dot(N, D)));
			XMVECTOR LinePlaneIntersection = A + D * t;
			C = vz::math::ClosestPointOnLineSegment(A, B, LinePlaneIntersection);
		}

		dist = vz::math::GetPlanePointDistance(O, N, C);

		if (dist < 0)
		{
			direction.x *= -1;
			direction.y *= -1;
			direction.z *= -1;
			dist = std::abs(dist);
		}

		dist = dist - b.radius;

		if (dist < 0)
		{
			XMMATRIX planeProjection = XMLoadFloat4x4(&projection);
			XMVECTOR clipSpacePos = XMVector3Transform(C, planeProjection);
			XMVECTOR uvw = clipSpacePos * XMVectorSet(0.5f, -0.5f, 0.5f, 1) + XMVectorSet(0.5f, 0.5f, 0.5f, 0);
			XMVECTOR uvw_sat = XMVectorSaturate(uvw);
			XMVECTOR uvw_diff = XMVectorAbs(uvw - uvw_sat);
			if (XMVectorGetX(uvw_diff) > std::numeric_limits<float>::epsilon())
				dist = 0; // force no collision
			else if (XMVectorGetY(uvw_diff) > std::numeric_limits<float>::epsilon())
				dist = 0; // force no collision
			else if (XMVectorGetZ(uvw_diff) > std::numeric_limits<float>::epsilon())
				dist = 0; // force no collision
		}
		return dist < 0;
	}
	bool Plane::intersects(const Ray& b) const
	{
		float dist;
		XMFLOAT3 direction;
		return intersects(b, dist, direction);
	}
	bool Plane::intersects(const Ray& b, float& dist) const
	{
		XMFLOAT3 direction;
		return intersects(b, dist, direction);
	}
	bool Plane::intersects(const Ray& b, float& dist, XMFLOAT3& direction) const
	{
		dist = 0;
		direction = normal;

		XMVECTOR N = XMLoadFloat3(&normal);
		XMVECTOR D = XMLoadFloat3(&b.direction);
		if (std::abs(XMVectorGetX(XMVector3Dot(N, D))) < std::numeric_limits<float>::epsilon())
			return false; // parallel line-plane

		XMVECTOR O = XMLoadFloat3(&b.origin);
		XMVECTOR A = O + D * b.TMin;
		XMVECTOR B = O + D * b.TMax;

		dist = XMVectorGetX(XMVector3Dot(N, (XMLoadFloat3(&origin) - O) / XMVector3Dot(N, D))); // plane intersection
		if (dist <= 0)
			return false;

		XMVECTOR C = O + D * dist;

		XMMATRIX planeProjection = XMLoadFloat4x4(&projection);
		XMVECTOR clipSpacePos = XMVector3Transform(C, planeProjection);
		XMVECTOR uvw = clipSpacePos * XMVectorSet(0.5f, -0.5f, 0.5f, 1) + XMVectorSet(0.5f, 0.5f, 0.5f, 0);
		XMVECTOR uvw_sat = XMVectorSaturate(uvw);
		XMVECTOR uvw_diff = XMVectorAbs(uvw - uvw_sat);
		if (XMVectorGetX(uvw_diff) > std::numeric_limits<float>::epsilon())
			return false; // force no collision
		else if (XMVectorGetY(uvw_diff) > std::numeric_limits<float>::epsilon())
			return false; // force no collision
		else if (XMVectorGetZ(uvw_diff) > std::numeric_limits<float>::epsilon())
			return false; // force no collision

		return true;
	}


	bool Ray::intersects(const AABB& b) const
	{
		return b.intersects(*this);
	}
	bool Ray::intersects(const Sphere& b) const
	{
		return b.intersects(*this);
	}
	bool Ray::intersects(const Sphere& b, float& dist) const
	{
		bool intersects = b.intersects(*this, dist);
		return intersects;
	}
	bool Ray::intersects(const Sphere& b, float& dist, XMFLOAT3& direction) const
	{
		bool intersects = b.intersects(*this, dist, direction);
		return intersects;
	}
	bool Ray::intersects(const Capsule& b) const
	{
		return b.intersects(*this);
	}
	bool Ray::intersects(const Capsule& b, float& dist) const
	{
		bool intersects = b.intersects(*this, dist);
		return intersects;
	}
	bool Ray::intersects(const Capsule& b, float& dist, XMFLOAT3& direction) const
	{
		bool intersects = b.intersects(*this, dist, direction);
		return intersects;
	}
	bool Ray::intersects(const Plane& b) const
	{
		return b.intersects(*this);
	}
	bool Ray::intersects(const Plane& b, float& dist) const
	{
		return b.intersects(*this, dist);
	}
	bool Ray::intersects(const Plane& b, float& dist, XMFLOAT3& direction) const
	{
		return b.intersects(*this, dist, direction);
	}
	void Ray::CreateFromPoints(const XMFLOAT3& a, const XMFLOAT3& b)
	{
		XMVECTOR A = XMLoadFloat3(&a);
		XMVECTOR B = XMLoadFloat3(&b);
		XMVECTOR D = B - A;
		XMVECTOR L = XMVector3Length(D);
		D /= L;
		XMStoreFloat3(&origin, A);
		XMStoreFloat3(&direction, D);
		XMStoreFloat3(&direction_inverse, XMVectorReciprocal(D));
		TMin = 0;
		TMax = XMVectorGetX(L);
	}
	XMFLOAT4X4 Ray::GetPlacementOrientation(const XMFLOAT3& position, const XMFLOAT3& normal) const
	{
		XMVECTOR N = XMLoadFloat3(&normal);
		XMVECTOR P = XMLoadFloat3(&position);
		XMVECTOR E = XMLoadFloat3(&origin);
		XMVECTOR T = XMVector3Normalize(XMVector3Cross(N, P - E));
		XMVECTOR B = XMVector3Normalize(XMVector3Cross(T, N));
		XMMATRIX M = { T, N, B, P };
		XMFLOAT4X4 orientation;
		XMStoreFloat4x4(&orientation, M);
		return orientation;
	}




	void Frustum::Create(const XMMATRIX& viewProjection)
	{
		// We are interested in columns of the matrix, so transpose because we can access only rows:
		const XMMATRIX mat = XMMatrixTranspose(viewProjection);

		// Near plane:
		XMStoreFloat4(&planes[0], XMPlaneNormalize(mat.r[2]));

		// Far plane:
		XMStoreFloat4(&planes[1], XMPlaneNormalize(mat.r[3] - mat.r[2]));

		// Left plane:
		XMStoreFloat4(&planes[2], XMPlaneNormalize(mat.r[3] + mat.r[0]));

		// Right plane:
		XMStoreFloat4(&planes[3], XMPlaneNormalize(mat.r[3] - mat.r[0]));

		// Top plane:
		XMStoreFloat4(&planes[4], XMPlaneNormalize(mat.r[3] - mat.r[1]));

		// Bottom plane:
		XMStoreFloat4(&planes[5], XMPlaneNormalize(mat.r[3] + mat.r[1]));
	}

	bool Frustum::CheckPoint(const XMFLOAT3& point) const
	{
		XMVECTOR p = XMLoadFloat3(&point);
		for (short i = 0; i < 6; i++)
		{
			if (XMVectorGetX(XMPlaneDotCoord(XMLoadFloat4(&planes[i]), p)) < 0.0f)
			{
				return false;
			}
		}

		return true;
	}
	bool Frustum::CheckSphere(const XMFLOAT3& center, float radius) const
	{
		int i;
		XMVECTOR c = XMLoadFloat3(&center);
		for (i = 0; i < 6; i++)
		{
			if (XMVectorGetX(XMPlaneDotCoord(XMLoadFloat4(&planes[i]), c)) < -radius)
			{
				return false;
			}
		}

		return true;
	}
	Frustum::BoxFrustumIntersect Frustum::CheckBox(const AABB& box) const
	{
		if (!box.IsValid())
			return BOX_FRUSTUM_OUTSIDE;
		int iTotalIn = 0;
		for (int p = 0; p < 6; ++p)
		{

			int iInCount = 8;
			int iPtIn = 1;

			for (int i = 0; i < 8; ++i)
			{
				XMFLOAT3 C = box.corner(i);
				if (XMVectorGetX(XMPlaneDotCoord(XMLoadFloat4(&planes[p]), XMLoadFloat3(&C))) < 0.0f)
				{
					iPtIn = 0;
					--iInCount;
				}
			}
			if (iInCount == 0)
				return BOX_FRUSTUM_OUTSIDE;
			iTotalIn += iPtIn;
		}
		if (iTotalIn == 6)
			return(BOX_FRUSTUM_INSIDE);
		return(BOX_FRUSTUM_INTERSECTS);
	}
	bool Frustum::CheckBoxFast(const AABB& box) const
	{
		if (!box.IsValid())
			return false;
		XMVECTOR max = XMLoadFloat3(&box._max);
		XMVECTOR min = XMLoadFloat3(&box._min);
		XMVECTOR zero = XMVectorZero();
		for (size_t p = 0; p < 6; ++p)
		{
			XMVECTOR plane = XMLoadFloat4(&planes[p]);
			XMVECTOR lt = XMVectorLess(plane, zero);
			XMVECTOR furthestFromPlane = XMVectorSelect(max, min, lt);
			if (XMVectorGetX(XMPlaneDotCoord(plane, furthestFromPlane)) < 0.0f)
			{
				return false;
			}
		}
		return true;
	}

	const XMFLOAT4& Frustum::getNearPlane() const { return planes[0]; }
	const XMFLOAT4& Frustum::getFarPlane() const { return planes[1]; }
	const XMFLOAT4& Frustum::getLeftPlane() const { return planes[2]; }
	const XMFLOAT4& Frustum::getRightPlane() const { return planes[3]; }
	const XMFLOAT4& Frustum::getTopPlane() const { return planes[4]; }
	const XMFLOAT4& Frustum::getBottomPlane() const { return planes[5]; }



	bool Hitbox2D::intersects(const XMFLOAT2& b) const
	{
		if (pos.x + siz.x < b.x)
			return false;
		else if (pos.x > b.x)
			return false;
		else if (pos.y + siz.y < b.y)
			return false;
		else if (pos.y > b.y)
			return false;
		return true;
	}
	bool Hitbox2D::intersects(const Hitbox2D& b) const
	{
		return vz::math::Collision2D(pos, siz, b.pos, b.siz);
	}

}

namespace vz::geometrics
{
	// Simple fast update BVH
	//	https://jacco.ompf2.com/2022/04/13/how-to-build-a-bvh-part-1-basics/
	// https://github.com/jbikker/bvh_article?tab=readme-ov-file
	struct BVH
	{
		struct Node
		{
			vz::geometrics::AABB aabb;
			uint32_t left = 0;
			uint32_t offset = 0;
			uint32_t count = 0;
			constexpr bool isLeaf() const { return count > 0; }
		};
		std::vector<uint8_t> allocation;
		Node* nodes = nullptr;
		uint32_t node_count = 0;
		uint32_t* leaf_indices = nullptr;
		uint32_t leaf_count = 0;

		constexpr bool IsValid() const { return nodes != nullptr; }

		void Build(const vz::geometrics::AABB* aabbs, uint32_t aabb_count)
		{
			node_count = 0;
			if (aabb_count == 0)
				return;

			const uint32_t node_capacity = aabb_count * 2 - 1;
			allocation.reserve(
				sizeof(Node) * node_capacity +
				sizeof(uint32_t) * aabb_count
			);
			nodes = (Node*)allocation.data();
			leaf_indices = (uint32_t*)(nodes + node_capacity);
			leaf_count = aabb_count;

			Node& node = nodes[node_count++];
			node = {};
			node.count = aabb_count;
			for (uint32_t i = 0; i < aabb_count; ++i)
			{
				node.aabb = vz::geometrics::AABB::Merge(node.aabb, aabbs[i]);
				leaf_indices[i] = i;
			}
			Subdivide(0, aabbs);
		}

		void Subdivide(uint32_t nodeIndex, const vz::geometrics::AABB* leaf_aabb_data)
		{
			Node& node = nodes[nodeIndex];
			if (node.count <= 2)
				return;

			XMFLOAT3 extent = node.aabb.getHalfWidth();
			XMFLOAT3 min = node.aabb.getMin();
			int axis = 0;
			if (extent.y > extent.x) axis = 1;
			if (extent.z > ((float*)&extent)[axis]) axis = 2;
			float splitPos = ((float*)&min)[axis] + ((float*)&extent)[axis] * 0.5f;

			// in-place partition
			int i = node.offset;
			int j = i + node.count - 1;
			while (i <= j)
			{
				XMFLOAT3 center = leaf_aabb_data[leaf_indices[i]].getCenter();
				float value = ((float*)&center)[axis];

				if (value < splitPos)
				{
					i++;
				}
				else
				{
					std::swap(leaf_indices[i], leaf_indices[j--]);
				}
			}

			// abort split if one of the sides is empty
			int leftCount = i - node.offset;
			if (leftCount == 0 || leftCount == node.count)
				return;

			// create child nodes
			uint32_t left_child_index = node_count++;
			uint32_t right_child_index = node_count++;
			node.left = left_child_index;
			nodes[left_child_index] = {};
			nodes[left_child_index].offset = node.offset;
			nodes[left_child_index].count = leftCount;
			nodes[right_child_index] = {};
			nodes[right_child_index].offset = i;
			nodes[right_child_index].count = node.count - leftCount;
			node.count = 0;
			UpdateNodeBounds(left_child_index, leaf_aabb_data);
			UpdateNodeBounds(right_child_index, leaf_aabb_data);

			// recurse
			Subdivide(left_child_index, leaf_aabb_data);
			Subdivide(right_child_index, leaf_aabb_data);
		}

		void UpdateNodeBounds(uint32_t nodeIndex, const vz::geometrics::AABB* leaf_aabb_data)
		{
			Node& node = nodes[nodeIndex];
			node.aabb = {};
			for (uint32_t i = 0; i < node.count; ++i)
			{
				uint32_t offset = node.offset + i;
				uint32_t index = leaf_indices[offset];
				node.aabb = vz::geometrics::AABB::Merge(node.aabb, leaf_aabb_data[index]);
			}
		}

		template <typename T>
		void Intersects(
			const T& primitive,
			uint32_t nodeIndex,
			const std::function<void(uint32_t index)>& callback
		) const
		{
			Node& node = nodes[nodeIndex];
			if (!node.aabb.intersects(primitive))
				return;
			if (node.isLeaf())
			{
				for (uint32_t i = 0; i < node.count; ++i)
				{
					callback(leaf_indices[node.offset + i]);
				}
			}
			else
			{
				Intersects(primitive, node.left, callback);
				Intersects(primitive, node.left + 1, callback);
			}
		}

		// Returning true from callback will immediately exit the whole search
		template <typename T>
		bool IntersectsFirst(
			const T& primitive,
			const std::function<bool(uint32_t index)>& callback
		) const
		{
			uint32_t stack[64];
			uint32_t count = 0;
			stack[count++] = 0; // push node 0
			while (count > 0)
			{
				const uint32_t nodeIndex = stack[--count];
				Node& node = nodes[nodeIndex];
				if (!node.aabb.intersects(primitive))
					continue;
				if (node.isLeaf())
				{
					for (uint32_t i = 0; i < node.count; ++i)
					{
						if (callback(leaf_indices[node.offset + i]))
							return true;
					}
				}
				else
				{
					stack[count++] = node.left;
					stack[count++] = node.left + 1;
				}
			}
			return false;
		}
	};
}