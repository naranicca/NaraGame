#pragma once

#include "../asset/AssetGuid.h"
#include "Component.h"

#include <NaraGame/assets/MiniJson.h>

#include <string>

namespace SceneEditor {

// Attaches a Lua script asset to an entity. M6c4 ships the data
// model + inspector wiring; M6c5 routes AnimationEvent dispatches
// through whichever ScriptComponent the firing entity carries +
// calls the named entry function instead of the global
// OnAnimationEvent fallback.
//
// `script` is the GUID of a `.lua` asset that ScriptHost::LoadFile
// has already executed at project-open time (see M6c3); this
// component just associates the script with the entity. The
// `entryFunction` lets the user point at a custom name when one
// script declares per-entity handlers like "OnFootstep_Player".
// An empty entry function name means the dispatch falls back to
// the global `OnAnimationEvent` -- same surface as the bootstrap.
class ScriptComponent final : public Component {
public:
    AssetGuid   script;
    std::string entryFunction;  // e.g. "OnFootstep_Player", empty = default

    static const char* StaticTypeName() { return "Script"; }
    const char* TypeName() const override { return StaticTypeName(); }

    void Serialize(NaraGame::MiniJson::Value& out) const override;
    void Deserialize(const NaraGame::MiniJson::Value& obj) override;
};

} // namespace SceneEditor
