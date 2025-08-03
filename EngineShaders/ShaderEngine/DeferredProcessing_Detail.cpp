#include "RenderPath3D_Detail.h"
#include "GPUBVH.h"

namespace vz::renderer
{
	void GRenderPath3DDetails::ProcessDeferredResourceRequests(CommandList cmd)
	{
		std::lock_guard<std::mutex> lock(renderer::deferredResourceMutex);

		if (deferredGeometryGPUBVHGens.size() +
			deferredMIPGens.size() +
			deferredBCQueue.size() +
			deferredBufferUpdate.size() +
			deferredTextureCopy.size() == 0)
		{
			return;
		}

		// TODO: paint texture...
		//deferredResourceLock.lock();

		for (auto& it : deferredMIPGens)
		{
			MIPGEN_OPTIONS mipopt;
			mipopt.preserve_coverage = it.second;
			GenerateMipChain(it.first, MIPGENFILTER_LINEAR, cmd, mipopt);
		}

		deferredMIPGens.clear();
		for (auto& it : deferredBCQueue)
		{
			BlockCompress(it.first, it.second, cmd);
		}
		deferredBCQueue.clear();

		for (auto& it : deferredBufferUpdate)
		{
			GPUBuffer& buffer = it.first;
			void* data = it.second.first;
			size_t size = it.second.second;

			device->Barrier(GPUBarrier::Buffer(&buffer, ResourceState::SHADER_RESOURCE_COMPUTE, ResourceState::COPY_DST), cmd);
			device->UpdateBuffer(&buffer, data, cmd);
			device->Barrier(GPUBarrier::Buffer(&buffer, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE_COMPUTE), cmd);
		}
		deferredBufferUpdate.clear();

		for (auto& it : deferredTextureCopy)
		{
			Texture& src = it.first;
			Texture& dst = it.second;
			GPUBarrier barriers1[] = {
				//GPUBarrier::Image(&src, src.desc.layout, ResourceState::COPY_SRC),
				GPUBarrier::Image(&dst, ResourceState::SHADER_RESOURCE_COMPUTE, ResourceState::COPY_DST),
			};
			device->Barrier(barriers1, arraysize(barriers1), cmd);
			device->CopyResource(&dst, &src, cmd);
			GPUBarrier barriers2[] = {
				//GPUBarrier::Image(&src, ResourceState::COPY_SRC, src.desc.layout),
				GPUBarrier::Image(&dst, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE_COMPUTE),
			};
			device->Barrier(barriers2, arraysize(barriers2), cmd);
		}
		deferredTextureCopy.clear();

		std::vector<Entity> wait_entities;
		for (auto& it : deferredGeometryGPUBVHGens)
		{
			if (!gpubvh::UpdateGeometryGPUBVH(it, cmd)) {
				wait_entities.push_back(it);
			}
		}
		deferredGeometryGPUBVHGens.clear();
		deferredGeometryGPUBVHGens = wait_entities;

		//deferredResourceLock.unlock();
	}

