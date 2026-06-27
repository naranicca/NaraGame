#pragma once

// Tiny recursive-descent JSON parser + writer. Covers what the engine
// needs across asset loading + editor serialization: nested objects /
// arrays, numbers (int + float), strings, and the keywords true /
// false / null. No comments. The parser was originally written for the
// glTF loader; it's now exposed publicly so editor tools (project
// metadata, scene save/load, asset .meta files) can reuse it.

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
    bool IsBool()   const { return type == Type::Bool;   }
    bool IsNull()   const { return type == Type::Null;   }

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
    bool GetBool(const char* key, bool def = false) const {
        const Value* v = Find(key);
        if (!v || v->type != Type::Bool) return def;
        return v->boolValue;
    }
    const std::string& GetString(const char* key,
                                 const std::string& def = std::string{}) const {
        const Value* v = Find(key);
        static const std::string empty;
        if (!v || v->type != Type::String) return def.empty() ? empty : def;
        return v->stringValue;
    }

    // --- Builders for the writer side. ---
    //
    // Editor / serialization code builds a Value tree top-down and
    // hands it to Write() at the end. The Make* helpers return rvalues
    // so the typical use is `obj.Add("key", Value::MakeString("foo"))`.
    // Add() / Push() take a Value by value and move it into the parent.

    static Value MakeNull()                 { Value v; v.type = Type::Null;   return v; }
    static Value MakeBool(bool b)           { Value v; v.type = Type::Bool;   v.boolValue   = b; return v; }
    static Value MakeNumber(double n)       { Value v; v.type = Type::Number; v.numberValue = n; return v; }
    static Value MakeString(std::string s)  { Value v; v.type = Type::String; v.stringValue = std::move(s); return v; }
    static Value MakeArray()                { Value v; v.type = Type::ArrayT; return v; }
    static Value MakeObject()               { Value v; v.type = Type::ObjectT; return v; }

    // Append to an array. Caller is responsible for the Value already
    // being an array (this->type == ArrayT); we promote it implicitly
    // if it's still Null to make the first push ergonomic.
    void Push(Value v) {
        if (type == Type::Null) type = Type::ArrayT;
        arrayValue.push_back(std::move(v));
    }

    // Add a (key, value) pair to an object. Last write wins if `key`
    // already exists (we don't dedupe -- callers don't double-add).
    // Promotes Null to ObjectT the same way Push does.
    void Add(std::string key, Value v) {
        if (type == Type::Null) type = Type::ObjectT;
        objectKeys.push_back(std::move(key));
        objectValues.push_back(std::move(v));
    }
};

// Parse `source`. On a parse failure, returns a Null value and sets
// `outError` to a short diagnostic when non-null. Success: returns the
// root value (typically an object for glTF).
Value Parse(const std::string& source, std::string* outError = nullptr);

// Serialise a Value tree to a JSON string. `pretty` adds a 2-space
// indent + newlines; `false` packs everything onto one line. Numbers
// are emitted as their shortest round-trippable form.
std::string Write(const Value& root, bool pretty = true);

} // namespace MiniJson
} // namespace NaraGame
