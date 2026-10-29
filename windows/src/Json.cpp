#include "Json.h"
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <system_error>
#include <utility>

namespace json {

namespace {

constexpr std::size_t MaxNestingDepth = 64;

struct Parser {
    const char* p;
    const char* end;
    bool ok = true;

    void skipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    }

    bool consume(char c) {
        skipWs();
        if (p < end && *p == c) { ++p; return true; }
        return false;
    }

    Value parseValue(std::size_t depth = 0) {
        skipWs();
        if (p >= end) { ok = false; return {}; }
        if ((*p == '{' || *p == '[') && depth >= MaxNestingDepth) {
            ok = false;
            return {};
        }
        switch (*p) {
        case '{': return parseObject(depth + 1);
        case '[': return parseArray(depth + 1);
        case '"': return Value(parseString());
        case 't': return parseLit("true", Value(true));
        case 'f': return parseLit("false", Value(false));
        case 'n': return parseLit("null", Value());
        default: return parseNumber();
        }
    }

    Value parseLit(const char* lit, Value v) {
        const std::size_t n = std::strlen(lit);
        if ((std::size_t)(end - p) >= n && std::memcmp(p, lit, n) == 0) { p += n; return v; }
        ok = false; return {};
    }

    bool consumeDigits() {
        const char* first = p;
        while (p < end && *p >= '0' && *p <= '9') ++p;
        return p != first;
    }

    Value parseNumber() {
        const char* first = p;
        if (p < end && *p == '-') ++p;
        if (p >= end) { ok = false; return {}; }
        if (*p == '0') {
            ++p;
            if (p < end && *p >= '0' && *p <= '9') { ok = false; return {}; }
        } else if (*p >= '1' && *p <= '9') consumeDigits();
        else { ok = false; return {}; }
        if (p < end && *p == '.') {
            ++p;
            if (!consumeDigits()) { ok = false; return {}; }
        }
        if (p < end && (*p == 'e' || *p == 'E')) {
            ++p;
            if (p < end && (*p == '+' || *p == '-')) ++p;
            if (!consumeDigits()) { ok = false; return {}; }
        }
        double d = 0;
        const auto parsed = std::from_chars(first, p, d, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != p || !std::isfinite(d)) {
            ok = false;
            return {};
        }
        return Value(d);
    }

    void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    unsigned parseHex4() {
        if (end - p < 4) { ok = false; return 0; }
        unsigned v = 0;
        for (int i = 0; i < 4; ++i, ++p) {
            char c = *p;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else { ok = false; return 0; }
        }
        return v;
    }

    void parseUnicodeEscape(std::string& out) {
        unsigned cp = parseHex4();
        if (!ok) return;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (end - p < 6 || p[0] != '\\' || p[1] != 'u') { ok = false; return; }
            p += 2;
            const unsigned lo = parseHex4();
            if (!ok || lo < 0xDC00 || lo > 0xDFFF) { ok = false; return; }
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            ok = false;
            return;
        }
        appendUtf8(out, cp);
    }

    void parseEscape(std::string& out) {
        if (p >= end) { ok = false; return; }
        switch (*p++) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': parseUnicodeEscape(out); break;
        default: ok = false; break;
        }
    }

    void parseRawUtf8(std::string& out) {
        const auto lead = static_cast<unsigned char>(*p);
        std::size_t length = 0;
        unsigned cp = 0;
        unsigned minimum = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2; cp = lead & 0x1F; minimum = 0x80;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3; cp = lead & 0x0F; minimum = 0x800;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4; cp = lead & 0x07; minimum = 0x10000;
        } else { ok = false; return; }
        if (static_cast<std::size_t>(end - p) < length) { ok = false; return; }
        for (std::size_t i = 1; i < length; ++i) {
            const auto next = static_cast<unsigned char>(p[i]);
            if ((next & 0xC0) != 0x80) { ok = false; return; }
            cp = (cp << 6) | (next & 0x3F);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            ok = false;
            return;
        }
        out.append(p, length);
        p += length;
    }

    std::string parseString() {
        std::string out;
        if (p >= end || *p != '"') { ok = false; return out; }
        ++p;
        while (ok && p < end && *p != '"') {
            const auto c = static_cast<unsigned char>(*p);
            if (c == '\\') {
                ++p;
                parseEscape(out);
            } else if (c < 0x20) ok = false;
            else if (c < 0x80) out += *p++;
            else parseRawUtf8(out);
        }
        if (ok && p < end && *p == '"') ++p; else ok = false;
        return out;
    }

    Value parseObject(std::size_t depth) {
        ++p; // '{'
        Object obj;
        skipWs();
        if (consume('}')) return Value(std::move(obj));
        while (ok) {
            skipWs();
            std::string key = parseString();
            if (!ok || !consume(':')) { ok = false; break; }
            obj[key] = parseValue(depth);
            if (!ok) break;
            if (consume(',')) continue;
            if (consume('}')) break;
            ok = false;
        }
        return Value(std::move(obj));
    }

    Value parseArray(std::size_t depth) {
        ++p; // '['
        Array arr;
        skipWs();
        if (consume(']')) return Value(std::move(arr));
        while (ok) {
            arr.push_back(parseValue(depth));
            if (!ok) break;
            if (consume(',')) continue;
            if (consume(']')) break;
            ok = false;
        }
        return Value(std::move(arr));
    }
};

void dumpValue(const Value& v, std::string& out) {
    switch (v.type()) {
    case Value::Type::Null: out += "null"; break;
    case Value::Type::Bool: out += v.asBool() ? "true" : "false"; break;
    case Value::Type::Number: {
        double d = v.asNumber();
        char buf[32];
        if (d == std::floor(d) && std::fabs(d) < 1e15)
            snprintf(buf, sizeof(buf), "%lld", (long long)d);
        else
            snprintf(buf, sizeof(buf), "%.17g", d);
        out += buf;
        break;
    }
    case Value::Type::String: out += Escape(v.asString()); break;
    case Value::Type::Object: {
        out += '{';
        bool first = true;
        for (auto& [k, val] : *v.object()) {
            if (!first) out += ',';
            first = false;
            out += Escape(k);
            out += ':';
            dumpValue(val, out);
        }
        out += '}';
        break;
    }
    case Value::Type::Array: {
        out += '[';
        bool first = true;
        for (auto& item : *v.array()) {
            if (!first) out += ',';
            first = false;
            dumpValue(item, out);
        }
        out += ']';
        break;
    }
    }
}

} // namespace

std::string Value::dump() const {
    std::string out;
    dumpValue(*this, out);
    return out;
}

Value Parse(const std::string& text, bool* okOut) {
    Parser parser{text.data(), text.data() + text.size()};
    Value v = parser.parseValue();
    parser.skipWs();
    parser.ok = parser.ok && parser.p == parser.end;
    if (okOut) *okOut = parser.ok;
    return parser.ok ? v : Value();
}

std::string Escape(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += (char)c;
            }
        }
    }
    out += '"';
    return out;
}

} // namespace json
