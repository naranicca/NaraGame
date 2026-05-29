#pragma once

#include "NaraGame/renderer/MSDFFont.h"

#include <string>

namespace NaraGame {

class RHIDevice;

// Loads an MSDF atlas produced by msdf-atlas-gen. Pass the .json path; the
// matching PNG is found automatically by swapping the extension. Returns an
// invalid (Valid() == false) MSDFFont on any failure -- check before use.
//
// The atlas texture is always loaded with Linear filtering: MSDF requires
// linear interpolation across the distance field to produce smooth edges at
// the threshold; Point sampling would re-introduce the blocky steps that the
// whole pipeline is here to eliminate.
MSDFFont LoadMSDFFont(RHIDevice& device, const std::string& jsonPath);

} // namespace NaraGame
