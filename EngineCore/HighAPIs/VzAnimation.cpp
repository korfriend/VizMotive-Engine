#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

#define GET_ANI_COMP(COMP, RET) AnimationComponent* COMP = compfactory::GetAnimationComponent(componentVID_); \
	if (!COMP) {post("AnimationFontComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

using namespace vz;
using namespace std;
using namespace backlog;

/*
> �ϳ��� �����͸� ���� ä���� ������ ���� ����

�� �½��ϴ�!���� ä���� �ϳ��� AnimationDataComponent�� ������ �� �ֽ��ϴ�.

������ �ٽ� ���� :
-AnimationSampler�� data �ʵ�(1724��)�� wi::ecs::Entity�� AnimationDataComponent�� ����ŵ�ϴ�
- ���� AnimationSampler�� ���� data ��ƼƼ�� ������ �� �ֽ��ϴ�
- �ּ�(1734��)������ "The data is now not part of the sampler, so it can be shared"��� ��õǾ� �ֽ��ϴ�

���� ��� :
-�ϳ��� AnimationDataComponent�� translation Ű������ �����Ͱ� �ִٸ�
- ���� ä���� sampler���� �� ���� �����͸� �����ؼ� ���� �ٸ� Ÿ�� ������Ʈ�鿡 ���� �ִϸ��̼��� ������ ��
�ֽ��ϴ�

�̷��� ����� ������ �޸� ȿ������ ������ ������ ���ؼ��Դϴ�.
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