#pragma once
#define __dojostatic extern "C" __declspec(dllexport)
#define __dojoclass class //__declspec(dllexport)
#define __dojostruct struct __declspec(dllexport)

#define __FP (float*)&
#define VZRESULT int
#define VZ_OK 0
#define VZ_FAIL 1
#define VZ_JOB_WAIT 1

#define SAFE_GET_COPY(DST_PTR, SRC_PTR, TYPE, ELEMENTS) { if(DST_PTR) memcpy(DST_PTR, SRC_PTR, sizeof(TYPE)*ELEMENTS); }
#define GET_COPY(DST_PTR, SRC_PTR, TYPE, ELEMENTS) { memcpy(DST_PTR, SRC_PTR, sizeof(TYPE)*ELEMENTS); }

// std
#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <set>
#include <any>
#include <list>
#include <memory>
#include <algorithm>
#include <chrono>

using VID = uint32_t;
using TimeStamp = std::chrono::high_resolution_clock::time_point;

constexpr float VZ_PI = 3.141592654f;
constexpr float VZ_2PI = 6.283185307f;
constexpr float VZ_1DIVPI = 0.318309886f;
constexpr float VZ_1DIV2PI = 0.159154943f;
constexpr float VZ_PIDIV2 = 1.570796327f;
constexpr float VZ_PIDIV4 = 0.785398163f;

namespace vzm
{
	__dojostatic inline void TransformPoint(const float posSrc[3], const float mat[16], const bool rowMajor, float posDst[3]);
	__dojostatic inline void TransformVector(const float vecSrc[3], const float mat[16], const bool rowMajor, float vecDst[3]);
	__dojostatic inline void ComputeBoxTransformMatrix(const float cubeScale[3], const float posCenter[3],
		const float yAxis[3], const float zAxis[3], const bool rowMajor, float mat[16], float matInv[16]);

	template <typename ID> struct ParamMap {
	private:
		std::string __PM_VERSION = "LIBI_1.4";
		std::unordered_map<ID, std::any> __params;
	public:
		bool FindParam(const ID& param_name) {
			auto it = __params.find(param_name);
			return !(it == __params.end());
		}
		template <typename SRCV> bool GetParamCheck(const ID& key, SRCV& param) {
			auto it = __params.find(key);
			if (it == __params.end()) return false;
			param = std::any_cast<SRCV&>(it->second);
			return true;
		}
		template <typename SRCV> SRCV GetParam(const ID& key, const SRCV& init_v) const {
			auto it = __params.find(key);
			if (it == __params.end()) return init_v;
			return std::any_cast<const SRCV&>(it->second);
		}
		template <typename SRCV> SRCV* GetParamPtr(const ID& key) {
			auto it = __params.find(key);
			if (it == __params.end()) return NULL;
			return (SRCV*)&std::any_cast<SRCV&>(it->second);
		}
		template <typename SRCV, typename DSTV> bool GetParamCastingCheck(const ID& key, DSTV& param) {
			auto it = __params.find(key);
			if (it == __params.end()) return false;
			param = (DSTV)std::any_cast<SRCV&>(it->second);
			return true;
		}
		template <typename SRCV, typename DSTV> DSTV GetParamCasting(const ID& key, const DSTV& init_v) {
			auto it = __params.find(key);
			if (it == __params.end()) return init_v;
			return (DSTV)std::any_cast<SRCV&>(it->second);
		}
		void SetParam(const ID& key, const std::any& param) {
			__params[key] = param;
		}
		void RemoveParam(const ID& key) {
			auto it = __params.find(key);
			if (it != __params.end()) {
				__params.erase(it);
			}
		}
		void RemoveAll() {
			__params.clear();
		}
		size_t Size() {
			return __params.size();
		}
		std::string GetPMapVersion() {
			return __PM_VERSION;
		}

		typedef std::unordered_map<ID, std::any> MapType;
		typename typedef MapType::iterator iterator;
		typename typedef MapType::const_iterator const_iterator;
		typename typedef MapType::reference reference;
		iterator begin() { return __params.begin(); }
		const_iterator begin() const { return __params.begin(); }
		iterator end() { return __params.end(); }
		const_iterator end() const { return __params.end(); }
	};

