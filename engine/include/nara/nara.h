#pragma once

// Umbrella header: pulls in the public engine API in one include.

#include "nara/core/Platform.h"
#include "nara/core/Application.h"
#include "nara/core/Log.h"
#include "nara/core/Assert.h"
#include "nara/core/Time.h"

#include "nara/assets/BMFont.h"
#include "nara/assets/Image.h"
#include "nara/assets/TextureLoader.h"
#include "nara/assets/TileLevel.h"

#include "nara/audio/AudioClip.h"
#include "nara/audio/AudioDevice.h"

#include "nara/io/ConfigFile.h"

#include "nara/math/Math.h"

#include "nara/platform/Window.h"
#include "nara/input/Input.h"

#include "nara/renderer/BitmapFont.h"
#include "nara/renderer/Camera2D.h"
#include "nara/renderer/Font.h"
#include "nara/renderer/Renderer2D.h"
#include "nara/renderer/SpriteSheet.h"
#include "nara/renderer/TextureAtlas.h"
#include "nara/renderer/TextureRegion.h"
#include "nara/renderer/Tilemap.h"
#include "nara/renderer/rhi/RHIDevice.h"
#include "nara/renderer/rhi/RHIResources.h"
#include "nara/renderer/rhi/RHITypes.h"

#include "nara/scene/Scene.h"
#include "nara/scene/SceneManager.h"
