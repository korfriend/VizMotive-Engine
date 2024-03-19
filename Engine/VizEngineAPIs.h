#pragma once
#define __dojostatic extern "C" __declspec(dllexport)
#define __dojoclass class __declspec(dllexport)
#define __dojostruct struct __declspec(dllexport)

#define __FP (float*)&
#define VZRESULT int
#define VZ_OK 0
#define VZ_FAIL 1

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

using VID = uint32_t;

constexpr float VZ_PI = 3.141592654f;
constexpr float VZ_2PI = 6.283185307f;
constexpr float VZ_1DIVPI = 0.318309886f;
constexpr float VZ_1DIV2PI = 0.159154943f;
constexpr float VZ_PIDIV2 = 1.570796327f;
constexpr float VZ_PIDIV4 = 0.785398163f;

namespace vmath
{
	__dojostatic inline void TransformPoint(const float* pos_src, const float* mat, const bool is_rowMajor, float* pos_dst);
	__dojostatic inline void TransformVector(const float* vec_src, const float* mat, const bool is_rowMajor, float* vec_dst);
	__dojostatic inline void ComputeBoxTransformMatrix(const float* cube_scale, const float* pos_center, const float* y_axis, const float* z_axis, const bool is_rowMajor, float* mat_tr, float* inv_mat_tr);
}

