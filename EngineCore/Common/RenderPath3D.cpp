#include "RenderPath3D.h"
#include "GBackend/GModuleLoader.h"
#include "Common/Initializer.h"
#include "Components/GComponents.h"
#include "Utils/Profiler.h"
#include "Utils/Jobsystem.h"

namespace vz
{
	extern GShaderEngineLoader shaderEngine;

	XMMATRIX CreateScreenToProjectionMatrix(const graphics::Viewport& viewport)
	{
		float scaleX = 2.0f / viewport.width;
		float scaleY = -2.0f / viewport.height;
		float scaleZ = 1.0f / (viewport.max_depth - viewport.min_depth);

		float offsetX = -1.0f - (2.0f * viewport.top_left_x / viewport.width);
		float offsetY = 1.0f + (2.0f * viewport.top_left_y / viewport.height);
		float offsetZ = -viewport.min_depth * scaleZ;

		return XMMatrixSet(
			scaleX, 0.0f, 0.0f, 0.0f,
			0.0f, scaleY, 0.0f, 0.0f,
			0.0f, 0.0f, scaleZ, 0.0f,
			offsetX, offsetY, offsetZ, 1.0f
		);
	}

	XMMATRIX CreateProjectionToScreenMatrix(const graphics::Viewport& viewport)
	{
		float scaleX = viewport.width / 2.0f;
		float scaleY = viewport.height / 2.0f;
		float scaleZ = viewport.max_depth - viewport.min_depth;

		float offsetX = viewport.top_left_x + scaleX;
		float offsetY = viewport.top_left_y + scaleY;
		float offsetZ = viewport.min_depth;

		return XMMatrixSet(
			scaleX, 0.0f, 0.0f, 0.0f,
			0.0f, -scaleY, 0.0f, 0.0f,
			0.0f, 0.0f, scaleZ, 0.0f,
			offsetX, offsetY, offsetZ, 1.0f
		);
	}

	void RenderPath3D::updateViewportTransforms()
	{
		XMStoreFloat4x4(&matScreen_, CreateProjectionToScreenMatrix(viewport_));
		XMStoreFloat4x4(&matScreenInv_, CreateScreenToProjectionMatrix(viewport_));
	}

	void RenderPath3D::stableCountTest()
	{
		if (frameCount == 0)
		{
			return;
		}

		if (TimeDurationCount(recentRender3D_UpdateTime, camera->GetTimeStamp()) > 0)
		{
			stableCount_++;
		}
		else
		{
			stableCount_ = 0;
		}

		handlerRenderPath3D_->stableCount = stableCount_;
	}

	RenderPath3D::RenderPath3D(const Entity entity, graphics::GraphicsDevice* graphicsDevice)
		: RenderPath2D(entity, graphicsDevice) 
	{
		handlerRenderPath3D_ = shaderEngine.pluginNewGRenderPath3D(swapChain_, rtRenderFinal_);
		assert(handlerRenderPath3D_->version == GRenderPath3D::GRenderPath3D_INTERFACE_VERSION);

		handlerRenderPath2D_ = handlerRenderPath3D_;

		type_ = "RenderPath3D";
	}

	RenderPath3D::~RenderPath3D() 
	{ 
		DeleteGPUResources(false); 

		if (handlerRenderPath3D_)
		{
			handlerRenderPath3D_->Destroy();
			delete handlerRenderPath3D_;
			handlerRenderPath3D_ = nullptr;
		}
	}

	void RenderPath3D::DeleteGPUResources(const bool resizableOnly)
	{
		RenderPath2D::DeleteGPUResources(resizableOnly);
		handlerRenderPath3D_->Destroy();
		if (!resizableOnly)
		{
			// Scene?!
		}
	}

	void RenderPath3D::ResizeResources()
	{
		RenderPath3D::DeleteGPUResources(true);
		RenderPath2D::ResizeResources();
		handlerRenderPath3D_->ResizeCanvas(width_, height_);

		//viewport
		if (!useManualSetViewport)
		{
			viewport_.top_left_x = 0;
			viewport_.top_left_y = 0;
			viewport_.width = (float)width_;
			viewport_.height = (float)height_;
			updateViewportTransforms();
		}
		//scissor
		if (!useManualSetScissor)
		{
			scissor_.left = 0;
			scissor_.right = width_;
			scissor_.top = 0;
			scissor_.bottom = height_;
		}

		updateViewportTransforms();
	}

	// scene and camera updates
	void RenderPath3D::Update(const float dt)
	{
		RenderPath2D::Update(dt);

		if (camera == nullptr || scene == nullptr) return;

		stableCountTest();

		// this must be called after scene's update
		HierarchyComponent* hier = compfactory::GetHierarchyComponent(camera->GetEntity());
		if (hier->GetParentEntity() != INVALID_ENTITY)
		{
			camera->SetWorldLookAtFromHierarchyTransforms();
		}

		LayeredMaskComponent* layeredmask = compfactory::GetLayeredMaskComponent(camera->GetEntity());

		if (camera->IsSlicer())
		{
			((GSlicerComponent*)camera)->layeredmask = layeredmask;
			((GSlicerComponent*)camera)->transform = compfactory::GetTransformComponent(camera->GetEntity());
			if (camera->IsCurvedSlicer())
			{
				((GSlicerComponent*)camera)->UpdateCurve(); // include dirty check!
			}
		}
		else
		{
			((GCameraComponent*)camera)->layeredmask = layeredmask;
			((GCameraComponent*)camera)->transform = compfactory::GetTransformComponent(camera->GetEntity());
			if (camera->IsDirty())
			{
				camera->UpdateMatrix();
			}
		}
	}
	
