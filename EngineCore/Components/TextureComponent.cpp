#include "GComponents.h"
#include "Common/ResourceManager.h"
#include "Utils/Helpers.h"
#include "Utils/Backlog.h"

//enum class DataType : uint8_t
//{
//	UNDEFINED = 0,
//	BOOL,
//	CHAR,
//	CHAR2,
//	CHAR3,
//	CHAR4,
//	BYTE,
//	BYTE2,
//	BYTE3,
//	BYTE4,
//	SHORT,
//	SHORT2,
//	SHORT3,
//	SHORT4,
//	USHORT,
//	USHORT2,
//	USHORT3,
//	USHORT4,
//	FLOAT,
//	FLOAT2,
//	FLOAT3,
//	FLOAT4,
//	INT,
//	INT2,
//	INT3,
//	INT4,
//	UINT,
//	UINT2,
//	UINT3,
//	UINT4,
//	MAT3,   //!< a 3x3 float matrix
//	MAT4,   //!< a 4x4 float matrix
//	STRUCT
//};
//
//inline size_t GetStride(const DataType dtype)
//{
//	switch (dtype)
//	{
//	case DataType::BOOL: return 1;
//	case DataType::CHAR: return 1;
//	case DataType::CHAR2: return 2;
//	case DataType::CHAR3: return 3;
//	case DataType::CHAR4: return 4;
//	case DataType::BYTE: return 1;
//	case DataType::BYTE2: return 2;
//	case DataType::BYTE3: return 3;
//	case DataType::BYTE4: return 4;
//	case DataType::SHORT: return 2;
//	case DataType::SHORT2: return 4;
//	case DataType::SHORT3: return 6;
//	case DataType::SHORT4: return 8;
//	case DataType::USHORT: return 2;
//	case DataType::USHORT2: return 4;
//	case DataType::USHORT3: return 6;
//	case DataType::USHORT4: return 8;
//	case DataType::FLOAT: return 4;
//	case DataType::FLOAT2: return 8;
//	case DataType::FLOAT3: return 12;
//	case DataType::FLOAT4: return 16;
//	case DataType::INT: return 4;
//	case DataType::INT2: return 8;
//	case DataType::INT3: return 12;
//	case DataType::INT4: return 16;
//	case DataType::UINT: return 4;
//	case DataType::UINT2: return 8;
//	case DataType::UINT3: return 12;
//	case DataType::UINT4: return 16;
//	case DataType::MAT3: return 36;   //!< a 3x3 float matrix
//	case DataType::MAT4: return 64;   //!< a 4x4 float matrix
//	default:
//		break;
//	}
//	return 0;
//}

namespace vz
{
#define GETTER_RES(RES, RET) Resource* RES = resource_.get(); if (RES == nullptr) { backlog::post("Invalid Resource >> TextureComponent", backlog::LogLevel::Error); return RET; };

	TextureComponent::TextureType getTextureType(const graphics::Texture& texture)
	{
		TextureComponent::TextureType tType = TextureComponent::TextureType::Undefined;
		if (texture.desc.width > 0 && texture.desc.width <= TEXTURE_MAX_RESOLUTION)
		{
			tType = TextureComponent::TextureType::Texture1D;
			if (texture.desc.height > 1)
			{
				tType = TextureComponent::TextureType::Texture2D;
				if (texture.desc.depth > 1)
				{
					tType = TextureComponent::TextureType::Texture3D;
					assert(texture.desc.array_size == 1);
				}
				else if (texture.desc.array_size > 1)
				{
					tType = TextureComponent::TextureType::Texture2D_Array;
				}
			}
		}
		if (texture.desc.width > TEXTURE_MAX_RESOLUTION)
		{
			tType = TextureComponent::TextureType::Buffer;
		}
		return tType;
	}

	bool TextureComponent::IsValid() const
	{
		GETTER_RES(resource, false);
		return resource->IsValid();
	}
	const std::vector<uint8_t>& TextureComponent::GetData() const
	{
		static std::vector<uint8_t> empty;
		GETTER_RES(resource, empty);
		return resource->GetFileData();
	}
	int TextureComponent::GetFontStyle() const
	{
		GETTER_RES(resource, -1);
		return resource->GetFontStyle();
	}
	void TextureComponent::CopyFromData(const std::vector<uint8_t>& data)
	{
		GETTER_RES(resource, );
		assert(resource->GetFileData().size() == data.size());
		resource->CopyFromData(data);
		resource->SetOutdated();
	}
	void TextureComponent::MoveFromData(std::vector<uint8_t>&& data)
	{
		GETTER_RES(resource, );
		assert(resource->GetFileData().size() == data.size());
		resource->MoveFromData(std::move(data));
		resource->SetOutdated();
	}

