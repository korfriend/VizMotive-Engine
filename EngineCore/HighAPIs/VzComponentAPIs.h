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

using VID = uint32_t;
using SceneVID = VID;
using RendererVID = VID;
using CamVID = VID;
using ActorVID = VID;
using LightVID = VID;
using GeometryVID = VID;
using MaterialVID = VID;
using TextureVID = VID;
using VolumeVID = VID;

inline constexpr VID INVALID_VID = 0;
using TimeStamp = std::chrono::high_resolution_clock::time_point;

constexpr float VZ_PI = 3.141592654f;
constexpr float VZ_2PI = 6.283185307f;
constexpr float VZ_1DIVPI = 0.318309886f;
constexpr float VZ_1DIV2PI = 0.159154943f;
constexpr float VZ_PIDIV2 = 1.570796327f;
constexpr float VZ_PIDIV4 = 0.785398163f;

using uint = uint32_t;

namespace vzm
{
    template <typename ID> struct ParamMap {
    protected:
        std::string __PM_VERSION = "LIBI_2.0";
        std::unordered_map<ID, std::any> __params;
    public:
		std::unordered_map<ID, std::any>& GetMap() { return __params; }
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
		void SetString(const ID& key, const std::string& param) {
			__params[key] = param;
		}
		std::string GetString(const ID& key, const std::string& init_v) const {
			auto it = __params.find(key);
			if (it == __params.end()) return init_v;
            if (it->second.type() != typeid(std::string)) return init_v;
			return std::any_cast<const std::string&>(it->second);;
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

		// render interface component
		SCENE,
		RENDERER,

		// scene components
		CAMERA,
		LIGHT,
		ACTOR,

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
		// User data
		ParamMap<std::string> attributes;
		VzBaseComp(const VID vid, const std::string& originFrom, const COMPONENT_TYPE& type)
			: componentVID_(vid), originFrom_(originFrom), type_(type)
		{
			UpdateTimeStamp();
		}
		VID GetVID() const { return componentVID_; }
		COMPONENT_TYPE GetType() { return type_; };
		TimeStamp GetTimeStamp() { return timeStamp_; };
		void UpdateTimeStamp()
		{
			timeStamp_ = std::chrono::high_resolution_clock::now();
		}
		std::string GetName();
		void SetName(const std::string& name);
	};
    struct API_EXPORT VzSceneComp : VzBaseComp
    {
		VzSceneComp(const VID vid, const std::string& originFrom, const COMPONENT_TYPE& type)
			: VzBaseComp(vid, originFrom, type) {}

		bool IsDirtyTransform();
		bool IsMatrixAutoUpdate();
		void SetMatrixAutoUpdate(const bool enable);

		void GetWorldPosition(float v[3]);
		void GetWorldRotation(float v[4]);
		void GetWorldScale(float v[3]);
        void GetWorldForward(float v[3]);
        void GetWorldRight(float v[3]);
        void GetWorldUp(float v[3]);
        void GetWorldMatrix(float mat[16], const bool rowMajor = false);

        // local
		void GetPosition(float v[3]);
		void GetRotation(float v[4]);
		void GetScale(float v[3]);
        void GetLocalMatrix(float mat[16], const bool rowMajor = false);

		// local transforms
		void SetPosition(const float v[3]);
		void SetScale(const float v[3]);
		void SetEulerAngleZXY(const float v[3]); // ROLL->PITCH->YAW (mainly used CG-convention) 
		void SetEulerAngleZXYInDegree(const float v[3]); // ROLL->PITCH->YAW (mainly used CG-convention) 
		void SetQuaternion(const float v[4]);
        void SetMatrix(const float value[16], const bool rowMajor = false);

		void UpdateMatrix();	// local matrix
		void UpdateWorldMatrix(); // call UpdateMatrix() if necessary

        ActorVID GetParent();
        std::vector<ActorVID> GetChildren();

		// note:
		//	engine-level renderable components can belong to multiple scenes
		//	high-level scene components can belong to a single scene
		SceneVID sceneVid = INVALID_VID;
    };
	struct API_EXPORT VzResource : VzBaseComp
	{
		VzResource(const VID vid, const std::string& originFrom, const COMPONENT_TYPE& type)
			: VzBaseComp(vid, originFrom, type) {}
	};
}

#include "VzActor.h"
#include "VzCamera.h"
#include "VzLight.h"
#include "VzGeometry.h"
#include "VzMaterial.h"
#include "VzTexture.h"
#include "VzScene.h"
#include "VzRenderer.h"
#endif
