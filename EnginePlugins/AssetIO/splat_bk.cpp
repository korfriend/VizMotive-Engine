#include "AssetIO.h"
#include "Utils/Backlog.h"
#include "Utils/Helpers.h"
#include "Utils/vzMath.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>

const int BASE_COMPONENT_COUNT = 14;

// Calculate the number of components for a given spherical harmonics degree
inline int getSphericalHarmonicsComponentCountForDegree(int degree) {
	// Example implementation - adjust based on actual requirements
	return (degree + 1) * (degree + 1) - 1;
}

class UncompressedSplatArray {
public:
	// Static OFFSET structure definition
	struct OFFSET {
		static const int X = 0;
		static const int Y = 1;
		static const int Z = 2;
		static const int SCALE0 = 3;
		static const int SCALE1 = 4;
		static const int SCALE2 = 5;
		static const int ROTATION0 = 6;
		static const int ROTATION1 = 7;
		static const int ROTATION2 = 8;
		static const int ROTATION3 = 9;
		static const int FDC0 = 10;
		static const int FDC1 = 11;
		static const int FDC2 = 12;
		static const int OPACITY = 13;
		static const int FRC0 = 14;
		static const int FRC1 = 15;
		static const int FRC2 = 16;
		static const int FRC3 = 17;
		static const int FRC4 = 18;
		static const int FRC5 = 19;
		static const int FRC6 = 20;
		static const int FRC7 = 21;
		static const int FRC8 = 22;
		static const int FRC9 = 23;
		static const int FRC10 = 24;
		static const int FRC11 = 25;
		static const int FRC12 = 26;
		static const int FRC13 = 27;
		static const int FRC14 = 28;
		static const int FRC15 = 29;
		static const int FRC16 = 30;
		static const int FRC17 = 31;
		static const int FRC18 = 32;
		static const int FRC19 = 33;
		static const int FRC20 = 34;
		static const int FRC21 = 35;
		static const int FRC22 = 36;
		static const int FRC23 = 37;
	};

	int sphericalHarmonicsDegree;
	int sphericalHarmonicsCount;
	int componentCount;
	std::vector<float> defaultSphericalHarmonics;
	std::vector<std::vector<float>> splats;
	int splatCount;

	// Constructor
	UncompressedSplatArray(int sphericalHarmonicsDegree = 0) :
		sphericalHarmonicsDegree(sphericalHarmonicsDegree),
		sphericalHarmonicsCount(getSphericalHarmonicsComponentCountForDegree(sphericalHarmonicsDegree)),
		componentCount(getSphericalHarmonicsComponentCountForDegree(sphericalHarmonicsDegree) + BASE_COMPONENT_COUNT),
		splatCount(0) {
		// Initialize default spherical harmonics
		defaultSphericalHarmonics.resize(sphericalHarmonicsCount, 0.0f);
	}

	// Static method to create a splat
	static std::vector<float> createSplat(int sphericalHarmonicsDegree = 0) {
		std::vector<float> baseSplat = { 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
		int shEntries = getSphericalHarmonicsComponentCountForDegree(sphericalHarmonicsDegree);
		for (int i = 0; i < shEntries; i++) {
			baseSplat.push_back(0);
		}
		return baseSplat;
	}

	// Add a splat to the array
	void addSplat(const std::vector<float>& splat) {
		splats.push_back(splat);
		splatCount++;
	}

	// Get a splat by index
	const std::vector<float>& getSplat(int index) const {
		return splats[index];
	}

	// Add a default splat to the array
	std::vector<float>& addDefaultSplat() {
		std::vector<float> newSplat = UncompressedSplatArray::createSplat(sphericalHarmonicsDegree);
		addSplat(newSplat);
		return splats.back();
	}

	// Add a splat from components - standard version
	std::vector<float>& addSplatFromComonents(float x, float y, float z,
		float scale0, float scale1, float scale2,
		float rot0, float rot1, float rot2, float rot3,
		float r, float g, float b, float opacity) {
		// Add base parameters
		std::vector<float> newSplat = { x, y, z, scale0, scale1, scale2, rot0, rot1, rot2, rot3, r, g, b, opacity };

		// Add default spherical harmonics values
		newSplat.insert(newSplat.end(), defaultSphericalHarmonics.begin(), defaultSphericalHarmonics.end());

		addSplat(newSplat);
		return splats.back();
	}

	// Add a splat from components - variable arguments version
	std::vector<float>& addSplatFromComonents(float x, float y, float z,
		float scale0, float scale1, float scale2,
		float rot0, float rot1, float rot2, float rot3,
		float r, float g, float b, float opacity,
		std::initializer_list<float> rest) {
		// Add base parameters
		std::vector<float> newSplat = { x, y, z, scale0, scale1, scale2, rot0, rot1, rot2, rot3, r, g, b, opacity };

		// Add default spherical harmonics values
		newSplat.insert(newSplat.end(), defaultSphericalHarmonics.begin(), defaultSphericalHarmonics.end());

		// Assign additional values to respective positions
		int i = 0;
		for (float value : rest) {
			if (i < sphericalHarmonicsCount) {
				newSplat[BASE_COMPONENT_COUNT + i] = value;
				i++;
			}
			else {
				break;
			}
		}

		addSplat(newSplat);
		return splats.back();
	}

	// Add a splat from another UncompressedSplatArray
	void addSplatFromArray(const UncompressedSplatArray& src, int srcIndex) {
		const auto& srcSplat = src.splats[srcIndex];
		std::vector<float> newSplat = UncompressedSplatArray::createSplat(sphericalHarmonicsDegree);

		for (int i = 0; i < componentCount && i < static_cast<int>(srcSplat.size()); i++) {
			newSplat[i] = srcSplat[i];
		}

		addSplat(newSplat);
	}
};

// Constants class definition
class Constants {
public:
	static const int DefaultSplatSortDistanceMapPrecision = 16;
	static const int MemoryPageSize = 65536;
	static const int BytesPerFloat = 4;
	static const int BytesPerInt = 4;
	static const int MaxScenes = 32;
	static const int ProgressiveLoadSectionSize = 262144;
	static const int ProgressiveLoadSectionDelayDuration = 15;
	static const float SphericalHarmonics8BitCompressionRange;
};

// Initialize static constants
const float Constants::SphericalHarmonics8BitCompressionRange = 3.0f;

// Constants for spherical harmonics compression
const float DefaultSphericalHarmonics8BitCompressionRange = Constants::SphericalHarmonics8BitCompressionRange;
const float DefaultSphericalHarmonics8BitCompressionHalfRange = DefaultSphericalHarmonics8BitCompressionRange / 2.0f;

// Convert 32-bit float to 16-bit half float
// Using DirectXMath's XMConvertFloatToHalf
uint16_t toHalfFloat(float f) {
	return XMConvertFloatToHalf(f);
}

// Convert 16-bit half float to 32-bit float
// Using DirectXMath's XMConvertHalfToFloat
float fromHalfFloat(uint16_t h) {
	return XMConvertHalfToFloat(h);
}

// Convert a float to 8-bit unsigned integer within a range
uint8_t toUint8(float v, float rangeMin, float rangeMax) {
	v = clamp(v, rangeMin, rangeMax);
	const float range = (rangeMax - rangeMin);
	return static_cast<uint8_t>(clamp(std::floor((v - rangeMin) / range * 255.0f), 0.0f, 255.0f));
}

// Convert an 8-bit unsigned integer to float within a range
float fromUint8(uint8_t v, float rangeMin, float rangeMax) {
	const float range = (rangeMax - rangeMin);
	return (static_cast<float>(v) / 255.0f * range + rangeMin);
}

// Convert from half float to 8-bit uint
uint8_t fromHalfFloatToUint8(uint16_t v, float rangeMin, float rangeMax) {
	return toUint8(fromHalfFloat(v), rangeMin, rangeMax);
}

// Convert from 8-bit uint to half float
uint16_t fromUint8ToHalfFloat(uint8_t v, float rangeMin, float rangeMax) {
	return toHalfFloat(fromUint8(v, rangeMin, rangeMax));
}

// Convert float to appropriate representation based on compression level
float toUncompressedFloat(uint32_t f, int compressionLevel, bool isSH = false,
	float range8BitMin = -DefaultSphericalHarmonics8BitCompressionHalfRange,
	float range8BitMax = DefaultSphericalHarmonics8BitCompressionHalfRange) {
	if (compressionLevel == 0) {
		return *reinterpret_cast<float*>(&f);
	}
	else if (compressionLevel == 1 || (compressionLevel == 2 && !isSH)) {
		return fromHalfFloat(static_cast<uint16_t>(f));
	}
	else if (compressionLevel == 2) {
		return fromUint8(static_cast<uint8_t>(f), range8BitMin, range8BitMax);
	}
	return 0.0f; // Default return if no condition is met
}

// Get value from DataView based on compression level
uint32_t dataViewFloatForCompressionLevel(const uint8_t* dataView, size_t floatIndex, int compressionLevel, bool isSH = false) {
	if (compressionLevel == 0) {
		float value;
		memcpy(&value, dataView + floatIndex * 4, sizeof(float));
		return *reinterpret_cast<uint32_t*>(&value);
	}
	else if (compressionLevel == 1 || (compressionLevel == 2 && !isSH)) {
		uint16_t value;
		memcpy(&value, dataView + floatIndex * 2, sizeof(uint16_t));
		return value;
	}
	else {
		return dataView[floatIndex];
	}
}

// Convert value between compression levels
uint32_t convertBetweenCompressionLevels(uint32_t val, int fromLevel, int toLevel, bool isSH = false,
	float range8BitMin = -DefaultSphericalHarmonics8BitCompressionHalfRange,
	float range8BitMax = DefaultSphericalHarmonics8BitCompressionHalfRange) {
	if (fromLevel == toLevel) return val;

	if (fromLevel == 2 && isSH) {
		if (toLevel == 1) {
			return fromUint8ToHalfFloat(static_cast<uint8_t>(val), range8BitMin, range8BitMax);
		}
		else if (toLevel == 0) {
			float fVal = fromUint8(static_cast<uint8_t>(val), range8BitMin, range8BitMax);
			return *reinterpret_cast<uint32_t*>(&fVal);
		}
	}
	else if (fromLevel == 2 || fromLevel == 1) {
		if (toLevel == 0) {
			float fVal = fromHalfFloat(static_cast<uint16_t>(val));
			return *reinterpret_cast<uint32_t*>(&fVal);
		}
		else if (toLevel == 2) {
			if (!isSH) return val;
			else return fromHalfFloatToUint8(static_cast<uint16_t>(val), range8BitMin, range8BitMax);
		}
	}
	else if (fromLevel == 0) {
		float fVal = *reinterpret_cast<float*>(&val);
		if (toLevel == 1) {
			return toHalfFloat(fVal);
		}
		else if (toLevel == 2) {
			if (!isSH) return toHalfFloat(fVal);
			else return toUint8(fVal, range8BitMin, range8BitMax);
		}
	}

	return val; // Default: return original value if no condition matches
}

// Copy data between buffers
void copyBetweenBuffers(const uint8_t* srcBuffer, size_t srcOffset, uint8_t* destBuffer, size_t destOffset, size_t byteCount = 0) {
	const uint8_t* src = srcBuffer + srcOffset;
	uint8_t* dest = destBuffer + destOffset;

	//for (size_t i = 0; i < byteCount; i++) {
	//	dest[i] = src[i];
	//}

	// Alternative: Use memcpy for better performance
	memcpy(dest, src, byteCount);
}

/**
 * SplatBuffer: Container for splat data from a single scene/file and capable of (mediocre) compression.
 */
class SplatBuffer {
public:
	inline static const int CurrentMajorVersion = 0;
	inline static const int CurrentMinorVersion = 1;

