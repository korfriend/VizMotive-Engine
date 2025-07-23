#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzRenderer : VzBaseComp
	{
		enum ActorFilter: uint32_t
		{
			MESH_OPAQUE = 1 << 0,
			MESH_TRANSPARENT = 1 << 1,
			MESH_WATER = 1 << 2,
			MESH_NAVIGATION = 1 << 3,
			COLLIDER = 1 << 4,
			VOLUME = 1 << 5,

			// Include everything:
			RENDERABLE_ALL = ~0u,
		};

		// Tone mapping HDR -> LDR
		enum class Tonemap
		{
			Reinhard,
			ACES
		};

		VzRenderer(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, COMPONENT_TYPE::RENDERER) {
		}
		virtual ~VzRenderer() = default;

		void SetCanvas(const uint32_t w, const uint32_t h, const float dpi, void* window = nullptr);
		void ResizeCanvas(const uint32_t w, const uint32_t h, const CamVID vidCam = 0u); // preserves dpi and window handler
		void GetCanvas(uint32_t* VZ_NULLABLE w, uint32_t* VZ_NULLABLE h, float* VZ_NULLABLE dpi, void** VZ_NULLABLE window = nullptr);

		void SetViewport(const float x, const float y, const float w, const float h);
		void GetViewport(float* VZ_NULLABLE x, float* VZ_NULLABLE y, float* VZ_NULLABLE w, float* VZ_NULLABLE h);
		void SetScissor(const int32_t left, const int32_t top, const int32_t right, const int32_t bottom);
		void GetScissor(int32_t* VZ_NULLABLE left, int32_t* VZ_NULLABLE top, int32_t* VZ_NULLABLE right, int32_t* VZ_NULLABLE bottom);
		void UseCanvasAsViewport();
		// add scissor interfaces

		void SetLayerMask(const uint32_t layerMask);

		void SetClearColor(const vfloat4& color);
		void GetClearColor(vfloat4& color) const;

		void EnableClear(const bool enabled);
		void SkipPostprocess(const bool skip);

		void SetAllowHDR(const bool enable);
		bool GetAllowHDR() const;

		void SetTonemap(const Tonemap tonemap);

		void EnableFrameLock(const bool enabled, const bool frameSkip = true, const float targetFrameRate = 60.f);
		// MUST BE CALLED WITHIN THE SAME THREAD
		bool Render(const SceneVID vidScene, const CamVID vidCam, const float dt = -1.f);
		bool Render(const VzScene* scene, const VzCamera* camera, const float dt = -1.f) { return Render(scene->GetVID(), camera->GetVID(), dt); };

		void RenderChain(const std::vector<ChainUnitSCam>& scChain);

		bool Picking(const SceneVID vidScene, const CamVID vidCam, const vfloat2& pos, const uint32_t filterFlags, const float toleranceRadius,
			vfloat3& worldPosition, ActorVID& vid, int* primitiveID = nullptr, int* maskValue = nullptr) const;
		bool Picking(const VzScene* scene, const VzCamera* camera, const vfloat2& pos, const uint32_t filterFlags, const float toleranceRadius,
			vfloat3& worldPosition, ActorVID& vid, int* primitiveID = nullptr, int* maskValue = nullptr) const {
			return Picking(scene->GetVID(), camera->GetVID(), pos, filterFlags, toleranceRadius, worldPosition, vid, primitiveID, maskValue);
		}

		vfloat3 UnprojToWorld(const vfloat2& posOnScreen, const VzCamera* camera = nullptr);

		// the render target resource must be fenced before calling the next Render()
		struct SharedResourceTarget
		{
			void* resourcePtr = nullptr;
			uint64_t descriptorHandle = 0u; // DX12 and Vulkan only
		};
		bool GetSharedRenderTarget(const void* graphicsDev2, const void* srvDescHeap2, const int descriptorIndex, SharedResourceTarget& resTarget, uint32_t* w, uint32_t* h);

		bool StoreRenderTarget(std::vector<uint8_t>& rawBuffer, uint32_t* w = nullptr, uint32_t* h = nullptr);
		bool StoreRenderTargetInfoFile(const std::string& fileName);

		// just for dev. mode
		// "NONE" : original
		// "PRIMITIVE_ID"
		void ShowDebugBuffer(const std::string& debugMode = "NONE");
	};
}
