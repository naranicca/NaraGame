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
    // Position of the node in the graph editor's local coordinate
    // space. Defaults to (0, 0); the editor auto-fans the nodes out
    // on first load when every state still sits at the origin.
    float       nodeX = 0.0f;
    float       nodeY = 0.0f;
};

// Parameter declared by the controller. AnimatorComponent stores the
// live value at runtime in a type-matched map; transitions read it +
// compare against their own per-type value to decide whether to fire.
struct AnimatorParameter {
    // Bool / Float / Int are regular sticky values; Trigger is a
    // one-shot bool: the runtime auto-resets the live value to false
    // the instant a transition fires that reads it. Live trigger
    // state still lives in AnimatorComponent::paramBools to keep the
    // storage path consistent.
    enum class Type { Bool, Float, Int, Trigger };
    std::string name;
    Type        type         = Type::Bool;
    bool        defaultBool  = false;
    float       defaultFloat = 0.0f;
    int         defaultInt   = 0;
};

// Edge in the state machine. fromIdx == -1 means "any state" (i.e.
// a global transition that fires regardless of where you currently
// are). Transitions are checked in declaration order each frame.
struct AnimatorTransition {
    enum class Op { Equals, NotEqual, Less, LessEqual, Greater, GreaterEqual };
    int         fromIdx    = -1;
    int         toIdx      =  0;
    std::string param;
    Op          op         = Op::Equals;
    bool        valueBool  = true;
    float       valueFloat = 0.0f;
    int         valueInt   = 0;
    // When `hasExitTime` is true, the transition only fires after the
    // source animation has played past `exitTime` of its total
    // duration (0..1 normalised). Pairs naturally with an empty
    // `param` field for "auto-progress to the next state" patterns,
    // and with a real condition for "wait at least this long before
    // re-evaluating" patterns.
    bool        hasExitTime = false;
    float       exitTime    = 0.0f;
    // Seconds spent blending into the target state. 0 = instant
    // swap (the M5c4 behaviour). During the blend the editor cross-
    // fades the SpriteRenderer's alpha; the target state takes over
    // at the midpoint.
    float       duration    = 0.0f;
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