	inline static const int CenterComponentCount = 3;
	inline static const int ScaleComponentCount = 3;
	inline static const int RotationComponentCount = 4;
	inline static const int ColorComponentCount = 4;
	inline static const int CovarianceComponentCount = 6;

	inline static const int SplatScaleOffsetFloat = 3;
	inline static const int SplatRotationOffsetFloat = 6;

	// Definition of compression level structure
	struct CompressionLevel {
		int BytesPerCenter;
		int BytesPerScale;
		int BytesPerRotation;
		int BytesPerColor;
		int ScaleOffsetBytes;
		int RotationffsetBytes;
		int ColorOffsetBytes;
		int SphericalHarmonicsOffsetBytes;
		int ScaleRange;
		int BytesPerSphericalHarmonicsComponent;
		int SphericalHarmonicsOffsetFloat;

		struct SphericalHarmonicsDegree {
			int BytesPerSplat;
		};

		SphericalHarmonicsDegree SphericalHarmonicsDegrees[3]; // Information for degrees 0, 1, 2
	};

	// CompressionLevels array definition (using initialization function in C++ instead of direct object definition like in JavaScript)
	// Information for compression levels 0, 1, 2
	inline static CompressionLevel CompressionLevels[3] = {
		// Level 0
		{
			12, // BytesPerCenter
			12, // BytesPerScale
			16, // BytesPerRotation
			4,  // BytesPerColor
			12, // ScaleOffsetBytes
			24, // RotationffsetBytes
			40, // ColorOffsetBytes
			44, // SphericalHarmonicsOffsetBytes
			1,  // ScaleRange
			4,  // BytesPerSphericalHarmonicsComponent
			11, // SphericalHarmonicsOffsetFloat
			{   // SphericalHarmonicsDegrees
				{44}, // Degree 0
				{80}, // Degree 1
				{140} // Degree 2
			}
		},
		// Level 1
		{
			6,  // BytesPerCenter
			6,  // BytesPerScale
			8,  // BytesPerRotation
			4,  // BytesPerColor
			6,  // ScaleOffsetBytes
			12, // RotationffsetBytes
			20, // ColorOffsetBytes
			24, // SphericalHarmonicsOffsetBytes
			32767, // ScaleRange
			2,  // BytesPerSphericalHarmonicsComponent
			12, // SphericalHarmonicsOffsetFloat
			{   // SphericalHarmonicsDegrees
				{24}, // Degree 0
				{42}, // Degree 1
				{72}  // Degree 2
			}
		},
		// Level 2
		{
			6,  // BytesPerCenter
			6,  // BytesPerScale
			8,  // BytesPerRotation
			4,  // BytesPerColor
			6,  // ScaleOffsetBytes
			12, // RotationffsetBytes
			20, // ColorOffsetBytes
			24, // SphericalHarmonicsOffsetBytes
			32767, // ScaleRange
			1,  // BytesPerSphericalHarmonicsComponent
			12, // SphericalHarmonicsOffsetFloat
			{   // SphericalHarmonicsDegrees
				{24}, // Degree 0
				{33}, // Degree 1
				{48}  // Degree 2
			}
		}
	};


	inline static const int CovarianceSizeFloats = 6;

	inline static const int HeaderSizeBytes = 4096;
	inline static const int SectionHeaderSizeBytes = 1024;

	inline static const int BucketStorageSizeBytes = 12;
	inline static const int BucketStorageSizeFloats = 3;

	inline static const float BucketBlockSize = 5.0;
	inline static const int BucketSize = 256;

	struct Header {
		int versionMajor;
		int versionMinor;
		int maxSectionCount;
		int sectionCount;
		int maxSplatCount;
		int splatCount;
		int compressionLevel;
		XMFLOAT3 sceneCenter;
		float minSphericalHarmonicsCoeff;
		float maxSphericalHarmonicsCoeff;
	};

	static Header parseHeader(const void* buffer) {
		const uint8_t* headerArrayUint8 = static_cast<const uint8_t*>(buffer);
		const uint16_t* headerArrayUint16 = static_cast<const uint16_t*>(buffer);
		const uint32_t* headerArrayUint32 = static_cast<const uint32_t*>(buffer);
		const float* headerArrayFloat32 = static_cast<const float*>(buffer);

		int versionMajor = headerArrayUint8[0];
		int versionMinor = headerArrayUint8[1];
		int maxSectionCount = headerArrayUint32[1];
		int sectionCount = headerArrayUint32[2];
		int maxSplatCount = headerArrayUint32[3];
		int splatCount = headerArrayUint32[4];
		int compressionLevel = headerArrayUint16[10];
		XMFLOAT3 sceneCenter(headerArrayFloat32[6], headerArrayFloat32[7], headerArrayFloat32[8]);

		// Using DefaultSphericalHarmonics8BitCompressionHalfRange as a fallback value
		float minSphericalHarmonicsCoeff = headerArrayFloat32[9] != 0.0f ?
			headerArrayFloat32[9] :
			-DefaultSphericalHarmonics8BitCompressionHalfRange;
		float maxSphericalHarmonicsCoeff = headerArrayFloat32[10] != 0.0f ?
			headerArrayFloat32[10] :
			DefaultSphericalHarmonics8BitCompressionHalfRange;

		return {
			versionMajor,
			versionMinor,
			maxSectionCount,
			sectionCount,
			maxSplatCount,
			splatCount,
			compressionLevel,
			sceneCenter,
			minSphericalHarmonicsCoeff,
			maxSphericalHarmonicsCoeff
		};
	}