	bool TextureComponent::LoadImageFile(const std::string& fileName)
	{
		resource_ = std::make_shared<Resource>(
			resourcemanager::Load(fileName, resourcemanager::Flags::IMPORT_RETAIN_FILEDATA | resourcemanager::Flags::STREAMING)
		);

		resName_ = fileName;
		Resource& resource = *resource_.get();
		if (resource.IsValid())
		{
			graphics::Texture texture = resource.GetTexture();
			width_ = texture.desc.width;
			height_ = texture.desc.height;
			depth_ = texture.desc.depth;
			arraySize_ = texture.desc.array_size;
			stride_ = graphics::GetFormatStride(texture.desc.format);
			textureType_ = getTextureType(texture);
			textureFormat_ = static_cast<TextureFormat>(texture.desc.format);
			hasRenderData_ = true;
		}
		return resource.IsValid();
	}

	bool TextureComponent::LoadMemory(const std::string& name,
		const std::vector<uint8_t>& data, const TextureFormat textureFormat,
		const uint32_t w, const uint32_t h, const uint32_t d)
	{
		if (resourcemanager::Contains(name))
		{
			backlog::post("do not allow the same key for resource manager for TextureComponent::LoadMemory", backlog::LogLevel::Error);
			assert(0 && resourcemanager::Contains(name) && "do not allow the same key for resource manager for TextureComponent::LoadMemory");
			return false;
		}

		resource_ = std::make_shared<Resource>(
			resourcemanager::LoadMemory(name, resourcemanager::Flags::IMPORT_RETAIN_FILEDATA, data.data(), w, h, d, textureFormat)
		);

		resName_ = name;
		Resource& resource = *resource_.get();
		if (resource.IsValid())
		{
			graphics::Texture texture = resource.GetTexture();
			width_ = texture.desc.width;
			height_ = texture.desc.height;
			depth_ = texture.desc.depth;
			arraySize_ = texture.desc.array_size;
			stride_ = graphics::GetFormatStride(texture.desc.format);
			textureType_ = getTextureType(texture);
			textureFormat_ = static_cast<TextureFormat>(texture.desc.format);
			hasRenderData_ = true;
		}
		return resource.IsValid();
	}
}

template<typename T>
void updateHistoValues(const uint8_t* data, const uint32_t w, const uint32_t h, const uint32_t d, vz::Histogram& histogram)
{
	const T* data_t = (const T*)data;
	uint32_t num_voxels = w * h * d;
	for (uint32_t i = 0; i < num_voxels; ++i)
	{
		histogram.CountValue((float)data_t[i]);
	}
}

namespace vz
{
	void VolumeComponent::UpdateHistogram(const float minValue, const float maxValue, const size_t numBins)
	{
		if (!IsValid())
		{
			return;
		}

		histogram_.CreateHistogram(minValue, maxValue, numBins);

		const uint8_t* data = GetData().data();
		switch (volFormat_)
		{
		case vz::VolumeComponent::VolumeFormat::UINT8:
			updateHistoValues<uint8_t>(data, width_, height_, depth_, histogram_);
			break;
		case vz::VolumeComponent::VolumeFormat::UINT16:
			updateHistoValues<uint16_t>(data, width_, height_, depth_, histogram_);
			break;
		case vz::VolumeComponent::VolumeFormat::FLOAT:
			updateHistoValues<float>(data, width_, height_, depth_, histogram_);
			break;
		default:
			break;
		}
	}

