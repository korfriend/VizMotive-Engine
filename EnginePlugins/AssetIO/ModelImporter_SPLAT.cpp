#include "AssetIO.h"
#include "Components/GComponents.h"
#include "Utils/Backlog.h"
#include "Utils/Helpers.h"
#include "Utils/vzMath.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>

using namespace vz;

struct SplatAttributes {
    std::vector<float> positions;     // xyz positions
    std::vector<float> scales;        // xyz scales
    std::vector<float> rotations;     // quaternions (xyzw)
    std::vector<float> shCoefficients; // SH coefficients for RGB channels
    std::vector<float> opacities;     // alpha values

    void clear()
    {
		positions.clear();
        scales.clear();
        rotations.clear();
        shCoefficients.clear();
        opacities.clear();
    }

    uint32_t numSplats() const {
        return (uint32_t)(positions.size() / 3);
    }
};

uint32_t parseSplatData(SplatAttributes& attributes, const uint8_t* splatData, size_t dataSize) {

    // Original data buffer structure:
    // - Each splat is 32 bytes (rowLength)
    // - 0-11 bytes: xyz position (float32 * 3)
    // - 12-23 bytes: xyz scale (float32 * 3)
    // - 24-27 bytes: rgba color (uint8 * 4)
    // - 28-31 bytes: quaternion rotation (uint8 * 4)

    const int rowLength = 32; // Size of each splat in bytes
    const int vertexCount = dataSize / rowLength;

    // Pre-allocate vectors to the required size
    attributes.positions.reserve(vertexCount * 3);
    attributes.scales.reserve(vertexCount * 3);
    attributes.rotations.reserve(vertexCount * 4);
    attributes.shCoefficients.reserve(vertexCount * 3);  // Basic RGB (can be expanded for SH coefficients)
    attributes.opacities.reserve(vertexCount);

    for (int i = 0; i < vertexCount; i++) {
        const float* f_buffer = reinterpret_cast<const float*>(splatData + i * rowLength);
        const uint8_t* u_buffer = splatData + i * rowLength;

        // Extract positions (xyz)
        attributes.positions.push_back(f_buffer[0]); // x
        attributes.positions.push_back(f_buffer[1]); // y
        attributes.positions.push_back(f_buffer[2]); // z

        // Extract scales (xyz)
        attributes.scales.push_back(f_buffer[3]); // scale_x
        attributes.scales.push_back(f_buffer[4]); // scale_y
        attributes.scales.push_back(f_buffer[5]); // scale_z

        // Extract colors as SH coefficients (currently just base RGB)
        // Would need more space for higher-order SH coefficients
        attributes.shCoefficients.push_back(u_buffer[24] / 255.0f); // r
        attributes.shCoefficients.push_back(u_buffer[25] / 255.0f); // g
        attributes.shCoefficients.push_back(u_buffer[26] / 255.0f); // b

        // Extract opacity
        attributes.opacities.push_back(u_buffer[27] / 255.0f); // alpha

        // Extract rotation (quaternion xyzw)
        // Decompressing from uint8 representation
        attributes.rotations.push_back((u_buffer[28] - 128) / 128.0f); // qx
        attributes.rotations.push_back((u_buffer[29] - 128) / 128.0f); // qy
        attributes.rotations.push_back((u_buffer[30] - 128) / 128.0f); // qz
        attributes.rotations.push_back((u_buffer[31] - 128) / 128.0f); // qw
    }

    return attributes.numSplats();
}

// Function to expand basic RGB colors to SH coefficients
std::vector<float> expandToSH(const std::vector<float>& baseCoefficients, int shOrder) {
    // Expand RGB colors to SH coefficients
    // Currently handles only 0th order (basic RGB) as an example
    const float SH_C0 = 0.28209479177387814f;
    std::vector<float> shCoeffs;

    int numCoeffs = shOrder * shOrder; // Number of SH coefficients
    int rgbCount = baseCoefficients.size() / 3;

    shCoeffs.resize(rgbCount * 3 * numCoeffs);

    for (int i = 0; i < rgbCount; i++) {
        // 0th order coefficients are based on base color transformation
        // Process each color channel
        for (int c = 0; c < 3; c++) {
            // 0th order term is inverse transformation of base color
            shCoeffs[i * 3 * numCoeffs + c * numCoeffs + 0] =
                (baseCoefficients[i * 3 + c] - 0.5f) / SH_C0;

            // Higher-order SH coefficients are set to 0 by default
            // (Would be set here if additional data were available)
            for (int j = 1; j < numCoeffs; j++) {
                shCoeffs[i * 3 * numCoeffs + c * numCoeffs + j] = 0.0f;
            }
        }
    }

    return shCoeffs;
}

