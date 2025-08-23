#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzKeyFrameData : VzBaseComp
	{
		VzKeyFrameData(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, COMPONENT_TYPE::KEYFRAMEDATA) {
		}
		virtual ~VzKeyFrameData() = default;

		void SetKeyFrameTimes(std::vector<float>& times);
		void SetKeyFrameData(std::vector<float>& data);
		std::vector<float> GetKeyFrameTimes() const;
		std::vector<float> GetKeyFrameData() const;
		float GetDuration() const;
	};

	struct API_EXPORT VzAnimation : VzBaseComp
	{
		VzAnimation(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, COMPONENT_TYPE::ANIMATION) {
		}
		virtual ~VzAnimation() = default;

		struct Channel
		{
			enum class Path : uint32_t
			{
				TRANSLATION,
				ROTATION,
				SCALE,
				WEIGHTS,

				LIGHT_COLOR,
				LIGHT_INTENSITY,
				LIGHT_RANGE,
				LIGHT_INNERCONE,
				LIGHT_OUTERCONE,
				// additional light paths can go here...
				_LIGHT_RANGE_END = LIGHT_COLOR + 1000,

				EMITTER_EMITCOUNT,
				// additional emitter paths can go here...
				_EMITTER_RANGE_END = EMITTER_EMITCOUNT + 1000,

				CAMERA_FOV,
				CAMERA_FOCAL_LENGTH,
				CAMERA_APERTURE_SIZE,
				CAMERA_APERTURE_SHAPE,
				// additional camera paths can go here...
				_CAMERA_RANGE_END = CAMERA_FOV + 1000,

				// future work: SCRIPT-binding...
				SCRIPT_PLAY,
				SCRIPT_STOP,
				// additional script paths can go here...
				_SCRIPT_RANGE_END = SCRIPT_PLAY + 1000,

				MATERIAL_COLOR,
				MATERIAL_EMISSIVE,
				MATERIAL_ROUGHNESS,
				MATERIAL_METALNESS,
				MATERIAL_REFLECTANCE,
				MATERIAL_TEXMULADD,
				// additional material paths can go here...
				_MATERIAL_RANGE_END = MATERIAL_COLOR + 1000,

				UNKNOWN,
			} path;

			int samplerIndex = -1;
			int retargetIndex = -1;

			VID targetVID = INVALID_VID;
		};
		struct Sampler
		{
			enum class Interpolation : uint32_t
			{
				LINEAR = 0,
				STEP,
				CUBIC_SPLINE,
				MODE_FORCE_UINT32 = 0xFFFFFFFF
			} mode;

			KeyFrameVID keyframeVID;
		};

		void Play();
		void Pause();
		void Stop();
		void Reset();
		void SetTime(float time);
		float GetTime() const;
		float GetDuration() const;

		void SetSpeed(float speed);
		float GetSpeed() const;

		void SetLooped(const bool value = true);
		void SetPingPong(const bool value = true);
		void SetPlayOnce();

		bool IsPlaying() const;
		bool IsLooped() const;	
		bool IsPingPong() const;
		bool IsPlayingOnce() const; 
		bool IsFinished() const;

		uint32_t AddChannel(const Channel& channel);
		uint32_t AddSampler(const Sampler& sampler);
		void SetChennel(const int index, const Channel& channel) const;
		void SetSampler(const int index, const Sampler& sampler) const;
		const Channel& GetChennel(const int index) const;
		const Sampler& GetSampler(const int index) const;
		void RemoveChannel(const int index);
		void RemoveSampler(const int index);
		void ClearChannels();
		void ClearSampler();
		size_t GetChannelCount() const;
		size_t GetSamplerCount() const;
	};

	using AnimationChannel = VzAnimation::Channel;
	using AnimationSampler = VzAnimation::Sampler;
}
