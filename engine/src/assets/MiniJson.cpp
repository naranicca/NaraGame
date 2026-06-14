#include "MiniJson.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace NaraGame {
namespace MiniJson {

namespace {

class Parser {
public:
    Parser(const char* p, size_t n) : m_start(p), m_p(p), m_end(p + n) {}

    Value Parse() {
        SkipWs();
        Value root = ParseValue();
        SkipWs();
        if (m_p != m_end) Throw("trailing characters");
        return root;
    }

    const std::string& Error() const { return m_error; }

private:
    [[noreturn]] void Throw(const char* what) {
        // Compose a position-aware error so the caller can locate the bad
        // token without re-scanning the source.
        size_t line = 1, col = 1;
        for (const char* q = m_start; q < m_p; ++q) {
            if (*q == '\n') { ++line; col = 1; } else { ++col; }
        }
        m_error = std::string("MiniJson: ") + what + " at line "
                + std::to_string(line) + ", col " + std::to_string(col);
        throw std::runtime_error(m_error);
    }

    void SkipWs() {
        while (m_p < m_end) {
            const char c = *m_p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++m_p;
            else break;
        }
    }

    bool Match(char c) {
        if (m_p < m_end && *m_p == c) { ++m_p; return true; }
        return false;
    }

    void Expect(char c) {
        if (!Match(c)) {
            char msg[64];
            std::snprintf(msg, sizeof(msg), "expected '%c'", c);
            Throw(msg);
        }
    }

    bool MatchLiteral(const char* lit) {
        const size_t n = std::strlen(lit);
        if (m_p + n > m_end) return false;
        for (size_t i = 0; i < n; ++i)
            if (m_p[i] != lit[i]) return false;
        m_p += n;
        return true;
    }

    Value ParseValue() {
        SkipWs();
        if (m_p == m_end) Throw("unexpected end of input");
        const char c = *m_p;
        if (c == '{') return ParseObject();
        if (c == '[') return ParseArray();
        if (c == '"') return ParseString();
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();
        if (MatchLiteral("true"))  { Value v; v.type = Value::Type::Bool;   v.boolValue = true;  return v; }
        if (MatchLiteral("false")) { Value v; v.type = Value::Type::Bool;   v.boolValue = false; return v; }
        if (MatchLiteral("null"))  { Value v; v.type = Value::Type::Null;                       return v; }
        Throw("expected value");
    }

    Value ParseObject() {
        Expect('{');
        Value v;
        v.type = Value::Type::ObjectT;
        SkipWs();
        if (Match('}')) return v;
        for (;;) {
            SkipWs();
            // Object keys must be strings -- glTF never uses anything else.
            if (m_p == m_end || *m_p != '"') Throw("expected string key");
            Value keyVal = ParseString();
            SkipWs();
            Expect(':');
            Value child = ParseValue();
            v.objectKeys.push_back(std::move(keyVal.stringValue));
            v.objectValues.push_back(std::move(child));
            SkipWs();
            if (Match(',')) continue;
            Expect('}');
            return v;
        }
    }

    Value ParseArray() {
        Expect('[');
        Value v;
        v.type = Value::Type::ArrayT;
        SkipWs();
        if (Match(']')) return v;
        for (;;) {
            Value child = ParseValue();
            v.arrayValue.push_back(std::move(child));
            SkipWs();
            if (Match(',')) continue;
            Expect(']');
            return v;
        }
    }

    Value ParseString() {
        Expect('"');
        Value v;
        v.type = Value::Type::String;
        std::string& out = v.stringValue;
        while (m_p < m_end) {
            const char c = *m_p++;
            if (c == '"') return v;
            if (c == '\\') {
                if (m_p == m_end) Throw("unterminated escape");
                const char e = *m_p++;
                switch (e) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        // Minimal \uXXXX handling: BMP only, no surrogate
                        // pairs. glTF strings don't usually need this but
                        // we parse it to keep simple files compatible.
                        if (m_p + 4 > m_end) Throw("short \\u escape");
                        unsigned int cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char h = m_p[i];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= unsigned(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= unsigned(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= unsigned(h - 'A' + 10);
                            else Throw("bad hex in \\u");
                        }
                        m_p += 4;
                        if (cp < 0x80) {
                            out.push_back(char(cp));
                        } else if (cp < 0x800) {
                            out.push_back(char(0xC0 | (cp >> 6)));
                            out.push_back(char(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(char(0xE0 |  (cp >> 12)));
                            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(char(0x80 |  (cp & 0x3F)));
                        }
                        break;
                    }
                    default: Throw("unknown escape"); break;
                }
            } else {
                out.push_back(c);
            }
        }
        Throw("unterminated string");
    }

    Value ParseNumber() {
        const char* start = m_p;
        if (*m_p == '-') ++m_p;
        while (m_p < m_end && *m_p >= '0' && *m_p <= '9') ++m_p;
        bool isFloat = false;
        if (m_p < m_end && *m_p == '.') {
            isFloat = true;
            ++m_p;
            while (m_p < m_end && *m_p >= '0' && *m_p <= '9') ++m_p;
        }
        if (m_p < m_end && (*m_p == 'e' || *m_p == 'E')) {
            isFloat = true;
            ++m_p;
            if (m_p < m_end && (*m_p == '+' || *m_p == '-')) ++m_p;
            while (m_p < m_end && *m_p >= '0' && *m_p <= '9') ++m_p;
        }
        (void)isFloat;  // both ints and floats are stored as double
        // strtod copes with the prefix-form we parsed because we stopped
        // exactly at the first non-numeric byte.
        char* endptr = nullptr;
        const double d = std::strtod(start, &endptr);
        if (endptr != m_p) Throw("bad number");
        Value v;
        v.type = Value::Type::Number;
        v.numberValue = d;
        return v;
    }

    const char* m_start = nullptr;
    const char* m_p     = nullptr;
    const char* m_end   = nullptr;
    std::string m_error;
};

} // namespace

Value Parse(const std::string& source, std::string* outError) {
    Parser p(source.data(), source.size());
    try {
        return p.Parse();
    } catch (const std::exception&) {
        if (outError) *outError = p.Error();
        return Value{};
    }
}

} // namespace MiniJson
} // namespace NaraGame
