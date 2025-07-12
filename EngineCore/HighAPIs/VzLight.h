#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzLight : VzSceneObject
	{
		enum class LightType : uint32_t {
			DIRECTIONAL = 0,
			POINT,
			SPOT,
			COUNT
		};

		VzLight(const VID vid, const std::string& originFrom)
			: VzSceneObject(vid, originFrom, COMPONENT_TYPE::LIGHT) {
		}
		virtual ~VzLight() = default;

		void SetLightType(const LightType type);

		void SetSpotlightConeAngle(const float outerConeAngle, const float innerConeAngle = 0); // only for spotlight
		void SetPointlightLength(const float length);
		void SetRange(const float range);
		void SetIntensity(const float intensity);
		void SetColor(const vfloat3 color);
		void SetRadius(const float radius);
		void SetLength(const float length);
		void EnableVisualizer(const bool enabled);
		void EnableCastShadow(const bool enabled);
		//void SetLightTexture(const LightVID); //  TODO
	};

	using LightType = VzLight::LightType;
}