	struct Section {
		int bytesPerSplat;
		int splatCountOffset;
		int splatCount;
		int maxSplatCount;
		int bucketSize;
		int bucketCount;
		float bucketBlockSize;
		float halfBucketBlockSize;
		int bucketStorageSizeBytes;
		int bucketsStorageSizeBytes;
		int splatDataStorageSizeBytes;
		int storageSizeBytes;
		int compressionScaleRange;
		float compressionScaleFactor;
		size_t base;
		size_t bucketsBase;
		size_t dataBase;
		int fullBucketCount;
		int partiallyFilledBucketCount;
		int sphericalHarmonicsDegree;
		std::vector<float> bucketArray;                 // Added for data storage
		std::vector<int> partiallyFilledBucketLengths;  // Added for partial bucket info
	};

	static std::vector<Section> parseSectionHeaders(const Header& header, const void* buffer,
		size_t offset, bool secLoadedCountsToMax) {
		const int compressionLevel = header.compressionLevel;

		const int maxSectionCount = header.maxSectionCount;
		const uint16_t* sectionHeaderArrayUint16 = reinterpret_cast<const uint16_t*>(
			static_cast<const uint8_t*>(buffer) + offset);
		const uint32_t* sectionHeaderArrayUint32 = reinterpret_cast<const uint32_t*>(
			static_cast<const uint8_t*>(buffer) + offset);
		const float* sectionHeaderArrayFloat32 = reinterpret_cast<const float*>(
			static_cast<const uint8_t*>(buffer) + offset);

		std::vector<Section> sectionHeaders;
		sectionHeaders.reserve(maxSectionCount);

		size_t sectionHeaderBase = 0;
		size_t sectionHeaderBaseUint16 = sectionHeaderBase / 2;
		size_t sectionHeaderBaseUint32 = sectionHeaderBase / 4;
		size_t sectionBase = SplatBuffer::HeaderSizeBytes + header.maxSectionCount * SplatBuffer::SectionHeaderSizeBytes;
		int splatCountOffset = 0;

		for (int i = 0; i < maxSectionCount; i++) {
			const int maxSplatCount = sectionHeaderArrayUint32[sectionHeaderBaseUint32 + 1];
			const int bucketSize = sectionHeaderArrayUint32[sectionHeaderBaseUint32 + 2];
			const int bucketCount = sectionHeaderArrayUint32[sectionHeaderBaseUint32 + 3];
			const float bucketBlockSize = sectionHeaderArrayFloat32[sectionHeaderBaseUint32 + 4];
			const float halfBucketBlockSize = bucketBlockSize / 2.0f;
			const int bucketStorageSizeBytes = sectionHeaderArrayUint16[sectionHeaderBaseUint16 + 10];

			// Use default compression scale range if not provided
			const int compressionScaleRange = sectionHeaderArrayUint32[sectionHeaderBaseUint32 + 6] ?
				sectionHeaderArrayUint32[sectionHeaderBaseUint32 + 6] :
				SplatBuffer::CompressionLevels[compressionLevel].ScaleRange;

			const int fullBucketCount = sectionHeaderArrayUint32[sectionHeaderBaseUint32 + 8];
			const int partiallyFilledBucketCount = sectionHeaderArrayUint32[sectionHeaderBaseUint32 + 9];
			const int bucketsMetaDataSizeBytes = partiallyFilledBucketCount * 4;
			const int bucketsStorageSizeBytes = bucketStorageSizeBytes * bucketCount + bucketsMetaDataSizeBytes;

			const int sphericalHarmonicsDegree = sectionHeaderArrayUint16[sectionHeaderBaseUint16 + 20];

			// Call to calculateComponentStorage to get bytesPerSplat
			int bytesPerSplat = SplatBuffer::calculateComponentStorage(compressionLevel, sphericalHarmonicsDegree).bytesPerSplat;

			const int splatDataStorageSizeBytes = bytesPerSplat * maxSplatCount;
			const int storageSizeBytes = splatDataStorageSizeBytes + bucketsStorageSizeBytes;

			Section sectionHeader;
			sectionHeader.bytesPerSplat = bytesPerSplat;
			sectionHeader.splatCountOffset = splatCountOffset;
			sectionHeader.splatCount = secLoadedCountsToMax ? maxSplatCount : 0;
			sectionHeader.maxSplatCount = maxSplatCount;
			sectionHeader.bucketSize = bucketSize;
			sectionHeader.bucketCount = bucketCount;
			sectionHeader.bucketBlockSize = bucketBlockSize;
			sectionHeader.halfBucketBlockSize = halfBucketBlockSize;
			sectionHeader.bucketStorageSizeBytes = bucketStorageSizeBytes;
			sectionHeader.bucketsStorageSizeBytes = bucketsStorageSizeBytes;
			sectionHeader.splatDataStorageSizeBytes = splatDataStorageSizeBytes;
			sectionHeader.storageSizeBytes = storageSizeBytes;
			sectionHeader.compressionScaleRange = compressionScaleRange;
			sectionHeader.compressionScaleFactor = halfBucketBlockSize / compressionScaleRange;
			sectionHeader.base = sectionBase;
			sectionHeader.bucketsBase = sectionBase + bucketsMetaDataSizeBytes;
			sectionHeader.dataBase = sectionBase + bucketsStorageSizeBytes;
			sectionHeader.fullBucketCount = fullBucketCount;
			sectionHeader.partiallyFilledBucketCount = partiallyFilledBucketCount;
			sectionHeader.sphericalHarmonicsDegree = sphericalHarmonicsDegree;

			sectionHeaders.push_back(sectionHeader);

			sectionBase += storageSizeBytes;
			sectionHeaderBase += SplatBuffer::SectionHeaderSizeBytes;
			sectionHeaderBaseUint16 = sectionHeaderBase / 2;
			sectionHeaderBaseUint32 = sectionHeaderBase / 4;
			splatCountOffset += maxSplatCount;
		}

		return sectionHeaders;
	}

	struct ComponentStorage {
		int bytesPerCenter;
		int bytesPerScale;
		int bytesPerRotation;
		int bytesPerColor;
		int sphericalHarmonicsComponentsPerSplat;
		int sphericalHarmonicsBytesPerSplat;
		int bytesPerSplat;
	};

	static ComponentStorage calculateComponentStorage(int compressionLevel, int sphericalHarmonicsDegree) {
		const int bytesPerCenter = SplatBuffer::CompressionLevels[compressionLevel].BytesPerCenter;
		const int bytesPerScale = SplatBuffer::CompressionLevels[compressionLevel].BytesPerScale;
		const int bytesPerRotation = SplatBuffer::CompressionLevels[compressionLevel].BytesPerRotation;
		const int bytesPerColor = SplatBuffer::CompressionLevels[compressionLevel].BytesPerColor;

		// Assuming getSphericalHarmonicsComponentCountForDegree is a separate function
		const int sphericalHarmonicsComponentsPerSplat = getSphericalHarmonicsComponentCountForDegree(sphericalHarmonicsDegree);

		const int sphericalHarmonicsBytesPerSplat =
			SplatBuffer::CompressionLevels[compressionLevel].BytesPerSphericalHarmonicsComponent *
			sphericalHarmonicsComponentsPerSplat;

		const int bytesPerSplat = bytesPerCenter + bytesPerScale + bytesPerRotation +
			bytesPerColor + sphericalHarmonicsBytesPerSplat;

		return {
			bytesPerCenter,
			bytesPerScale,
			bytesPerRotation,
			bytesPerColor,
			sphericalHarmonicsComponentsPerSplat,
			sphericalHarmonicsBytesPerSplat,
			bytesPerSplat
		};
	}

	struct Bucket {
		std::vector<int> splats;
		std::vector<float> center;
	};