	void VolumeComponent::UpdateAlignmentMatrix(const XMFLOAT3& axisVolX, const XMFLOAT3& axisVolY, const bool isRHS)
	{
		if (!isDirty_ || !IsValid() || voxelSize_.x * voxelSize_.y * voxelSize_.z == 0)
		{
			return;
		}

		XMVECTOR vec_axisy_os = XMLoadFloat3(&axisVolX);
		XMVECTOR vec_axisx_os = XMLoadFloat3(&axisVolY);
		XMVECTOR z_vec_rhs = XMVector3Cross(vec_axisy_os, vec_axisx_os); // note the z-dir in lookat
		XMVECTOR origin = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		XMMATRIX mat_lookat = VZMatrixLookTo(origin, z_vec_rhs, vec_axisy_os);
		XMMATRIX mat_r = XMMatrixInverse(nullptr, mat_lookat); // orientation for rs2os 
		if (!isRHS)
		{
			XMMATRIX matInverseZ = XMMatrixScaling(1.f, 1.f, -1.f);
			mat_r = matInverseZ * mat_r;
		}
		XMFLOAT3 vol_size = { (float)width_, (float)height_, (float)depth_ };
		XMMATRIX mat_s = XMMatrixScaling(voxelSize_.x, voxelSize_.y, voxelSize_.z);
		XMFLOAT3 pos_min_vs = XMFLOAT3(-0.5f, -0.5f, -0.5f);
		XMFLOAT3 pos_max_vs = XMFLOAT3(vol_size.x - 0.5f, vol_size.y - 0.5f, vol_size.z - 0.5f);
		XMVECTOR vpos_min_vs = XMLoadFloat3(&pos_min_vs);
		XMVECTOR vpos_max_vs = XMLoadFloat3(&pos_max_vs);
		XMVECTOR vpos_min_os = XMVector3TransformCoord(vpos_min_vs, mat_s);
		XMVECTOR vpos_max_os = XMVector3TransformCoord(vpos_max_vs, mat_s);
		XMVECTOR vtranslate = -(vpos_min_os + vpos_max_os) * 0.5;
		XMFLOAT3 translate_pos;
		XMStoreFloat3(&translate_pos, vtranslate);
		XMMATRIX mat_t = XMMatrixTranslation(translate_pos.x, translate_pos.y, translate_pos.z);

		XMMATRIX mat_vs2os = mat_s * mat_r * mat_t;
		XMStoreFloat4x4(&matVS2OS_, mat_vs2os);

		XMMATRIX mat_os2vs = XMMatrixInverse(NULL, mat_vs2os);
		XMStoreFloat4x4(&matOS2VS_, mat_os2vs);

		// for texture space
		mat_t = XMMatrixTranslation(0.5f, 0.5f, 0.5f);
		mat_s = XMMatrixScaling(1.f / vol_size.x, 1.f / vol_size.y, 1.f / vol_size.z);
		XMMATRIX mat_vs2ts = mat_t * mat_s;
		XMStoreFloat4x4(&matVS2TS_, mat_vs2ts);

		XMMATRIX mat_ts2vs = XMMatrixInverse(NULL, mat_vs2ts);
		XMStoreFloat4x4(&matTS2VS_, mat_ts2vs);

		isDirty_ = false;
	}

	geometrics::AABB VolumeComponent::ComputeAABB() const
	{
		XMFLOAT3 volume_size((float)width_, (float)height_, (float)depth_);
		XMVECTOR volume_box_max_v = XMLoadFloat3(&volume_size) * XMLoadFloat3(&voxelSize_) * 0.5f;
		XMFLOAT3 volume_box_max;
		XMStoreFloat3(&volume_box_max, volume_box_max_v);
		geometrics::AABB aabb;
		aabb._max = volume_box_max;
		aabb._min = XMFLOAT3(-volume_box_max.x, -volume_box_max.y, -volume_box_max.z);
		return aabb;
	}


	bool VolumeComponent::IsValidVolume() const
	{
		return !isDirty_ && voxelSize_.x > 0 && voxelSize_.y > 0 && voxelSize_.z > 0 && IsValid();
	}

	bool VolumeComponent::LoadVolume(const std::string& fileName, const std::vector<uint8_t>& volData,
		const uint32_t w, const uint32_t h, const uint32_t d, const VolumeFormat volFormat)
	{
		std::string wildcard_file_ext = helper::toUpper(fileName.substr(fileName.find_last_of("#") + 1));
		std::string ext = helper::toUpper(helper::GetExtensionFromFileName(fileName));
		if (wildcard_file_ext != "DCM" && ext != "DCM")
		{
			return false;
		}

		resource_ = std::make_shared<Resource>(
			resourcemanager::LoadVolume(fileName, resourcemanager::Flags::IMPORT_RETAIN_FILEDATA, volData.data(), w, h, d, volFormat)
		);

		resName_ = fileName;
		Resource& resource = *resource_.get();
		if (resource.IsValid())
		{
			graphics::Texture texture = resource.GetTexture();
			width_ = texture.desc.width;
			height_ = texture.desc.height;
			depth_ = texture.desc.depth;
			arraySize_ = texture.desc.array_size;
			stride_ = graphics::GetFormatStride(texture.desc.format);
			textureType_ = TextureType::Texture3D;
			volFormat_ = volFormat;

			switch (volFormat)
			{
			case VolumeFormat::UINT8:
				textureFormat_ = TextureFormat::R8_UNORM; break;
			case VolumeFormat::UINT16:
				textureFormat_ = TextureFormat::R16_UNORM; break;
			case VolumeFormat::FLOAT:
				textureFormat_ = TextureFormat::R32_FLOAT; break;
			default:
				assert(0);
			}

			hasRenderData_ = true;
		}

		isDirty_ = true;
		// 

		//SetVolumeSize(const uint32_t w, const uint32_t h, const uint32_t d);
		//SetDataType(const DataType dtype) { dataType_ = dtype; }
		//SetVoxelSize(const XMFLOAT3 & voxelSize) { voxelSize_ = voxelSize; }
		//SetOriginalDataType(const DataType originalDataType) { originalDataType_ = originalDataType; }
		//SetStoredMinMax(const XMFLOAT2 minMax) { storedMinMax_ = minMax; }
		//SetOriginalMinMax(const XMFLOAT2 minMax) { originalMinMax_ = minMax; }
		//SetAlign(const XMFLOAT3 & axisVolX, const XMFLOAT3 & axisVolY, const bool isRHS);

		return resource.IsValid();
	}
}

