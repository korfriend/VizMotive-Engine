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
	void TextureComponent::CopyFromData(const std::vector<uint8_t>& filedata)
	{
		GETTER_RES(resource, );
		assert(resource->GetFileData().size() == filedata.size());
		resource->CopyFromData(filedata);
		resource->SetOutdated();
	}
	void TextureComponent::MoveFromData(std::vector<uint8_t>&& filedata)
	{
		GETTER_RES(resource, );
		assert(resource->GetFileData().size() == filedata.size());
		resource->MoveFromData(std::move(filedata));
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

	void VolumeComponent::SetAlign(const XMFLOAT3& axisVolX, const XMFLOAT3& axisVolY, const bool isRHS)
	{
		XMVECTOR vec_axisy_os = XMLoadFloat3(&axisVolX);
		XMVECTOR vec_axisx_os = XMLoadFloat3(&axisVolY);
		XMVECTOR z_vec_rhs = XMVector3Cross(vec_axisy_os, vec_axisx_os); // note the z-dir in lookat
		XMVECTOR origin = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		XMMATRIX mat_t = VZMatrixLookTo(origin, z_vec_rhs, vec_axisy_os);
		XMMATRIX mat_rs2os = XMMatrixInverse(nullptr, mat_t);
		if (!isRHS)
		{
			XMMATRIX matInverseZ = XMMatrixScaling(1.f, 1.f, -1.f);
			mat_rs2os = matInverseZ * mat_rs2os;
		}
		XMStoreFloat4x4(&matAlign_, mat_rs2os);
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
			hasRenderData_ = true;
		}
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