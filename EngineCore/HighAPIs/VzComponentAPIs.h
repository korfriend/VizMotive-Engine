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
#define VZRESULT int
#define VZ_OK 0
#define VZ_FAIL 1
#define VZ_JOB_WAIT 2
#define VZ_WARNNING 3
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

using VID = uint32_t;
inline constexpr VID INVALID_VID = 0;
using TimeStamp = std::chrono::high_resolution_clock::time_point;

constexpr float VZ_PI = 3.141592654f;
constexpr float VZ_2PI = 6.283185307f;
constexpr float VZ_1DIVPI = 0.318309886f;
constexpr float VZ_1DIV2PI = 0.159154943f;
constexpr float VZ_PIDIV2 = 1.570796327f;
constexpr float VZ_PIDIV4 = 0.785398163f;

using uint = uint32_t;

#include "VzEnums.h"
using namespace vz;

namespace vzm
{
    template <typename ID> struct ParamMap {
    private:
        std::string __PM_VERSION = "LIBI_2.0";
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

	enum class SCENE_COMPONENT_TYPE // every component involves a transform and a name
	{
		// camera, light, actor component can have renderable resources technically
		SCENEBASE = 0,  // empty (only transform and name)

		CAMERA,

		// lights
		LIGHT,

		// actors
		ACTOR,
		SPRITE_ACTOR,
		TEXT_SPRITE_ACTOR,
	};

	enum class RES_COMPONENT_TYPE
	{
		RESOURCE = 0,
		GEOMATRY,
		MATERIAL,
		MATERIALINSTANCE,
		TEXTURE,
		FONT,
	};

	struct API_EXPORT VzBaseComp
	{
	private:
		VID componentVID_ = INVALID_VID;
		TimeStamp timeStamp_ = {}; // will be automatically set 
		std::string originFrom_;
		std::string type_;
	public:
		// User data
		ParamMap<std::string> attributes;
		VzBaseComp(const VID vid, const std::string& originFrom, const std::string& typeName)
			: componentVID_(vid), originFrom_(originFrom), type_(typeName)
		{
			UpdateTimeStamp();
		}
		VID GetVID() const { return componentVID_; }
		std::string GetType() { return type_; };
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
	private:
		SCENE_COMPONENT_TYPE scenecompType_ = SCENE_COMPONENT_TYPE::SCENEBASE;
	public:
		VzSceneComp(const VID vid, const std::string& originFrom, const std::string& typeName, const SCENE_COMPONENT_TYPE scenecompType)
			: VzBaseComp(vid, originFrom, typeName), scenecompType_(scenecompType) {}
		SCENE_COMPONENT_TYPE GetSceneCompType() { return scenecompType_; };

        void GetWorldPosition(float v[3]);
        void GetWorldForward(float v[3]);
        void GetWorldRight(float v[3]);
        void GetWorldUp(float v[3]);
        void GetWorldTransform(float mat[16], const bool rowMajor = false);
        void GetWorldInvTransform(float mat[16], const bool rowMajor = false);

        void GetLocalTransform(float mat[16], const bool rowMajor = false);
        void GetLocalInvTransform(float mat[16], const bool rowMajor = false);

        // local transforms
        void SetTransform(const float s[3] = nullptr, const float q[4] = nullptr, const float t[3] = nullptr, const bool additiveTransform = false);
        void SetMatrix(const float value[16], const bool additiveTransform = false, const bool rowMajor = false);

        VID GetParent();
        std::vector<VID> GetChildren();
        VID GetScene();

        void GetPosition(float position[3]) const;
        void GetRotation(float rotation[3], enums::EulerAngle* euler = nullptr) const;
        void GetQuaternion(float quaternion[4]) const;
        void GetScale(float scale[3]) const;

        void SetPosition(const float position[3]);
        void SetRotation(const float rotation[3], const enums::EulerAngle euler = enums::EulerAngle::XYZ);
        void SetQuaternion(const float quaternion[4]);
        void SetScale(const float scale[3]);

        bool IsMatrixAutoUpdate() const;
        void SetMatrixAutoUpdate(const bool matrixAutoUpdate);

        void UpdateMatrix();
    };
	struct API_EXPORT VzResource : VzBaseComp
	{
	private:
		RES_COMPONENT_TYPE resType_ = RES_COMPONENT_TYPE::RESOURCE;
	public:
		VzResource(const VID vid, const std::string& originFrom, const std::string& typeName, const RES_COMPONENT_TYPE resType)
			: VzBaseComp(vid, originFrom, typeName), resType_(resType) {}
		RES_COMPONENT_TYPE GetResType() { return resType_; }
	};
}

#include "VzActor.h"
#include "VzCamera.h"
#include "VzLight.h"
#include "VzGeometry.h"
#include "VzMaterial.h"
#include "VzMI.h"
#include "VzTexture.h"
#include "VzScene.h"
#include "VzRenderer.h"
#endif