#include "ep_sample001.h"

#include "vzmcore/GComponents.h"
#include "vzmcore/utils/Helpers.h"
#include "vzmcore/utils/Backlog.h"

using namespace vz;

bool ImportDicom(std::unordered_map<std::string, std::any>& io)
{
	std::string version = vz::GetComponentVersion();
	vzlog_assert(version == vz::COMPONENT_INTERFACE_VERSION, "Engine lib vertion does NOT match the current Engine headers!");

	auto it = io.find("filename");
	if (it == io.end()) return false;
	std::string filename = std::any_cast<std::string>(it->second);

	it = io.find("volume texture entity");
	if (it == io.end()) return false;
	Entity texture_entity = std::any_cast<Entity>(it->second);
	
	VolumeComponent* volume = compfactory::GetVolumeComponent(texture_entity);
	if (volume == nullptr) return false;
	
	// === format determination ===
	typedef uint8_t DATA_TYPE;
	VolumeComponent::VolumeFormat vol_format = VolumeComponent::VolumeFormat::UINT8;
	float typescaling = 255.f;
	uint32_t stride = 1;
	// ============================
	uint32_t volume_w = 128;
	uint32_t volume_h = 128;
	uint32_t volume_d = 128;

	float voxel_x = 0.02f;
	float voxel_y = 0.02f;
	float voxel_z = 0.02f;

	std::vector<uint8_t> data(volume_w * volume_h * volume_d * stride);


	// https://web.cs.ucdavis.edu/~okreylos/PhDStudies/Spring2000/ECS277/DataSets.html
	//  this include 28-bytes of meta information in its header
	std::string fileName = "../Assets/C60Large.vol";
	std::string directory = helper::GetDirectoryFromPath("../Assets/C60Large.vol"); // 128 x 128 x 128
	std::string name = helper::GetFileNameFromPath(fileName);

	std::vector<uint8_t> filedata;
	bool success = helper::FileRead(fileName, filedata);
	memcpy(data.data(), filedata.data() + 28, volume_w * volume_h * volume_d * stride);

	/*
	float sigma = 1.4f;
	float l = (float)volume_w;
	float min_v = FLT_MAX;
	float max_v = -FLT_MAX;
	DATA_TYPE* vol_data = (DATA_TYPE*)data.data();
	XMFLOAT3 center((float)volume_w * .5f, (float)volume_h * .5f, (float)volume_d * .5f);
	XMVECTOR xcenter = XMLoadFloat3(&center);
	for (uint32_t z = 0; z < volume_d; z++)
	{
		for (uint32_t y = 0; y < volume_h; y++)
		{
			for (uint32_t x = 0; x < volume_w; x++)
			{
				XMFLOAT3 p((float)x, (float)y, (float)z);
				XMVECTOR xp = XMLoadFloat3(&p);

				// distance to center
				XMVECTOR distance_vec = XMVectorSubtract(xp, xcenter);
				float distance = XMVectorGetX(XMVector3Length(distance_vec)) / 32.f;

				// gaussian function
				float gaussian_value = expf(-0.5f * (distance * distance) / (sigma * sigma));
				DATA_TYPE intensity = static_cast<DATA_TYPE>(gaussian_value * typescaling);

				min_v = std::min(min_v, (float)intensity);
				max_v = std::max(max_v, (float)intensity);

				// stored value based on gaussian function
				vol_data[z * volume_h * volume_w + y * volume_w + x] = intensity;
			}
		}
	}
	/**/

	volume->LoadVolume("my volume #dcm", data, volume_w, volume_h, volume_d, vol_format);
	volume->SetVoxelSize({ voxel_x , voxel_y , voxel_z });
	volume->SetStoredMinMax({ 0, typescaling });
	volume->SetOriginalMinMax({ 0, 1.f });
	volume->UpdateAlignmentMatrix({ 1, 0, 0 }, { 0, 1, 0 }, true);
	volume->UpdateHistogram(0, typescaling, 200);

	((GVolumeComponent*)volume)->UpdateVolumeMinMaxBlocks({8, 8, 8});

	//pointer..

	// modify volume data if required
	//volume->MoveFromData(std::move(data));x
	//texture->MoveFromData();

	return true;
}