#include "Helpers2.h"
#include "Platform.h"
#include "Backlog.h"
#include "EventHandler.h"
#include "Color.h"
#include "Helpers.h"
#include "Libs/Math.h"

#include "ThirdParty/lodepng.h"
#include "ThirdParty/dds.h"
#include "ThirdParty/stb_image_write.h"
#include "ThirdParty/basis_universal/encoder/basisu_comp.h"
#include "ThirdParty/basis_universal/encoder/basisu_gpu_texture.h"

#include <thread>
#include <locale>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <codecvt> // string conversion
#include <filesystem>
#include <vector>
#include <iostream>
#include <cstdlib>

#if defined(_WIN32)
#include <direct.h>
#include <Psapi.h> // GetProcessMemoryInfo
#include <Commdlg.h> // openfile
#include <WinBase.h>
#else
#include "Utility/portable-file-dialogs.h"
#endif // _WIN32

namespace vz::helper2
{
	using namespace vz::helper;

	std::string screenshot(const vz::graphics::SwapChain& swapchain, const std::string& name)
	{
		std::string directory;
		if (name.empty())
		{
			directory = std::filesystem::current_path().string() + "/screenshots";
		}
		else
		{
			directory = GetDirectoryFromPath(name);
		}

		if (!directory.empty()) //PE: Crash if only filename is used with no folder.
			DirectoryCreate(directory);

		std::string filename = name;
		if (filename.empty())
		{
			filename = directory + "/sc_" + getCurrentDateTimeAsString() + ".png";
		}

		bool result = saveTextureToFile(vz::graphics::GetDevice()->GetBackBuffer(&swapchain), filename);
		assert(result);

		if (result)
		{
			return filename;
		}
		return "";
	}

	bool saveTextureToMemory(const vz::graphics::Texture& texture, std::vector<uint8_t>& texturedata)
	{
		using namespace vz::graphics;

		GraphicsDevice* device = vz::graphics::GetDevice();

		TextureDesc desc = texture.GetDesc();

		Texture stagingTex;
		TextureDesc staging_desc = desc;
		staging_desc.usage = Usage::READBACK;
		staging_desc.layout = ResourceState::COPY_DST;
		staging_desc.bind_flags = BindFlag::NONE;
		staging_desc.misc_flags = ResourceMiscFlag::NONE;
		bool success = device->CreateTexture(&staging_desc, nullptr, &stagingTex);
		assert(success);

		CommandList cmd = device->BeginCommandList();

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Image(&texture,texture.desc.layout,ResourceState::COPY_SRC),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->CopyResource(&stagingTex, &texture, cmd);

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Image(&texture,ResourceState::COPY_SRC,texture.desc.layout),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->SubmitCommandLists();
		device->WaitForGPU();

		texturedata.clear();

		if (stagingTex.mapped_data != nullptr)
		{
			texturedata.resize(ComputeTextureMemorySizeInBytes(desc));

			const uint32_t data_stride = GetFormatStride(desc.format);
			const uint32_t block_size = GetFormatBlockSize(desc.format);
			const uint32_t num_blocks_x = desc.width / block_size;
			const uint32_t num_blocks_y = desc.height / block_size;
			size_t cpy_offset = 0;
			size_t subresourceIndex = 0;
			for (uint32_t layer = 0; layer < desc.array_size; ++layer)
			{
				uint32_t mip_width = num_blocks_x;
				uint32_t mip_height = num_blocks_y;
				uint32_t mip_depth = desc.depth;
				for (uint32_t mip = 0; mip < desc.mip_levels; ++mip)
				{
					assert(subresourceIndex < stagingTex.mapped_subresource_count);
					const SubresourceData& subresourcedata = stagingTex.mapped_subresources[subresourceIndex++];
					const size_t dst_rowpitch = mip_width * data_stride;
					for (uint32_t z = 0; z < mip_depth; ++z)
					{
						uint8_t* dst_slice = texturedata.data() + cpy_offset;
						uint8_t* src_slice = (uint8_t*)subresourcedata.data_ptr + subresourcedata.slice_pitch * z;
						for (uint32_t i = 0; i < mip_height; ++i)
						{
							std::memcpy(
								dst_slice + i * dst_rowpitch,
								src_slice + i * subresourcedata.row_pitch,
								dst_rowpitch
							);
						}
						cpy_offset += mip_height * dst_rowpitch;
					}

					mip_width = std::max(1u, mip_width / 2);
					mip_height = std::max(1u, mip_height / 2);
					mip_depth = std::max(1u, mip_depth / 2);
				}
			}
		}
		else
		{
			assert(0);
		}

		return stagingTex.mapped_data != nullptr;
	}

