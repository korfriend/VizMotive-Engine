#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

#define GET_ANI_COMP(COMP, RET) AnimationComponent* COMP = compfactory::GetAnimationComponent(componentVID_); \
	if (!COMP) {post("AnimationFontComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

using namespace vz;
using namespace std;
using namespace backlog;

/*
> 하나의 데이터를 여러 채널이 공유할 수도 있지

● 맞습니다!여러 채널이 하나의 AnimationDataComponent를 공유할 수 있습니다.

구조를 다시 보면 :
-AnimationSampler의 data 필드(1724줄)는 wi::ecs::Entity로 AnimationDataComponent를 가리킵니다
- 여러 AnimationSampler가 같은 data 엔티티를 참조할 수 있습니다
- 주석(1734줄)에서도 "The data is now not part of the sampler, so it can be shared"라고 명시되어 있습니다

예를 들어 :
-하나의 AnimationDataComponent에 translation 키프레임 데이터가 있다면
- 여러 채널의 sampler들이 이 같은 데이터를 공유해서 서로 다른 타겟 오브젝트들에 같은 애니메이션을 적용할 수
있습니다

이렇게 설계된 이유는 메모리 효율성과 데이터 재사용을 위해서입니다.
/**/
namespace vzm
{
	// VzAnimation member functions
	void VzAnimation::Play()
	{
		GET_ANI_COMP(comp, );
		comp->Play();
	}

	void VzAnimation::Pause()
	{
		GET_ANI_COMP(comp, );
		comp->Pause();
	}

	void VzAnimation::Stop()
	{
		GET_ANI_COMP(comp, );
		comp->Stop();
	}

	void VzAnimation::Reset()
	{
		GET_ANI_COMP(comp, );
		comp->SetTime(0);
	}

	void VzAnimation::SetTime(float time)
	{
		GET_ANI_COMP(comp, );
		comp->SetTime(time);
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

	void VzAnimation::SetSpeed(float speed)
	{
		GET_ANI_COMP(comp, );
		comp->SetSpeed(speed);
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
	}
	void VzAnimation::SetPingPong(const bool value)
	{
		GET_ANI_COMP(comp, );
		comp->SetPingPong(value);
	}
	void VzAnimation::SetPlayOnce()
	{
		GET_ANI_COMP(comp, );
		comp->SetPlayOnce();
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

	uint32_t VzAnimation::AddChannel(const Channel& channel)
	{
		GET_ANI_COMP(comp, true);
		AnimationComponent::Channel ani_channel;
		ani_channel.samplerIndex = channel.samplerIndex;
		ani_channel.retargetIndex = channel.retargetIndex;
		ani_channel.path = (AnimationComponent::Channel::Path)channel.path;
		VzBaseComp* target_comp = vzm::GetComponent(channel.targetVID);
		if (target_comp == nullptr)
		{
			vzlog_error("Invalid Target VID!");
			return comp->GetChannelCount();
		}
		compfactory::GetEntityByVUID()
		return comp->AddChannel();
	}
	uint32_t VzAnimation::AddSampler(const Sampler& sampler);
	void VzAnimation::SetChennel(const int index, const Channel& channel) const;
	void VzAnimation::SetSampler(const int index, const Sampler& sampler) const;
	const VzAnimation::Channel& GetChennel(const int index) const;
	const VzAnimation::Sampler& GetSampler(const int index) const;
	void VzAnimation::RemoveChannel(const int index);
	void VzAnimation::RemoveSampler(const int index);
	void VzAnimation::ClearChannels();
	void VzAnimation::ClearSampler();
	size_t VzAnimation::GetChannelCount() const;
	size_t VzAnimation::GetSamplerCount() const;
}