// Define SceneFormat enum class
enum class SplatFormat {
	UNKNOWN,
	PLY,
	SPLAT,
	KSPLAT,
	SPZ,
};

// Function to determine SplatFormat from file path
SplatFormat splatFormatFromPath(const std::string& path) {
	// Lambda function to check if string ends with a specific suffix (case insensitive)
	auto endsWithIgnoreCase = [](const std::string& str, const std::string& suffix) {
		if (str.size() < suffix.size()) return false;

		// Get substring for comparison
		std::string strEnd = str.substr(str.size() - suffix.size());

		// Convert both to lowercase for case-insensitive comparison
		std::transform(strEnd.begin(), strEnd.end(), strEnd.begin(),
			[](unsigned char c) { return std::tolower(c); });

		std::string suffixLower = suffix;
		std::transform(suffixLower.begin(), suffixLower.end(), suffixLower.begin(),
			[](unsigned char c) { return std::tolower(c); });

		return strEnd == suffixLower;
		};

	// Check file extension and return appropriate format
	if (endsWithIgnoreCase(path, ".ply")) return SplatFormat::PLY;
	else if (endsWithIgnoreCase(path, ".splat")) return SplatFormat::SPLAT;
	else if (endsWithIgnoreCase(path, ".ksplat")) return SplatFormat::KSPLAT;
	else if (endsWithIgnoreCase(path, ".spz")) return SplatFormat::SPZ;

	// Return Unknown for unsupported formats
	return SplatFormat::UNKNOWN;
}