	struct BucketInfo {
		std::vector<Bucket> fullBuckets;
		std::vector<Bucket> partiallyFullBuckets;
	};

	static BucketInfo computeBucketsForUncompressedSplatArray(
		const UncompressedSplatArray& splatArray,
		float blockSize,
		int bucketSize) {

		int splatCount = splatArray.splatCount;
		float halfBlockSize = blockSize / 2.0f;

		// Initialize min and max vectors to track bounds
		XMFLOAT3 min, max;
		min.x = min.y = min.z = FLT_MAX;
		max.x = max.y = max.z = -FLT_MAX;

		// Find the bounding box of all splats
		for (int i = 0; i < splatCount; i++) {
			const std::vector<float>& targetSplat = splatArray.getSplat(i);
			float center[3] = {
				targetSplat[UncompressedSplatArray::OFFSET::X],
				targetSplat[UncompressedSplatArray::OFFSET::Y],
				targetSplat[UncompressedSplatArray::OFFSET::Z]
			};

			if (i == 0 || center[0] < min.x) min.x = center[0];
			if (i == 0 || center[0] > max.x) max.x = center[0];
			if (i == 0 || center[1] < min.y) min.y = center[1];
			if (i == 0 || center[1] > max.y) max.y = center[1];
			if (i == 0 || center[2] < min.z) min.z = center[2];
			if (i == 0 || center[2] > max.z) max.z = center[2];
		}

		// Calculate dimensions and number of blocks
		XMFLOAT3 dimensions;
		dimensions.x = max.x - min.x;
		dimensions.y = max.y - min.y;
		dimensions.z = max.z - min.z;

		int xBlocks = (int)std::ceil(dimensions.x / blockSize);
		int yBlocks = (int)std::ceil(dimensions.y / blockSize);
		int zBlocks = (int)std::ceil(dimensions.z / blockSize);

		XMFLOAT3 blockCenter;
		std::vector<Bucket> fullBuckets;
		std::map<int, Bucket> partiallyFullBuckets;

		// Assign splats to buckets
		for (int i = 0; i < splatCount; i++) {
			const std::vector<float>& targetSplat = splatArray.getSplat(i);
			float center[3] = {
				targetSplat[UncompressedSplatArray::OFFSET::X],
				targetSplat[UncompressedSplatArray::OFFSET::Y],
				targetSplat[UncompressedSplatArray::OFFSET::Z]
			};

			int xBlock = (int)std::floor((center[0] - min.x) / blockSize);
			int yBlock = (int)std::floor((center[1] - min.y) / blockSize);
			int zBlock = (int)std::floor((center[2] - min.z) / blockSize);

			blockCenter.x = xBlock * blockSize + min.x + halfBlockSize;
			blockCenter.y = yBlock * blockSize + min.y + halfBlockSize;
			blockCenter.z = zBlock * blockSize + min.z + halfBlockSize;

			int bucketId = xBlock * (yBlocks * zBlocks) + yBlock * zBlocks + zBlock;

			auto it = partiallyFullBuckets.find(bucketId);
			if (it == partiallyFullBuckets.end()) {
				// Create a new bucket if it doesn't exist
				Bucket bucket;
				bucket.center = { blockCenter.x, blockCenter.y, blockCenter.z };
				bucket.splats.push_back(i);
				partiallyFullBuckets[bucketId] = bucket;
			}
			else {
				// Add to existing bucket
				it->second.splats.push_back(i);

				// If bucket is full, move to fullBuckets
				if (it->second.splats.size() >= static_cast<size_t>(bucketSize)) {
					fullBuckets.push_back(it->second);
					partiallyFullBuckets.erase(it);
				}
			}
		}

		// Convert the map of partially full buckets to a vector
		std::vector<Bucket> partiallyFullBucketArray;
		for (const auto& pair : partiallyFullBuckets) {
			partiallyFullBucketArray.push_back(pair.second);
		}

		return {
			fullBuckets,
			partiallyFullBucketArray
		};
	}

	/**
 * Write header data to a buffer
 * @param header The header structure
 * @param buffer The buffer to write to
 */
	static void writeHeaderToBuffer(const Header& header, void* buffer) {
		uint8_t* headerArrayUint8 = static_cast<uint8_t*>(buffer);
		uint16_t* headerArrayUint16 = reinterpret_cast<uint16_t*>(buffer);
		uint32_t* headerArrayUint32 = reinterpret_cast<uint32_t*>(buffer);
		float* headerArrayFloat32 = reinterpret_cast<float*>(buffer);

		headerArrayUint8[0] = header.versionMajor;
		headerArrayUint8[1] = header.versionMinor;
		headerArrayUint8[2] = 0; // unused for now
		headerArrayUint8[3] = 0; // unused for now
		headerArrayUint32[1] = header.maxSectionCount;
		headerArrayUint32[2] = header.sectionCount;
		headerArrayUint32[3] = header.maxSplatCount;
		headerArrayUint32[4] = header.splatCount;
		headerArrayUint16[10] = header.compressionLevel;
		headerArrayFloat32[6] = header.sceneCenter.x;
		headerArrayFloat32[7] = header.sceneCenter.y;
		headerArrayFloat32[8] = header.sceneCenter.z;

		// Use default values if coefficients are not provided
		float minSH = header.minSphericalHarmonicsCoeff;
		if (minSH == 0.0f) minSH = -DefaultSphericalHarmonics8BitCompressionHalfRange;

		float maxSH = header.maxSphericalHarmonicsCoeff;
		if (maxSH == 0.0f) maxSH = DefaultSphericalHarmonics8BitCompressionHalfRange;

		headerArrayFloat32[9] = minSH;
		headerArrayFloat32[10] = maxSH;
	}