	void RenderPath3D::Render(const float dt)
	{
		if (camera == nullptr || scene == nullptr)
		{
			vzlog_warning("RenderPath3D::Render >> No Scene or No Camera/Slicer in RenderPath!");
			return;
		}

		if (skipStableCount > 0)
		{
			if (scene->stableCount > skipStableCount && stableCount_ > skipStableCount
				&& !forceToRenderCall)
			{
				return;
			}
		}
		if (forceToRenderCall)
		{
			stableCount_ = 0;
			forceToRenderCall = false;
		}

		// This involves the following process
		//	1. update view (for each rendering pipeline)
		//	2. update render data
		//	3. execute rendering pipelines

		// ----- RenderPath3D Attributes -----
		handlerRenderPath3D_->scene = scene;
		handlerRenderPath3D_->camera = camera;
		handlerRenderPath3D_->viewport = viewport_;
		handlerRenderPath3D_->clearEnabled = clearEnabled_;
		handlerRenderPath3D_->skipPostprocess = skipPostprocess_;

		handlerRenderPath3D_->matToScreen = matScreen_;
		handlerRenderPath3D_->matToScreenInv = matScreenInv_;
		handlerRenderPath3D_->tonemap = static_cast<GRenderPath3D::Tonemap>(tonemap);

		// ----- RenderPath2D Attributes -----
		handlerRenderPath3D_->scissor = scissor_;
		handlerRenderPath3D_->colorspace = colorSpace_;
		handlerRenderPath3D_->msaaSampleCount = GetMSAASampleCount();

		bool is_resized = UpdateResizedCanvas();
		if (is_resized)
		{
			//graphics::GetDevice()->WaitForGPU();
			ResizeResources(); // call RenderPath2D::ResizeResources();
			// since IsCanvasResized() updates the canvas size,
			//	resizing sources does not happen redundantly 
		}

		// Clear Option //
		if (swapChain_.IsValid())
		{
			memcpy(swapChain_.desc.clear_color, clearColor, sizeof(float) * 4);
		}
		else
		{
			assert(rtRenderFinal_.IsValid());
			memcpy(rtRenderFinal_.desc.clear.color, clearColor, sizeof(float) * 4);
		}


		if (initializer::IsInitializeFinished(initializer::INITIALIZED_SYSTEM_RENDERER))
		{
			handlerRenderPath3D_->Render(dt);
			recentRender3D_UpdateTime = TimerNow;
		}

		RenderPath2D::Render(dt);
	}

	const graphics::Texture* RenderPath3D::GetLastProcessRT() const 
	{ 
		if (handlerRenderPath3D_ == nullptr)
		{
			return nullptr;
		}
		return &handlerRenderPath3D_->GetLastProcessRT(); 
	}

	constexpr size_t FNV1aHash(std::string_view str, size_t hash = 14695981039346656037ULL) {
		for (char c : str) {
			hash ^= static_cast<size_t>(c);
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	geometrics::Ray RenderPath3D::GetPickRay(float screenX, float screenY, const CameraComponent& camera)
	{
		XMMATRIX V = XMLoadFloat4x4(&camera.GetView());
		XMMATRIX P = XMLoadFloat4x4(&camera.GetProjection());
		XMMATRIX W = XMMatrixIdentity();
		XMVECTOR lineStart = XMVector3Unproject(XMVectorSet(screenX, screenY, 1, 1), 0, 0, (float)width_, (float)height_, 0.0f, 1.0f, P, V, W);
		XMVECTOR lineEnd = XMVector3Unproject(XMVectorSet(screenX, screenY, 0, 1), 0, 0, (float)width_, (float)height_, 0.0f, 1.0f, P, V, W);
		XMVECTOR rayDirection = XMVector3Normalize(XMVectorSubtract(lineEnd, lineStart));
		return geometrics::Ray(lineStart, rayDirection);
	}

	constexpr static size_t HASH_PRIMITIVE_ID = FNV1aHash("PRIMITIVE_ID"); // target at rtPrimitiveID
	constexpr static size_t HASH_INSTANCE_ID = FNV1aHash("INSTANCE_ID");   // target at rtPrimitiveID
	constexpr static size_t HASH_LINEAR_DEPTH = FNV1aHash("LINEAR_DEPTH"); // target at rtLinearDepth
	constexpr static size_t HASH_WITHOUT_POSTPROCESSING = FNV1aHash("WITHOUT_POSTPROCESSING"); // target at rtMain

	void RenderPath3D::ShowDebugBuffer(const std::string& debugMode)
	{
		using DEBUG_BUFFER = GRenderPath3D::DEBUG_BUFFER;

		size_t hash_debug = FNV1aHash(debugMode);
		switch (hash_debug)
		{
		case HASH_PRIMITIVE_ID: handlerRenderPath3D_->debugMode = DEBUG_BUFFER::PRIMITIVE_ID; break;
		case HASH_INSTANCE_ID: handlerRenderPath3D_->debugMode = DEBUG_BUFFER::INSTANCE_ID; break;
		case HASH_LINEAR_DEPTH: handlerRenderPath3D_->debugMode = DEBUG_BUFFER::LINEAR_DEPTH; break;
		case HASH_WITHOUT_POSTPROCESSING: handlerRenderPath3D_->debugMode = DEBUG_BUFFER::WITHOUT_POSTPROCESSING; break;
		default: handlerRenderPath3D_->debugMode = DEBUG_BUFFER::NONE; break;
		}
	}
}