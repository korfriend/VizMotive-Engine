#pragma once
#include "vzECS.h"
#include "vzScene.h"
#include "vzJobSystem.h"
#include "vzPrimitive.h"

#include <memory>

namespace vz::physics
{
	// Initializes the physics engine
	void Initialize();

	// Enable/disable the physics engine all together
	void SetEnabled(bool value);
	bool IsEnabled();

	// Enable/disable the physics simulation.
	//	Physics engine state will be updated but not simulated
	void SetSimulationEnabled(bool value);
	bool IsSimulationEnabled();

	// Enable/disable debug drawing of physics objects
	void SetDebugDrawEnabled(bool value);
	bool IsDebugDrawEnabled();

	// Set the accuracy of the simulation
	//	This value corresponds to maximum simulation step count
	//	Higher values will be slower but more accurate
	void SetAccuracy(int value);
	int GetAccuracy();

	// Set frames per second value of physics simulation (default = 120 FPS)
	void SetFrameRate(float value);
	float GetFrameRate();

	// Update the physics state, run simulation, etc.
	void RunPhysicsUpdateSystem(
		vz::jobsystem::context& ctx,
		vz::scene::Scene& scene,
		float dt
	);

	// Set linear velocity to rigid body
	void SetLinearVelocity(
		vz::scene::RigidBodyPhysicsComponent& physicscomponent,
		const XMFLOAT3& velocity
	);
	// Set angular velocity to rigid body
	void SetAngularVelocity(
		vz::scene::RigidBodyPhysicsComponent& physicscomponent,
		const XMFLOAT3& velocity
	);

	// Apply force at body center
	void ApplyForce(
		vz::scene::RigidBodyPhysicsComponent& physicscomponent,
		const XMFLOAT3& force
	);
	// Apply force at body local position
	void ApplyForceAt(
		vz::scene::RigidBodyPhysicsComponent& physicscomponent,
		const XMFLOAT3& force,
		const XMFLOAT3& at
	);

	// Apply impulse at body center
	void ApplyImpulse(
		vz::scene::RigidBodyPhysicsComponent& physicscomponent,
		const XMFLOAT3& impulse
	);
	void ApplyImpulse(
		vz::scene::HumanoidComponent& humanoid,
		vz::scene::HumanoidComponent::HumanoidBone bone,
		const XMFLOAT3& impulse
	);
	// Apply impulse at body local position
	void ApplyImpulseAt(
		vz::scene::RigidBodyPhysicsComponent& physicscomponent,
		const XMFLOAT3& impulse,
		const XMFLOAT3& at
	);
	void ApplyImpulseAt(
		vz::scene::HumanoidComponent& humanoid,
		vz::scene::HumanoidComponent::HumanoidBone bone,
		const XMFLOAT3& impulse,
		const XMFLOAT3& at
	);

	void ApplyTorque(
		vz::scene::RigidBodyPhysicsComponent& physicscomponent,
		const XMFLOAT3& torque
	);
	void ApplyTorqueImpulse(
		vz::scene::RigidBodyPhysicsComponent& physicscomponent,
		const XMFLOAT3& torque
	);

	enum class ActivationState
	{
		Active,
		Inactive,
	};
	void SetActivationState(
		vz::scene::RigidBodyPhysicsComponent& physicscomponent,
		ActivationState state
	);
	void SetActivationState(
		vz::scene::SoftBodyPhysicsComponent& physicscomponent,
		ActivationState state
	);

	struct RayIntersectionResult
	{
		vz::ecs::Entity entity = vz::ecs::INVALID_ENTITY;
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		XMFLOAT3 position_local = XMFLOAT3(0, 0, 0);
		XMFLOAT3 normal = XMFLOAT3(0, 0, 0);
		vz::ecs::Entity humanoid_ragdoll_entity = vz::ecs::INVALID_ENTITY;
		vz::scene::HumanoidComponent::HumanoidBone humanoid_bone = vz::scene::HumanoidComponent::HumanoidBone::Count;
		const void* physicsobject = nullptr;
		constexpr bool IsValid() const { return entity != vz::ecs::INVALID_ENTITY; }
	};
	RayIntersectionResult Intersects(
		const vz::scene::Scene& scene,
		vz::primitive::Ray ray
	);

	struct PickDragOperation
	{
		std::shared_ptr<void> internal_state;
		inline bool IsValid() const { return internal_state != nullptr; }
	};
	void PickDrag(
		const vz::scene::Scene& scene,
		vz::primitive::Ray ray,
		PickDragOperation& op
	);
}
