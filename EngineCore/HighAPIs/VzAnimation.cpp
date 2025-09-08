#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

#define GET_ANI_COMP(COMP, RET) AnimationComponent* COMP = compfactory::GetAnimationComponent(componentVID_); \
	if (!COMP) {post("AnimationFontComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

#define GET_KEYFRAME_COMP(COMP, RET) AnimationDataComponent* COMP = compfactory::GetAnimationDataComponent(componentVID_); \
	if (!COMP) {post("AnimationFontComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
	void VzKeyFrameData::SetKeyFrameTimes(std::vector<float>& times)
	{
		GET_KEYFRAME_COMP(comp, );
		comp->SetKeyFrameTimes(times);
	}
	void VzKeyFrameData::SetKeyFrameData(std::vector<float>& data)
	{
		GET_KEYFRAME_COMP(comp, );
		comp->SetKeyFrameData(data);
	}
	std::vector<float> VzKeyFrameData::GetKeyFrameTimes() const
	{
		GET_KEYFRAME_COMP(comp, std::vector<float>());
		return comp->GetKeyFrameTimes();
	}
	std::vector<float> VzKeyFrameData::GetKeyFrameData() const
	{
		GET_KEYFRAME_COMP(comp, std::vector<float>());
		return comp->GetKeyFrameData();
	}
	float VzKeyFrameData::GetDuration() const
	{
		GET_KEYFRAME_COMP(comp, 0);
		return comp->GetDuration();
	}
}

namespace vzm
{
	// VzAnimation member functions
	void VzAnimation::Play()
	{
		GET_ANI_COMP(comp, );
		comp->Play();
		UpdateTimeStamp();
	}

	void VzAnimation::Pause()
	{
		GET_ANI_COMP(comp, );
		comp->Pause();
		UpdateTimeStamp();
	}

	void VzAnimation::Stop()
	{
		GET_ANI_COMP(comp, );
		comp->Stop();
		UpdateTimeStamp();
	}

	void VzAnimation::Reset()
	{
		GET_ANI_COMP(comp, );
		comp->SetTime(0);
		UpdateTimeStamp();
	}

	void VzAnimation::SetTime(const float time)
	{
		GET_ANI_COMP(comp, );
		comp->SetTime(time);
		UpdateTimeStamp();
	}

	void VzAnimation::SetStartTime(const float time)
	{
		GET_ANI_COMP(comp, );
		comp->SetStart(time);
		UpdateTimeStamp();
	}
	void VzAnimation::SetEndTime(const float time)
	{
		GET_ANI_COMP(comp, );
		comp->SetEnd(time);
		UpdateTimeStamp();
	}

	float VzAnimation::GetTime() const
	{
		GET_ANI_COMP(comp, 0.0f);
		return comp->GetTime();
	}

	float VzAnimation::GetDuration() const
	{
		GET_ANI_COMP(comp, 0.0f);
		return comp->GetDuration();
	}

	float VzAnimation::GetStartTime() const
	{
		GET_ANI_COMP(comp, 0.0f);
		return comp->GetStart();
	}
	float VzAnimation::GetEndTime() const
	{
		GET_ANI_COMP(comp, 0.0f);
		return comp->GetEnd();
	}

	void VzAnimation::SetSpeed(float speed)
	{
		GET_ANI_COMP(comp, );
		comp->SetSpeed(speed);
		UpdateTimeStamp();
	}

	float VzAnimation::GetSpeed() const
	{
		GET_ANI_COMP(comp, 1.0f);
		return comp->GetSpeed();
	}

	void VzAnimation::SetLooped(const bool value)
	{
		GET_ANI_COMP(comp, );
		comp->SetLooped(value);
		UpdateTimeStamp();
	}
	void VzAnimation::SetPingPong(const bool value)
	{
		GET_ANI_COMP(comp, );
		comp->SetPingPong(value);
		UpdateTimeStamp();
	}
	void VzAnimation::SetPlayOnce()
	{
		GET_ANI_COMP(comp, );
		comp->SetPlayOnce();
		UpdateTimeStamp();
	}
	bool VzAnimation::IsLooped() const
	{
		GET_ANI_COMP(comp, false);
		return comp->IsLooped();
	}
	bool VzAnimation::IsPingPong() const
	{
		GET_ANI_COMP(comp, false);
		return comp->IsPingPong();
	}
	bool VzAnimation::IsPlayingOnce() const
	{
		GET_ANI_COMP(comp, false);
		return comp->IsPlayingOnce();
	}
	bool VzAnimation::IsPlaying() const
	{
		GET_ANI_COMP(comp, false);
		return comp->IsPlaying();
	}
	bool VzAnimation::IsFinished() const
	{
		GET_ANI_COMP(comp, true);
		return comp->IsEnded();
	}

	auto convertChannelAPI = [](AnimationComponent* comp, const VzAnimation::Channel& channel) -> AnimationComponent::Channel {
		AnimationComponent::Channel ani_channel;
		ani_channel.samplerIndex = channel.samplerIndex;
		ani_channel.retargetIndex = channel.retargetIndex;
		ani_channel.path = (AnimationComponent::Channel::Path)channel.path;
		NameComponent* name = compfactory::GetNameComponent(channel.targetVID);
		if (name == nullptr)
		{
			vzlog_error("Invalid Target VID!");
			return AnimationComponent::Channel();
		}
		ani_channel.targetNameVUID = name->GetVUID();
		return ani_channel;
		};

	auto convertSamplerAPI = [](AnimationComponent* comp, const VzAnimation::Sampler& sampler) -> AnimationComponent::Sampler {
		AnimationComponent::Sampler ani_sampler;
		ani_sampler.mode = (AnimationComponent::Sampler::Mode)sampler.mode;
		AnimationDataComponent* ani_data = compfactory::GetAnimationDataComponent(sampler.keyframeVID);
		if (ani_data == nullptr)
		{
			vzlog_error("Invalid Target VID!");
			return AnimationComponent::Sampler();
		}
		ani_sampler.dataVUID = ani_data->GetVUID();
		return ani_sampler;
		};

	uint32_t VzAnimation::AddChannel(const Channel& channel)
	{
		GET_ANI_COMP(comp, 0);
		AnimationComponent::Channel ani_channel = convertChannelAPI(comp, channel);
		UpdateTimeStamp();
		return comp->AddChannel(ani_channel);
	}
	uint32_t VzAnimation::AddSampler(const Sampler& sampler)
	{
		GET_ANI_COMP(comp, 0);
		AnimationComponent::Sampler ani_sampler = convertSamplerAPI(comp, sampler);
		UpdateTimeStamp();
		return comp->AddSampler(ani_sampler);
	}
	void VzAnimation::SetChennel(const int index, const Channel& channel)
	{
		GET_ANI_COMP(comp, );
		AnimationComponent::Channel ani_channel = convertChannelAPI(comp, channel);
		vzlog_assert(comp->SetChannel(index, ani_channel), "Failure! VzAnimation::SetChennel");
		UpdateTimeStamp();
	}
	void VzAnimation::SetSampler(const int index, const Sampler& sampler)
	{
		GET_ANI_COMP(comp, );
		AnimationComponent::Sampler ani_sampler = convertSamplerAPI(comp, sampler);
		vzlog_assert(comp->SetSampler(index, ani_sampler), "Failure! VzAnimation::SetSampler");
		UpdateTimeStamp();
	}
	VzAnimation::Channel VzAnimation::GetChennel(const int index) const
	{
		GET_ANI_COMP(comp, Channel());
		AnimationComponent::Channel ani_channel;
		vzlog_assert(comp->GetChannel(index, ani_channel), "Failure! VzAnimation::GetChennel");
		Channel channel;
		channel.retargetIndex = ani_channel.retargetIndex;
		channel.samplerIndex = ani_channel.samplerIndex;
		channel.path = (Channel::Path)ani_channel.path;
		channel.targetVID = compfactory::GetEntityByVUID(ani_channel.targetNameVUID);
		return channel;
	}
	VzAnimation::Sampler VzAnimation::GetSampler(const int index) const
	{
		GET_ANI_COMP(comp, VzAnimation::Sampler());
		AnimationComponent::Sampler ani_sampler;
		vzlog_assert(comp->GetSampler(index, ani_sampler), "Failure! VzAnimation::GetSampler");
		VzAnimation::Sampler sampler;
		sampler.mode = (VzAnimation::Sampler::Interpolation)ani_sampler.mode;
		sampler.keyframeVID = compfactory::GetEntityByVUID(ani_sampler.dataVUID);
		return sampler;
	}
	void VzAnimation::RemoveChannel(const int index)
	{
		GET_ANI_COMP(comp, );
		vzlog_assert(comp->RemoveChannel(index), "Failure! VzAnimation::RemoveChannel");
		UpdateTimeStamp();
	}
	void VzAnimation::RemoveSampler(const int index)
	{
		GET_ANI_COMP(comp, );
		vzlog_assert(comp->RemoveSampler(index), "Failure! VzAnimation::RemoveSampler");
		UpdateTimeStamp();
	}
	void VzAnimation::ClearChannels()
	{
		GET_ANI_COMP(comp, );
		comp->ClearChannels();
		UpdateTimeStamp();
	}
	void VzAnimation::ClearSamplers()
	{
		GET_ANI_COMP(comp, );
		comp->ClearSamplers();
		UpdateTimeStamp();
	}
	size_t VzAnimation::GetChannelCount() const
	{
		GET_ANI_COMP(comp, 0);
		return comp->GetChannelCount();
	}
	size_t VzAnimation::GetSamplerCount() const
	{
		GET_ANI_COMP(comp, 0);
		return comp->GetSamplerCount();
	}
}