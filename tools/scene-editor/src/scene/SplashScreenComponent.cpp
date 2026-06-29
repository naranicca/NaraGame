#include "SplashScreenComponent.h"

#include "../asset/AssetGuid.h"

namespace SceneEditor {

using NaraGame::MiniJson::Value;

void SplashScreenComponent::Serialize(Value& out) const {
    out.Add("texture",   Value::MakeString(texture.ToString()));
    out.Add("nextScene", Value::MakeString(nextScene));
    out.Add("fadeIn",    Value::MakeNumber(fadeIn));
    out.Add("hold",      Value::MakeNumber(hold));
    out.Add("fadeOut",   Value::MakeNumber(fadeOut));
    out.Add("priority",  Value::MakeNumber(priority));
    Value bg = Value::MakeObject();
    bg.Add("r", Value::MakeNumber(background.r));
    bg.Add("g", Value::MakeNumber(background.g));
    bg.Add("b", Value::MakeNumber(background.b));
    bg.Add("a", Value::MakeNumber(background.a));
    out.Add("background", std::move(bg));
    Value tn = Value::MakeObject();
    tn.Add("r", Value::MakeNumber(tint.r));
    tn.Add("g", Value::MakeNumber(tint.g));
    tn.Add("b", Value::MakeNumber(tint.b));
    tn.Add("a", Value::MakeNumber(tint.a));
    out.Add("tint", std::move(tn));
}

void SplashScreenComponent::Deserialize(const Value& obj) {
    texture   = AssetGuid::FromString(obj.GetString("texture", ""));
    nextScene = obj.GetString("nextScene", "");
    fadeIn    = float(obj.GetNumber("fadeIn",  0.5));
    hold      = float(obj.GetNumber("hold",    1.5));
    fadeOut   = float(obj.GetNumber("fadeOut", 0.5));
    priority  = obj.GetInt   ("priority", 0);
    if (fadeIn  < 0.0f) fadeIn  = 0.0f;
    if (hold    < 0.0f) hold    = 0.0f;
    if (fadeOut < 0.0f) fadeOut = 0.0f;
    if (const Value* bg = obj.Find("background")) {
        if (bg->IsObject()) {
            background.r = float(bg->GetNumber("r", background.r));
            background.g = float(bg->GetNumber("g", background.g));
            background.b = float(bg->GetNumber("b", background.b));
            background.a = float(bg->GetNumber("a", background.a));
        }
    }
    if (const Value* tn = obj.Find("tint")) {
        if (tn->IsObject()) {
            tint.r = float(tn->GetNumber("r", tint.r));
            tint.g = float(tn->GetNumber("g", tint.g));
            tint.b = float(tn->GetNumber("b", tint.b));
            tint.a = float(tn->GetNumber("a", tint.a));
        }
    }
    _elapsed    = 0.0f;
    _completed  = false;
    _everPlayed = false;
}

} // namespace SceneEditor