	bool saveTextureToMemoryFile(const vz::graphics::Texture& texture, const std::string& fileExtension, std::vector<uint8_t>& filedata)
	{
		using namespace vz::graphics;
		TextureDesc desc = texture.GetDesc();
		std::vector<uint8_t> texturedata;
		if (saveTextureToMemory(texture, texturedata))
		{
			return saveTextureToMemoryFile(texturedata, desc, fileExtension, filedata);
		}
		return false;
	}

	bool saveTextureToMemoryFile(const std::vector<uint8_t>& texturedata, const vz::graphics::TextureDesc& desc, const std::string& fileExtension, std::vector<uint8_t>& filedata)
	{
		using namespace vz::graphics;
		const uint32_t data_stride = GetFormatStride(desc.format);

		std::string extension = vz::helper::toUpper(fileExtension);

		if (extension.compare("DDS") == 0)
		{
			filedata.resize(sizeof(dds::Header) + texturedata.size());
			dds::DXGI_FORMAT dds_format = dds::DXGI_FORMAT_UNKNOWN;
			switch (desc.format)
			{
			case vz::graphics::Format::R32G32B32A32_FLOAT:
				dds_format = dds::DXGI_FORMAT_R32G32B32A32_FLOAT;
				break;
			case vz::graphics::Format::R32G32B32A32_UINT:
				dds_format = dds::DXGI_FORMAT_R32G32B32A32_UINT;
				break;
			case vz::graphics::Format::R32G32B32A32_SINT:
				dds_format = dds::DXGI_FORMAT_R32G32B32A32_SINT;
				break;
			case vz::graphics::Format::R32G32B32_FLOAT:
				dds_format = dds::DXGI_FORMAT_R32G32B32_FLOAT;
				break;
			case vz::graphics::Format::R32G32B32_UINT:
				dds_format = dds::DXGI_FORMAT_R32G32B32_UINT;
				break;
			case vz::graphics::Format::R32G32B32_SINT:
				dds_format = dds::DXGI_FORMAT_R32G32B32_SINT;
				break;
			case vz::graphics::Format::R16G16B16A16_FLOAT:
				dds_format = dds::DXGI_FORMAT_R16G16B16A16_FLOAT;
				break;
			case vz::graphics::Format::R16G16B16A16_UNORM:
				dds_format = dds::DXGI_FORMAT_R16G16B16A16_UNORM;
				break;
			case vz::graphics::Format::R16G16B16A16_UINT:
				dds_format = dds::DXGI_FORMAT_R16G16B16A16_UINT;
				break;
			case vz::graphics::Format::R16G16B16A16_SNORM:
				dds_format = dds::DXGI_FORMAT_R16G16B16A16_SNORM;
				break;
			case vz::graphics::Format::R16G16B16A16_SINT:
				dds_format = dds::DXGI_FORMAT_R16G16B16A16_SINT;
				break;
			case vz::graphics::Format::R32G32_FLOAT:
				dds_format = dds::DXGI_FORMAT_R32G32_FLOAT;
				break;
			case vz::graphics::Format::R32G32_UINT:
				dds_format = dds::DXGI_FORMAT_R32G32_UINT;
				break;
			case vz::graphics::Format::R32G32_SINT:
				dds_format = dds::DXGI_FORMAT_R32G32_SINT;
				break;
			case vz::graphics::Format::R10G10B10A2_UNORM:
				dds_format = dds::DXGI_FORMAT_R10G10B10A2_UNORM;
				break;
			case vz::graphics::Format::R10G10B10A2_UINT:
				dds_format = dds::DXGI_FORMAT_R10G10B10A2_UINT;
				break;
			case vz::graphics::Format::R11G11B10_FLOAT:
				dds_format = dds::DXGI_FORMAT_R11G11B10_FLOAT;
				break;
			case vz::graphics::Format::R8G8B8A8_UNORM:
				dds_format = dds::DXGI_FORMAT_R8G8B8A8_UNORM;
				break;
			case vz::graphics::Format::R8G8B8A8_UNORM_SRGB:
				dds_format = dds::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
				break;
			case vz::graphics::Format::R8G8B8A8_UINT:
				dds_format = dds::DXGI_FORMAT_R8G8B8A8_UINT;
				break;
			case vz::graphics::Format::R8G8B8A8_SNORM:
				dds_format = dds::DXGI_FORMAT_R8G8B8A8_SNORM;
				break;
			case vz::graphics::Format::R8G8B8A8_SINT:
				dds_format = dds::DXGI_FORMAT_R8G8B8A8_SINT;
				break;
			case vz::graphics::Format::B8G8R8A8_UNORM:
				dds_format = dds::DXGI_FORMAT_B8G8R8A8_UNORM;
				break;
			case vz::graphics::Format::B8G8R8A8_UNORM_SRGB:
				dds_format = dds::DXGI_FORMAT_R16G16_SINT;
				break;
			case vz::graphics::Format::R16G16_FLOAT:
				dds_format = dds::DXGI_FORMAT_R16G16_FLOAT;
				break;
			case vz::graphics::Format::R16G16_UNORM:
				dds_format = dds::DXGI_FORMAT_R16G16_UNORM;
				break;
			case vz::graphics::Format::R16G16_UINT:
				dds_format = dds::DXGI_FORMAT_R16G16_UINT;
				break;
			case vz::graphics::Format::R16G16_SNORM:
				dds_format = dds::DXGI_FORMAT_R16G16_SNORM;
				break;
			case vz::graphics::Format::R16G16_SINT:
				dds_format = dds::DXGI_FORMAT_R16G16_SINT;
				break;
			case vz::graphics::Format::D32_FLOAT:
			case vz::graphics::Format::R32_FLOAT:
				dds_format = dds::DXGI_FORMAT_R32_FLOAT;
				break;
			case vz::graphics::Format::R32_UINT:
				dds_format = dds::DXGI_FORMAT_R32_UINT;
				break;
			case vz::graphics::Format::R32_SINT:
				dds_format = dds::DXGI_FORMAT_R32_SINT;
				break;
			case vz::graphics::Format::R9G9B9E5_SHAREDEXP:
				dds_format = dds::DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
				break;
			case vz::graphics::Format::R8G8_UNORM:
				dds_format = dds::DXGI_FORMAT_R8G8_UNORM;
				break;
			case vz::graphics::Format::R8G8_UINT:
				dds_format = dds::DXGI_FORMAT_R8G8_UINT;
				break;
			case vz::graphics::Format::R8G8_SNORM:
				dds_format = dds::DXGI_FORMAT_R8G8_SNORM;
				break;
			case vz::graphics::Format::R8G8_SINT:
				dds_format = dds::DXGI_FORMAT_R8G8_SINT;
				break;
			case vz::graphics::Format::R16_FLOAT:
				dds_format = dds::DXGI_FORMAT_R16_FLOAT;
				break;
			case vz::graphics::Format::D16_UNORM:
			case vz::graphics::Format::R16_UNORM:
				dds_format = dds::DXGI_FORMAT_R16_UNORM;
				break;
			case vz::graphics::Format::R16_UINT:
				dds_format = dds::DXGI_FORMAT_R16_UINT;
				break;
			case vz::graphics::Format::R16_SNORM:
				dds_format = dds::DXGI_FORMAT_R16_SNORM;
				break;
			case vz::graphics::Format::R16_SINT:
				dds_format = dds::DXGI_FORMAT_R16_SINT;
				break;
			case vz::graphics::Format::R8_UNORM:
				dds_format = dds::DXGI_FORMAT_R8_UNORM;
				break;
			case vz::graphics::Format::R8_UINT:
				dds_format = dds::DXGI_FORMAT_R8_UINT;
				break;
			case vz::graphics::Format::R8_SNORM:
				dds_format = dds::DXGI_FORMAT_R8_SNORM;
				break;
			case vz::graphics::Format::R8_SINT:
				dds_format = dds::DXGI_FORMAT_R8_SINT;
				break;
			case vz::graphics::Format::BC1_UNORM:
				dds_format = dds::DXGI_FORMAT_BC1_UNORM;
				break;
			case vz::graphics::Format::BC1_UNORM_SRGB:
				dds_format = dds::DXGI_FORMAT_BC1_UNORM_SRGB;
				break;
			case vz::graphics::Format::BC2_UNORM:
				dds_format = dds::DXGI_FORMAT_BC2_UNORM;
				break;
			case vz::graphics::Format::BC2_UNORM_SRGB:
				dds_format = dds::DXGI_FORMAT_BC2_UNORM_SRGB;
				break;
			case vz::graphics::Format::BC3_UNORM:
				dds_format = dds::DXGI_FORMAT_BC3_UNORM;
				break;
			case vz::graphics::Format::BC3_UNORM_SRGB:
				dds_format = dds::DXGI_FORMAT_BC3_UNORM_SRGB;
				break;
			case vz::graphics::Format::BC4_UNORM:
				dds_format = dds::DXGI_FORMAT_BC4_UNORM;
				break;
			case vz::graphics::Format::BC4_SNORM:
				dds_format = dds::DXGI_FORMAT_BC4_SNORM;
				break;
			case vz::graphics::Format::BC5_UNORM:
				dds_format = dds::DXGI_FORMAT_BC5_UNORM;
				break;
			case vz::graphics::Format::BC5_SNORM:
				dds_format = dds::DXGI_FORMAT_BC5_SNORM;
				break;
			case vz::graphics::Format::BC6H_UF16:
				dds_format = dds::DXGI_FORMAT_BC6H_UF16;
				break;
			case vz::graphics::Format::BC6H_SF16:
				dds_format = dds::DXGI_FORMAT_BC6H_SF16;
				break;
			case vz::graphics::Format::BC7_UNORM:
				dds_format = dds::DXGI_FORMAT_BC7_UNORM;
				break;
			case vz::graphics::Format::BC7_UNORM_SRGB:
				dds_format = dds::DXGI_FORMAT_BC7_UNORM_SRGB;
				break;
			default:
				assert(0);
				return false;
			}
			dds::write_header(
				filedata.data(),
				dds_format,
				desc.width,
				desc.type == TextureDesc::Type::TEXTURE_1D ? 0 : desc.height,
				desc.mip_levels,
				desc.array_size,
				has_flag(desc.misc_flags, ResourceMiscFlag::TEXTURECUBE),
				desc.type == TextureDesc::Type::TEXTURE_3D ? desc.depth : 0
			);
			std::memcpy(filedata.data() + sizeof(dds::Header), texturedata.data(), texturedata.size());
			return true;
		}

		const bool is_png = extension.compare("PNG") == 0;

		if (is_png)
		{
			if (desc.format == Format::R16_UNORM || desc.format == Format::R16_UINT)
			{
				// Specialized handling for 16-bit single channel PNG:
				std::vector<uint8_t> src_bigendian = texturedata;
				uint16_t* dest = (uint16_t*)src_bigendian.data();
				for (uint32_t i = 0; i < desc.width * desc.height; ++i)
				{
					uint16_t r = dest[i];
					r = (r >> 8) | ((r & 0xFF) << 8); // little endian to big endian
					dest[i] = r;
				}
				unsigned error = lodepng::encode(filedata, src_bigendian, desc.width, desc.height, LCT_GREY, 16);
				return error == 0;
			}
			if (desc.format == Format::R16G16_UNORM || desc.format == Format::R16G16_UINT)
			{
				// Specialized handling for 16-bit PNG:
				//	Two channel RG data is expanded to RGBA (2-channel PNG is not good because that is interpreted as red and alpha)
				std::vector<uint8_t> src_bigendian = texturedata;
				const uint32_t* src_rg = (const uint32_t*)src_bigendian.data();
				std::vector<vz::Color16> dest_rgba(desc.width * desc.height);
				for (uint32_t i = 0; i < desc.width * desc.height; ++i)
				{
					uint32_t rg = src_rg[i];
					vz::Color16& rgba = dest_rgba[i];
					uint16_t r = rg & 0xFFFF;
					r = (r >> 8) | ((r & 0xFF) << 8); // little endian to big endian
					uint16_t g = (rg >> 16u) & 0xFFFF;
					g = (g >> 8) | ((g & 0xFF) << 8); // little endian to big endian
					rgba = vz::Color16(r, g, 0xFFFF, 0xFFFF);
				}
				unsigned error = lodepng::encode(filedata, (const unsigned char*)dest_rgba.data(), desc.width, desc.height, LCT_RGBA, 16);
				return error == 0;
			}
			if (desc.format == Format::R16G16B16A16_UNORM || desc.format == Format::R16G16B16A16_UINT)
			{
				// Specialized handling for 16-bit PNG:
				std::vector<uint8_t> src_bigendian = texturedata;
				vz::Color16* dest = (vz::Color16*)src_bigendian.data();
				for (uint32_t i = 0; i < desc.width * desc.height; ++i)
				{
					vz::Color16 rgba = dest[i];
					uint16_t r = rgba.getR();
					r = (r >> 8) | ((r & 0xFF) << 8); // little endian to big endian
					uint16_t g = rgba.getG();
					g = (g >> 8) | ((g & 0xFF) << 8); // little endian to big endian
					uint16_t b = rgba.getB();
					b = (b >> 8) | ((b & 0xFF) << 8); // little endian to big endian
					uint16_t a = rgba.getA();
					a = (a >> 8) | ((a & 0xFF) << 8); // little endian to big endian
					rgba = vz::Color16(r, g, b, a);
					dest[i] = rgba;
				}
				unsigned error = lodepng::encode(filedata, src_bigendian, desc.width, desc.height, LCT_RGBA, 16);
				return error == 0;
			}
		}

		struct MipDesc
		{
			const uint8_t* address = nullptr;
			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t depth = 0;
		};
		std::vector<MipDesc> mips;
		mips.reserve(desc.mip_levels);

		uint32_t data_count = 0;
		uint32_t mip_width = desc.width;
		uint32_t mip_height = desc.height;
		uint32_t mip_depth = desc.depth;
		for (uint32_t mip = 0; mip < desc.mip_levels; ++mip)
		{
			MipDesc& mipdesc = mips.emplace_back();
			mipdesc.address = texturedata.data() + data_count * data_stride;
			data_count += mip_width * mip_height * mip_depth;
			mipdesc.width = mip_width;
			mipdesc.height = mip_height;
			mipdesc.depth = mip_depth;
			mip_width = std::max(1u, mip_width / 2);
			mip_height = std::max(1u, mip_height / 2);
			mip_depth = std::max(1u, mip_depth / 2);
		}

		bool basis = !extension.compare("BASIS");
		bool ktx2 = !extension.compare("KTX2");
		basisu::image basis_image;
		basisu::vector<basisu::image> basis_mipmaps;

		int dst_channel_count = 4;
		if (desc.format == Format::R10G10B10A2_UNORM)
		{
			// This will be converted first to rgba8 before saving to common format:
			uint32_t* data32 = (uint32_t*)texturedata.data();

			for (uint32_t i = 0; i < data_count; ++i)
			{
				uint32_t pixel = data32[i];
				float r = ((pixel >> 0) & 1023) / 1023.0f;
				float g = ((pixel >> 10) & 1023) / 1023.0f;
				float b = ((pixel >> 20) & 1023) / 1023.0f;
				float a = ((pixel >> 30) & 3) / 3.0f;

				uint32_t rgba8 = 0;
				rgba8 |= (uint32_t)(r * 255.0f) << 0;
				rgba8 |= (uint32_t)(g * 255.0f) << 8;
				rgba8 |= (uint32_t)(b * 255.0f) << 16;
				rgba8 |= (uint32_t)(a * 255.0f) << 24;

				data32[i] = rgba8;
			}
		}
		else if (desc.format == Format::R32G32B32A32_FLOAT)
		{
			// This will be converted first to rgba8 before saving to common format:
			XMFLOAT4* dataSrc = (XMFLOAT4*)texturedata.data();
			uint32_t* data32 = (uint32_t*)texturedata.data();

			for (uint32_t i = 0; i < data_count; ++i)
			{
				XMFLOAT4 pixel = dataSrc[i];
				float r = std::max(0.0f, std::min(pixel.x, 1.0f));
				float g = std::max(0.0f, std::min(pixel.y, 1.0f));
				float b = std::max(0.0f, std::min(pixel.z, 1.0f));
				float a = std::max(0.0f, std::min(pixel.w, 1.0f));

				uint32_t rgba8 = 0;
				rgba8 |= (uint32_t)(r * 255.0f) << 0;
				rgba8 |= (uint32_t)(g * 255.0f) << 8;
				rgba8 |= (uint32_t)(b * 255.0f) << 16;
				rgba8 |= (uint32_t)(a * 255.0f) << 24;

				data32[i] = rgba8;
			}
		}
		else if (desc.format == Format::R16G16B16A16_FLOAT)
		{
			// This will be converted first to rgba8 before saving to common format:
			XMHALF4* dataSrc = (XMHALF4*)texturedata.data();
			uint32_t* data32 = (uint32_t*)texturedata.data();

			for (uint32_t i = 0; i < data_count; ++i)
			{
				XMHALF4 pixel = dataSrc[i];
				float r = std::max(0.0f, std::min(XMConvertHalfToFloat(pixel.x), 1.0f));
				float g = std::max(0.0f, std::min(XMConvertHalfToFloat(pixel.y), 1.0f));
				float b = std::max(0.0f, std::min(XMConvertHalfToFloat(pixel.z), 1.0f));
				float a = std::max(0.0f, std::min(XMConvertHalfToFloat(pixel.w), 1.0f));

				uint32_t rgba8 = 0;
				rgba8 |= (uint32_t)(r * 255.0f) << 0;
				rgba8 |= (uint32_t)(g * 255.0f) << 8;
				rgba8 |= (uint32_t)(b * 255.0f) << 16;
				rgba8 |= (uint32_t)(a * 255.0f) << 24;

				data32[i] = rgba8;
			}
		}
		else if (desc.format == Format::R16G16B16A16_UNORM || desc.format == Format::R16G16B16A16_UINT)
		{
			// This will be converted first to rgba8 before saving to common format:
			vz::Color16* dataSrc = (vz::Color16*)texturedata.data();
			vz::Color* data32 = (vz::Color*)texturedata.data();

			for (uint32_t i = 0; i < data_count; ++i)
			{
				vz::Color16 pixel16 = dataSrc[i];
				data32[i] = vz::Color::fromFloat4(pixel16.toFloat4());
			}
		}
		else if (desc.format == Format::R11G11B10_FLOAT)
		{
			// This will be converted first to rgba8 before saving to common format:
			XMFLOAT3PK* dataSrc = (XMFLOAT3PK*)texturedata.data();
			uint32_t* data32 = (uint32_t*)texturedata.data();

			for (uint32_t i = 0; i < data_count; ++i)
			{
				XMFLOAT3PK pixel = dataSrc[i];
				XMVECTOR V = XMLoadFloat3PK(&pixel);
				XMFLOAT3 pixel3;
				XMStoreFloat3(&pixel3, V);
				float r = std::max(0.0f, std::min(pixel3.x, 1.0f));
				float g = std::max(0.0f, std::min(pixel3.y, 1.0f));
				float b = std::max(0.0f, std::min(pixel3.z, 1.0f));
				float a = 1;

				uint32_t rgba8 = 0;
				rgba8 |= (uint32_t)(r * 255.0f) << 0;
				rgba8 |= (uint32_t)(g * 255.0f) << 8;
				rgba8 |= (uint32_t)(b * 255.0f) << 16;
				rgba8 |= (uint32_t)(a * 255.0f) << 24;

				data32[i] = rgba8;
			}
		}
		else if (desc.format == Format::R9G9B9E5_SHAREDEXP)
		{
			// This will be converted first to rgba8 before saving to common format:
			XMFLOAT3SE* dataSrc = (XMFLOAT3SE*)texturedata.data();
			uint32_t* data32 = (uint32_t*)texturedata.data();

			for (uint32_t i = 0; i < data_count; ++i)
			{
				XMFLOAT3SE pixel = dataSrc[i];
				XMVECTOR V = XMLoadFloat3SE(&pixel);
				XMFLOAT3 pixel3;
				XMStoreFloat3(&pixel3, V);
				float r = std::max(0.0f, std::min(pixel3.x, 1.0f));
				float g = std::max(0.0f, std::min(pixel3.y, 1.0f));
				float b = std::max(0.0f, std::min(pixel3.z, 1.0f));
				float a = 1;

				uint32_t rgba8 = 0;
				rgba8 |= (uint32_t)(r * 255.0f) << 0;
				rgba8 |= (uint32_t)(g * 255.0f) << 8;
				rgba8 |= (uint32_t)(b * 255.0f) << 16;
				rgba8 |= (uint32_t)(a * 255.0f) << 24;

				data32[i] = rgba8;
			}
		}
		else if (desc.format == Format::B8G8R8A8_UNORM || desc.format == Format::B8G8R8A8_UNORM_SRGB)
		{
			// This will be converted first to rgba8 before saving to common format:
			uint32_t* data32 = (uint32_t*)texturedata.data();

			for (uint32_t i = 0; i < data_count; ++i)
			{
				uint32_t pixel = data32[i];
				uint8_t b = (pixel >> 0u) & 0xFF;
				uint8_t g = (pixel >> 8u) & 0xFF;
				uint8_t r = (pixel >> 16u) & 0xFF;
				uint8_t a = (pixel >> 24u) & 0xFF;
				data32[i] = r | (g << 8u) | (b << 16u) | (a << 24u);
			}
		}
		else if (desc.format == Format::R8_UNORM)
		{
			// This can be saved by reducing target channel count, no conversion needed
			dst_channel_count = 1;
		}
		else if (desc.format == Format::R8G8_UNORM)
		{
			// This can be saved by reducing target channel count, no conversion needed
			dst_channel_count = 2;
		}
		else if (IsFormatBlockCompressed(desc.format))
		{
			basisu::texture_format fmt;
			switch (desc.format)
			{
			default:
				assert(0);
				return false;
			case Format::BC1_UNORM:
			case Format::BC1_UNORM_SRGB:
				fmt = basisu::texture_format::cBC1;
				break;
			case Format::BC3_UNORM:
			case Format::BC3_UNORM_SRGB:
				fmt = basisu::texture_format::cBC3;
				break;
			case Format::BC4_UNORM:
				fmt = basisu::texture_format::cBC4;
				break;
			case Format::BC5_UNORM:
				fmt = basisu::texture_format::cBC5;
				break;
			case Format::BC7_UNORM:
			case Format::BC7_UNORM_SRGB:
				fmt = basisu::texture_format::cBC7;
				break;
			}
			basisu::gpu_image basis_gpu_image;
			basis_gpu_image.init(fmt, desc.width, desc.height);
			std::memcpy(basis_gpu_image.get_ptr(), texturedata.data(), std::min(texturedata.size(), (size_t)basis_gpu_image.get_size_in_bytes()));
			basis_gpu_image.unpack(basis_image);
		}
		else
		{
			assert(desc.format == Format::R8G8B8A8_UNORM || desc.format == Format::R8G8B8A8_UNORM_SRGB); // If you need to save other texture format, implement data conversion for it
		}

		if (basis || ktx2)
		{
			if (basis_image.get_total_pixels() == 0)
			{
				basis_image.init(texturedata.data(), desc.width, desc.height, 4);
				if (desc.mip_levels > 1)
				{
					basis_mipmaps.reserve(desc.mip_levels - 1);
					for (uint32_t mip = 1; mip < desc.mip_levels; ++mip)
					{
						basisu::image basis_mip;
						const MipDesc& mipdesc = mips[mip];
						basis_mip.init(mipdesc.address, mipdesc.width, mipdesc.height, 4);
						basis_mipmaps.push_back(basis_mip);
					}
				}
			}
			static bool encoder_initialized = false;
			if (!encoder_initialized)
			{
				encoder_initialized = true;
				basisu::basisu_encoder_init(false, false);
			}
			basisu::basis_compressor_params params;
			params.m_source_images.push_back(basis_image);
			if (desc.mip_levels > 1)
			{
				params.m_source_mipmap_images.push_back(basis_mipmaps);
			}
			if (ktx2)
			{
				params.m_create_ktx2_file = true;
			}
			else
			{
				params.m_create_ktx2_file = false;
			}
#if 1
			params.m_compression_level = basisu::BASISU_DEFAULT_COMPRESSION_LEVEL;
#else
			params.m_compression_level = basisu::BASISU_MAX_COMPRESSION_LEVEL;
#endif
			// Disable CPU mipmap generation:
			//	instead we provide mipmap data that was downloaded from the GPU with m_source_mipmap_images.
			//	This is better, because engine specific mipgen options will be retained, such as coverage preserving mipmaps
			params.m_mip_gen = false;
			params.m_quality_level = basisu::BASISU_QUALITY_MAX;
			params.m_multithreading = true;
			int num_threads = std::max(1u, std::thread::hardware_concurrency());
			basisu::job_pool jpool(num_threads);
			params.m_pJob_pool = &jpool;
			basisu::basis_compressor compressor;
			if (compressor.init(params))
			{
				auto result = compressor.process();
				if (result == basisu::basis_compressor::cECSuccess)
				{
					if (basis)
					{
						const auto& basis_file = compressor.get_output_basis_file();
						filedata.resize(basis_file.size());
						std::memcpy(filedata.data(), basis_file.data(), basis_file.size());
						return true;
					}
					else if (ktx2)
					{
						const auto& ktx2_file = compressor.get_output_ktx2_file();
						filedata.resize(ktx2_file.size());
						std::memcpy(filedata.data(), ktx2_file.data(), ktx2_file.size());
						return true;
					}
				}
				else
				{
					vz::backlog::post("basisu::basis_compressor::process() failure!", vz::backlog::LogLevel::Error);
					assert(0);
				}
			}
			return false;
		}

		int write_result = 0;

		filedata.clear();
		stbi_write_func* func = [](void* context, void* data, int size) {
			std::vector<uint8_t>& filedata = *(std::vector<uint8_t>*)context;
			for (int i = 0; i < size; ++i)
			{
				filedata.push_back(*((uint8_t*)data + i));
			}
			};

		static int mip_request = 0; // you can use this while debugging to write specific mip level to file (todo: option param?)
		const MipDesc& mip = mips[mip_request];

		if (is_png)
		{
			write_result = stbi_write_png_to_func(func, &filedata, (int)mip.width, (int)mip.height, dst_channel_count, mip.address, 0);
		}
		else if (!extension.compare("JPG") || !extension.compare("JPEG"))
		{
			write_result = stbi_write_jpg_to_func(func, &filedata, (int)mip.width, (int)mip.height, dst_channel_count, mip.address, 100);
		}
		else if (!extension.compare("TGA"))
		{
			write_result = stbi_write_tga_to_func(func, &filedata, (int)mip.width, (int)mip.height, dst_channel_count, mip.address);
		}
		else if (!extension.compare("BMP"))
		{
			write_result = stbi_write_bmp_to_func(func, &filedata, (int)mip.width, (int)mip.height, dst_channel_count, mip.address);
		}
		else
		{
			assert(0 && "Unsupported extension");
		}

		return write_result != 0;
	}

	bool saveTextureToFile(const vz::graphics::Texture& texture, const std::string& fileName)
	{
		using namespace vz::graphics;
		TextureDesc desc = texture.GetDesc();
		std::vector<uint8_t> data;
		if (saveTextureToMemory(texture, data))
		{
			return saveTextureToFile(data, desc, fileName);
		}
		return false;
	}

	bool saveTextureToFile(const std::vector<uint8_t>& texturedata, const vz::graphics::TextureDesc& desc, const std::string& fileName)
	{
		using namespace vz::graphics;

		std::string ext = GetExtensionFromFileName(fileName);
		std::vector<uint8_t> filedata;
		if (saveTextureToMemoryFile(texturedata, desc, ext, filedata))
		{
			return FileWrite(fileName, filedata.data(), filedata.size());
		}

		return false;
	}
}
