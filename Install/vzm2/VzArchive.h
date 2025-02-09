#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzArchive : VzBaseComp
	{
		VzArchive(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, COMPONENT_TYPE::ARCHIVE) {}

		void Close();
		void Load(const VID vid);
		void Load(const VzBaseComp* comp) { Load(comp->GetVID()); }
		void Store(const VID vid);
		void Store(const VzBaseComp* comp) { Store(comp->GetVID()); }
	};
}
