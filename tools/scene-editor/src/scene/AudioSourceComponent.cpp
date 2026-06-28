#include "AudioSourceComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void AudioSourceComponent::Serialize(Value& out) const {
    out.Add("clip",        Value::MakeString(clip.ToString()));
    out.Add("volume",      Value::MakeNumber(volume));
    out.Add("playOnStart", Value::MakeBool(playOnStart));
    out.Add("loop",        Value::MakeBool(loop));
}

void AudioSourceComponent::Deserialize(const Value& obj) {
    clip        = AssetGuid::FromString(obj.GetString("clip", ""));
    volume      = float(obj.GetNumber("volume", 1.0));
    playOnStart = obj.GetBool("playOnStart", true);
    loop        = obj.GetBool("loop", false);
    _started    = false;
}

} // namespace SceneEditor
