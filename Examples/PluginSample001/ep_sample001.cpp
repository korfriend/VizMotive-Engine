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
	uint32_t volume_w = 64;
	uint32_t volume_h = 64;
	uint32_t volume_d = 64;

	float voxel_x = 0.02f;
	float voxel_y = 0.02f;
	float voxel_z = 0.02f;

	std::vector<uint8_t> data(volume_w * volume_h * volume_d * 2);

	uint16_t* vol_data = (uint16_t*)data.data();
	XMFLOAT3 center((float)volume_w * .5f, (float)volume_h * .5f, (float)volume_d * .5f);
	XMVECTOR xcenter = XMLoadFloat3(&center);
	float sigma = 1.5f;
	float l = (float)volume_w;
	for (uint32_t z = 0; z < volume_d; z++)
	{
		for (uint32_t y = 0; y < volume_h; y++)
		{
			for (uint32_t x = 0; x < volume_w; x++)
			{
				XMFLOAT3 p((float)x * .5f, (float)y * .5f, (float)z * .5f);
				XMVECTOR xp = XMLoadFloat3(&p);

				// distance to center
				XMVECTOR distance_vec = XMVectorSubtract(xp, xcenter);
				float distance = XMVectorGetX(XMVector3Length(distance_vec));

				// gaussian function
				float gaussian_value = expf(-0.5f * (distance * distance) / (sigma * sigma));
				uint16_t intensity = static_cast<uint16_t>(gaussian_value * 65535.0f);

				// stored value based on gaussian function
				vol_data[z * volume_h * volume_w + y * volume_w + x] = intensity;
			}
		}
	}

	volume->LoadVolume("my volume #dcm", data, volume_w, volume_h, volume_d, VolumeComponent::VolumeFormat::UINT16);
	volume->SetVoxelSize({ 0.2f , 0.2f , 0.2f });
	volume->SetStoredMinMax({ 0, 65535.f });
	volume->SetOriginalMinMax({ 0, 1.f });
	volume->UpdateAlignmentMatrix({ 1, 0, 0 }, { 0, 1, 0 }, true);
	volume->UpdateHistogram(0, 65535, 4096);

	((GVolumeComponent*)volume)->UpdateVolumeMinMaxBlocks({8, 8, 8});

	//pointer..

	// modify volume data if required
	//volume->MoveFromData(std::move(data));x
	//texture->MoveFromData();

	return true;
}