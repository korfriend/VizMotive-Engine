#include "ep_sample001.h"

using namespace vz;

bool ImportDicom(std::unordered_map<std::string, std::any>& io)
{
	auto it = io.find("filename");
	if (it == io.end()) return false;
	std::string filename = std::any_cast<std::string>(it->second);

	it = io.find("volume texture entity");
	if (it == io.end()) return false;
	Entity texture_entity = std::any_cast<Entity>(it->second);
	
	TextureComponent* texture = compfactory::GetTextureComponent(texture_entity);
	if (texture == nullptr) return false;
	
	// TODO
	uint32_t volume_w = 516;
	uint32_t volume_h = 516;
	uint32_t volume_d = 516;

	float voxel_x = 0.2f;
	float voxel_y = 0.2f;
	float voxel_z = 0.2f;

	texture->SetTextureDimension(volume_w, volume_h, volume_d, 1);

	//pointer..

	std::vector<uint8_t> data(volume_w * volume_h * volume_d * 2);

	// fill data...
	//texture->MoveFromData();

	return true;
}