#include "AudioSourceComponent.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void AudioSourceComponent::Serialize(Value& out) const {
    out.Add("clip",        Value::MakeString(clip.ToString()));
    out.Add("volume",      Value::MakeNumber(volume));
    out.Add("playOnStart", Value::MakeBool(playOnStart));
    out.Add("loop",        Value::MakeBool(loop));
    out.Add("is3D",        Value::MakeBool(is3D));
    out.Add("minDistance", Value::MakeNumber(minDistance));
    out.Add("maxDistance", Value::MakeNumber(maxDistance));
}

void AudioSourceComponent::Deserialize(const Value& obj) {
    clip        = AssetGuid::FromString(obj.GetString("clip", ""));
    volume      = float(obj.GetNumber("volume", 1.0));
    playOnStart = obj.GetBool("playOnStart", true);
    loop        = obj.GetBool("loop", false);
    is3D        = obj.GetBool("is3D", false);
    minDistance = float(obj.GetNumber("minDistance", 1.0));
    maxDistance = float(obj.GetNumber("maxDistance", 10.0));
    _started    = false;
}

} // namespace SceneEditor