	static SplatBuffer* generateFromUncompressedSplatArrays(
		const std::vector<UncompressedSplatArray>& splatArrays,
		float minimumAlpha,
		int compressionLevel,
		const XMFLOAT3& sceneCenter,
		float blockSize,
		int bucketSize,
		const std::vector<std::map<std::string, float>>& options = {}) {

		// Find maximum spherical harmonics degree
		int shDegree = 0;
		for (size_t sa = 0; sa < splatArrays.size(); sa++) {
			const UncompressedSplatArray& splatArray = splatArrays[sa];
			shDegree = std::max(splatArray.sphericalHarmonicsDegree, shDegree);
		}

		// Find min/max spherical harmonics coefficients
		float minSphericalHarmonicsCoeff = 0.0f;
		float maxSphericalHarmonicsCoeff = 0.0f;
		bool firstCoeffFound = false;

		for (size_t sa = 0; sa < splatArrays.size(); sa++) {
			const UncompressedSplatArray& splatArray = splatArrays[sa];
			for (int i = 0; i < splatArray.splatCount; i++) {
				const std::vector<float>& splat = splatArray.getSplat(i);
				for (int sc = UncompressedSplatArray::OFFSET::FRC0;
					sc < UncompressedSplatArray::OFFSET::FRC23 && sc < static_cast<int>(splat.size());
					sc++) {
					if (!firstCoeffFound || splat[sc] < minSphericalHarmonicsCoeff) {
						minSphericalHarmonicsCoeff = splat[sc];
						firstCoeffFound = true;
					}
					if (!firstCoeffFound || splat[sc] > maxSphericalHarmonicsCoeff) {
						maxSphericalHarmonicsCoeff = splat[sc];
						firstCoeffFound = true;
					}
				}
			}
		}

		// Use default values if no coefficients found
		if (!firstCoeffFound) {
			minSphericalHarmonicsCoeff = -DefaultSphericalHarmonics8BitCompressionHalfRange;
			maxSphericalHarmonicsCoeff = DefaultSphericalHarmonics8BitCompressionHalfRange;
		}

		// Calculate component storage based on compression level and SH degree
		auto componentStorage = SplatBuffer::calculateComponentStorage(compressionLevel, shDegree);
		int bytesPerSplat = componentStorage.bytesPerSplat;
		int compressionScaleRange = SplatBuffer::CompressionLevels[compressionLevel].ScaleRange;

		std::vector<std::vector<char>> sectionBuffers;
		std::vector<std::vector<char>> sectionHeaderBuffers;
		int totalSplatCount = 0;

		for (size_t sa = 0; sa < splatArrays.size(); sa++) {
			const UncompressedSplatArray& splatArray = splatArrays[sa];

			// Filter valid splats (with alpha >= minimumAlpha)
			UncompressedSplatArray validSplats(shDegree);
			for (int i = 0; i < splatArray.splatCount; i++) {
				const std::vector<float>& targetSplat = splatArray.getSplat(i);
				float opacity = 0.0f;
				if (UncompressedSplatArray::OFFSET::OPACITY < targetSplat.size()) {
					opacity = targetSplat[UncompressedSplatArray::OFFSET::OPACITY];
				}

				if (opacity >= minimumAlpha) {
					validSplats.addSplat(targetSplat);
				}
			}

			// Get section options
			float sectionBlockSizeFactor = 1.0f;
			float sectionBucketSizeFactor = 1.0f;

			if (sa < options.size()) {
				const auto& sectionOptions = options[sa];
				auto it = sectionOptions.find("blockSizeFactor");
				if (it != sectionOptions.end()) sectionBlockSizeFactor = it->second;

				it = sectionOptions.find("bucketSizeFactor");
				if (it != sectionOptions.end()) sectionBucketSizeFactor = it->second;
			}

			float sectionBlockSize = sectionBlockSizeFactor * (blockSize ? blockSize : SplatBuffer::BucketBlockSize);
			int sectionBucketSize = (int)std::ceil(sectionBucketSizeFactor * (bucketSize ? bucketSize : SplatBuffer::BucketSize));

			// Compute buckets for the splat array
			auto bucketInfo = SplatBuffer::computeBucketsForUncompressedSplatArray(
				validSplats, sectionBlockSize, sectionBucketSize);

			int fullBucketCount = bucketInfo.fullBuckets.size();

			std::vector<int> partiallyFullBucketLengths;
			for (const auto& bucket : bucketInfo.partiallyFullBuckets) {
				partiallyFullBucketLengths.push_back(bucket.splats.size());
			}

			int partiallyFilledBucketCount = partiallyFullBucketLengths.size();

			// Combine full and partially full buckets
			std::vector<Bucket> buckets;
			buckets.insert(buckets.end(), bucketInfo.fullBuckets.begin(), bucketInfo.fullBuckets.end());
			buckets.insert(buckets.end(), bucketInfo.partiallyFullBuckets.begin(), bucketInfo.partiallyFullBuckets.end());

			// Calculate section buffer size
			int sectionDataSizeBytes = validSplats.splatCount * bytesPerSplat;
			int bucketMetaDataSizeBytes = partiallyFilledBucketCount * 4;
			int bucketDataBytes = (compressionLevel >= 1) ?
				buckets.size() * SplatBuffer::BucketStorageSizeBytes + bucketMetaDataSizeBytes : 0;
			int sectionSizeBytes = sectionDataSizeBytes + bucketDataBytes;

			// Create section buffer
			std::vector<char> sectionBuffer(sectionSizeBytes, 0);

			float compressionScaleFactor = compressionScaleRange / (sectionBlockSize * 0.5f);
			XMFLOAT3 bucketCenter;

			int outSplatCount = 0;
			for (size_t b = 0; b < buckets.size(); b++) {
				const Bucket& bucket = buckets[b];

				// Convert bucket center to XMFLOAT3
				bucketCenter.x = bucket.center[0];
				bucketCenter.y = bucket.center[1];
				bucketCenter.z = bucket.center[2];

				for (size_t i = 0; i < bucket.splats.size(); i++) {
					int row = bucket.splats[i];
					const std::vector<float>& targetSplat = validSplats.getSplat(row);
					int bufferOffset = bucketDataBytes + outSplatCount * bytesPerSplat;

					SplatBuffer::writeSplatDataToSectionBuffer(
						targetSplat,
						sectionBuffer.data(),
						bufferOffset,
						compressionLevel,
						shDegree,
						bucketCenter,
						compressionScaleFactor,
						compressionScaleRange,
						minSphericalHarmonicsCoeff,
						maxSphericalHarmonicsCoeff
					);

					outSplatCount++;
				}
			}
			totalSplatCount += outSplatCount;

			// Write bucket metadata if compression level >= 1
			if (compressionLevel >= 1) {
				// Write partially filled bucket lengths
				uint32_t* bucketMetaDataArray = reinterpret_cast<uint32_t*>(sectionBuffer.data());
				for (int pfb = 0; pfb < partiallyFilledBucketCount; pfb++) {
					bucketMetaDataArray[pfb] = partiallyFullBucketLengths[pfb];
				}

				// Write bucket centers
				float* bucketArray = reinterpret_cast<float*>(
					sectionBuffer.data() + bucketMetaDataSizeBytes);

				for (size_t b = 0; b < buckets.size(); b++) {
					const Bucket& bucket = buckets[b];
					const int base = b * 3;
					bucketArray[base] = bucket.center[0];
					bucketArray[base + 1] = bucket.center[1];
					bucketArray[base + 2] = bucket.center[2];
				}
			}

			sectionBuffers.push_back(sectionBuffer);

			// Create section header buffer
			std::vector<char> sectionHeaderBuffer(SplatBuffer::SectionHeaderSizeBytes, 0);

			// Create section header data
			SectionHeader sectionHeader;
			sectionHeader.maxSplatCount = outSplatCount;
			sectionHeader.splatCount = outSplatCount;
			sectionHeader.bucketSize = sectionBucketSize;
			sectionHeader.bucketCount = buckets.size();
			sectionHeader.bucketBlockSize = sectionBlockSize;
			sectionHeader.compressionScaleRange = compressionScaleRange;
			sectionHeader.storageSizeBytes = sectionSizeBytes;
			sectionHeader.fullBucketCount = fullBucketCount;
			sectionHeader.partiallyFilledBucketCount = partiallyFilledBucketCount;
			sectionHeader.sphericalHarmonicsDegree = shDegree;

			// Write section header to buffer
			SplatBuffer::writeSectionHeaderToBuffer(
				sectionHeader,
				compressionLevel,
				sectionHeaderBuffer.data(),
				0
			);

			sectionHeaderBuffers.push_back(sectionHeaderBuffer);
		}

		// Calculate total buffer size
		size_t sectionsCumulativeSizeBytes = 0;
		for (const auto& sectionBuffer : sectionBuffers) {
			sectionsCumulativeSizeBytes += sectionBuffer.size();
		}

		size_t unifiedBufferSize = SplatBuffer::HeaderSizeBytes +
			SplatBuffer::SectionHeaderSizeBytes * sectionBuffers.size() +
			sectionsCumulativeSizeBytes;

		// Create unified buffer
		std::vector<char> unifiedBuffer(unifiedBufferSize, 0);

		// Create header data
		Header header;
		header.versionMajor = 0;
		header.versionMinor = 1;
		header.maxSectionCount = sectionBuffers.size();
		header.sectionCount = sectionBuffers.size();
		header.maxSplatCount = totalSplatCount;
		header.splatCount = totalSplatCount;
		header.compressionLevel = compressionLevel;
		header.sceneCenter = sceneCenter;
		header.minSphericalHarmonicsCoeff = minSphericalHarmonicsCoeff;
		header.maxSphericalHarmonicsCoeff = maxSphericalHarmonicsCoeff;

		// Write header to buffer
		SplatBuffer::writeHeaderToBuffer(header, unifiedBuffer.data());

		// Copy section headers and section data to unified buffer
		size_t currentUnifiedBase = SplatBuffer::HeaderSizeBytes;

		for (const auto& sectionHeaderBuffer : sectionHeaderBuffers) {
			std::memcpy(
				unifiedBuffer.data() + currentUnifiedBase,
				sectionHeaderBuffer.data(),
				SplatBuffer::SectionHeaderSizeBytes
			);
			currentUnifiedBase += SplatBuffer::SectionHeaderSizeBytes;
		}

		for (const auto& sectionBuffer : sectionBuffers) {
			std::memcpy(
				unifiedBuffer.data() + currentUnifiedBase,
				sectionBuffer.data(),
				sectionBuffer.size()
			);
			currentUnifiedBase += sectionBuffer.size();
		}

		// Create and return the SplatBuffer
		return new SplatBuffer(unifiedBuffer.data());
	}


