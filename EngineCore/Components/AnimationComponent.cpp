#include "GComponents.h"
#include "Utils/Backlog.h"

namespace vz
{
	AnimationComponent::Channel::PathDataType AnimationComponent::Channel::GetPathDataType() const
	{
		switch (path)
		{
		case AnimationComponent::Channel::Path::TRANSLATION:
			return PathDataType::Float3;
		case AnimationComponent::Channel::Path::ROTATION:
			return PathDataType::Float4;
		case AnimationComponent::Channel::Path::SCALE:
			return PathDataType::Float3;
		case AnimationComponent::Channel::Path::WEIGHTS:
			return PathDataType::Weights;

		case AnimationComponent::Channel::Path::LIGHT_COLOR:
			return PathDataType::Float3;
		case AnimationComponent::Channel::Path::LIGHT_INTENSITY:
		case AnimationComponent::Channel::Path::LIGHT_RANGE:
		case AnimationComponent::Channel::Path::LIGHT_INNERCONE:
		case AnimationComponent::Channel::Path::LIGHT_OUTERCONE:
			return PathDataType::Float;

		//case AnimationComponent::Channel::Path::SOUND_PLAY:
		//case AnimationComponent::Channel::Path::SOUND_STOP:
		//	return PathDataType::Event;
		//case AnimationComponent::Channel::Path::SOUND_VOLUME:
		//	return PathDataType::Float;

		case AnimationComponent::Channel::Path::EMITTER_EMITCOUNT:
			return PathDataType::Float;

		case AnimationComponent::Channel::Path::CAMERA_FOV:
		case AnimationComponent::Channel::Path::CAMERA_FOCAL_LENGTH:
		case AnimationComponent::Channel::Path::CAMERA_APERTURE_SIZE:
			return PathDataType::Float;
		case AnimationComponent::Channel::Path::CAMERA_APERTURE_SHAPE:
			return PathDataType::Float2;

		case AnimationComponent::Channel::Path::SCRIPT_PLAY:
		case AnimationComponent::Channel::Path::SCRIPT_STOP:
			return PathDataType::Event;

		case AnimationComponent::Channel::Path::MATERIAL_COLOR:
		case AnimationComponent::Channel::Path::MATERIAL_EMISSIVE:
		case AnimationComponent::Channel::Path::MATERIAL_TEXMULADD:
			return PathDataType::Float4;
		case AnimationComponent::Channel::Path::MATERIAL_ROUGHNESS:
		case AnimationComponent::Channel::Path::MATERIAL_REFLECTANCE:
		case AnimationComponent::Channel::Path::MATERIAL_METALNESS:
			return PathDataType::Float;

		default:
			assert(0);
			break;
		}
		return PathDataType::Event;
	}

	float AnimationComponent::GetDuration() const
	{
		float duration = -1.f;
		for (auto& sampler : samplers_)
		{
			AnimationDataComponent* ani_data_comp = compfactory::GetAnimationDataComponentByVUID(sampler.dataVUID);
			vzlog_assert(ani_data_comp, "sampler.dataVUID is INVALID!");
			duration = std::max(duration, ani_data_comp->GetDuration());
		}
		return duration;
	}