namespace vz
{
	void GVolumeComponent::UpdateVolumeMinMaxBlocks(const XMUINT3 blockSize)
	{
		GETTER_RES(resource, );

		if (!IsValid())
		{
			backlog::post("UpdateVolumeMinMaxBlocks requires a valid texture", backlog::LogLevel::Error);
			return;
		}

		blockPitch_ = blockSize;
		volumeMinMaxBlocks_ = {};

		const uint8_t* vol_data = resource_->GetFileData().data();

		using uint = uint32_t;

		uint modX = width_ % blockPitch_.x;
		uint modY = height_ % blockPitch_.y;
		uint modZ = depth_ % blockPitch_.z;
		uint num_blocksX = width_ / blockPitch_.x + (modX != 0);
		uint num_blocksY = height_ / blockPitch_.y + (modY != 0);
		uint num_blocksZ = depth_ / blockPitch_.z + (modZ != 0);

		uint xy = num_blocksX * num_blocksY;

		uint num_blocksXY = num_blocksX * num_blocksY;
		uint num_blocksXYZ = num_blocksXY * num_blocksZ;

		// MinMax Block Setting
		uint stride = graphics::GetFormatStride(static_cast<graphics::Format>(textureFormat_));

		std::vector<uint8_t> blocks(num_blocksXYZ * stride * 2); // here, 2 refers min and max
		uint8_t* block_data = blocks.data();
		XMUINT3 blk_idx = XMUINT3(0, 0, 0);
		for (uint z = 0; z < depth_; z += blockPitch_.z, blk_idx.z++)
		{
			blk_idx.y = 0;
			for (uint y = 0; y < height_; y += blockPitch_.y, blk_idx.y++)
			{
				blk_idx.x = 0;
				for (uint x = 0; x < width_; x += blockPitch_.x, blk_idx.x++)
				{
					float min_v = FLT_MAX;		// Min
					float max_v = -FLT_MAX;		// Max

					XMUINT3 boundary_size = blockPitch_;
					if (width_ < x + blockPitch_.x)
						boundary_size.x = modX;
					if (height_ < y + blockPitch_.y)
						boundary_size.y = modY;
					if (depth_ < z + blockPitch_.z)
						boundary_size.y = modZ;

					XMUINT3 start_index = XMUINT3(0, 0, 0);
					XMUINT3 end_index = boundary_size;
					if (blk_idx.x > 0)
						start_index.x = -1;
					if (blk_idx.y > 0)
						start_index.y = -1;
					if (blk_idx.z > 0)
						start_index.z = -1;
					if (blk_idx.x == num_blocksX - 1)
						end_index.x += -1;
					if (blk_idx.y == num_blocksY - 1)
						end_index.y += -1;
					if (blk_idx.z == num_blocksZ - 1)
						end_index.z += -1;
					for (uint sub_z = start_index.z; sub_z <= end_index.z; sub_z++)
					{
						for (uint sub_y = start_index.y; sub_y <= end_index.y; sub_y++)
						{
							for (uint sub_x = start_index.x; sub_x <= end_index.x; sub_x++)
							{
								float v = 0;
								uint addr = (z + sub_z) * num_blocksXY + (y + sub_y) * num_blocksX + x + sub_x;
								switch (volFormat_)
								{
								case VolumeFormat::UINT8: v = (float)vol_data[addr]; break;
								case VolumeFormat::UINT16: v = (float)((uint16_t*)vol_data)[addr]; break;
								case VolumeFormat::FLOAT: v = ((float*)vol_data)[addr]; break;
								default: assert(0);
								}

								if (v < min_v)
									min_v = v;
								if (v > max_v)
									max_v = v;
							}
						}
					}

					uint block_x = x / blockPitch_.x;
					uint block_y = y / blockPitch_.y;
					uint block_z = z / blockPitch_.z;
					uint block_addr = block_z * num_blocksXY + block_y * num_blocksX + block_x;

					switch (volFormat_)
					{
					case VolumeFormat::UINT8: 
						((uint16_t*)block_data)[block_addr] = (uint8_t)min_v | (((uint8_t)max_v) << 8); break;
					case VolumeFormat::UINT16: 
						((uint32_t*)block_data)[block_addr] = (uint16_t)min_v | (((uint16_t)max_v) << 16); break;
					case VolumeFormat::FLOAT: 
						((XMFLOAT2*)block_data)[block_addr] = XMFLOAT2(min_v, max_v); break;
					default: assert(0);
					}
				}
			}
		}

		using namespace graphics;
		{
			GraphicsDevice* device = graphics::GetDevice();

			TextureDesc desc;
			desc.array_size = 1;
			desc.bind_flags = BindFlag::SHADER_RESOURCE;
			desc.width = num_blocksX;
			desc.height = num_blocksY;
			desc.depth = num_blocksZ;
			desc.mip_levels = 1;
			desc.array_size = 1;
			switch (volFormat_)
			{
			case VolumeComponent::VolumeFormat::UINT8:
				desc.format = Format::R8G8_UNORM; break;
			case VolumeComponent::VolumeFormat::UINT16:
				desc.format = Format::R16G16_UNORM; break;
			case VolumeComponent::VolumeFormat::FLOAT:
				desc.format = Format::R32G32_FLOAT; break;
			default:
				assert(0);
				break;
			}
			desc.layout = ResourceState::SHADER_RESOURCE;

			uint block_stride = GetFormatStride(desc.format);
			SubresourceData initdata;
			initdata.data_ptr = block_data;
			initdata.row_pitch = num_blocksX * block_stride;
			initdata.slice_pitch = initdata.row_pitch * num_blocksY;

			bool success = device->CreateTexture(&desc, &initdata, &volumeMinMaxBlocks_);
			device->SetName(&volumeMinMaxBlocks_, "volumeMinMaxBlocks_");
			success &= device->CreateSubresource(&volumeMinMaxBlocks_, SubresourceType::SRV, 0, -1, 0, -1) >= 0;

			assert(success);
		}
	}
}