	// Section header structure definition for SplatBuffer
	struct SectionHeader {
		int maxSplatCount;             // Maximum number of splats in the section
		int splatCount;                // Current number of splats in the section
		int bucketSize;                // Size of each bucket
		int bucketCount;               // Number of buckets
		float bucketBlockSize;         // Size of a bucket block
		int compressionScaleRange;     // Compression scale range
		int storageSizeBytes;          // Storage size in bytes
		int fullBucketCount;           // Number of fully filled buckets
		int partiallyFilledBucketCount; // Number of partially filled buckets
		int sphericalHarmonicsDegree;   // Degree of spherical harmonics
	};

	// SplatBuffer 클래스 내에 추가할 정적 메서드
	static void writeSplatDataToSectionBuffer(
		const std::vector<float>& targetSplat,
		void* sectionBuffer,
		size_t bufferOffset,
		int compressionLevel,
		int sphericalHarmonicsDegree,
		const XMFLOAT3& bucketCenter,
		float compressionScaleFactor,
		int compressionScaleRange,
		float minSphericalHarmonicsCoeff = -DefaultSphericalHarmonics8BitCompressionHalfRange,
		float maxSphericalHarmonicsCoeff = DefaultSphericalHarmonics8BitCompressionHalfRange) {

		// 임시 버퍼들
		static uint8_t tempCenterBuffer[12];
		static uint8_t tempScaleBuffer[12];
		static uint8_t tempRotationBuffer[16];
		static uint8_t tempColorBuffer[4];
		static uint8_t tempSHBuffer[256];

		// 쿼터니언과 스케일을 위한 임시 변수들
		XMFLOAT4 tempQuat;
		XMFLOAT3 tempScale;
		XMFLOAT3 bucketCenterDelta;

		// UncompressedSplatArray::OFFSET에서 오프셋 값들 가져오기
		const int OFFSET_X = UncompressedSplatArray::OFFSET::X;
		const int OFFSET_Y = UncompressedSplatArray::OFFSET::Y;
		const int OFFSET_Z = UncompressedSplatArray::OFFSET::Z;
		const int OFFSET_SCALE0 = UncompressedSplatArray::OFFSET::SCALE0;
		const int OFFSET_SCALE1 = UncompressedSplatArray::OFFSET::SCALE1;
		const int OFFSET_SCALE2 = UncompressedSplatArray::OFFSET::SCALE2;
		const int OFFSET_ROT0 = UncompressedSplatArray::OFFSET::ROTATION0;
		const int OFFSET_ROT1 = UncompressedSplatArray::OFFSET::ROTATION1;
		const int OFFSET_ROT2 = UncompressedSplatArray::OFFSET::ROTATION2;
		const int OFFSET_ROT3 = UncompressedSplatArray::OFFSET::ROTATION3;
		const int OFFSET_FDC0 = UncompressedSplatArray::OFFSET::FDC0;
		const int OFFSET_FDC1 = UncompressedSplatArray::OFFSET::FDC1;
		const int OFFSET_FDC2 = UncompressedSplatArray::OFFSET::FDC2;
		const int OFFSET_OPACITY = UncompressedSplatArray::OFFSET::OPACITY;
		const int OFFSET_FRC0 = UncompressedSplatArray::OFFSET::FRC0;
		const int OFFSET_FRC9 = UncompressedSplatArray::OFFSET::FRC9;

		// 위치 오프셋 압축을 위한 도우미 함수
		auto compressPositionOffset = [](float v, float compressionScaleFactor, int compressionScaleRange) -> uint16_t {
			const int doubleCompressionScaleRange = compressionScaleRange * 2 + 1;
			v = std::round(v * compressionScaleFactor) + compressionScaleRange;
			return static_cast<uint16_t>(clamp(v, 0.0f, static_cast<float>(doubleCompressionScaleRange)));
			};

		// 압축 레벨에 따른 컴포넌트 개수 계산
		const int sphericalHarmonicsComponentsPerSplat = getSphericalHarmonicsComponentCountForDegree(sphericalHarmonicsDegree);
		const int bytesPerCenter = SplatBuffer::CompressionLevels[compressionLevel].BytesPerCenter;
		const int bytesPerScale = SplatBuffer::CompressionLevels[compressionLevel].BytesPerScale;
		const int bytesPerRotation = SplatBuffer::CompressionLevels[compressionLevel].BytesPerRotation;
		const int bytesPerColor = SplatBuffer::CompressionLevels[compressionLevel].BytesPerColor;

		// 각 컴포넌트의 기본 오프셋 계산
		const size_t centerBase = bufferOffset;
		const size_t scaleBase = centerBase + bytesPerCenter;
		const size_t rotationBase = scaleBase + bytesPerScale;
		const size_t colorBase = rotationBase + bytesPerRotation;
		const size_t sphericalHarmonicsBase = colorBase + bytesPerColor;

		// 쿼터니언 설정
		if (OFFSET_ROT0 < targetSplat.size() && targetSplat[OFFSET_ROT0] != 0.0f) {
			tempQuat.w = targetSplat[OFFSET_ROT0];
			tempQuat.x = targetSplat[OFFSET_ROT1];
			tempQuat.y = targetSplat[OFFSET_ROT2];
			tempQuat.z = targetSplat[OFFSET_ROT3];

			// 쿼터니언 정규화
			float length = std::sqrt(tempQuat.x * tempQuat.x + tempQuat.y * tempQuat.y +
				tempQuat.z * tempQuat.z + tempQuat.w * tempQuat.w);
			if (length > 0.0f) {
				tempQuat.x /= length;
				tempQuat.y /= length;
				tempQuat.z /= length;
				tempQuat.w /= length;
			}
		}
		else {
			// 기본 쿼터니언 (identity)
			tempQuat.x = 0.0f;
			tempQuat.y = 0.0f;
			tempQuat.z = 0.0f;
			tempQuat.w = 1.0f;
		}

		// 스케일 설정
		if (OFFSET_SCALE0 < targetSplat.size() && targetSplat[OFFSET_SCALE0] != 0.0f) {
			tempScale.x = targetSplat[OFFSET_SCALE0];
			tempScale.y = targetSplat[OFFSET_SCALE1];
			tempScale.z = targetSplat[OFFSET_SCALE2];
		}
		else {
			tempScale.x = 0.0f;
			tempScale.y = 0.0f;
			tempScale.z = 0.0f;
		}

		// 압축 레벨에 따른 데이터 처리
		if (compressionLevel == 0) {
			// 압축 없음
			float* center = reinterpret_cast<float*>(static_cast<uint8_t*>(sectionBuffer) + centerBase);
			float* rot = reinterpret_cast<float*>(static_cast<uint8_t*>(sectionBuffer) + rotationBase);
			float* scale = reinterpret_cast<float*>(static_cast<uint8_t*>(sectionBuffer) + scaleBase);

			// 쿼터니언 컴포넌트 저장
			rot[0] = tempQuat.x;
			rot[1] = tempQuat.y;
			rot[2] = tempQuat.z;
			rot[3] = tempQuat.w;

			// 스케일 컴포넌트 저장
			scale[0] = tempScale.x;
			scale[1] = tempScale.y;
			scale[2] = tempScale.z;

			// 중심 위치 저장
			center[0] = targetSplat[OFFSET_X];
			center[1] = targetSplat[OFFSET_Y];
			center[2] = targetSplat[OFFSET_Z];

			// 구면 조화 함수 처리 (Spherical Harmonics)
			if (sphericalHarmonicsDegree > 0) {
				float* shOut = reinterpret_cast<float*>(static_cast<uint8_t*>(sectionBuffer) + sphericalHarmonicsBase);
				if (sphericalHarmonicsDegree >= 1) {
					for (int s = 0; s < 9; s++) {
						shOut[s] = (OFFSET_FRC0 + s < targetSplat.size()) ? targetSplat[OFFSET_FRC0 + s] : 0.0f;
					}
					if (sphericalHarmonicsDegree >= 2) {
						for (int s = 0; s < 15; s++) {
							shOut[s + 9] = (OFFSET_FRC9 + s < targetSplat.size()) ? targetSplat[OFFSET_FRC9 + s] : 0.0f;
						}
					}
				}
			}
		}
		else {
			// 압축 사용
			uint16_t* center = reinterpret_cast<uint16_t*>(tempCenterBuffer);
			uint16_t* rot = reinterpret_cast<uint16_t*>(tempRotationBuffer);
			uint16_t* scale = reinterpret_cast<uint16_t*>(tempScaleBuffer);

			// 쿼터니언을 half float으로 변환
			rot[0] = toHalfFloat(tempQuat.x);
			rot[1] = toHalfFloat(tempQuat.y);
			rot[2] = toHalfFloat(tempQuat.z);
			rot[3] = toHalfFloat(tempQuat.w);

			// 스케일을 half float으로 변환
			scale[0] = toHalfFloat(tempScale.x);
			scale[1] = toHalfFloat(tempScale.y);
			scale[2] = toHalfFloat(tempScale.z);

			// 버킷 중심과의 델타 계산 및 압축
			bucketCenterDelta.x = targetSplat[OFFSET_X] - bucketCenter.x;
			bucketCenterDelta.y = targetSplat[OFFSET_Y] - bucketCenter.y;
			bucketCenterDelta.z = targetSplat[OFFSET_Z] - bucketCenter.z;

			center[0] = compressPositionOffset(bucketCenterDelta.x, compressionScaleFactor, compressionScaleRange);
			center[1] = compressPositionOffset(bucketCenterDelta.y, compressionScaleFactor, compressionScaleRange);
			center[2] = compressPositionOffset(bucketCenterDelta.z, compressionScaleFactor, compressionScaleRange);

			// 구면 조화 함수 처리 (Spherical Harmonics)
			if (sphericalHarmonicsDegree > 0) {
				// 압축 레벨에 따라 다른 타입 사용
				const int bytesPerSHComponent = (compressionLevel == 1) ? 2 : 1;

				if (sphericalHarmonicsDegree >= 1) {
					// Degree 1 구면 조화 함수 처리
					for (int s = 0; s < 9; s++) {
						float srcVal = (OFFSET_FRC0 + s < targetSplat.size()) ? targetSplat[OFFSET_FRC0 + s] : 0.0f;

						if (compressionLevel == 1) {
							uint16_t* shOut = reinterpret_cast<uint16_t*>(tempSHBuffer);
							shOut[s] = toHalfFloat(srcVal);
						}
						else {
							uint8_t* shOut = reinterpret_cast<uint8_t*>(tempSHBuffer);
							shOut[s] = toUint8(srcVal, minSphericalHarmonicsCoeff, maxSphericalHarmonicsCoeff);
						}
					}

					const int degree1ByteCount = 9 * bytesPerSHComponent;
					copyBetweenBuffers(tempSHBuffer, 0,
						static_cast<uint8_t*>(sectionBuffer) + sphericalHarmonicsBase, 0,
						degree1ByteCount);

					if (sphericalHarmonicsDegree >= 2) {
						// Degree 2 구면 조화 함수 처리
						for (int s = 0; s < 15; s++) {
							float srcVal = (OFFSET_FRC9 + s < targetSplat.size()) ? targetSplat[OFFSET_FRC9 + s] : 0.0f;

							if (compressionLevel == 1) {
								uint16_t* shOut = reinterpret_cast<uint16_t*>(tempSHBuffer);
								shOut[s + 9] = toHalfFloat(srcVal);
							}
							else {
								uint8_t* shOut = reinterpret_cast<uint8_t*>(tempSHBuffer);
								shOut[s + 9] = toUint8(srcVal, minSphericalHarmonicsCoeff, maxSphericalHarmonicsCoeff);
							}
						}

						copyBetweenBuffers(tempSHBuffer, degree1ByteCount,
							static_cast<uint8_t*>(sectionBuffer) + sphericalHarmonicsBase + degree1ByteCount, 0,
							15 * bytesPerSHComponent);
					}
				}
			}

			// 데이터를 최종 버퍼로 복사
			copyBetweenBuffers(tempCenterBuffer, 0,
				static_cast<uint8_t*>(sectionBuffer) + centerBase, 0, 6);
			copyBetweenBuffers(tempScaleBuffer, 0,
				static_cast<uint8_t*>(sectionBuffer) + scaleBase, 0, 6);
			copyBetweenBuffers(tempRotationBuffer, 0,
				static_cast<uint8_t*>(sectionBuffer) + rotationBase, 0, 8);
		}

		// 색상 및 불투명도 처리
		uint8_t* rgba = reinterpret_cast<uint8_t*>(tempColorBuffer);
		rgba[0] = static_cast<uint8_t>((OFFSET_FDC0 < targetSplat.size()) ? targetSplat[OFFSET_FDC0] : 0.0f);
		rgba[1] = static_cast<uint8_t>((OFFSET_FDC1 < targetSplat.size()) ? targetSplat[OFFSET_FDC1] : 0.0f);
		rgba[2] = static_cast<uint8_t>((OFFSET_FDC2 < targetSplat.size()) ? targetSplat[OFFSET_FDC2] : 0.0f);
		rgba[3] = static_cast<uint8_t>((OFFSET_OPACITY < targetSplat.size()) ? targetSplat[OFFSET_OPACITY] : 0.0f);

		// 색상 데이터를 최종 버퍼로 복사
		copyBetweenBuffers(tempColorBuffer, 0,
			static_cast<uint8_t*>(sectionBuffer) + colorBase, 0, 4);
	}


