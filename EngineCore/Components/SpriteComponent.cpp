#include "GComponents.h"
#include "Common/ResourceManager.h"
#include "Utils/Helpers.h"
#include "Utils/Backlog.h"

namespace vz
{
	void SpriteComponent::FixedUpdate()
	{
		if (IsDisableUpdate())
			return;
	}

	void SpriteComponent::Update(float dt)
	{
		if (IsDisableUpdate())
			return;
		position_.x += anim_.vel.x * dt;
		position_.y += anim_.vel.y * dt;
		position_.z += anim_.vel.z * dt;
		rotation_ += anim_.rot * dt;
		scale_.x += anim_.scaleX * dt;
		scale_.y += anim_.scaleY * dt;
		opacity_ += anim_.opa * dt;
		if (opacity_ >= 1) {
			if (anim_.repeatable) { opacity_ = 0.99f; anim_.opa *= -1; }
			else				opacity_ = 1;
		}
		else if (opacity_ <= 0) {
			if (anim_.repeatable) { opacity_ = 0.01f; anim_.opa *= -1; }
			else				opacity_ = 0;
		}
		fade_ += anim_.fad * dt;
		if (fade_ >= 1) {
			if (anim_.repeatable) { fade_ = 0.99f; anim_.fad *= -1; }
			else				fade_ = 1;
		}
		else if (fade_ <= 0) {
			if (anim_.repeatable) { fade_ = 0.01f; anim_.fad *= -1; }
			else				fade_ = 0;
		}

		uvOffset_.x += anim_.movingTexAnim.speedX * dt;
		uvOffset_.y += anim_.movingTexAnim.speedY * dt;

		// Draw rect anim:
		if (anim_.drawRectAnim.frameCount > 1)
		{
			anim_.drawRectAnim._elapsedTime += dt * anim_.drawRectAnim.frameRate;
			if (anim_.drawRectAnim._elapsedTime >= 1.0f)
			{
				// Reset timer:
				anim_.drawRectAnim._elapsedTime = 0;

				if (anim_.drawRectAnim.horizontalFrameCount == 0)
				{
					// If no horizontal frame count was specified, it means that all the frames are horizontal:
					anim_.drawRectAnim.horizontalFrameCount = anim_.drawRectAnim.frameCount;
				}

				// Advance frame counter:
				anim_.drawRectAnim._currentFrame++;

				if (anim_.drawRectAnim._currentFrame < anim_.drawRectAnim.frameCount)
				{
					// Step one frame horizontally if animation is still playing:
					drawRect_.x += drawRect_.z;

					if (anim_.drawRectAnim._currentFrame > 0 &&
						anim_.drawRectAnim._currentFrame % anim_.drawRectAnim.horizontalFrameCount == 0)
					{
						// if the animation is multiline, we reset horizontally and step downwards:
						drawRect_.x -= drawRect_.z * anim_.drawRectAnim.horizontalFrameCount;
						drawRect_.y += drawRect_.w;
					}
				}
				else if (anim_.repeatable)
				{
					// restart if repeatable:
					const int rewind_X = (anim_.drawRectAnim.frameCount - 1) % anim_.drawRectAnim.horizontalFrameCount;
					const int rewind_Y = (anim_.drawRectAnim.frameCount - 1) / anim_.drawRectAnim.horizontalFrameCount;
					drawRect_.x -= drawRect_.z * rewind_X;
					drawRect_.y -= drawRect_.w * rewind_Y;
					anim_.drawRectAnim._currentFrame = 0;
				}

			}
		}

		// Wobble anim:
		if (anim_.wobbleAnim.amount.x > 0 || anim_.wobbleAnim.amount.y > 0)
		{
			// Rotate each corner on a scaled circle (ellipsoid):
			//	Since the rotations are randomized, it will look like a wobble effect
			//	Also use two circles on each other to achieve more random look
			for (int i = 0; i < 4; ++i)
			{
				anim_.wobbleAnim.corner_angles[i] += XM_2PI * anim_.wobbleAnim.speed * anim_.wobbleAnim.corner_speeds[i] * dt;
				anim_.wobbleAnim.corner_angles2[i] += XM_2PI * anim_.wobbleAnim.speed * anim_.wobbleAnim.corner_speeds2[i] * dt;
				corners_[i].x += std::sin(anim_.wobbleAnim.corner_angles[i]) * anim_.wobbleAnim.amount.x * 0.5f;
				corners_[i].y += std::cos(anim_.wobbleAnim.corner_angles[i]) * anim_.wobbleAnim.amount.y * 0.5f;
				corners_[i].x += std::sin(anim_.wobbleAnim.corner_angles2[i]) * anim_.wobbleAnim.amount.x * 0.25f;
				corners_[i].y += std::cos(anim_.wobbleAnim.corner_angles2[i]) * anim_.wobbleAnim.amount.y * 0.25f;
			}
		}

	}
}