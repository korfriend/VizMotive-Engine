#ifndef WICKEDENGINE
#define WICKEDENGINE
#define WICKED_ENGINE

// NOTE:
// The purpose of this file is to expose all engine features.
// It should be included in the engine's implementing application not the engine itself!
// It should be included in the precompiled header if available.

#include "CommonInclude.h"

// High-level interface:
#include "vzApplication.h"
#include "vzRenderPath.h"
#include "vzRenderPath2D.h"
#include "vzRenderPath3D.h"
#include "vzRenderPath3D_PathTracing.h"
#include "vzLoadingScreen.h"

// Engine-level systems
#include "vzVersion.h"
#include "vzPlatform.h"
#include "vzBacklog.h"
#include "vzPrimitive.h"
#include "vzImage.h"
#include "vzFont.h"
#include "vzSprite.h"
#include "vzSpriteFont.h"
#include "vzScene.h"
#include "vzECS.h"
#include "vzEmittedParticle.h"
#include "vzHairParticle.h"
#include "vzRenderer.h"
#include "vzMath.h"
#include "vzAudio.h"
#include "vzResourceManager.h"
#include "vzTimer.h"
#include "vzHelper.h"
#include "vzInput.h"
#include "vzRawInput.h"
#include "vzXInput.h"
#include "vzSDLInput.h"
#include "vzTextureHelper.h"
#include "vzRandom.h"
#include "vzColor.h"
#include "vzPhysics.h"
#include "vzEnums.h"
#include "vzInitializer.h"
#include "vzGraphics.h"
#include "vzGraphicsDevice.h"
#include "vzGUI.h"
#include "vzArchive.h"
#include "vzSpinLock.h"
#include "vzRectPacker.h"
#include "vzProfiler.h"
#include "vzOcean.h"
#include "vzFFTGenerator.h"
#include "vzArguments.h"
#include "vzGPUBVH.h"
#include "vzGPUSortLib.h"
#include "vzJobSystem.h"
#include "vzNetwork.h"
#include "vzEventHandler.h"
#include "vzShaderCompiler.h"
#include "vzCanvas.h"
#include "vzUnorderedMap.h"
#include "vzUnorderedSet.h"
#include "vzVector.h"
#include "vzNoise.h"
#include "vzConfig.h"
#include "vzTerrain.h"
#include "vzLocalization.h"
#include "vzVideo.h"
#include "vzVoxelGrid.h"
#include "vzPathQuery.h"

#ifdef PLATFORM_WINDOWS_DESKTOP
#pragma comment(lib,"Engine_Windows.lib")
#endif // PLATFORM_WINDOWS_DESKTOP

#ifdef PLATFORM_UWP
#pragma comment(lib,"Engine_UWP.lib")
#endif // PLATFORM_UWP

#endif // WICKEDENGINE
