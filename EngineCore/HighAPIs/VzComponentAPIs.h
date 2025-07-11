#ifndef VIZAPIS
#define VIZAPIS

#pragma warning (disable : 4251)
#pragma warning (disable : 4819)
//#pragma warning (disable : 4146)
//#pragma warning (disable : 4068)
#pragma warning (disable : 4267)
#pragma warning (disable : 4018)
//#pragma warning (disable : 4244)
//#pragma warning (disable : 4067)

#ifdef _WIN32
#define API_EXPORT __declspec(dllexport)
#else
#define API_EXPORT __attribute__((visibility("default")))
#endif

#define __FP (float*)&
#define __FC2 *(vfloat2*)&
#define __FC3 *(vfloat3*)&
#define __FC4 *(vfloat4*)&
#define __FC44 *(vfloat4x4*)&

#if defined(__clang__)
#define VZ_NONNULL _Nonnull
#define VZ_NULLABLE _Nullable
#else
#define VZ_NONNULL
#define VZ_NULLABLE
#endif
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
#include <functional>

#define VZ_NONNULL
#define VZ_NULLABLE

using VID = uint64_t;
using ArchiveVID = VID;
using SceneVID = VID;
using RendererVID = VID;
using CamVID = VID;
using ActorVID = VID;
using LightVID = VID;
using GeometryVID = VID;
using MaterialVID = VID;
using TextureVID = VID;
using VolumeVID = VID;

constexpr VID INVALID_VID = 0;
using TimeStamp = std::chrono::high_resolution_clock::time_point;

constexpr float VZ_PI = 3.141592654f;
constexpr float VZ_2PI = 6.283185307f;
constexpr float VZ_1DIVPI = 0.318309886f;
constexpr float VZ_1DIV2PI = 0.159154943f;
constexpr float VZ_PIDIV2 = 1.570796327f;
constexpr float VZ_PIDIV4 = 0.785398163f;

using uint = uint32_t;

struct vfloat2 { float x, y; };
struct vfloat3 { float x, y, z; };
struct vfloat4 { float x, y, z, w; };
struct vfloat4x4 { float m[4][4]; };

namespace vzm
{
	template<typename... Args>
	std::string FormatString(const char* format, Args... args) {
		int size_s = snprintf(nullptr, 0, format, args...) + 1; // +1 for exiting NULL string
		if (size_s <= 0) { return ""; } // error case
		std::vector<char> buf(size_s);
		snprintf(buf.data(), size_s, format, args...);
		return std::string(buf.data(), buf.data() + size_s - 1);
	}

