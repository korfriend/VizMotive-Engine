#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzArchive : VzBaseComp
	{
		VzArchive(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, COMPONENT_TYPE::ARCHIVE) {}
		virtual ~VzArchive() = default;

		void Close();
		void Load(const VID vid);
		void Load(const std::vector<VID> vids); // MUST BE PAIRED with Store(const std::vector<VID> vids)
		void Load(const VzBaseComp* comp) { Load(comp->GetVID()); }
		void Store(const VID vid);
		void Store(const std::vector<VID> vids);
		void Store(const VzBaseComp* comp) { Store(comp->GetVID()); }

		bool SaveFile(const std::string& fileName, const bool saveWhenClose = false);
		bool ReadFile(const std::string& fileName);
	};
}
