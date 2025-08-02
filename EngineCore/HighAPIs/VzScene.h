#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzScene : VzBaseComp
	{
	private:
		float iblRotation_ = 0.0f;
	public:
		VzScene(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, COMPONENT_TYPE::SCENE) {
		}
		virtual ~VzScene() = default;

		bool LoadIBL(const std::string& iblPath);

		void AppendChild(const VzBaseComp* child);
		void DetachChild(const VzBaseComp* child);
		void AttachToParent(const VzBaseComp* parent);

		const std::vector<VID>& GetChildrenVIDs() const;

		bool RenderChain(const std::vector<ChainUnitRCam>& rendererChain, const float dt = -1.f);
	};
}
