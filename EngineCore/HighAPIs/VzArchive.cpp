#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Common/Archive.h"
#include "Utils/Backlog.h"
#include "Utils/helpers.h"

#include <mutex>
#include <thread>

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_ARCHIVE(ARCHIVE, RET) Archive* ARCHIVE = (Archive*)Archive::GetArchive(componentVID_); \
	if (!ARCHIVE) {post("Archive(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;} 

	void VzArchive::Close()
	{
		GET_ARCHIVE(archive, );
		archive->Close();
	}

	void serialize(VzBaseComp* comp, Archive* archive)
	{
		Entity entity = comp->GetVID();
		switch (comp->GetType())
		{
		case COMPONENT_TYPE::CAMERA:
			// name, hierarchy, transform, camera
		{
			NameComponent* name = compfactory::GetNameComponent(entity);
			TransformComponent* transform = compfactory::GetTransformComponent(entity);
			HierarchyComponent* hierarchy = compfactory::GetHierarchyComponent(entity);
			CameraComponent* camera = compfactory::GetCameraComponent(entity);
			assert(name && transform && hierarchy && camera);
			name->Serialize(*archive, 0);
			hierarchy->Serialize(*archive, 0);
			transform->Serialize(*archive, 0);
			camera->Serialize(*archive, 0);
		} break;

		default:
			assert(0 && "NOT YET SUPPORTED!");
			return;
		}
	}

	void VzArchive::Load(const VID vid)
	{
		GET_ARCHIVE(archive, );
		VzBaseComp* comp = GetComponent(vid);
		if (comp == nullptr)
		{
			post("invalid VID!", LogLevel::Error);
			return;
		}
		archive->SetReadModeAndResetPos(true);
		serialize(comp, archive);
	}
	void VzArchive::Load(const std::vector<VID> vids)
	{
		GET_ARCHIVE(archive, );
		vzlog_assert(0, "TODO");
	}
	void VzArchive::Store(const VID vid)
	{
		GET_ARCHIVE(archive, );
		VzBaseComp* comp = GetComponent(vid);
		if (comp == nullptr)
		{
			post("invalid VID!", LogLevel::Error);
			return;
		}
		archive->SetReadModeAndResetPos(false);
		serialize(comp, archive);
	}
	void VzArchive::Store(const std::vector<VID> vids)
	{
		GET_ARCHIVE(archive, );
		vzlog_assert(0, "TODO");
	}

	bool VzArchive::SaveFile(const std::string& fileName, const bool saveWhenClose)
	{
		GET_ARCHIVE(archive, false);
		archive->SetCompressionEnabled(true);
		archive->SetFileName(fileName);
		return archive->SaveFile(fileName);
	}

	bool VzArchive::ReadFile(const std::string& fileName)
	{
		GET_ARCHIVE(archive, false);
		archive->SetCompressionEnabled(true);

		std::vector<uint8_t> data;
		if (vz::helper::FileRead(fileName, data))
		{
			archive->ReadData(data.data(), data.size());
			return true;
		}

		return false;
	}
}