#include "ep_sample001.h"

#include "Components/GComponents.h"

using namespace vz;

bool ImportDicom(std::unordered_map<std::string, std::any>& io)
{
	std::string version = vz::GetComponentVersion();
	assert(version == vz::COMPONENT_INTERFACE_VERSION);

	auto it = io.find("filename");
	if (it == io.end()) return false;
	std::string filename = std::any_cast<std::string>(it->second);

	it = io.find("volume texture entity");
	if (it == io.end()) return false;
	Entity texture_entity = std::any_cast<Entity>(it->second);
	
	VolumeComponent* volume = compfactory::GetVolumeComponent(texture_entity);
	if (volume == nullptr) return false;
	
	// TODO
	uint32_t volume_w = 516;
	uint32_t volume_h = 516;
	uint32_t volume_d = 516;

	float voxel_x = 0.2f;
	float voxel_y = 0.2f;
	float voxel_z = 0.2f;

	std::vector<uint8_t> data(volume_w * volume_h * volume_d * 2);

	volume->LoadVolume("my volume #dcm", data, volume_w, volume_h, volume_d, VolumeComponent::VolumeFormat::UINT16);
	volume->SetVoxelSize({ 0.2f , 0.2f , 0.2f });

	((GVolumeComponent*)volume)->UpdateVolumeMinMaxBlocks({8, 8, 8});

	//pointer..

	// modify volume data if required
	//volume->MoveFromData(std::move(data));x
	//texture->MoveFromData();

	return true;
}