void updateRenderDataGaussianSplatting(GGeometryComponent* geometry, const SplatAttributes& splatData)
{
	geometry->UpdateCustomRenderData([&](graphics::GraphicsDevice* device) {

		using namespace graphics;
		using Vertex_POS32W = GGeometryComponent::Vertex_POS32W;
		using GPrimBuffers = GGeometryComponent::GPrimBuffers;
		using Primitive = GeometryComponent::Primitive;

		const size_t position_stride = GetFormatStride(geometry->positionFormat);

		for (size_t part_index = 0, n = geometry->GetNumParts(); part_index < n; ++part_index)
		{
			const Primitive& primitive = *geometry->GetPrimitive(part_index);
			assert(primitive.GetPrimitiveType() == GeometryComponent::PrimitiveType::POINTS);
			assert(primitive.IsValid());

			GPrimBuffers& part_buffers = *geometry->GetGPrimBuffer(part_index);
			GPUBufferDesc bd;
			if (device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
			{
				// In UMA mode, it is better to create UPLOAD buffer, this avoids one copy from UPLOAD to DEFAULT
				bd.usage = Usage::UPLOAD;
			}
			else
			{
				bd.usage = Usage::DEFAULT;
			}
			bd.bind_flags = BindFlag::VERTEX_BUFFER | BindFlag::SHADER_RESOURCE;
			bd.misc_flags = ResourceMiscFlag::BUFFER_RAW | ResourceMiscFlag::TYPED_FORMAT_CASTING | ResourceMiscFlag::NO_DEFAULT_DESCRIPTORS;
			if (device->CheckCapability(GraphicsDeviceCapability::RAYTRACING))
			{
				bd.misc_flags |= ResourceMiscFlag::RAY_TRACING;
			}
			const uint64_t alignment = device->GetMinOffsetAlignment(&bd);

			const std::vector<XMFLOAT3>& vertex_positions = primitive.GetVtxPositions();

			bd.size = AlignTo(vertex_positions.size() * position_stride, alignment);

			GPUBuffer& generalBuffer = part_buffers.generalBuffer;
			BufferView& vb_pos = part_buffers.vbPosW;
			
			auto init_callback = [&](void* dest) {

				uint8_t* buffer_data = (uint8_t*)dest;
				uint64_t buffer_offset = 0ull;

				// vertexBuffer - POSITION: Vertex_POS32W::FORMAT
				{
					vb_pos.offset = buffer_offset;
					vb_pos.size = vertex_positions.size() * sizeof(Vertex_POS32W);
					Vertex_POS32W* vertices = (Vertex_POS32W*)(buffer_data + buffer_offset);
					//buffer_offset += AlignTo(vb_pos.size, alignment);
					for (size_t i = 0; i < vertex_positions.size(); ++i)
					{
						const XMFLOAT3& pos = vertex_positions[i];
						// something special?? e.g., density or probability for volume-geometric processing
						const uint8_t weight = 0; // vertex_weights.empty() ? 0xFF : vertex_weights[i];
						Vertex_POS32W vert;
						vert.FromFULL(pos, weight);
						std::memcpy(vertices + i, &vert, sizeof(vert));
					}

					//std::memcpy(buffer_data, vertex_positions.data(), sizeof(XMFLOAT3) * vertex_positions.size());
				}
				};

			bool success = device->CreateBuffer2(&bd, init_callback, &part_buffers.generalBuffer);
			assert(success);
			device->SetName(&part_buffers.generalBuffer, "GGeometryComponent::bufferHandle_::generalBuffer (Gaussian Splatting)");

			assert(vb_pos.IsValid());
			vb_pos.subresource_srv = device->CreateSubresource(&generalBuffer, SubresourceType::SRV, vb_pos.offset, vb_pos.size, &geometry->positionFormat);
			vb_pos.descriptor_srv = device->GetDescriptorIndex(&generalBuffer, SubresourceType::SRV, vb_pos.subresource_srv);

			const std::vector<std::vector<uint8_t>>& splat_buffers = primitive.GetCustomBuffers();

			const std::vector<uint8_t>& buffer_SOs = splat_buffers[0];
			const std::vector<uint8_t>& buffer_Qts = splat_buffers[1];
			const std::vector<uint8_t>& buffer_SHs = splat_buffers[2];

			const float* vertex_SHs = (const float*)buffer_SHs.data();
			const XMFLOAT4* vertex_quaterions = (const XMFLOAT4*)buffer_Qts.data();
			const XMFLOAT4* vertex_scale_opacities = (const XMFLOAT4*)buffer_SOs.data();

			vzlog_assert(!buffer_SOs.empty(), "SHs must not be empty!");
			vzlog_assert(!buffer_Qts.empty(), "Quaternions must not be empty!");
			vzlog_assert(!buffer_SHs.empty(), "Scales and Opacities must not be empty!");

			size_t num_shCoeffs = (primitive.shLevel + 1) * (primitive.shLevel + 1);
			size_t num_gaussian_kernels = buffer_SHs.size() / (4 * (num_shCoeffs * 3));
			assert(num_gaussian_kernels == buffer_Qts.size() / 16 && num_gaussian_kernels == buffer_SOs.size() / 16);

			geometry->allowGaussianSplatting = true;
			
			std::vector<std::string> custom_buffer_names = {
				"gaussianSHs",
				"gaussianScale_Opacities",
				"gaussianQuaterinions",
				"gaussianKernelAttributes",
				"offsetTiles",
				"gaussianReplicationKey",
				"gaussianReplicationValue",
				"sortedIndices",
				"gaussianCounterBuffer",
				"gaussianCounterBuffer_readback_0",
				"gaussianCounterBuffer_readback_1",
			};
			for (size_t i = 0, n = custom_buffer_names.size(); i < n; ++i)
			{
				part_buffers.customBufferMap[custom_buffer_names[i]] = part_buffers.customBuffers.size();
				part_buffers.customBuffers.push_back(GPUBuffer());
			}

			const uint32_t GAUSSIAN_SH = part_buffers.customBufferMap["gaussianSHs"];
			const uint32_t GAUSSIAN_SO = part_buffers.customBufferMap["gaussianScale_Opacities"];
			const uint32_t GAUSSIAN_QT = part_buffers.customBufferMap["gaussianQuaterinions"];
			const uint32_t GAUSSIAN_RENDER_ATTRIBUTE = part_buffers.customBufferMap["gaussianKernelAttributes"];
			const uint32_t GAUSSIAN_OFFSET_TILES = part_buffers.customBufferMap["offsetTiles"];
			const uint32_t GAUSSIAN_REPLICATE_KEY = part_buffers.customBufferMap["gaussianReplicationKey"];
			const uint32_t GAUSSIAN_REPLICATE_VALUE = part_buffers.customBufferMap["gaussianReplicationValue"];
			const uint32_t GAUSSIAN_SORTED_INDICES = part_buffers.customBufferMap["sortedIndices"];
			const uint32_t GAUSSIAN_COUNTER = part_buffers.customBufferMap["gaussianCounterBuffer"];
			const uint32_t GAUSSIAN_COUNTER_READBACK_0 = part_buffers.customBufferMap["gaussianCounterBuffer_readback_0"];
			const uint32_t GAUSSIAN_COUNTER_READBACK_1 = part_buffers.customBufferMap["gaussianCounterBuffer_readback_1"];
			
			{
				// vertex_SHs, vertex_scale_opacities, vertex_quaterions
				bd.bind_flags = BindFlag::SHADER_RESOURCE;
				bd.misc_flags = ResourceMiscFlag::BUFFER_RAW;
				bd.size = num_gaussian_kernels * num_shCoeffs * 3 * sizeof(float);
				bool success = device->CreateBuffer(&bd, vertex_SHs, &part_buffers.customBuffers[GAUSSIAN_SH]);
				assert(success);
				device->SetName(&part_buffers.customBuffers[GAUSSIAN_SH], "GGeometryComponent::bufferHandle_::gaussianSHs");

				bd.size = num_gaussian_kernels * sizeof(XMFLOAT4);
				success = device->CreateBuffer(&bd, vertex_scale_opacities, &part_buffers.customBuffers[GAUSSIAN_SO]);
				assert(success);
				device->SetName(&part_buffers.customBuffers[GAUSSIAN_SO], "GGeometryComponent::bufferHandle_::gaussianScale_Opacities");

				bd.size = num_gaussian_kernels * sizeof(XMFLOAT4);
				success = device->CreateBuffer(&bd, vertex_quaterions, &part_buffers.customBuffers[GAUSSIAN_QT]);
				assert(success);
				device->SetName(&part_buffers.customBuffers[GAUSSIAN_QT], "GGeometryComponent::bufferHandle_::gaussianQuaterinions");
			}

			{
				// Inter-processing buffers (read/write)
				bd.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				
				struct GaussianKernelAttribute
				{
					XMFLOAT4 conic_opacity;
					XMFLOAT4 color_radii;
					XMUINT4 aabb; // bounding box 

					XMFLOAT2 uv; // pixel coords that is output of ndx2pix() func;
					float depth;
					uint32_t padding0;
				};

				bd.size = num_gaussian_kernels * sizeof(GaussianKernelAttribute);
				success = device->CreateBuffer(&bd, nullptr, &part_buffers.customBuffers[GAUSSIAN_RENDER_ATTRIBUTE]);
				assert(success);
				device->SetName(&part_buffers.customBuffers[GAUSSIAN_RENDER_ATTRIBUTE], "GGeometryComponent::bufferHandle_::gaussianKernelAttributes");

				bd.size = num_gaussian_kernels * sizeof(UINT);
				success = device->CreateBuffer(&bd, nullptr, &part_buffers.customBuffers[GAUSSIAN_OFFSET_TILES]);
				assert(success);
				device->SetName(&part_buffers.customBuffers[GAUSSIAN_OFFSET_TILES], "GGeometryComponent::bufferHandle_::offsetTiles");

				{
					uint32_t num_replicate_kernels = num_gaussian_kernels * 2;

					bd.size = num_replicate_kernels * sizeof(UINT) * 2; // uint_64
					assert(device->CreateBuffer(&bd, nullptr, &part_buffers.customBuffers[GAUSSIAN_REPLICATE_KEY]));
					device->SetName(&part_buffers.customBuffers[GAUSSIAN_REPLICATE_KEY], "GaussianSplattingBuffers::replicatedGaussianKey");

					bd.size = num_replicate_kernels * sizeof(UINT);
					assert(device->CreateBuffer(&bd, nullptr, &part_buffers.customBuffers[GAUSSIAN_REPLICATE_VALUE]));
					device->SetName(&part_buffers.customBuffers[GAUSSIAN_REPLICATE_VALUE], "GaussianSplattingBuffers::replicatedGaussianValue");

					bd.size = num_replicate_kernels * sizeof(UINT);
					assert(device->CreateBuffer(&bd, nullptr, &part_buffers.customBuffers[GAUSSIAN_SORTED_INDICES]));
					device->SetName(&part_buffers.customBuffers[GAUSSIAN_SORTED_INDICES], "GaussianSplattingBuffers::sortedIndices");
				}

				bd.size = sizeof(uint32_t) * 2;
				success = device->CreateBuffer(&bd, nullptr, &part_buffers.customBuffers[GAUSSIAN_COUNTER]);
				assert(success);
				device->SetName(&part_buffers.customBuffers[GAUSSIAN_COUNTER], "GGeometryComponent::bufferHandle_::gaussianCounterBuffer");

				bd.usage = Usage::READBACK;
				bd.bind_flags = BindFlag::NONE;
				bd.misc_flags = ResourceMiscFlag::NONE;
				device->CreateBuffer(&bd, nullptr, &part_buffers.customBuffers[GAUSSIAN_COUNTER_READBACK_0]);
				device->SetName(&part_buffers.customBuffers[GAUSSIAN_COUNTER_READBACK_0], "GGeometryComponent::bufferHandle_::gaussianCounterBuffer_readback_0");
				device->CreateBuffer(&bd, nullptr, &part_buffers.customBuffers[GAUSSIAN_COUNTER_READBACK_1]);
				device->SetName(&part_buffers.customBuffers[GAUSSIAN_COUNTER_READBACK_1], "GGeometryComponent::bufferHandle_::gaussianCounterBuffer_readback_1");

			}
		}
		});
}

//void updateCapacityGaussians(uint32_t capacityGaussians)
//{
//	GPrimBuffers* prim_buffers = GetGPrimBuffer(0);
//	if (!allowGaussianSplatting || prim_buffers == nullptr)
//	{
//		return;
//	}
//	GaussianSplattingBuffers& gaussianSplattingBuffers = prim_buffers->gaussianSplattingBuffers;
//	if (capacityGaussians > gaussianSplattingBuffers.capacityGaussians)
//	{
//		GraphicsDevice* device = graphics::GetDevice();
//
//		gaussianSplattingBuffers.capacityGaussians = capacityGaussians * 2;
//
//		vzlog("GaussianSplattingBuffers's capacity update: request (%d) and allocate (%d)", capacityGaussians, gaussianSplattingBuffers.capacityGaussians);
//
//		GPUBufferDesc bd;
//		if (device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
//		{
//			// In UMA mode, it is better to create UPLOAD buffer, this avoids one copy from UPLOAD to DEFAULT
//			bd.usage = Usage::UPLOAD;
//		}
//		else
//		{
//			bd.usage = Usage::DEFAULT;
//		}
//		bd.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
//		bd.misc_flags = ResourceMiscFlag::BUFFER_RAW;
//
//		bd.size = gaussianSplattingBuffers.capacityGaussians * sizeof(UINT) * 2; // uint_64
//		assert(device->CreateBuffer(&bd, nullptr, &gaussianSplattingBuffers.replicatedGaussianKey));
//		device->SetName(&gaussianSplattingBuffers.replicatedGaussianKey, "GaussianSplattingBuffers::duplicatedGaussianKey");
//
//		bd.size = gaussianSplattingBuffers.capacityGaussians * sizeof(UINT);
//		assert(device->CreateBuffer(&bd, nullptr, &gaussianSplattingBuffers.replicatedGaussianValue));
//		device->SetName(&gaussianSplattingBuffers.replicatedGaussianValue, "GaussianSplattingBuffers::duplicatedGaussianValue");
//
//		bd.size = gaussianSplattingBuffers.capacityGaussians * sizeof(UINT);
//		assert(device->CreateBuffer(&bd, nullptr, &gaussianSplattingBuffers.sortedIndices));
//		device->SetName(&gaussianSplattingBuffers.sortedIndices, "GaussianSplattingBuffers::sortedIndices");
//	}
//}

bool ImportModel_SPLAT(const std::string& fileName, const Entity geometryEntity)
{
	GeometryComponent* geometry = compfactory::GetGeometryComponent(geometryEntity);
	if (geometry == nullptr)
	{
		vzlog_error("Invalid Entity(%llu)!", geometryEntity);
		return false;
	}

	SplatFormat format = splatFormatFromPath(fileName);
	if (format == SplatFormat::UNKNOWN)
	{
		vzlog_error("UNKNOWN SPLAT FORMAT!");
		return false;
	}

	std::ifstream file(fileName, std::ios::binary);
	if (!file.is_open())
	{
		backlog::post("Error opening SPLAT file: " + fileName, backlog::LogLevel::Error);
		return false;
	}

	file.seekg(0, std::ios::end);
	std::streamsize fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<char> filebuffer(fileSize);

	if (!file.read(filebuffer.data(), fileSize)) {
		vzlog_error("File read failure!");
		return false;
	}

	const uint8_t* splatdata = (uint8_t*)filebuffer.data();

    SplatAttributes attributes;
    if (parseSplatData(attributes, splatdata, filebuffer.size()) == 0)
    {
        return false;
    }

	// Prepare GeometryComponent
	using Primitive = GeometryComponent::Primitive;

	std::vector<Primitive> parts(1);
	geometry->CopyPrimitivesFrom(parts);

	Primitive* mutable_primitive = geometry->GetMutablePrimitive(0);
	mutable_primitive->SetPrimitiveType(GeometryComponent::PrimitiveType::POINTS);

	std::vector<XMFLOAT3>& vertex_positions = mutable_primitive->GetMutableVtxPositions();
	std::vector<std::vector<uint8_t>>& splat_buffers = mutable_primitive->GetMutableCustomBuffers();

    uint32_t num_splats = attributes.numSplats();
	vertex_positions.reserve(num_splats);

	splat_buffers.resize(3); // SO, QT, SH

	std::vector<uint8_t>& buffer_SOs = splat_buffers[0];
	std::vector<uint8_t>& buffer_Qts = splat_buffers[1];
	std::vector<uint8_t>& buffer_SHs = splat_buffers[2];
	buffer_SOs.resize(num_splats * sizeof(XMFLOAT4));
	buffer_Qts.resize(num_splats * sizeof(XMFLOAT4));
	buffer_SHs.resize(num_splats * sizeof(XMFLOAT3)); // TODO

	uint8_t* buffer_SOs_ptr = buffer_SOs.data();
	uint8_t* buffer_Qts_ptr = buffer_Qts.data();
	uint8_t* buffer_SHs_ptr = buffer_SHs.data();

    for (uint32_t i = 0; i < num_splats; ++i)
    {
		vertex_positions.push_back(XMFLOAT3(attributes.positions[3 * i + 0], 
            attributes.positions[3 * i + 1], attributes.positions[3 * i + 2])
		);

		((XMFLOAT4*)buffer_SOs_ptr)[i] = XMFLOAT4(attributes.scales[3 * i + 0],
			attributes.scales[3 * i + 1], attributes.scales[3 * i + 2], attributes.opacities[i]);
        
		((XMFLOAT4*)buffer_Qts_ptr)[i] = XMFLOAT4(attributes.rotations[4 * i + 0],
			attributes.rotations[4 * i + 1], attributes.rotations[4 * i + 2], attributes.rotations[4 * i + 3]);

		((XMFLOAT3*)buffer_SHs_ptr)[i] = XMFLOAT3(attributes.shCoefficients[3 * i + 0],
			attributes.shCoefficients[3 * i + 1], attributes.shCoefficients[3 * i + 2]);
    }

	updateRenderDataGaussianSplatting((GGeometryComponent*)geometry, attributes);

	return true;
}