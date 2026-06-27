#pragma once

#include "AssetGuid.h"

#include <NaraGame/assets/MiniJson.h>

#include <string>
#include <vector>

namespace SceneEditor {

// One animation in the controller's state machine. Each state names a
// sprite-animation asset to play while the state is active; `speed`
// scales the playback delta (1.0 = normal).
struct AnimatorState {
    std::string name;
    AssetGuid   animation;
    float       speed = 1.0f;
};

// Bool parameter declared by the controller. AnimatorComponent stores
// the live value at runtime; transitions read it to decide whether to
// fire. M5c4 ships bool-only parameters; the JSON schema reserves
// space for float / int / trigger to land later.
struct AnimatorParameter {
    std::string name;
    bool        defaultValue = false;
};

// Edge in the state machine. fromIdx == -1 means "any state" (i.e.
// a global transition that fires regardless of where you currently
// are). Transitions are checked in declaration order each frame.
struct AnimatorTransition {
    int         fromIdx  = -1;
    int         toIdx    =  0;
    std::string param;
    bool        expected = true;
};

// .ncontroller asset: a small animation state machine. The runtime
// owns currentState + parameter values; the controller asset is
// read-only data describing what's allowed.
class AnimatorControllerAsset {
public:
    std::vector<AnimatorState>      states;
    int                             defaultState = 0;
    std::vector<AnimatorParameter>  parameters;
    std::vector<AnimatorTransition> transitions;

    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

    void FromJson(const NaraGame::MiniJson::Value& root);
    NaraGame::MiniJson::Value ToJson() const;
};

} // namespace SceneEditor