	void AnimationComponent::Update(const float dt)
	{
		if (!IsPlaying() || dt == 0)
			return;

		lastUpdateTime_ = playTimer_;

#define INVALID_RETURN { vzlog_error("invalid animation data!"); return; }

		for (const Channel& channel : channels_)
		{
			assert(channel.samplerIndex < (int)samplers_.size());
			const Sampler& sampler = samplers_[channel.samplerIndex];

			const AnimationDataComponent* animationdata = compfactory::GetAnimationDataComponentByVUID(sampler.dataVUID);
			if (animationdata == nullptr)
				INVALID_RETURN;

			const std::vector<float>& keyframe_times = animationdata->GetKeyFrameTimes();
			
			if (keyframe_times.empty())
				return;

			const Channel::PathDataType path_data_type = channel.GetPathDataType();

			float timeFirst = std::numeric_limits<float>::max();
			float timeLast = std::numeric_limits<float>::min();
			int keyLeft = 0;	float timeLeft = std::numeric_limits<float>::min();
			int keyRight = 0;	float timeRight = std::numeric_limits<float>::max();

			// search for usable keyframes:
			for (int k = 0; k < (int)keyframe_times.size(); ++k)
			{
				const float time = keyframe_times[k];
				if (time < timeFirst)
				{
					timeFirst = time;
				}
				if (time > timeLast)
				{
					timeLast = time;
				}
				if (time <= playTimer_ && time > timeLeft)
				{
					timeLeft = time;
					keyLeft = k;
				}
				if (time >= playTimer_ && time < timeRight)
				{
					timeRight = time;
					keyRight = k;
				}
			}
			if (path_data_type != Channel::PathDataType::Event)
			{
				if (playTimer_ < timeFirst)
				{
					// animation beginning haven't been reached, force first keyframe:
					timeLeft = timeFirst;
					timeRight = timeFirst;
					keyLeft = 0;
					keyRight = 0;
				}
			}
			else
			{
				timeLeft = std::max(timeLeft, timeFirst);
				timeRight = std::max(timeRight, timeLast);
			}

			const float left = keyframe_times[keyLeft];
			const float right = keyframe_times[keyRight];

			union Interpolator
			{
				XMFLOAT4 f4;
				XMFLOAT3 f3;
				XMFLOAT2 f2;
				float f;
			} interpolator = {};

			TransformComponent* target_transform = nullptr;
			GeometryComponent* target_geo = nullptr;
			LightComponent* target_light = nullptr;
			//SoundComponent* target_sound = nullptr;
			//EmittedParticleSystem* target_emitter = nullptr;
			CameraComponent* target_camera = nullptr;
			//ScriptComponent* target_script = nullptr;
			MaterialComponent* target_material = nullptr;

			NameComponent* target_name = compfactory::GetNameComponentByVUID(channel.targetNameVUID);
			Entity target_entity = target_name->GetEntity();

			if (
				channel.path == Channel::Path::TRANSLATION ||
				channel.path == Channel::Path::ROTATION ||
				channel.path == Channel::Path::SCALE
				)
			{
				target_transform = compfactory::GetTransformComponent(target_entity);
				if (target_transform == nullptr)
					INVALID_RETURN;
				switch (channel.path)
				{
				case Channel::Path::TRANSLATION:
					interpolator.f3 = target_transform->GetPosition(); // translation_local;
					break;
				case Channel::Path::ROTATION:
					interpolator.f4 = target_transform->GetRotation(); // rotation_local;
					break;
				case Channel::Path::SCALE:
					interpolator.f3 = target_transform->GetScale(); // scale_local;
					break;
				default:
					break;
				}
			}
			else if (channel.path == Channel::Path::WEIGHTS)
			{
				target_geo = compfactory::GetGeometryComponent(target_entity);
				if (target_geo == nullptr)
				{
					// Also try going through object's mesh reference:
					RenderableComponent* renderable = compfactory::GetRenderableComponent(target_entity);
					if (renderable == nullptr)
						INVALID_RETURN;
					target_geo = compfactory::GetGeometryComponent(renderable->GetGeometry());
				}
				if (target_geo == nullptr)
					INVALID_RETURN;

				size_t morph_weights = 0;

				for (auto& part : target_geo->GetPrimitives())
				{
					morph_weights += part.GetMorphTargets().size();
				}
				morphWeightsTemp_.resize(morph_weights);
			}
			else if (
				channel.path >= Channel::Path::LIGHT_COLOR &&
				channel.path < Channel::Path::_LIGHT_RANGE_END
				)
			{
				target_light = compfactory::GetLightComponent(target_entity);
				if (target_light == nullptr)
					INVALID_RETURN;
				switch (channel.path)
				{
				case Channel::Path::LIGHT_COLOR:
					interpolator.f3 = target_light->GetColor();
					break;
				case Channel::Path::LIGHT_INTENSITY:
					interpolator.f = target_light->GetIntensity();
					break;
				case Channel::Path::LIGHT_RANGE:
					interpolator.f = target_light->GetRange();
					break;
				case Channel::Path::LIGHT_INNERCONE:
					interpolator.f = target_light->GetInnerConeAngle();
					break;
				case Channel::Path::LIGHT_OUTERCONE:
					interpolator.f = target_light->GetOuterConeAngle();
					break;
				default:
					break;
				}
			}
			//else if (
			//	channel.path >= Channel::Path::SOUND_PLAY &&
			//	channel.path < Channel::Path::_SOUND_RANGE_END
			//	)
			//{
			//	target_sound = sounds.GetComponent(channel.target);
			//	if (target_sound == nullptr)
			//		INVALID_RETURN;
			//	switch (channel.path)
			//	{
			//	case Channel::Path::SOUND_VOLUME:
			//		interpolator.f = target_sound->volume;
			//		break;
			//	default:
			//		break;
			//	}
			//}
			//else if (
			//	channel.path >= Channel::Path::EMITTER_EMITCOUNT &&
			//	channel.path < Channel::Path::_EMITTER_RANGE_END
			//	)
			//{
			//	target_emitter = emitters.GetComponent(channel.target);
			//	if (target_emitter == nullptr)
			//		INVALID_RETURN;
			//	switch (channel.path)
			//	{
			//	case Channel::Path::EMITTER_EMITCOUNT:
			//		interpolator.f = target_emitter->count;
			//		break;
			//	default:
			//		break;
			//	}
			//}
			else if (
				channel.path >= Channel::Path::CAMERA_FOV &&
				channel.path < Channel::Path::_CAMERA_RANGE_END
				)
			{
				target_camera = compfactory::GetCameraComponent(target_entity);
				if (target_camera == nullptr)
					INVALID_RETURN;
				switch (channel.path)
				{
				case Channel::Path::CAMERA_FOV:
					interpolator.f = target_camera->GetFovVertical();
					break;
				case Channel::Path::CAMERA_FOCAL_LENGTH:
					interpolator.f = target_camera->GetFocalLength();
					break;
				case Channel::Path::CAMERA_APERTURE_SIZE:
					interpolator.f = target_camera->GetApertureSize();
					break;
				case Channel::Path::CAMERA_APERTURE_SHAPE:
					interpolator.f2 = target_camera->GetApertureShape();
					break;
				default:
					break;
				}
			}
			//else if (
			//	channel.path >= Channel::Path::SCRIPT_PLAY &&
			//	channel.path < Channel::Path::_SCRIPT_RANGE_END
			//	)
			//{
			//	target_script = scripts.GetComponent(channel.target);
			//	if (target_script == nullptr)
			//		continue;
			//}
			else if (
				channel.path >= Channel::Path::MATERIAL_COLOR &&
				channel.path < Channel::Path::_MATERIAL_RANGE_END
				)
			{
				target_material = compfactory::GetMaterialComponent(target_entity);
				if (target_material == nullptr)
					INVALID_RETURN;
				switch (channel.path)
				{
				case Channel::Path::MATERIAL_COLOR:
					interpolator.f4 = target_material->GetBaseColor();
					break;
				case Channel::Path::MATERIAL_EMISSIVE:
					interpolator.f4 = target_material->GetEmissiveColor();
					break;
				case Channel::Path::MATERIAL_ROUGHNESS:
					interpolator.f = target_material->GetRoughness();
					break;
				case Channel::Path::MATERIAL_METALNESS:
					interpolator.f = target_material->GetMetalness();
					break;
				case Channel::Path::MATERIAL_REFLECTANCE:
					interpolator.f = target_material->GetReflectance();
					break;
				case Channel::Path::MATERIAL_TEXMULADD:
					interpolator.f4 = target_material->GetTexMulAdd();
					break;
				default:
					break;
				}
			}
			else
			{
				assert(0);
				continue;
			}

			if (path_data_type == Channel::PathDataType::Event)
			{
				// No path data, only event trigger:
				if (keyLeft == channel.next_event && playTimer_ >= timeLeft)
				{
					channel.next_event++;
					//switch (channel.path)
					//{
					//case Channel::Path::SOUND_PLAY:
					//	target_sound->Play();
					//	break;
					//case Channel::Path::SOUND_STOP:
					//	target_sound->Stop();
					//	break;
					//case Channel::Path::SCRIPT_PLAY:
					//	target_script->Play();
					//	break;
					//case Channel::Path::SCRIPT_STOP:
					//	target_script->Stop();
					//	break;
					//default:
					//	break;
					//}
				}
			}
			else
			{
				const std::vector<float>& keyframe_data = animationdata->GetKeyFrameData();
				// Path data interpolation:
				switch (sampler.mode)
				{
				default:
				case Sampler::Mode::STEP:
				{
					// Nearest neighbor method:
					const int key = math::InverseLerp(timeLeft, timeRight, playTimer_) > 0.5f ? keyRight : keyLeft;
					switch (path_data_type)
					{
					default:
					case Channel::PathDataType::Float:
					{
						assert(keyframe_data.size() == keyframe_times.size());
						interpolator.f = keyframe_data[key];
					}
					break;
					case Channel::PathDataType::Float2:
					{
						assert(keyframe_data.size() == keyframe_times.size() * 2);
						interpolator.f2 = ((const XMFLOAT2*)keyframe_data.data())[key];
					}
					break;
					case Channel::PathDataType::Float3:
					{
						assert(keyframe_data.size() == keyframe_times.size() * 3);
						interpolator.f3 = ((const XMFLOAT3*)keyframe_data.data())[key];
					}
					break;
					case Channel::PathDataType::Float4:
					{
						assert(keyframe_data.size() == keyframe_times.size() * 4);
						interpolator.f4 = ((const XMFLOAT4*)keyframe_data.data())[key];
					}
					break;
					case Channel::PathDataType::Weights:
					{
						assert(keyframe_data.size() == keyframe_times.size() * morphWeightsTemp_.size());
						for (size_t j = 0; j < morphWeightsTemp_.size(); ++j)
						{
							morphWeightsTemp_[j] = keyframe_data[key * morphWeightsTemp_.size() + j];
						}
					}
					break;
					}
				}
				break;
				case Sampler::Mode::LINEAR:
				{
					// Linear interpolation method:
					float t;
					if (keyLeft == keyRight)
					{
						t = 0;
					}
					else
					{
						t = (playTimer_ - left) / (right - left);
					}
					t = saturate(t);

					switch (path_data_type)
					{
					default:
					case Channel::PathDataType::Float:
					{
						assert(keyframe_data.size() == keyframe_times.size());
						float vLeft = keyframe_data[keyLeft];
						float vRight = keyframe_data[keyRight];
						float vAnim = math::Lerp(vLeft, vRight, t);
						interpolator.f = vAnim;
					}
					break;
					case Channel::PathDataType::Float2:
					{
						assert(keyframe_data.size() == keyframe_times.size() * 2);
						const XMFLOAT2* data = (const XMFLOAT2*)keyframe_data.data();
						XMVECTOR vLeft = XMLoadFloat2(&data[keyLeft]);
						XMVECTOR vRight = XMLoadFloat2(&data[keyRight]);
						XMVECTOR vAnim = XMVectorLerp(vLeft, vRight, t);
						XMStoreFloat2(&interpolator.f2, vAnim);
					}
					break;
					case Channel::PathDataType::Float3:
					{
						assert(keyframe_data.size() == keyframe_times.size() * 3);
						const XMFLOAT3* data = (const XMFLOAT3*)keyframe_data.data();
						XMVECTOR vLeft = XMLoadFloat3(&data[keyLeft]);
						XMVECTOR vRight = XMLoadFloat3(&data[keyRight]);
						XMVECTOR vAnim = XMVectorLerp(vLeft, vRight, t);
						XMStoreFloat3(&interpolator.f3, vAnim);
					}
					break;
					case Channel::PathDataType::Float4:
					{
						assert(keyframe_data.size() == keyframe_times.size() * 4);
						const XMFLOAT4* data = (const XMFLOAT4*)keyframe_data.data();
						XMVECTOR vLeft = XMLoadFloat4(&data[keyLeft]);
						XMVECTOR vRight = XMLoadFloat4(&data[keyRight]);
						XMVECTOR vAnim;
						if (channel.path == Channel::Path::ROTATION)
						{
							vAnim = XMQuaternionSlerp(vLeft, vRight, t);
							vAnim = XMQuaternionNormalize(vAnim);
						}
						else
						{
							vAnim = XMVectorLerp(vLeft, vRight, t);
						}
						XMStoreFloat4(&interpolator.f4, vAnim);
					}
					break;
					case Channel::PathDataType::Weights:
					{
						assert(keyframe_data.size() == keyframe_times.size() * morphWeightsTemp_.size());
						for (size_t j = 0; j < morphWeightsTemp_.size(); ++j)
						{
							float vLeft = keyframe_data[keyLeft * morphWeightsTemp_.size() + j];
							float vRight = keyframe_data[keyRight * morphWeightsTemp_.size() + j];
							float vAnim = math::Lerp(vLeft, vRight, t);
							morphWeightsTemp_[j] = vAnim;
						}
					}
					break;
					}
				}
				break;
				case Sampler::Mode::CUBICSPLINE:
				{
					// Cubic Spline interpolation method:
					float t;
					if (keyLeft == keyRight)
					{
						t = 0;
					}
					else
					{
						t = (playTimer_ - left) / (right - left);
					}
					t = saturate(t);

					const float t2 = t * t;
					const float t3 = t2 * t;

					switch (path_data_type)
					{
					default:
					case Channel::PathDataType::Float:
					{
						assert(keyframe_data.size() == keyframe_times.size());
						float vLeft = keyframe_data[keyLeft * 3 + 1];
						float vLeftTanOut = keyframe_data[keyLeft * 3 + 2];
						float vRightTanIn = keyframe_data[keyRight * 3 + 0];
						float vRight = keyframe_data[keyRight * 3 + 1];
						float vAnim = (2 * t3 - 3 * t2 + 1) * vLeft + (t3 - 2 * t2 + t) * vLeftTanOut + (-2 * t3 + 3 * t2) * vRight + (t3 - t2) * vRightTanIn;
						interpolator.f = vAnim;
					}
					break;
					case Channel::PathDataType::Float2:
					{
						assert(keyframe_data.size() == keyframe_times.size() * 2 * 3);
						const XMFLOAT2* data = (const XMFLOAT2*)keyframe_data.data();
						XMVECTOR vLeft = XMLoadFloat2(&data[keyLeft * 3 + 1]);
						XMVECTOR vLeftTanOut = dt * XMLoadFloat2(&data[keyLeft * 3 + 2]);
						XMVECTOR vRightTanIn = dt * XMLoadFloat2(&data[keyRight * 3 + 0]);
						XMVECTOR vRight = XMLoadFloat2(&data[keyRight * 3 + 1]);
						XMVECTOR vAnim = (2 * t3 - 3 * t2 + 1) * vLeft + (t3 - 2 * t2 + t) * vLeftTanOut + (-2 * t3 + 3 * t2) * vRight + (t3 - t2) * vRightTanIn;
						XMStoreFloat2(&interpolator.f2, vAnim);
					}
					break;
					case Channel::PathDataType::Float3:
					{
						assert(keyframe_data.size() == keyframe_times.size() * 3 * 3);
						const XMFLOAT3* data = (const XMFLOAT3*)keyframe_data.data();
						XMVECTOR vLeft = XMLoadFloat3(&data[keyLeft * 3 + 1]);
						XMVECTOR vLeftTanOut = dt * XMLoadFloat3(&data[keyLeft * 3 + 2]);
						XMVECTOR vRightTanIn = dt * XMLoadFloat3(&data[keyRight * 3 + 0]);
						XMVECTOR vRight = XMLoadFloat3(&data[keyRight * 3 + 1]);
						XMVECTOR vAnim = (2 * t3 - 3 * t2 + 1) * vLeft + (t3 - 2 * t2 + t) * vLeftTanOut + (-2 * t3 + 3 * t2) * vRight + (t3 - t2) * vRightTanIn;
						XMStoreFloat3(&interpolator.f3, vAnim);
					}
					break;
					case Channel::PathDataType::Float4:
					{
						assert(keyframe_data.size() == keyframe_times.size() * 4 * 3);
						const XMFLOAT4* data = (const XMFLOAT4*)keyframe_data.data();
						XMVECTOR vLeft = XMLoadFloat4(&data[keyLeft * 3 + 1]);
						XMVECTOR vLeftTanOut = dt * XMLoadFloat4(&data[keyLeft * 3 + 2]);
						XMVECTOR vRightTanIn = dt * XMLoadFloat4(&data[keyRight * 3 + 0]);
						XMVECTOR vRight = XMLoadFloat4(&data[keyRight * 3 + 1]);
						XMVECTOR vAnim = (2 * t3 - 3 * t2 + 1) * vLeft + (t3 - 2 * t2 + t) * vLeftTanOut + (-2 * t3 + 3 * t2) * vRight + (t3 - t2) * vRightTanIn;
						if (channel.path == Channel::Path::ROTATION)
						{
							vAnim = XMQuaternionNormalize(vAnim);
						}
						XMStoreFloat4(&interpolator.f4, vAnim);
					}
					break;
					case Channel::PathDataType::Weights:
					{
						assert(keyframe_data.size() == keyframe_times.size() * morphWeightsTemp_.size() * 3);
						for (size_t j = 0; j < morphWeightsTemp_.size(); ++j)
						{
							float vLeft = keyframe_data[(keyLeft * morphWeightsTemp_.size() + j) * 3 + 1];
							float vLeftTanOut = keyframe_data[(keyLeft * morphWeightsTemp_.size() + j) * 3 + 2];
							float vRightTanIn = keyframe_data[(keyRight * morphWeightsTemp_.size() + j) * 3 + 0];
							float vRight = keyframe_data[(keyRight * morphWeightsTemp_.size() + j) * 3 + 1];
							float vAnim = (2 * t3 - 3 * t2 + 1) * vLeft + (t3 - 2 * t2 + t) * vLeftTanOut + (-2 * t3 + 3 * t2) * vRight + (t3 - t2) * vRightTanIn;
							morphWeightsTemp_[j] = vAnim;
						}
					}
					break;
					}
				}
				break;
				}
			}

			// The interpolated raw values will be blended on top of component values:
			const float t = amount_;

			// CheckIf this channel is the root motion bone or not.
			const bool isRootBone = (IsRootMotion() && rootMotionBone_ != INVALID_ENTITY && 
				(target_transform == compfactory::GetTransformComponent(rootMotionBone_)));

			if (target_transform != nullptr)
			{
				XMMATRIX transform_local = XMLoadFloat4x4(&target_transform->GetLocalMatrix());

				switch (channel.path)
				{
				case Channel::Path::TRANSLATION:
				{
					const XMVECTOR aT = XMLoadFloat3(&target_transform->GetPosition());
					XMVECTOR bT = XMLoadFloat3(&interpolator.f3);
					if (channel.retargetIndex >= 0 && channel.retargetIndex < (int)retargets_.size())
					{
						// Retargeting transfer from source to destination:
						const AnimationComponent::RetargetSourceData& retarget = retargets_[channel.retargetIndex];
						TransformComponent* source_transform = compfactory::GetTransformComponentByVUID(retarget.source);
						if (source_transform != nullptr)
						{
							XMMATRIX dstRelativeMatrix = XMLoadFloat4x4(&retarget.dstRelativeMatrix);
							XMMATRIX srcRelativeParentMatrix = XMLoadFloat4x4(&retarget.srcRelativeParentMatrix);
							XMVECTOR S, R; // matrix decompose destinations
							TransformComponent transform = *source_transform;
							XMFLOAT3 P;
							XMStoreFloat3(&P, bT);
							transform.SetPosition(P);
							XMMATRIX localMatrix = dstRelativeMatrix * transform_local * srcRelativeParentMatrix;
							XMMatrixDecompose(&S, &R, &bT, localMatrix);
						}
					}
					const XMVECTOR T = XMVectorLerp(aT, bT, t);
					if (!isRootBone)
					{
						// Not root motion bone.
						XMFLOAT3 P;
						XMStoreFloat3(&P, T);
						target_transform->SetPosition(P);
					}
					else
					{
						if (XMVector4Equal(rootPrevTranslation_, INVALID_VECTOR) || end_ < prevLocTimer_)
						{
							// If root motion bone.
							rootPrevTranslation_ = T;
						}

						XMVECTOR rotation_quat = rootPrevRotation_;

						if (XMVector4Equal(rootPrevRotation_, INVALID_VECTOR) || end_ < prevRotTimer_)
						{
							// If root motion bone.
							rotation_quat = XMLoadFloat4(&target_transform->GetRotation());
						}

						const XMVECTOR root_trans = XMVectorSubtract(T, rootPrevTranslation_);
						XMVECTOR inverseQuaternion = XMQuaternionInverse(rotation_quat);
						XMVECTOR rotatedDirectionVector = XMVector3Rotate(root_trans, inverseQuaternion);

						XMMATRIX mat = XMLoadFloat4x4(&target_transform->GetWorldMatrix());
						rotatedDirectionVector = XMVector4Transform(rotatedDirectionVector, mat);

						// Store root motion offset
						XMStoreFloat3(&rootTranslationOffset_, rotatedDirectionVector);
						// If root motion bone.
						rootPrevTranslation_ = T;
						prevLocTimer_ = playTimer_;
					}
				}
				break;
				case Channel::Path::ROTATION:
				{
					const XMVECTOR aR = XMLoadFloat4(&target_transform->GetRotation());
					XMVECTOR bR = XMLoadFloat4(&interpolator.f4);
					if (channel.retargetIndex >= 0 && channel.retargetIndex < (int)retargets_.size())
					{
						// Retargeting transfer from source to destination:
						const RetargetSourceData& retarget = retargets_[channel.retargetIndex];
						TransformComponent* source_transform = compfactory::GetTransformComponentByVUID(retarget.source);
						if (source_transform != nullptr)
						{
							XMMATRIX dstRelativeMatrix = XMLoadFloat4x4(&retarget.dstRelativeMatrix);
							XMMATRIX srcRelativeParentMatrix = XMLoadFloat4x4(&retarget.srcRelativeParentMatrix);
							XMVECTOR S, T; // matrix decompose destinations
							TransformComponent transform = *source_transform;
							XMFLOAT4 qr;
							XMStoreFloat4(&qr, bR);
							transform.SetQuaternion(qr);
							XMMATRIX localMatrix = dstRelativeMatrix * transform_local * srcRelativeParentMatrix;
							XMMatrixDecompose(&S, &bR, &T, localMatrix);
						}
					}
					const XMVECTOR R = XMQuaternionSlerp(aR, bR, t);
					if (!isRootBone)
					{
						// Not root motion bone.
						XMFLOAT4 qr;
						XMStoreFloat4(&qr, R);
						target_transform->SetQuaternion(qr);
					}
					else
					{
						if (XMVector4Equal(rootPrevRotation_, INVALID_VECTOR) || end_ < prevRotTimer_)
						{
							// If root motion bone.
							rootPrevRotation_ = R;
						}

						// Assuming q1 and q2 are the two quaternions you want to subtract
						// // Let's say you want to find the relative rotation from q1 to q2
						XMMATRIX mat1 = XMMatrixRotationQuaternion(rootPrevRotation_);
						XMMATRIX mat2 = XMMatrixRotationQuaternion(R);
						// Compute the relative rotation matrix by multiplying the inverse of the first rotation
						// by the second rotation
						XMMATRIX relativeRotationMatrix = XMMatrixMultiply(XMMatrixTranspose(mat1), mat2);
						// Extract the quaternion representing the relative rotation
						XMVECTOR relativeRotationQuaternion = XMQuaternionRotationMatrix(relativeRotationMatrix);

						// Store root motion offset
						XMStoreFloat4(&rootRotationOffset_, relativeRotationQuaternion);
						// Swap Y and Z Axis for Unknown reason
						const float Y = rootRotationOffset_.y;
						rootRotationOffset_.y = rootRotationOffset_.z;
						rootRotationOffset_.z = Y;

						// If root motion bone.
						rootPrevRotation_ = R;
						prevRotTimer_ = playTimer_;
					}

				}
				break;
				case Channel::Path::SCALE:
				{
					const XMVECTOR aS = XMLoadFloat3(&target_transform->GetScale());
					XMVECTOR bS = XMLoadFloat3(&interpolator.f3);
					if (channel.retargetIndex >= 0 && channel.retargetIndex < (int)retargets_.size())
					{
						// Retargeting transfer from source to destination:
						const RetargetSourceData& retarget = retargets_[channel.retargetIndex];
						TransformComponent* source_transform = compfactory::GetTransformComponentByVUID(retarget.source);
						if (source_transform != nullptr)
						{
							XMMATRIX dstRelativeMatrix = XMLoadFloat4x4(&retarget.dstRelativeMatrix);
							XMMATRIX srcRelativeParentMatrix = XMLoadFloat4x4(&retarget.srcRelativeParentMatrix);
							XMVECTOR R, T; // matrix decompose destinations
							TransformComponent transform = *source_transform;
							XMFLOAT3 scale;
							XMStoreFloat3(&scale, bS);
							transform.SetScale(scale);
							XMMATRIX localMatrix = dstRelativeMatrix * transform_local * srcRelativeParentMatrix;
							XMMatrixDecompose(&bS, &R, &T, localMatrix);
						}
					}
					const XMVECTOR S = XMVectorLerp(aS, bS, t);
					XMFLOAT3 scale;
					XMStoreFloat3(&scale, S);
					target_transform->SetScale(scale);
				}
				break;
				default:
					break;
				}
			}

			if (target_geo != nullptr)
			{
				size_t offset = 0;
				for (auto& part : target_geo->GetMutablePrimitives())
				{
					auto& morph_targets = part.GetMutableMorphTargets();
					for (size_t j = 0; j < morph_targets.size(); ++j)
					{
						morph_targets[j].weight = math::Lerp(morph_targets[j].weight, morphWeightsTemp_[j + offset], t);
					}

					offset += morph_targets.size();
				}
			}

			if (target_light != nullptr)
			{
				switch (channel.path)
				{
				case Channel::Path::LIGHT_COLOR:
				{
					target_light->SetColor(math::Lerp(target_light->GetColor(), interpolator.f3, t));
				}
				break;
				case Channel::Path::LIGHT_INTENSITY:
				{
					target_light->SetIntensity(math::Lerp(target_light->GetIntensity(), interpolator.f, t));
				}
				break;
				case Channel::Path::LIGHT_RANGE:
				{
					target_light->SetRange(math::Lerp(target_light->GetRange(), interpolator.f, t));
				}
				break;
				case Channel::Path::LIGHT_INNERCONE:
				{
					target_light->SetInnerConeAngle(math::Lerp(target_light->GetInnerConeAngle(), interpolator.f, t));
				}
				break;
				case Channel::Path::LIGHT_OUTERCONE:
				{
					target_light->SetOuterConeAngle(math::Lerp(target_light->GetOuterConeAngle(), interpolator.f, t));
				}
				break;
				default:
					break;
				}
			}

			//if (target_sound != nullptr)
			//{
			//	switch (channel.path)
			//	{
			//	case Channel::Path::SOUND_VOLUME:
			//	{
			//		target_sound->volume = math::Lerp(target_sound->volume, interpolator.f, t);
			//	}
			//	break;
			//	default:
			//		break;
			//	}
			//}

			//if (target_emitter != nullptr)
			//{
			//	switch (channel.path)
			//	{
			//	case Channel::Path::EMITTER_EMITCOUNT:
			//	{
			//		target_emitter->count = math::Lerp(target_emitter->count, interpolator.f, t);
			//	}
			//	break;
			//	default:
			//		break;
			//	}
			//}

			if (target_camera != nullptr)
			{
				switch (channel.path)
				{
				case Channel::Path::CAMERA_FOV:
				{
					target_camera->SetFovVertical(math::Lerp(target_camera->GetFovVertical(), interpolator.f, t));
				}
				break;
				case Channel::Path::CAMERA_FOCAL_LENGTH:
				{
					target_camera->SetFocalLength(math::Lerp(target_camera->GetFocalLength(), interpolator.f, t));
				}
				break;
				case Channel::Path::CAMERA_APERTURE_SIZE:
				{
					target_camera->SetApertureSize(math::Lerp(target_camera->GetApertureSize(), interpolator.f, t));
				}
				break;
				case Channel::Path::CAMERA_APERTURE_SHAPE:
				{
					target_camera->SetApertureShape(math::Lerp(target_camera->GetApertureShape(), interpolator.f2, t));
				}
				break;
				default:
					break;
				}
			}

			if (target_material != nullptr)
			{
				switch (channel.path)
				{
				case Channel::Path::MATERIAL_COLOR:
				{
					target_material->SetBaseColor(math::Lerp(target_material->GetBaseColor(), interpolator.f4, t));
				}
				break;
				case Channel::Path::MATERIAL_EMISSIVE:
				{
					target_material->SetEmissiveColor(math::Lerp(target_material->GetEmissiveColor(), interpolator.f4, t));
				}
				break;
				case Channel::Path::MATERIAL_ROUGHNESS:
				{
					target_material->SetRoughness(math::Lerp(target_material->GetRoughness(), interpolator.f, t));
				}
				break;
				case Channel::Path::MATERIAL_METALNESS:
				{
					target_material->SetMatalness(math::Lerp(target_material->GetMetalness(), interpolator.f, t));
				}
				break;
				case Channel::Path::MATERIAL_REFLECTANCE:
				{
					target_material->SetReflectance(math::Lerp(target_material->GetReflectance(), interpolator.f, t));
				}
				break;
				case Channel::Path::MATERIAL_TEXMULADD:
				{
					target_material->SetTexMulAdd(math::Lerp(target_material->GetTexMulAdd(), interpolator.f4, t));
				}
				break;
				default:
					break;
				}
			}

		}

		if (playTimer_ > end_ && speed_ > 0)
		{
			if (IsLooped())
			{
				playTimer_ = start_;
				for (auto& channel : channels_)
				{
					channel.next_event = 0;
				}
			}
			else if (IsPingPong())
			{
				playTimer_ = end_;
				speed_ = -speed_;
			}
			else
			{
				playTimer_ = end_;
				flags_ &= ~PLAYING; // Pause();
			}
		}
		else if (playTimer_ < start_ && speed_ < 0)
		{
			if (IsLooped())
			{
				playTimer_ = end_;
				for (auto& channel : channels_)
				{
					channel.next_event = 0;
				}
			}
			else if (IsPingPong())
			{
				playTimer_ = start_;
				speed_ = -speed_;
			}
			else
			{
				playTimer_ = start_;
				flags_ &= ~PLAYING; // Pause();
			}
		}

		if (IsPlaying())
		{
			playTimer_ += dt * speed_;
		}
	}
}