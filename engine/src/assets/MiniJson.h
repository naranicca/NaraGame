#pragma once

// Tiny recursive-descent JSON parser, focused on what the glTF loader
// needs: nested objects, arrays, numbers (int + float), strings, and the
// keywords true / false / null. No comments. The parser is internal to
// the engine -- not part of the public API.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace NaraGame {
namespace MiniJson {

struct Value {
    enum class Type { Null, Bool, Number, String, ArrayT, ObjectT };

    Type        type = Type::Null;
    bool        boolValue   = false;
    double      numberValue = 0.0;
    std::string stringValue;
    // Both array and object are vectors of values; object pairs the keys
    // alongside in `objectKeys`. Using parallel vectors keeps Value
    // self-contained (no shared_ptr / unique_ptr indirection) while still
    // allowing recursion via std::vector's incomplete-type guarantee.
    std::vector<Value>       arrayValue;
    std::vector<std::string> objectKeys;
    std::vector<Value>       objectValues;

    bool IsObject() const { return type == Type::ObjectT; }
    bool IsArray()  const { return type == Type::ArrayT; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }

    // Look up a key in an object. Returns nullptr if the key is missing
    // or this isn't an object. The string view is borrowed -- no copy.
    const Value* Find(const char* key) const {
        if (type != Type::ObjectT) return nullptr;
        for (size_t i = 0; i < objectKeys.size(); ++i)
            if (objectKeys[i] == key) return &objectValues[i];
        return nullptr;
    }

    // Convenience accessors that fall back to a default when the lookup
    // misses or the type is wrong -- handy when walking glTF where most
    // fields are optional.
    int    GetInt(const char* key, int def = 0) const {
        const Value* v = Find(key);
        if (!v || v->type != Type::Number) return def;
        return int(v->numberValue);
    }
    double GetNumber(const char* key, double def = 0.0) const {
        const Value* v = Find(key);
        if (!v || v->type != Type::Number) return def;
        return v->numberValue;
    }
    const std::string& GetString(const char* key,
                                 const std::string& def = std::string{}) const {
        const Value* v = Find(key);
        static const std::string empty;
        if (!v || v->type != Type::String) return def.empty() ? empty : def;
        return v->stringValue;
    }
};

// Parse `source`. On a parse failure, returns a Null value and sets
// `outError` to a short diagnostic when non-null. Success: returns the
// root value (typically an object for glTF).
Value Parse(const std::string& source, std::string* outError = nullptr);

} // namespace MiniJson
} // namespace NaraGame
