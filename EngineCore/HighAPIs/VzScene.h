#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzEnvironment : VzBaseComp
	{
	public:
		VzEnvironment(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, COMPONENT_TYPE::ENVIRONMENT) {
		}
		virtual ~VzEnvironment() = default;

		// TODO : ADD COMPONENT's APIs
	};

	struct API_EXPORT VzScene : VzBaseComp
	{
	public:
		VzScene(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, COMPONENT_TYPE::SCENE) {
		}
		virtual ~VzScene() = default;

		bool LoadIBL(const std::string& iblPath);

		void AppendChild(const VzBaseComp* child);
		void DetachChild(const VzBaseComp* child);
		void AttachToParent(const VzBaseComp* parent);

		void AppendAnimation(const VzBaseComp* animation);
		void DetachAnimation(const VzBaseComp* animation);

		const std::vector<VID>& GetChildrenVIDs() const;

		bool RenderChain(const std::vector<ChainUnitRCam>& rendererChain, const float dt = -1.f);

		void SetOptionEnabled(const std::string& optionName, const bool enabled);
		void SetOptionValueArray(const std::string& optionName, const std::vector<float>& values);
	};
}