	/**
	 * Write section header data to a buffer
	 * @param sectionHeader The section header structure
	 * @param compressionLevel The compression level (0-2)
	 * @param buffer The buffer to write to
	 * @param offset Offset in the buffer (default 0)
	 */
	static void writeSectionHeaderToBuffer(
		const SectionHeader& sectionHeader,
		int compressionLevel,
		void* buffer,
		size_t offset = 0) {

		uint16_t* sectionHeaderArrayUint16 = reinterpret_cast<uint16_t*>(
			static_cast<uint8_t*>(buffer) + offset);
		uint32_t* sectionHeaderArrayUint32 = reinterpret_cast<uint32_t*>(
			static_cast<uint8_t*>(buffer) + offset);
		float* sectionHeaderArrayFloat32 = reinterpret_cast<float*>(
			static_cast<uint8_t*>(buffer) + offset);

		sectionHeaderArrayUint32[0] = sectionHeader.splatCount;
		sectionHeaderArrayUint32[1] = sectionHeader.maxSplatCount;
		sectionHeaderArrayUint32[2] = compressionLevel >= 1 ? sectionHeader.bucketSize : 0;
		sectionHeaderArrayUint32[3] = compressionLevel >= 1 ? sectionHeader.bucketCount : 0;
		sectionHeaderArrayFloat32[4] = compressionLevel >= 1 ? sectionHeader.bucketBlockSize : 0.0f;
		sectionHeaderArrayUint16[10] = compressionLevel >= 1 ? SplatBuffer::BucketStorageSizeBytes : 0;
		sectionHeaderArrayUint32[6] = compressionLevel >= 1 ? sectionHeader.compressionScaleRange : 0;
		sectionHeaderArrayUint32[7] = sectionHeader.storageSizeBytes;
		sectionHeaderArrayUint32[8] = compressionLevel >= 1 ? sectionHeader.fullBucketCount : 0;
		sectionHeaderArrayUint32[9] = compressionLevel >= 1 ? sectionHeader.partiallyFilledBucketCount : 0;
		sectionHeaderArrayUint16[20] = sectionHeader.sphericalHarmonicsDegree;
	}
	// Constructor
	SplatBuffer(const void* bufferData, bool secLoadedCountsToMax = true)
	{
		constructFromBuffer(bufferData, secLoadedCountsToMax);
	}

	// Space for methods to be added later
	int getSplatCount() {
		return this->splatCount;
	}

	int getMaxSplatCount() {
		return this->maxSplatCount;
	}


private:
	const void* bufferData;
	std::vector<int> globalSplatIndexToLocalSplatIndexMap;
	std::vector<int> globalSplatIndexToSectionMap;
	int versionMajor, versionMinor;
	int maxSectionCount, sectionCount;
	int maxSplatCount, splatCount;
	int compressionLevel;
	XMFLOAT3 sceneCenter;
	float minSphericalHarmonicsCoeff, maxSphericalHarmonicsCoeff;
	std::vector<Section> sections;