    template <typename ID> struct ParamMap {
    protected:
        std::string __PM_VERSION = "LIBI_2.0";
		std::unordered_map<ID, std::any> __params;
	public:
		~ParamMap() {
			__params.clear();
		}
		std::unordered_map<ID, std::any>& GetMap() { return __params; }
        bool FindParam(const ID& param_name) const {
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
		void SetString(const ID& key, const std::string& param) {
			__params[key] = param;
		}
		std::string GetString(const ID& key, const std::string& init_v) const {
			auto it = __params.find(key);
			if (it == __params.end()) return init_v;
            if (it->second.type() != typeid(std::string)) return init_v;
			return std::any_cast<const std::string&>(it->second);
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
        typedef typename MapType::iterator iterator;
        typedef typename MapType::const_iterator const_iterator;
        typedef typename MapType::reference reference;
        iterator begin() { return __params.begin(); }
        const_iterator begin() const { return __params.begin(); }
        iterator end() { return __params.end(); }
        const_iterator end() const { return __params.end(); }
    };

	enum class COMPONENT_TYPE
	{
		UNDEF = 0,

		ARCHIVE,

		// systems for rendering
		SCENE,
		RENDERER,

		// scene objects
		ACTOR_NODE,
		CAMERA,
		SLICER,
		LIGHT,
		ACTOR_STATIC_MESH,
		ACTOR_VOLUME,
		ACTOR_GSPLAT,
		ACTOR_SPRITE,
		ACTOR_SPRITEFONT,

		// resources
		GEOMETRY,
		MATERIAL,
		TEXTURE,
		VOLUME,
	};

	struct API_EXPORT VzBaseComp
	{
	protected:
		VID componentVID_ = INVALID_VID;
		TimeStamp timeStamp_ = {}; // will be automatically set 
		std::string originFrom_;
		COMPONENT_TYPE type_;
	public:
		VzBaseComp(const VID vid, const std::string& originFrom, const COMPONENT_TYPE& type)
			: componentVID_(vid), originFrom_(originFrom), type_(type)
		{
			UpdateTimeStamp();
		}
		virtual ~VzBaseComp() = default;

		// User data
		ParamMap<std::string> attributes;
		VID GetVID() const { return componentVID_; }
		COMPONENT_TYPE GetType() const { return type_; };
		TimeStamp GetTimeStamp() const { return timeStamp_; };
		void UpdateTimeStamp()
		{
			timeStamp_ = std::chrono::high_resolution_clock::now();
		}
		std::string GetName() const;
		void SetName(const std::string& name);
	};
    struct API_EXPORT VzSceneObject : VzBaseComp
    {
		VzSceneObject(const VID vid, const std::string& originFrom, const COMPONENT_TYPE& type)
			: VzBaseComp(vid, originFrom, type) {}
		virtual ~VzSceneObject() = default;

		bool IsDirtyTransform() const;
		bool IsMatrixAutoUpdate() const;
		void SetMatrixAutoUpdate(const bool enable);

		void GetWorldPosition(vfloat3& v)const;
		void GetWorldRotation(vfloat4& v)const;
		void GetWorldScale(vfloat3& v)const;
        void GetWorldForward(vfloat3& v)const;
        void GetWorldRight(vfloat3& v)const;
        void GetWorldUp(vfloat3& v)const;
        void GetWorldMatrix(vfloat4x4& mat, const bool rowMajor = false) const;

        // local
		void GetPosition(vfloat3& v) const;
		void GetRotation(vfloat4& v) const;
		void GetScale(vfloat3& v) const;
        void GetLocalMatrix(vfloat4x4& mat, const bool rowMajor = false) const;

		// local transforms
		void SetPosition(const vfloat3& v);
		void SetScale(const vfloat3& v);
		void SetEulerAngleZXY(const vfloat3& v); // ROLL->PITCH->YAW (mainly used CG-convention) 
		void SetEulerAngleZXYInDegree(const vfloat3& v); // ROLL->PITCH->YAW (mainly used CG-convention) 
		void SetQuaternion(const vfloat4& v);
		void SetRotateAxis(const vfloat3& v, const float angle); // angle in degree
		void SetRotateToLookUp(const vfloat3& view, const vfloat3& up); // view refers to (-)z-axis, up to y-axis in the local space based on LookTo interface
        void SetMatrix(const vfloat4x4& mat, const bool rowMajor = false);

		void UpdateMatrix();	// local matrix
		void UpdateWorldMatrix(); // call UpdateMatrix() if necessary

        ActorVID GetParent() const;
        std::vector<ActorVID> GetChildren() const;

		void AppendChild(const VzBaseComp* child);
		void DetachChild(const VzBaseComp* child);
		void AttachToParent(const VzBaseComp* parent);

		// Visible Layer Settings
		void SetVisibleLayerMask(const uint32_t visibleLayerMask, const bool includeDescendants = false);
		uint32_t GetVisibleLayerMask() const;
		bool IsVisibleWith(const uint32_t layerBits) const;
		void SetUserLayerMask(const uint32_t userLayerMask, const bool includeDescendants = false);
		uint32_t GetUserLayerMask() const;

		// note:
		//	engine-level renderable components can belong to multiple scenes
		//	high-level scene components can belong to a single scene
		SceneVID sceneVid = INVALID_VID;
    };
	struct API_EXPORT VzResource : VzBaseComp
	{
		VzResource(const VID vid, const std::string& originFrom, const COMPONENT_TYPE& type)
			: VzBaseComp(vid, originFrom, type) {}
		virtual ~VzResource() = default;

		void SetVisibleLayerMask(const uint32_t visibleLayerMask);
	};
}

namespace vzm
{
	struct ClearOption
	{
		bool enabled = false;
		uint32_t clearColor = 0u;
	};
	struct Viewport
	{
		float topLeftX = 0;
		float topLeftY = 0;
		float width = 0;
		float height = 0;
		float min_depth = 0;
		float max_depth = 1;
	};
	struct Scissor
	{
		int32_t left = 0;
		int32_t top = 0;
		int32_t right = 0;
		int32_t bottom = 0;

		constexpr void from_viewport(const Viewport& vp)
		{
			left = int32_t(vp.topLeftX);
			right = int32_t(vp.topLeftX + vp.width);
			top = int32_t(vp.topLeftY);
			bottom = int32_t(vp.topLeftY + vp.height);
		}
	};
	struct API_EXPORT ChainUnitRCam
	{
	private:
		bool isValid_ = false;
	public:
		ClearOption co = {};
		Scissor sc = {};
		Viewport vp = {};

		RendererVID rendererVid = INVALID_VID;
		CamVID camVid = INVALID_VID;

		ChainUnitRCam(const RendererVID rendererVid, const CamVID camVid);
		ChainUnitRCam(const VzBaseComp* renderer, const VzBaseComp* camera) : ChainUnitRCam(renderer->GetVID(), camera->GetVID()) {}

		bool IsValid() const { return isValid_; }
	};
	struct API_EXPORT ChainUnitSCam
	{
	private:
		bool isValid_ = false;
	public:
		ClearOption co = {};
		Scissor sc = {};
		Viewport vp = {};

		SceneVID sceneVid = INVALID_VID;
		CamVID camVid = INVALID_VID;

		ChainUnitSCam(const SceneVID sceneVid, const CamVID camVid);
		ChainUnitSCam(const VzBaseComp* scene, const VzBaseComp* camera) : ChainUnitSCam(scene->GetVID(), camera->GetVID()) {}

		bool IsValid() const { return isValid_; }
	};
}

namespace vzm
{
	enum class LookupTableSlot : uint32_t
	{
		LOOKUP_COLOR,
		LOOKUP_OTF,
		LOOKUP_WINDOWING,

		LOOKUPTABLE_COUNT
	};
}

#include "VzArchive.h"
#include "VzActor.h"
#include "VzCamera.h"
#include "VzLight.h"
#include "VzGeometry.h"
#include "VzMaterial.h"
#include "VzTexture.h"
#include "VzScene.h"
#include "VzRenderer.h"
#endif