	struct BoxTr
	{
	private:
		// from unit cube centered at origin
		float __cube_size[3] = { 1.f, 1.f, 1.f };
		float __pos_center[3] = { 0, 0, 0 };
		float __y_axis[3] = { 0, 1.f, 0 };
		float __z_axis[3] = { 0, 0, 1.f };
		float __mat_tr[16] = { 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f }; // target-sized box to unit axis-aligned box
		float __inv_mat_tr[16] = { 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f }; // inverse
		bool __is_rowMajor = false;
	public:
		void SetBoxTr(const float cubeSize[3], const float posCenter[3], const float yAxis[3], const float zAxis[3], const bool rowMajor = false) {
			SAFE_GET_COPY(__cube_size, cubeSize, float, 3);
			SAFE_GET_COPY(__pos_center, posCenter, float, 3);
			SAFE_GET_COPY(__y_axis, yAxis, float, 3);
			SAFE_GET_COPY(__z_axis, zAxis, float, 3);
			ComputeBoxTransformMatrix(cubeSize, posCenter, yAxis, zAxis, rowMajor, __mat_tr, __inv_mat_tr);
		}
		// mat_tr : to aligned unit-cube
		// inv_mat_tr : to oblique cube
		void GetMatrix(float mat[16], float matnv[16]) const {
			SAFE_GET_COPY(mat, __mat_tr, float, 16);
			SAFE_GET_COPY(matnv, __inv_mat_tr, float, 16);
		}
		void GetCubeInfo(float cubeSize[3], float posCenter[3], float yAxis[3], float zAxis[3]) const {
			SAFE_GET_COPY(cubeSize, __cube_size, float, 3);
			SAFE_GET_COPY(posCenter, __pos_center, float, 3);
			SAFE_GET_COPY(yAxis, __y_axis, float, 3);
			SAFE_GET_COPY(zAxis, __z_axis, float, 3);
		}
	};
	struct TextItem
	{
	private:
		int version = 230813;
	public:
		std::string textStr = "";
		std::string font = "";
		std::string alignment = ""; // CENTER, LEFT, RIGHT to the position
		float fontSize = 10.f; // Logical size of the font in DIP units. A DIP ("device-independent pixel") equals 1/96 inch.
		int iColor = 0xFFFFFF; //RGB ==> 0xFF0000 (Red), 0x00FF00 (Green)
		float alpha = 1.f;
		bool isItalic = false;
		int fontWeight = 4; // 1 : thinest, 4 : regular, 7 : bold, 9 : maximum heavy
		int posScreenX = 0, posScreenY = 0; // 
	};
	struct TransformParams
	{
	private:
		float position[3] = { 0, 0, 0 };
		float direction[3] = { 0, 1, 0 };
		float rotation[4] = { 0, 0, 0, 1 };	// quaternion
		float scale[3] = { 1, 1, 1 };

		bool rowMajor = false; // true : column major
		bool localTransform = true; // false : worldTransform
		float __pivot2os[16] = { 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f }; // original object space to pivot object space 
		float __os2ls[16] = { 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f }; // object space to local space (used in hierarchical tree structure)
		float __os2ws[16] = { 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f }; // 4x4 matrix col-major (same as in glm::fmat4x4)

	public:
		// note that those lookAt parameters are used for LOCAL matrix
		float pos[3] = { 0, 0, 0 };
		float view[3] = { 0, 0, -1.f };
		float up[3] = { 0, 1.f, 0 }; // default..

		// type T must have 3 float-type parameters 
		template<typename T> void SetPos(const T& p) {
			memcpy(pos, p, sizeof(float) * 3);
		}
		template<typename T> void SetView(const T& v) {
			memcpy(view, v, sizeof(float) * 3);
		}
		template<typename T> void SetUp(const T& u) {
			memcpy(up, u, sizeof(float) * 3);
		}