	void constructFromBuffer(const void* bufferData, bool secLoadedCountsToMax) {
		this->bufferData = bufferData;

		this->globalSplatIndexToLocalSplatIndexMap.clear();
		this->globalSplatIndexToSectionMap.clear();

		// Parse the header from buffer data
		Header header = SplatBuffer::parseHeader(this->bufferData);
		this->versionMajor = header.versionMajor;
		this->versionMinor = header.versionMinor;
		this->maxSectionCount = header.maxSectionCount;
		this->sectionCount = secLoadedCountsToMax ? header.maxSectionCount : 0;
		this->maxSplatCount = header.maxSplatCount;
		this->splatCount = secLoadedCountsToMax ? header.maxSplatCount : 0;
		this->compressionLevel = header.compressionLevel;
		this->sceneCenter = header.sceneCenter; // Assuming Vector3 copy constructor
		this->minSphericalHarmonicsCoeff = header.minSphericalHarmonicsCoeff;
		this->maxSphericalHarmonicsCoeff = header.maxSphericalHarmonicsCoeff;

		// Parse section headers
		this->sections = SplatBuffer::parseSectionHeaders(header, this->bufferData, SplatBuffer::HeaderSizeBytes, secLoadedCountsToMax);

		// Link buffer arrays and build maps
		this->linkBufferArrays();
		this->buildMaps();
	}

	void linkBufferArrays() {
		for (int i = 0; i < this->maxSectionCount; i++) {
			Section& section = this->sections[i];

			// Allocate and copy bucket array data
			section.bucketArray.resize(section.bucketCount * SplatBuffer::BucketStorageSizeFloats);
			const float* bucketData = reinterpret_cast<const float*>(
				static_cast<const uint8_t*>(this->bufferData) + section.bucketsBase);
			std::memcpy(section.bucketArray.data(), bucketData,
				section.bucketCount * SplatBuffer::BucketStorageSizeFloats * sizeof(float));

			// Allocate and copy partially filled bucket lengths if needed
			if (section.partiallyFilledBucketCount > 0) {
				section.partiallyFilledBucketLengths.resize(section.partiallyFilledBucketCount);
				const uint32_t* bucketLengths = reinterpret_cast<const uint32_t*>(
					static_cast<const uint8_t*>(this->bufferData) + section.base);
				std::memcpy(section.partiallyFilledBucketLengths.data(), bucketLengths,
					section.partiallyFilledBucketCount * sizeof(uint32_t));
			}
		}
	}

	void buildMaps() {
		int cumulativeSplatCount = 0;

		// Resize maps to accommodate all splats
		this->globalSplatIndexToLocalSplatIndexMap.resize(this->maxSplatCount);
		this->globalSplatIndexToSectionMap.resize(this->maxSplatCount);

		for (int i = 0; i < this->maxSectionCount; i++) {
			const Section& section = this->sections[i];
			for (int j = 0; j < section.maxSplatCount; j++) {
				const int globalSplatIndex = cumulativeSplatCount + j;
				this->globalSplatIndexToLocalSplatIndexMap[globalSplatIndex] = j;
				this->globalSplatIndexToSectionMap[globalSplatIndex] = i;
			}
			cumulativeSplatCount += section.maxSplatCount;
		}
	}
};

class SplatParser {
public:
	// Constants
	static const int RowSizeBytes = 32;
	static const int CenterSizeBytes = 12;
	static const int ScaleSizeBytes = 12;
	static const int RotationSizeBytes = 4;
	static const int ColorSizeBytes = 4;

	// Parse standard splat format to UncompressedSplatArray
	static UncompressedSplatArray parseStandardSplatToUncompressedSplatArray(const void* inBuffer, size_t bufferSize) {
		// Standard .splat row layout:
		// XYZ - Position (Float32)
		// XYZ - Scale (Float32)
		// RGBA - colors (uint8)
		// IJKL - quaternion/rot (uint8)

		const size_t splatCount = bufferSize / SplatParser::RowSizeBytes;
		UncompressedSplatArray splatArray;

		const uint8_t* bufferPtr = static_cast<const uint8_t*>(inBuffer);

		for (size_t i = 0; i < splatCount; i++) {
			const size_t inBase = i * SplatParser::RowSizeBytes;

			// Read position (center)
			const float* inCenter = reinterpret_cast<const float*>(bufferPtr + inBase);

			// Read scale
			const float* inScale = reinterpret_cast<const float*>(bufferPtr + inBase + SplatParser::CenterSizeBytes);

			// Read color
			const uint8_t* inColor = bufferPtr + inBase + SplatParser::CenterSizeBytes + SplatParser::ScaleSizeBytes;

			// Read rotation
			const uint8_t* inRotation = bufferPtr + inBase + SplatParser::CenterSizeBytes +
				SplatParser::ScaleSizeBytes + SplatParser::ColorSizeBytes;

			// Convert rotation from uint8 to quaternion
			// Normalize quaternion components from [0,255] to [-1,1]
			float qx = (inRotation[1] - 128) / 128.0f;
			float qy = (inRotation[2] - 128) / 128.0f;
			float qz = (inRotation[3] - 128) / 128.0f;
			float qw = (inRotation[0] - 128) / 128.0f;

			// Normalize quaternion
			float len = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
			qw /= len;
			qx /= len;
			qy /= len;
			qz /= len;

			// Add splat to array
			splatArray.addSplatFromComonents(
				inCenter[0], inCenter[1], inCenter[2],           // position
				inScale[0], inScale[1], inScale[2],              // scale
				qw, qx, qy, qz,                                  // rotation quaternion
				inColor[0], inColor[1], inColor[2], inColor[3]   // color + alpha
			);
		}

		return splatArray;
	}
};

class SplatLoader {
public:
	// Load from file data function
	static UncompressedSplatArray loadFromFileData(
		const void* splatFileData,
		size_t fileSize,
		float minimumAlpha,
		int compressionLevel,
		bool optimizeSplatData,
		int sectionSize,
		const XMFLOAT3& sceneCenter,
		float blockSize,
		int bucketSize) {

		// Parse the splat file data
		UncompressedSplatArray splatArray = SplatParser::parseStandardSplatToUncompressedSplatArray(
			splatFileData, fileSize);

		// Call finalize function
		return finalize(splatArray, optimizeSplatData, minimumAlpha, compressionLevel,
			sectionSize, sceneCenter, blockSize, bucketSize);
	}

private:
	// Implement finalize function
	static UncompressedSplatArray finalize(
		UncompressedSplatArray& splatArray,
		bool optimizeSplatData,
		float minimumAlpha,
		int compressionLevel,
		int sectionSize,
		const XMFLOAT3& sceneCenter,
		float blockSize,
		int bucketSize) {

		// Implementation of finalize logic would go here
		// This would likely include:
		// - Filtering splats by alpha
		// - Optimizing the data if needed
		// - Partitioning the data
		// - Compressing the data

		// For now, return the original array (this should be expanded based on actual needs)
		return splatArray;
	}
};

class KSplatLoader {
public:
	// Check if the file version is supported
	static bool checkVersion(const void* buffer) {
		const int minVersionMajor = SplatBuffer::CurrentMajorVersion;
		const int minVersionMinor = SplatBuffer::CurrentMinorVersion;

		SplatBuffer::Header header = SplatBuffer::parseHeader(buffer);

		if ((header.versionMajor == minVersionMajor &&
			header.versionMinor >= minVersionMinor) ||
			header.versionMajor > minVersionMajor) {
			return true;
		}
		else {
			// Format error message
			std::stringstream errorMsg;
			errorMsg << "KSplat version not supported: v" << header.versionMajor << "." << header.versionMinor
				<< ". Minimum required: v" << minVersionMajor << "." << minVersionMinor;

			// Throw exception with formatted message
			throw std::runtime_error(errorMsg.str());
		}
	}

	// Load from file data
	static SplatBuffer* loadFromFileData(const void* fileData) {
		try {
			// Check version first
			KSplatLoader::checkVersion(fileData);

			// Create and return a new SplatBuffer
			return new SplatBuffer(fileData);
		}
		catch (const std::exception& e) {
			// Log the error
			vzlog_error("KSplatLoader error: %s", e.what());
			return nullptr;
		}
	}
};

/**
* Options for splat buffer processing
*/
struct SplatBufferOptions {
	// Rotation or orientation settings
	XMFLOAT4 rotation;           // Quaternion for rotation

	// Position settings
	XMFLOAT3 position;           // Position offset

	// Scale settings
	XMFLOAT3 scale;              // Scale factor

	// Alpha threshold for removing splats
	float splatAlphaRemovalThreshold = 0.0f;

	// Block size factor for partitioning
	float blockSizeFactor = 1.0f;

	// Bucket size factor for partitioning
	float bucketSizeFactor = 1.0f;
};