	void GRenderPath3DDetails::GenerateMipChain(const Texture& texture, MIPGENFILTER filter, CommandList cmd, const MIPGEN_OPTIONS& options)
	{
		if (!texture.IsValid())
		{
			assert(0);
			return;
		}

		TextureDesc desc = texture.GetDesc();

		if (desc.mip_levels < 2)
		{
			assert(0);
			return;
		}

		bool hdr = !IsFormatUnorm(desc.format);

		MipgenPushConstants mipgen = {};

		if (options.preserve_coverage)
		{
			mipgen.mipgen_options |= MIPGEN_OPTION_BIT_PRESERVE_COVERAGE;
		}
		if (IsFormatSRGB(desc.format))
		{
			mipgen.mipgen_options |= MIPGEN_OPTION_BIT_SRGB;
		}

		if (desc.type == TextureDesc::Type::TEXTURE_1D)
		{
			assert(0); // not implemented
		}
		else if (desc.type == TextureDesc::Type::TEXTURE_2D)
		{

			if (has_flag(desc.misc_flags, ResourceMiscFlag::TEXTURECUBE))
			{

				if (desc.array_size > 6)
				{
					// Cubearray
					assert(options.arrayIndex >= 0 && "You should only filter a specific cube in the array for now, so provide its index!");

					switch (filter)
					{
					case MIPGENFILTER_POINT:
						device->EventBegin("GenerateMipChain CubeArray - PointFilter", cmd);
						device->BindComputeShader(&shaders[hdr ? CSTYPE_GENERATEMIPCHAINCUBEARRAY_FLOAT4 : CSTYPE_GENERATEMIPCHAINCUBEARRAY_UNORM4], cmd);
						mipgen.sampler_index = device->GetDescriptorIndex(&samplers[SAMPLER_POINT_CLAMP]);
						break;
					case MIPGENFILTER_LINEAR:
						device->EventBegin("GenerateMipChain CubeArray - LinearFilter", cmd);
						device->BindComputeShader(&shaders[hdr ? CSTYPE_GENERATEMIPCHAINCUBEARRAY_FLOAT4 : CSTYPE_GENERATEMIPCHAINCUBEARRAY_UNORM4], cmd);
						mipgen.sampler_index = device->GetDescriptorIndex(&samplers[SAMPLER_LINEAR_CLAMP]);
						break;
					default:
						assert(0);
						break;
					}

					for (uint32_t i = 0; i < desc.mip_levels - 1; ++i)
					{
						{
							GPUBarrier barriers[] = {
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, options.arrayIndex * 6 + 0),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, options.arrayIndex * 6 + 1),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, options.arrayIndex * 6 + 2),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, options.arrayIndex * 6 + 3),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, options.arrayIndex * 6 + 4),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, options.arrayIndex * 6 + 5),
							};
							device->Barrier(barriers, arraysize(barriers), cmd);
						}

						mipgen.texture_output = device->GetDescriptorIndex(&texture, SubresourceType::UAV, i + 1);
						mipgen.texture_input = device->GetDescriptorIndex(&texture, SubresourceType::SRV, i);
						desc.width = std::max(1u, desc.width / 2);
						desc.height = std::max(1u, desc.height / 2);

						mipgen.outputResolution.x = desc.width;
						mipgen.outputResolution.y = desc.height;
						mipgen.outputResolution_rcp.x = 1.0f / mipgen.outputResolution.x;
						mipgen.outputResolution_rcp.y = 1.0f / mipgen.outputResolution.y;
						mipgen.arrayIndex = options.arrayIndex;
						device->PushConstants(&mipgen, sizeof(mipgen), cmd);

						device->Dispatch(
							std::max(1u, (desc.width + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE),
							std::max(1u, (desc.height + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE),
							6,
							cmd);

						{
							GPUBarrier barriers[] = {
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, options.arrayIndex * 6 + 0),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, options.arrayIndex * 6 + 1),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, options.arrayIndex * 6 + 2),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, options.arrayIndex * 6 + 3),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, options.arrayIndex * 6 + 4),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, options.arrayIndex * 6 + 5),
							};
							device->Barrier(barriers, arraysize(barriers), cmd);
						}
					}
				}
				else
				{
					// Cubemap
					switch (filter)
					{
					case MIPGENFILTER_POINT:
						device->EventBegin("GenerateMipChain Cube - PointFilter", cmd);
						device->BindComputeShader(&shaders[hdr ? CSTYPE_GENERATEMIPCHAINCUBE_FLOAT4 : CSTYPE_GENERATEMIPCHAINCUBE_UNORM4], cmd);
						mipgen.sampler_index = device->GetDescriptorIndex(&samplers[SAMPLER_POINT_CLAMP]);
						break;
					case MIPGENFILTER_LINEAR:
						device->EventBegin("GenerateMipChain Cube - LinearFilter", cmd);
						device->BindComputeShader(&shaders[hdr ? CSTYPE_GENERATEMIPCHAINCUBE_FLOAT4 : CSTYPE_GENERATEMIPCHAINCUBE_UNORM4], cmd);
						mipgen.sampler_index = device->GetDescriptorIndex(&samplers[SAMPLER_LINEAR_CLAMP]);
						break;
					default:
						assert(0); // not implemented
						break;
					}

					for (uint32_t i = 0; i < desc.mip_levels - 1; ++i)
					{
						{
							GPUBarrier barriers[] = {
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, 0),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, 1),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, 2),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, 3),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, 4),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, 5),
							};
							device->Barrier(barriers, arraysize(barriers), cmd);
						}

						mipgen.texture_output = device->GetDescriptorIndex(&texture, SubresourceType::UAV, i + 1);
						mipgen.texture_input = device->GetDescriptorIndex(&texture, SubresourceType::SRV, i);
						desc.width = std::max(1u, desc.width / 2);
						desc.height = std::max(1u, desc.height / 2);

						mipgen.outputResolution.x = desc.width;
						mipgen.outputResolution.y = desc.height;
						mipgen.outputResolution_rcp.x = 1.0f / mipgen.outputResolution.x;
						mipgen.outputResolution_rcp.y = 1.0f / mipgen.outputResolution.y;
						mipgen.arrayIndex = 0;
						device->PushConstants(&mipgen, sizeof(mipgen), cmd);

						device->Dispatch(
							std::max(1u, (desc.width + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE),
							std::max(1u, (desc.height + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE),
							6,
							cmd);

						{
							GPUBarrier barriers[] = {
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, 0),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, 1),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, 2),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, 3),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, 4),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, 5),
							};
							device->Barrier(barriers, arraysize(barriers), cmd);
						}
					}
				}

			}
			else
			{
				// Texture
				switch (filter)
				{
				case MIPGENFILTER_POINT:
					device->EventBegin("GenerateMipChain 2D - PointFilter", cmd);
					device->BindComputeShader(&shaders[hdr ? CSTYPE_GENERATEMIPCHAIN2D_FLOAT4 : CSTYPE_GENERATEMIPCHAIN2D_UNORM4], cmd);
					mipgen.sampler_index = device->GetDescriptorIndex(&samplers[SAMPLER_POINT_CLAMP]);
					break;
				case MIPGENFILTER_LINEAR:
					device->EventBegin("GenerateMipChain 2D - LinearFilter", cmd);
					device->BindComputeShader(&shaders[hdr ? CSTYPE_GENERATEMIPCHAIN2D_FLOAT4 : CSTYPE_GENERATEMIPCHAIN2D_UNORM4], cmd);
					mipgen.sampler_index = device->GetDescriptorIndex(&samplers[SAMPLER_LINEAR_CLAMP]);
					break;
				case MIPGENFILTER_GAUSSIAN:
				{
					assert(options.gaussian_temp != nullptr); // needed for separate filter!
					device->EventBegin("GenerateMipChain 2D - GaussianFilter", cmd);
					// Gaussian filter is a bit different as we do it in a separable way:
					for (uint32_t i = 0; i < desc.mip_levels - 1; ++i)
					{
						Postprocess_Blur_Gaussian(texture, *options.gaussian_temp, texture, cmd, i, i + 1, options.wide_gauss);
					}
					device->EventEnd(cmd);
					return;
				}
				break;
				default:
					assert(0);
					break;
				}

				for (uint32_t i = 0; i < desc.mip_levels - 1; ++i)
				{
					{
						GPUBarrier barriers[] = {
							GPUBarrier::Image(&texture,texture.desc.layout,ResourceState::UNORDERED_ACCESS,i + 1),
						};
						device->Barrier(barriers, arraysize(barriers), cmd);
					}

					mipgen.texture_output = device->GetDescriptorIndex(&texture, SubresourceType::UAV, i + 1);
					mipgen.texture_input = device->GetDescriptorIndex(&texture, SubresourceType::SRV, i);
					desc.width = std::max(1u, desc.width / 2);
					desc.height = std::max(1u, desc.height / 2);

					mipgen.outputResolution.x = desc.width;
					mipgen.outputResolution.y = desc.height;
					mipgen.outputResolution_rcp.x = 1.0f / mipgen.outputResolution.x;
					mipgen.outputResolution_rcp.y = 1.0f / mipgen.outputResolution.y;
					mipgen.arrayIndex = options.arrayIndex >= 0 ? (uint)options.arrayIndex : 0;
					device->PushConstants(&mipgen, sizeof(mipgen), cmd);

					device->Dispatch(
						std::max(1u, (desc.width + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE),
						std::max(1u, (desc.height + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE),
						1,
						cmd);

					{
						GPUBarrier barriers[] = {
							GPUBarrier::Image(&texture,ResourceState::UNORDERED_ACCESS,texture.desc.layout,i + 1),
						};
						device->Barrier(barriers, arraysize(barriers), cmd);
					}
				}
			}


			device->EventEnd(cmd);
		}
		else if (desc.type == TextureDesc::Type::TEXTURE_3D)
		{
			switch (filter)
			{
			case MIPGENFILTER_POINT:
				device->EventBegin("GenerateMipChain 3D - PointFilter", cmd);
				device->BindComputeShader(&shaders[hdr ? CSTYPE_GENERATEMIPCHAIN3D_FLOAT4 : CSTYPE_GENERATEMIPCHAIN3D_UNORM4], cmd);
				mipgen.sampler_index = device->GetDescriptorIndex(&samplers[SAMPLER_POINT_CLAMP]);
				break;
			case MIPGENFILTER_LINEAR:
				device->EventBegin("GenerateMipChain 3D - LinearFilter", cmd);
				device->BindComputeShader(&shaders[hdr ? CSTYPE_GENERATEMIPCHAIN3D_FLOAT4 : CSTYPE_GENERATEMIPCHAIN3D_UNORM4], cmd);
				mipgen.sampler_index = device->GetDescriptorIndex(&samplers[SAMPLER_LINEAR_CLAMP]);
				break;
			default:
				assert(0); // not implemented
				break;
			}

			for (uint32_t i = 0; i < desc.mip_levels - 1; ++i)
			{
				mipgen.texture_output = device->GetDescriptorIndex(&texture, SubresourceType::UAV, i + 1);
				mipgen.texture_input = device->GetDescriptorIndex(&texture, SubresourceType::SRV, i);
				desc.width = std::max(1u, desc.width / 2);
				desc.height = std::max(1u, desc.height / 2);
				desc.depth = std::max(1u, desc.depth / 2);

				{
					GPUBarrier barriers[] = {
						GPUBarrier::Image(&texture,texture.desc.layout,ResourceState::UNORDERED_ACCESS,i + 1),
					};
					device->Barrier(barriers, arraysize(barriers), cmd);
				}

				mipgen.outputResolution.x = desc.width;
				mipgen.outputResolution.y = desc.height;
				mipgen.outputResolution.z = desc.depth;
				mipgen.outputResolution_rcp.x = 1.0f / mipgen.outputResolution.x;
				mipgen.outputResolution_rcp.y = 1.0f / mipgen.outputResolution.y;
				mipgen.outputResolution_rcp.z = 1.0f / mipgen.outputResolution.z;
				mipgen.arrayIndex = options.arrayIndex >= 0 ? (uint)options.arrayIndex : 0;
				mipgen.mipgen_options = 0;
				device->PushConstants(&mipgen, sizeof(mipgen), cmd);

				device->Dispatch(
					std::max(1u, (desc.width + GENERATEMIPCHAIN_3D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_3D_BLOCK_SIZE),
					std::max(1u, (desc.height + GENERATEMIPCHAIN_3D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_3D_BLOCK_SIZE),
					std::max(1u, (desc.depth + GENERATEMIPCHAIN_3D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_3D_BLOCK_SIZE),
					cmd);

				{
					GPUBarrier barriers[] = {
						GPUBarrier::Image(&texture,ResourceState::UNORDERED_ACCESS,texture.desc.layout,i + 1),
					};
					device->Barrier(barriers, arraysize(barriers), cmd);
				}
			}


			device->EventEnd(cmd);
		}
		else
		{
			assert(0);
		}
	}

	void GRenderPath3DDetails::BlockCompress(const graphics::Texture& texture_src, const graphics::Texture& texture_bc, graphics::CommandList cmd, uint32_t dst_slice_offset)
	{
		const uint32_t block_size = GetFormatBlockSize(texture_bc.desc.format);
		TextureDesc desc;
		desc.width = std::max(1u, texture_bc.desc.width / block_size);
		desc.height = std::max(1u, texture_bc.desc.height / block_size);
		desc.bind_flags = BindFlag::UNORDERED_ACCESS;
		desc.layout = ResourceState::UNORDERED_ACCESS;

		Texture bc_raw_dest;
		{
			// Find a raw block texture that will fit the request:
			static std::mutex locker;
			std::scoped_lock lock(locker);
			static Texture bc_raw_uint2;
			static Texture bc_raw_uint4;
			static Texture bc_raw_uint4_cubemap;
			Texture* bc_raw = nullptr;
			switch (texture_bc.desc.format)
			{
			case Format::BC1_UNORM:
			case Format::BC1_UNORM_SRGB:
				desc.format = Format::R32G32_UINT;
				bc_raw = &bc_raw_uint2;
				device->BindComputeShader(&shaders[CSTYPE_BLOCKCOMPRESS_BC1], cmd);
				device->EventBegin("BlockCompress - BC1", cmd);
				break;
			case Format::BC3_UNORM:
			case Format::BC3_UNORM_SRGB:
				desc.format = Format::R32G32B32A32_UINT;
				bc_raw = &bc_raw_uint4;
				device->BindComputeShader(&shaders[CSTYPE_BLOCKCOMPRESS_BC3], cmd);
				device->EventBegin("BlockCompress - BC3", cmd);
				break;
			case Format::BC4_UNORM:
				desc.format = Format::R32G32_UINT;
				bc_raw = &bc_raw_uint2;
				device->BindComputeShader(&shaders[CSTYPE_BLOCKCOMPRESS_BC4], cmd);
				device->EventBegin("BlockCompress - BC4", cmd);
				break;
			case Format::BC5_UNORM:
				desc.format = Format::R32G32B32A32_UINT;
				bc_raw = &bc_raw_uint4;
				device->BindComputeShader(&shaders[CSTYPE_BLOCKCOMPRESS_BC5], cmd);
				device->EventBegin("BlockCompress - BC5", cmd);
				break;
			case Format::BC6H_UF16:
				desc.format = Format::R32G32B32A32_UINT;
				if (has_flag(texture_src.desc.misc_flags, ResourceMiscFlag::TEXTURECUBE))
				{
					bc_raw = &bc_raw_uint4_cubemap;
					device->BindComputeShader(&shaders[CSTYPE_BLOCKCOMPRESS_BC6H_CUBEMAP], cmd);
					device->EventBegin("BlockCompress - BC6H - Cubemap", cmd);
					desc.array_size = texture_src.desc.array_size; // src array size not dst!!
				}
				else
				{
					bc_raw = &bc_raw_uint4;
					device->BindComputeShader(&shaders[CSTYPE_BLOCKCOMPRESS_BC6H], cmd);
					device->EventBegin("BlockCompress - BC6H", cmd);
				}
				break;
			default:
				assert(0); // not supported
				return;
			}

			if (!bc_raw->IsValid() || bc_raw->desc.width < desc.width || bc_raw->desc.height < desc.height || bc_raw->desc.array_size < desc.array_size)
			{
				TextureDesc bc_raw_desc = desc;
				bc_raw_desc.width = std::max(64u, bc_raw_desc.width);
				bc_raw_desc.height = std::max(64u, bc_raw_desc.height);
				bc_raw_desc.width = std::max(bc_raw->desc.width, bc_raw_desc.width);
				bc_raw_desc.height = std::max(bc_raw->desc.height, bc_raw_desc.height);
				bc_raw_desc.width = math::GetNextPowerOfTwo(bc_raw_desc.width);
				bc_raw_desc.height = math::GetNextPowerOfTwo(bc_raw_desc.height);
				device->CreateTexture(&bc_raw_desc, nullptr, bc_raw);
				device->SetName(bc_raw, "bc_raw");

				device->ClearUAV(bc_raw, 0, cmd);
				device->Barrier(GPUBarrier::Memory(bc_raw), cmd);

				std::string info;
				info += "BlockCompress created a new raw block texture to fit request: " + std::string(GetFormatString(texture_bc.desc.format)) + " (" + std::to_string(texture_bc.desc.width) + ", " + std::to_string(texture_bc.desc.height) + ")";
				info += "\n\tFormat = ";
				info += GetFormatString(bc_raw_desc.format);
				info += "\n\tResolution = " + std::to_string(bc_raw_desc.width) + " * " + std::to_string(bc_raw_desc.height);
				info += "\n\tArray Size = " + std::to_string(bc_raw_desc.array_size);
				size_t total_size = 0;
				total_size += ComputeTextureMemorySizeInBytes(bc_raw_desc);
				info += "\n\tMemory = " + helper::GetMemorySizeText(total_size) + "\n";
				backlog::post(info);
			}

			bc_raw_dest = *bc_raw;
		}

		for (uint32_t mip = 0; mip < texture_bc.desc.mip_levels; ++mip)
		{
			const uint32_t width = std::max(1u, desc.width >> mip);
			const uint32_t height = std::max(1u, desc.height >> mip);
			device->BindResource(&texture_src, 0, cmd, texture_src.desc.mip_levels == 1 ? -1 : mip);
			device->BindUAV(&bc_raw_dest, 0, cmd);
			device->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, desc.array_size, cmd);

			GPUBarrier barriers[] = {
				GPUBarrier::Image(&bc_raw_dest, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SRC),
				GPUBarrier::Image(&texture_bc, texture_bc.desc.layout, ResourceState::COPY_DST),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);

			for (uint32_t slice = 0; slice < desc.array_size; ++slice)
			{
				Box box;
				box.left = 0;
				box.right = width;
				box.top = 0;
				box.bottom = height;
				box.front = 0;
				box.back = 1;

				device->CopyTexture(
					&texture_bc, 0, 0, 0, mip, dst_slice_offset + slice,
					&bc_raw_dest, 0, slice,
					cmd,
					&box
				);
			}

			for (int i = 0; i < arraysize(barriers); ++i)
			{
				std::swap(barriers[i].image.layout_before, barriers[i].image.layout_after);
			}
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->EventEnd(cmd);
	}
}