namespace vzm 
{
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
		template <typename SRCV> SRCV GetParam(const ID& key, const SRCV& init_v) {
			auto it = __params.find(key);
			if (it == __params.end()) return init_v;
			return std::any_cast<SRCV&>(it->second);
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
		void SetBoxTr(const float* cube_size, const float* pos_center, const float* y_axis, const float* z_axis, const bool is_rowMajor = false) {
			SAFE_GET_COPY(__cube_size, cube_size, float, 3);
			SAFE_GET_COPY(__pos_center, pos_center, float, 3);
			SAFE_GET_COPY(__y_axis, y_axis, float, 3);
			SAFE_GET_COPY(__z_axis, z_axis, float, 3);
			vmath::ComputeBoxTransformMatrix(cube_size, pos_center, y_axis, z_axis, is_rowMajor, __mat_tr, __inv_mat_tr);
		}
		// mat_tr : to aligned unit-cube
		// inv_mat_tr : to oblique cube
		void GetMatrix(float* mat_tr, float* inv_mat_tr) const {
			SAFE_GET_COPY(mat_tr, __mat_tr, float, 16);
			SAFE_GET_COPY(inv_mat_tr, __inv_mat_tr, float, 16);
		}
		void GetCubeInfo(float* cube_size, float* pos_center, float* y_axis, float* z_axis) const {
			SAFE_GET_COPY(cube_size, __cube_size, float, 3);
			SAFE_GET_COPY(pos_center, __pos_center, float, 3);
			SAFE_GET_COPY(y_axis, __y_axis, float, 3);
			SAFE_GET_COPY(z_axis, __z_axis, float, 3);
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
	struct TransformParameter
	{
	private:
		float position[3] = { 0, 0, 0 };
		float direction[3] = { 0, 1, 0 };
		float rotation[4] = { 0, 0, 0, 1 };
		float scale[3] = { 1, 1, 1 };


		bool is_rowMajor = false;
		bool use_localTrans = true;
		float __pivot2os[16] = { 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f }; // original object space to pivot object space 
		float __os2ls[16] = { 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f }; // object space to local space (used in hierarchical tree structure)
		float __os2ws[16] = { 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f, 0, 0, 0, 0, 1.f }; // 4x4 matrix col-major (same as in glm::fmat4x4)

	public:
		// note that those lookAt parameters are used for LOCAL matrix
		float pos[3] = { 0, 0, 0 };
		float view[3] = { 0, 0, -1.f };
		float up[3] = { 0, 1.f, 0 }; // default..

		void SetLookAt(const float* look_at) { // note that pos must be set before calling this
			view[0] = look_at[0] - pos[0]; view[1] = look_at[1] - pos[1]; view[2] = look_at[2] - pos[2];
			float length = sqrt(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]);
			if (length > 0) { view[0] /= length; view[1] /= length; view[2] /= length; }
		}
		bool IsLocalTransform() { return use_localTrans; }
		void SetWorldTransform(const float* os2ws) {
			memcpy(__os2ws, os2ws, sizeof(float) * 16); use_localTrans = false;
		};
		void UpdateWorldTransform(const float* os2ws) {
			memcpy(__os2ws, os2ws, sizeof(float) * 16);
		}
		const float* GetWorldTransform() const { return __os2ws; };

		// those local interfaces will be implemented when supporting group interfaces! //
		void SetLocalTransform(const float* os2ls) {
			memcpy(__os2ls, os2ls, sizeof(float) * 16); use_localTrans = true;
		};
		const float* GetLocalTransform() const { return __os2ls; };

		void SetObjectPivot(const float* pivot2os) {
			memcpy(__pivot2os, pivot2os, sizeof(float) * 16);
		}

		const float* GetPivotTransform() const { return __pivot2os; };
	};
	struct CameraParameter : TransformParameter
	{
		enum ProjectionMode {
			UNDEFINED = 0,
			IMAGEPLANE_SIZE = 1, // use ip_w, ip_h instead of fov_y
			CAMERA_FOV = 2, // use fov_y, aspect_ratio
			CAMERA_INTRINSICS = 3, // AR mode
			SLICER_PLANE = 4, // mpr sectional mode
			SLICER_CURVED = 5, // pano sectional mode
		};
		enum VolumeRayCastMode {
			OPTICAL_INTEGRATION = 0,
			OPTICAL_INTEGRATION_MULTI_OTF = 23,
			OPTICAL_INTEGRATION_TRANSPARENCY = 1,
			OPTICAL_INTEGRATION_MULTI_OTF_TRANSPARENCY = 2,
			OPTICAL_INTEGRATION_SCULPT_MASK = 22,
			OPTICAL_INTEGRATION_SCULPT_MASK_TRANSPARENCY = 25,
			ISO_SURFACE = 21,
			ISO_SURFACE_MULTI_OTF = 26,
			VOLMASK_VISUALIZATION = 24,
			MAXIMUM_INTENSITY = 10,
			MINIMUM_INTENSITY = 11,
			AVERAGE_INTENSITY = 12
		};
		ProjectionMode projection_mode = ProjectionMode::UNDEFINED;
		VolumeRayCastMode volraycast_mode = VolumeRayCastMode::OPTICAL_INTEGRATION;
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
		int w = 0;
		int h = 0; // resolution. note that the aspect ratio is recomputed w.r.t. w and h during the camera setting.
		bool is_rgba_write = false; // if false, use BGRA order
		bool skip_sys_fb_update = false; // if false, skip the copyback process of the final rendertarget to system update buffers
		//HWND hWnd = NULL; // if NULL, offscreen rendering is performed

		ParamMap<std::string> script_params;
		ParamMap<std::string> test_params;
		ParamMap<std::string> text_items; // value must be TextItem
		bool displayCamTextItem = false;
		bool displayActorLabel = false;
		unsigned long long timeStamp = 0ull; // will be automatically set 

		std::set<int> hidden_actors;
		void SetCurvedSlicer(const float curved_plane_w, const float curved_plane_h, const float* curve_pos_pts, const float* curve_up_pts, const float* curve_tan_pts, const int num_curve_pts) {
			script_params.SetParam("CURVED_PLANE_WIDTH", curved_plane_w);
			script_params.SetParam("CURVED_PLANE_HEIGHT", curved_plane_h);
			std::vector<float> vf_curve_pos_pts(num_curve_pts * 3), vf_curve_up_pts(num_curve_pts * 3), vf_curve_tan_pts(num_curve_pts * 3);
			memcpy(&vf_curve_pos_pts[0], curve_pos_pts, sizeof(float) * 3 * num_curve_pts);
			memcpy(&vf_curve_up_pts[0], curve_up_pts, sizeof(float) * 3 * num_curve_pts);
			memcpy(&vf_curve_tan_pts[0], curve_tan_pts, sizeof(float) * 3 * num_curve_pts);
			script_params.SetParam("COUNT_INTERPOLATION_POINTS", num_curve_pts);
			script_params.SetParam("ARRAY_CURVE_INTERPOLATION_POS", vf_curve_pos_pts);
			script_params.SetParam("ARRAY_CURVE_INTERPOLATION_UP", vf_curve_up_pts);
			script_params.SetParam("ARRAY_CURVE_INTERPOLATION_TANGENT", vf_curve_tan_pts);
		}
		void SetOrthogonalProjection(const bool orthoproj_mode) {
			// only available when projection_mode == IMAGEPLANE_SIZE
			script_params.SetParam("ORTHOGONAL_PROJECTION", orthoproj_mode);
		}
		void SetSlicerThickness(const float thickness) {
			// only available when projection_mode == SLICER_PLANE or SLICER_CURVED
			script_params.SetParam("SLICER_THICKNESS", thickness);
		}
		void StoreSlicerCutLines(const bool is_store) {
			// only available when projection_mode == SLICER_PLANE or SLICER_CURVED
			script_params.SetParam("STORE_SLICERCUTLINES", is_store);
		}
		void Set2xVolumeRayCaster(const bool enable) {
			// only available when projection_mode == SLICER_PLANE or SLICER_CURVED
			script_params.SetParam("FAST_VOLRAYCASTER2X", enable);
		}
		void SetOutlineEffect(const int thick_pixs, const float depth_thres, const float* outline_color_rgb, const bool fadeEffect) {
			// if thick_pixs == 0, no outline
			script_params.SetParam("SILHOUETTE_THICKNESS", thick_pixs);
			script_params.SetParam("SILHOUETTE_DEPTH_THRES", depth_thres);
			if (outline_color_rgb != NULL) {
				std::vector<float> color = { outline_color_rgb[0], outline_color_rgb[1], outline_color_rgb[2] };
				script_params.SetParam("SILHOUETTE_COLOR_RGB", color);
			}
			script_params.SetParam("SILHOUETTE_FADEEFFECT", fadeEffect);
		}
		void HideActor(const int actor_id) {
			hidden_actors.insert(actor_id);
		}
		void DeactiveHiddenActor(const int actor_id) {
			auto it = hidden_actors.find(actor_id);
			if (it != hidden_actors.end())
				hidden_actors.erase(it);
		}
		// this clipper is prior to the actor's clipper
		void SetClipper(const BoxTr* clipBox = NULL, const float* plane = NULL) {
			if (clipBox != NULL) script_params.SetParam("BOX_CLIPPER", *clipBox);
			else script_params.RemoveParam("BOX_CLIPPER");
			if (plane != NULL) {
				std::vector<float> _plane(6);
				script_params.SetParam("PLANE_CLIPPER", std::vector<float>(plane, plane + 6));
			}
			else script_params.RemoveParam("PLANE_CLIPPER");
		}
		void Set2ndLayerDisplayOptions(const int patternInterval = 3, const float blendingW = 0.2f) {
			script_params.SetParam("SECOND_LAYER_PATTERN_INTERVAL", patternInterval);
			script_params.SetParam("SECOND_LAYER_BLENDING_WEIGHT", blendingW);
		}
		// when using negative value, the outline will use the actor's parameter
		void SetSlicerOutlinePixels(const float lineThicknessPix = -1.f) {
			script_params.SetParam("OUTLINE_THICKNESS_PIX", lineThicknessPix);
		}
	};
	struct ActorParameter : TransformParameter
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
		ParamMap<RES_USAGE> associated_obj_ids; // <usage, obj_id> 

	public:
		bool isVisible = true;
		bool isPickable = false;

		int GetResourceID(const RES_USAGE res_usage) {
			return associated_obj_ids.GetParam(res_usage, (int)0);
		}
		void SetResourceID(const RES_USAGE res_usage, const int obj_id) {
			associated_obj_ids.SetParam(res_usage, obj_id);
		}

		TextItem label;
		ParamMap<std::string> scriptParams;
		ParamMap<std::string> testParams; // direct mapping to VmActor

		void ShowOutline(const bool showOutline, const float* colorRGB = NULL, const int thick_pixs = 3, const bool fadeEffect = true) {
			scriptParams.SetParam("SHOW_OUTLINE", showOutline);
			scriptParams.SetParam("SILHOUETTE_THICKNESS", thick_pixs);
			if (colorRGB != NULL) {
				std::vector<float> color = { colorRGB[0], colorRGB[1], colorRGB[2] };
				scriptParams.SetParam("SILHOUETTE_COLOR_RGB", color);
			}
			scriptParams.SetParam("SILHOUETTE_FADEEFFECT", fadeEffect);
		}
		void ShowGroupOutline(const bool showOutline) {
			scriptParams.SetParam("SHOW_GROUPOUTLINE", showOutline);
		}
		void SetClipBox(const vzm::BoxTr* clipBox) {
			if (clipBox) scriptParams.SetParam("CLIPSETTING_BOX", *clipBox);
			else scriptParams.RemoveParam("CLIPSETTING_BOX");
		}
		void SetClipPlane(const float* pos_vec_plane) {
			if (pos_vec_plane) {
				std::vector<float> _pos_vec_plane = { pos_vec_plane[0], pos_vec_plane[1], pos_vec_plane[2], pos_vec_plane[3], pos_vec_plane[4], pos_vec_plane[5] };
				scriptParams.SetParam("CLIPSETTING_PLANE", _pos_vec_plane);
			}
			else scriptParams.RemoveParam("CLIPSETTING_PLANE");
		}
		void SetGeoOS2VolOS(const float* mat) {
			std::vector<float> matValues(16);
			memcpy(&matValues[0], mat, sizeof(float) * 16);
			scriptParams.SetParam("MATRIX_GeoOS2VolOS", matValues);
		}
		void SetSculptIndex(const int sculptIndex) {
			scriptParams.SetParam("SCULPT_INDEX", sculptIndex);
		}
		void DisableSolidFillingOnSlicer(const bool disableSolidFill) {
			scriptParams.SetParam("SLICER_NO_SOLID_FILL", disableSolidFill);
		}
		void SetDistanceMapper(const float vMin, const float vMax, const bool colorClip, const int aidDistTo) {
			scriptParams.SetParam("COLORMAP_VMIN", vMin);
			scriptParams.SetParam("COLORMAP_VMAX", vMax);
			scriptParams.SetParam("COLORMAP_CLIP", colorClip);
			scriptParams.SetParam("COLORMAP_DST_ACTOR", aidDistTo);
			//script_params.SetParam("VOLUMEACTOR_ISOVALUE", isoValue);
		}
		bool GetClipBox(vzm::BoxTr& clipBox) {
			return scriptParams.GetParamCheck("CLIPSETTING_BOX", clipBox);
		}
		bool GetClipPlane(std::vector<float>& pos_vec_plane) {
			return scriptParams.GetParamCheck("CLIPSETTING_PLANE", pos_vec_plane);
		}
		void SetClipFree(const bool clipFree) {
			scriptParams.SetParam("CLIP_FREE", clipFree);
		}
		void SetUndercut(const bool applyUndercut, const float* undercutDir, const float* undercutColorRGB, const bool useUndercutMap = true) {
			scriptParams.SetParam("APPLY_UNDERCUT", applyUndercut);
			scriptParams.SetParam("USE_UNDERCUTMAP", useUndercutMap);
			scriptParams.SetParam("UNDERCUT_DIR", std::vector<float> {undercutDir[0], undercutDir[1], undercutDir[2]});
			scriptParams.SetParam("UNDERCUT_COLOR", std::vector<float> {undercutColorRGB[0], undercutColorRGB[1], undercutColorRGB[2]});
		}
	};
	struct LightParameter : TransformParameter
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

	// This must be called before using engine APIs
	//  - paired with DeinitEngineLib()
	__dojostatic VZRESULT InitEngineLib(const std::string& coreName = "VzmEngine", const std::string& logFileName = "EngineApi.log");

	// Create new scene and return scene ID
	//  - return zero in case of failure (the name is already registered or overflow VID)
	__dojostatic VID NewScene(const std::string& sceneName);
	// Create new actor and return actor ID (global entity)
	//  - Must belong to a scene
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	__dojostatic VID NewActor(const VID sceneId, const std::string& actorName, const ActorParameter& aParams, const VID parentId = 0u);
	// Create new camera and return camera ID (global entity)
	//  - Must belong to a scene
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	__dojostatic VID NewCamera(const VID sceneId, const std::string& camName, const CameraParameter& cParams, const VID parentId = 0u);
	// Create new camera and return camera ID (global entity)
	//  - Must belong to a scene
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	__dojostatic VID NewLight(const VID sceneId, const std::string& lightName, const LightParameter& lParams, const VID parentId = 0u);

	__dojostatic VZRESULT DeinitEngineLib();
}
