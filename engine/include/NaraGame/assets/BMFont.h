#pragma once

#include "NaraGame/renderer/Font.h"

#include <string>

namespace NaraGame {

class RHIDevice;

// Load an AngelCode BMFont in the text ".fnt" format, plus its page PNG (taken
// from the directory of `fntPath`). Returns an invalid Font on failure.
Font LoadBMFont(RHIDevice& device, const std::string& fntPath);

} // namespace NaraGame