namespace vz
{
#define GETTER_RES_GTI(RES, RET) TextureComponent* texture = compfactory::GetTextureComponent(texureEntity_); \
	Resource* RES = texture->resource_.get(); if (RES == nullptr) { backlog::post("Invalid Resource >> TextureComponent", backlog::LogLevel::Error); \
	return RET; };

	int GTextureInterface::GetSparseResidencymapDescriptor() const
	{
		GETTER_RES_GTI(resource, -1);
		return resource->sparse_residencymap_descriptor;
	}
	int GTextureInterface::GetSparseFeedbackmapDescriptor() const
	{
		GETTER_RES_GTI(resource, -1);
		return resource->sparse_feedbackmap_descriptor;
	}
	const graphics::Texture& GTextureInterface::GetTexture() const
	{
		static graphics::Texture empty;
		GETTER_RES_GTI(resource, empty);
		return resource->GetTexture();
	}
	void GTextureInterface::SetTexture(const graphics::Texture& texture_, int srgb_subresource)
	{
		GETTER_RES_GTI(resource, );
		resource->SetTexture(texture_, srgb_subresource);
	}

	const graphics::GPUResource* GTextureInterface::GetGPUResource() const {
		GETTER_RES_GTI(resource, nullptr);
		if (!texture->IsValid() || !GetTexture().IsValid())
			return nullptr;
		return &GetTexture();
	}
}
namespace vz
{
	uint32_t GTextureComponent::GetUVSet() const
	{
		GETTER_RES(resource, 0);
		return resource->uvset;
	}
	float GTextureComponent::GetLodClamp() const
	{
		GETTER_RES(resource, 0);
		return resource->lod_clamp;
	}

	int GTextureComponent::GetTextureSRGBSubresource() const
	{
		GETTER_RES(resource, -1);
		return resource->GetTextureSRGBSubresource();
	}

	
	void GTextureComponent::StreamingRequestResolution(uint32_t resolution)
	{
		GETTER_RES(resource, );
		resource->StreamingRequestResolution(resolution);
	}
}
