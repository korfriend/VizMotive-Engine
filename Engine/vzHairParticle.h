#pragma once
#include "CommonInclude.h"
#include "vzGraphicsDevice.h"
#include "vzEnums.h"
#include "vzMath.h"
#include "vzECS.h"
#include "vzPrimitive.h"
#include "vzVector.h"
#include "vzScene_Decl.h"
#include "vzScene_Components.h"

namespace vz
{
	class Archive;
}

namespace vz
{
	class HairParticleSystem
	{
	public:
		vz::graphics::GPUBuffer constantBuffer;
		vz::graphics::GPUBuffer generalBuffer;
		vz::scene::MeshComponent::BufferView simulation_view;
		vz::scene::MeshComponent::BufferView vb_pos[2];
		vz::scene::MeshComponent::BufferView vb_nor;
		vz::scene::MeshComponent::BufferView vb_pos_raytracing;
		vz::scene::MeshComponent::BufferView vb_uvs;
		vz::scene::MeshComponent::BufferView ib_culled;
		vz::scene::MeshComponent::BufferView indirect_view;
		vz::graphics::GPUBuffer primitiveBuffer;

		vz::graphics::GPUBuffer indexBuffer;
		vz::graphics::GPUBuffer vertexBuffer_length;

		vz::graphics::RaytracingAccelerationStructure BLAS;

		void CreateFromMesh(const vz::scene::MeshComponent& mesh);
		void CreateRenderData();
		void CreateRaytracingRenderData();

		void UpdateCPU(
			const vz::scene::TransformComponent& transform,
			const vz::scene::MeshComponent& mesh,
			float dt
		);

		struct UpdateGPUItem
		{
			const HairParticleSystem* hair = nullptr;
			uint32_t instanceIndex = 0;
			const vz::scene::MeshComponent* mesh = nullptr;
			const vz::scene::MaterialComponent* material = nullptr;
		};
		// Update a batch of hair particles by GPU
		static void UpdateGPU(
			const UpdateGPUItem* items,
			uint32_t itemCount,
			vz::graphics::CommandList cmd
		);

		mutable bool gpu_initialized = false;
		void InitializeGPUDataIfNeeded(vz::graphics::CommandList cmd);

		void Draw(
			const vz::scene::MaterialComponent& material,
			vz::enums::RENDERPASS renderPass,
			vz::graphics::CommandList cmd
		) const;

		enum FLAGS
		{
			EMPTY = 0,
			_DEPRECATED_REGENERATE_FRAME = 1 << 0,
			REBUILD_BUFFERS = 1 << 1,
		};
		uint32_t _flags = EMPTY;

		vz::ecs::Entity meshID = vz::ecs::INVALID_ENTITY;

		uint32_t strandCount = 0;
		uint32_t segmentCount = 1;
		uint32_t randomSeed = 1;
		float length = 1.0f;
		float stiffness = 10.0f;
		float randomness = 0.2f;
		float viewDistance = 200;
		vz::vector<float> vertex_lengths;

		// Sprite sheet properties:
		uint32_t framesX = 1;
		uint32_t framesY = 1;
		uint32_t frameCount = 1;
		uint32_t frameStart = 0;

		// Non-serialized attributes:
		XMFLOAT4X4 world;
		vz::primitive::AABB aabb;
		vz::vector<uint32_t> indices; // it is dependent on vertex_lengths and contains triangles with non-zero lengths
		uint32_t layerMask = ~0u;
		mutable bool regenerate_frame = true;
		vz::graphics::Format position_format = vz::graphics::Format::R16G16B16A16_UNORM;

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);

		static void Initialize();

		constexpr uint32_t GetParticleCount() const { return strandCount * segmentCount; }
		uint64_t GetMemorySizeInBytes() const;
	};
}