		void SetLookAt(const float lookAt[3]) { // note that pos must be set before calling this
			view[0] = lookAt[0] - pos[0]; view[1] = lookAt[1] - pos[1]; view[2] = lookAt[2] - pos[2];
			float length = sqrt(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]);
			if (length > 0) { view[0] /= length; view[1] /= length; view[2] /= length; }
		}
		bool IsLocalTransform() { return localTransform; }
		void SetWorldTransform(const float os2ws[16]) {
			memcpy(__os2ws, os2ws, sizeof(float) * 16); localTransform = false;
		};
		void UpdateWorldTransform(const float os2ws[16]) {
			memcpy(__os2ws, os2ws, sizeof(float) * 16);
		}
		const float* GetWorldTransform() const { return __os2ws; };
		void SetLocalTransform(const float os2ls[16]) {
			memcpy(__os2ls, os2ls, sizeof(float) * 16); localTransform = true;
		};
		const float* GetLocalTransform() const { return __os2ls; };
		void SetObjectPivot(const float pivot2os[16]) {
			memcpy(__pivot2os, pivot2os, sizeof(float) * 16);
		}
		const float* GetPivotTransform() const { return __pivot2os; };
	};
	struct CameraParams : TransformParams
	{
		enum ProjectionMode {
			UNDEFINED = 0,
			IMAGEPLANE_SIZE = 1, // use ip_w, ip_h instead of fov_y
			CAMERA_FOV = 2, // use fov_y, aspect_ratio
			CAMERA_INTRINSICS = 3, // AR mode
			SLICER_PLANE = 4, // mpr sectional mode
			SLICER_CURVED = 5, // pano sectional mode
		};
		ProjectionMode projectionMode = ProjectionMode::UNDEFINED;
		union {
			struct { // projection_mode == 1 or 4 or 5
				float ip_w;
				float ip_h; // defined in CS
			};
			struct { // projection_mode == 2
				// fov_y should be smaller than PI
				float fov_y;
				float aspect_ratio; // ip_w / ip_h
			};
			struct { // projection_mode == 3
				// camera intrinsics mode 
				float fx;
				float fy;
				float sc;
				float cx;
				float cy;
			};
		};
		float np = 0.01f;
		float fp = 1000.f; // the scale difference is recommended : ~100000 (in a single precision (float))
		float w = 0;
		float h = 0; // resolution. note that the aspect ratio is recomputed w.r.t. w and h during the camera setting.
		float dpi = 96.f;
		bool displayCamTextItem = false;
		bool displayActorLabel = false;

		TimeStamp timeStamp = std::chrono::high_resolution_clock::now(); // will be automatically set 

		ParamMap<std::string> attributes;
		ParamMap<std::string> textItems; // value must be TextItem
	};
	struct ActorParams : TransformParams
	{
	public:
		enum RES_USAGE
		{
			GEOMETRY, // main volume or primitives
			VR_OTF,
			MPR_WINDOWING,
			COLOR_MAP,
			TEXTURE_VOLUME,
			TEXTURE_2D,
			MASK_VOLUME, // only for volume rendering ... multi-OTF
		};
	private:
		ParamMap<RES_USAGE> associatedObjIds; // <usage, obj_id> 

	public:
		bool isVisible = true;
		bool isPickable = false;

		VID GetResourceID(const RES_USAGE resUsage) const {
			return associatedObjIds.GetParam(resUsage, (VID)0);
		}
		void SetResourceID(const RES_USAGE resUsage, const VID resId) {
			associatedObjIds.SetParam(resUsage, resId);
		}

		TimeStamp timeStamp = std::chrono::high_resolution_clock::now(); // will be automatically set 

		TextItem label;
		ParamMap<std::string> attributes;
	};
	struct LightParams : TransformParams
	{
		enum FLAGS
		{
			EMPTY = 0,
			CAST_SHADOW = 1 << 0,
			VOLUMETRICS = 1 << 1,
			VISUALIZER = 1 << 2,
			LIGHTMAPONLY_STATIC = 1 << 3,
			VOLUMETRICCLOUDS = 1 << 4,
		};
		uint32_t _flags = EMPTY;

		enum LightType
		{
			DIRECTIONAL = 0,
			POINT,
			SPOT,
			LIGHTTYPE_COUNT,
			ENUM_FORCE_UINT32 = 0xFFFFFFFF,
		};
		LightType type = POINT;

		float color[3] = { 1, 1, 1 };
		float intensity = 1.0f; // Brightness of light in. The units that this is defined in depend on the type of light. Point and spot lights use luminous intensity in candela (lm/sr) while directional lights use illuminance in lux (lm/m2). https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual
		float range = 10.0f;
		float outerConeAngle = VZ_PIDIV4;
		float innerConeAngle = 0; // default value is 0, means only outer cone angle is used
		float radius = 0.025f;
		float length = 0;

		std::vector<float> cascade_distances = { 8,80,800 };
		std::vector<std::string> lensFlareNames;

		int forced_shadow_resolution = -1; // -1: disabled, greater: fixed shadow map resolution

		// Non-serialized attributes:
		float position[3] = { 0, 0, 0 };
		float direction[3] = { 0, 1, 0 };
		float rotation[4] = { 0, 0, 0, 1 };
		float scale[3] = { 1, 1, 1 };
		mutable int occlusionquery = -1;

		inline void SetCastShadow(bool value) { if (value) { _flags |= CAST_SHADOW; } else { _flags &= ~CAST_SHADOW; } }
		inline void SetVolumetricsEnabled(bool value) { if (value) { _flags |= VOLUMETRICS; } else { _flags &= ~VOLUMETRICS; } }
		inline void SetVisualizerEnabled(bool value) { if (value) { _flags |= VISUALIZER; } else { _flags &= ~VISUALIZER; } }
		inline void SetStatic(bool value) { if (value) { _flags |= LIGHTMAPONLY_STATIC; } else { _flags &= ~LIGHTMAPONLY_STATIC; } }
		inline void SetVolumetricCloudsEnabled(bool value) { if (value) { _flags |= VOLUMETRICCLOUDS; } else { _flags &= ~VOLUMETRICCLOUDS; } }

		inline bool IsCastingShadow() const { return _flags & CAST_SHADOW; }
		inline bool IsVolumetricsEnabled() const { return _flags & VOLUMETRICS; }
		inline bool IsVisualizerEnabled() const { return _flags & VISUALIZER; }
		inline bool IsStatic() const { return _flags & LIGHTMAPONLY_STATIC; }
		inline bool IsVolumetricCloudsEnabled() const { return _flags & VOLUMETRICCLOUDS; }

		inline float GetRange() const
		{
			float retval = range;
			retval = std::max(0.001f, retval);
			retval = std::min(retval, 65504.0f); // clamp to 16-bit float max value
			return retval;
		}

		inline void SetType(LightType val) { type = val; }
		inline LightType GetType() const { return type; }

		// Set energy amount with non physical light units (from before version 0.70.0):
		inline void BackCompatSetEnergy(float energy)
		{
			switch (type)
			{
			case POINT:
				intensity = energy * 20;
				break;
			case SPOT:
				intensity = energy * 200;
				break;
			default:
				break;
			}
		}
	};
}