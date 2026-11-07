// Minimal JSON value + parser + writer (no dependencies) for library/setlist/rig persistence.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace wr {
class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };
    Json() = default;
    Json (bool b) : t_ (Bool), b_ (b) {}
    Json (int v) : t_ (Number), n_ (v) {}
    Json (double v) : t_ (Number), n_ (v) {}
    Json (float v) : t_ (Number), n_ (v) {}
    Json (const char* s) : t_ (String), s_ (s) {}
    Json (std::string s) : t_ (String), s_ (std::move (s)) {}
    static Json array() { Json j; j.t_ = Array; return j; }
    static Json object() { Json j; j.t_ = Object; return j; }
    Type type() const { return t_; }
    bool isNull() const { return t_ == Null; } bool isArray() const { return t_ == Array; } bool isObject() const { return t_ == Object; }
    double num (double def = 0) const { return t_ == Number ? n_ : (t_ == Bool ? (b_ ? 1 : 0) : def); }
    int integer (int def = 0) const { return t_ == Number ? (int) std::lround (n_) : def; }
    bool boolean (bool def = false) const { return t_ == Bool ? b_ : (t_ == Number ? n_ != 0 : def); }
    const std::string& str() const { static const std::string e; return t_ == String ? s_ : e; }
    std::string str (const std::string& def) const { return t_ == String ? s_ : def; }
    // arrays
    size_t size() const { return t_ == Array ? a_.size() : (t_ == Object ? o_.size() : 0); }
    const Json& operator[] (size_t i) const { static const Json n; return (t_ == Array && i < a_.size()) ? a_[i] : n; }
    void push (Json v) { if (t_ != Array) { t_ = Array; a_.clear(); } a_.push_back (std::move (v)); }
    const std::vector<Json>& items() const { return a_; }
    // objects
    const Json& operator[] (const std::string& k) const { static const Json n; if (t_ != Object) return n; auto it = o_.find (k); return it == o_.end() ? n : it->second; }
    const Json& operator[] (const char* k) const { return (*this)[std::string (k)]; }
    Json& set (const std::string& k, Json v) { if (t_ != Object) { t_ = Object; o_.clear(); } o_[k] = std::move (v); return *this; }
    bool has (const std::string& k) const { return t_ == Object && o_.count (k) > 0; }
    const std::map<std::string, Json>& members() const { return o_; }

    std::string dump (int indent = 1) const { std::string out; write (out, indent, 0); return out; }
    static Json parse (const std::string& s, bool* ok = nullptr) { size_t i = 0; bool good = true; Json j = parseVal (s, i, good); ws (s, i); if (i != s.size()) good = false; if (ok) *ok = good; return good ? j : Json(); }

private:
    static void esc (std::string& o, const std::string& s) {
        o += '"';
        for (unsigned char c : s) { switch (c) { case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
            default: if (c < 0x20) { char b[8]; std::snprintf (b, sizeof b, "\\u%04x", c); o += b; } else o += (char) c; } }
        o += '"';
    }
    void write (std::string& o, int ind, int depth) const {
        auto nl = [&] (int d) { if (ind > 0) { o += '\n'; o.append ((size_t) (d * ind), ' '); } };
        switch (t_) {
            case Null: o += "null"; break; case Bool: o += b_ ? "true" : "false"; break;
            case Number: { char b[32]; if (std::floor (n_) == n_ && std::abs (n_) < 1e15) std::snprintf (b, sizeof b, "%lld", (long long) n_); else std::snprintf (b, sizeof b, "%.9g", n_); o += b; break; }
            case String: esc (o, s_); break;
            case Array: { o += '['; bool f = true; for (auto& v : a_) { if (! f) o += ','; f = false; nl (depth + 1); v.write (o, ind, depth + 1); } if (! a_.empty()) nl (depth); o += ']'; break; }
            case Object: { o += '{'; bool f = true; for (auto& kv : o_) { if (! f) o += ','; f = false; nl (depth + 1); esc (o, kv.first); o += ind > 0 ? ": " : ":"; kv.second.write (o, ind, depth + 1); } if (! o_.empty()) nl (depth); o += '}'; break; }
        }
    }
    static void ws (const std::string& s, size_t& i) { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    static void utf8 (std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char) cp; else if (cp < 0x800) { o += (char) (0xC0 | (cp >> 6)); o += (char) (0x80 | (cp & 63)); }
        else if (cp < 0x10000) { o += (char) (0xE0 | (cp >> 12)); o += (char) (0x80 | ((cp >> 6) & 63)); o += (char) (0x80 | (cp & 63)); }
        else { o += (char) (0xF0 | (cp >> 18)); o += (char) (0x80 | ((cp >> 12) & 63)); o += (char) (0x80 | ((cp >> 6) & 63)); o += (char) (0x80 | (cp & 63)); }
    }
    static Json parseVal (const std::string& s, size_t& i, bool& ok) {
        ws (s, i); if (i >= s.size()) { ok = false; return {}; }
        char c = s[i];
        if (c == '{') { ++i; Json o = object(); ws (s, i); if (i < s.size() && s[i] == '}') { ++i; return o; }
            while (ok) { ws (s, i); if (i >= s.size() || s[i] != '"') { ok = false; break; } Json k = parseVal (s, i, ok); ws (s, i); if (i >= s.size() || s[i] != ':') { ok = false; break; } ++i;
                Json v = parseVal (s, i, ok); o.set (k.str(), std::move (v)); ws (s, i); if (i < s.size() && s[i] == ',') { ++i; continue; } if (i < s.size() && s[i] == '}') { ++i; break; } ok = false; }
            return o; }
        if (c == '[') { ++i; Json a = array(); ws (s, i); if (i < s.size() && s[i] == ']') { ++i; return a; }
            while (ok) { a.push (parseVal (s, i, ok)); ws (s, i); if (i < s.size() && s[i] == ',') { ++i; continue; } if (i < s.size() && s[i] == ']') { ++i; break; } ok = false; }
            return a; }
        if (c == '"') { ++i; std::string o;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) { char e = s[++i];
                    if (e == 'n') o += '\n'; else if (e == 't') o += '\t'; else if (e == 'r') o += '\r'; else if (e == 'b') o += '\b'; else if (e == 'f') o += '\f';
                    else if (e == 'u' && i + 4 < s.size()) { unsigned cp = (unsigned) std::strtoul (s.substr (i + 1, 4).c_str(), nullptr, 16); i += 4;
                        if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') { unsigned lo = (unsigned) std::strtoul (s.substr (i + 3, 4).c_str(), nullptr, 16); cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
                        utf8 (o, cp); }
                    else o += e; ++i; }
                else o += s[i++]; }
            if (i >= s.size()) ok = false; else ++i; return Json (o); }
        if (s.compare (i, 4, "true") == 0) { i += 4; return Json (true); }
        if (s.compare (i, 5, "false") == 0) { i += 5; return Json (false); }
        if (s.compare (i, 4, "null") == 0) { i += 4; return {}; }
        char* end = nullptr; double v = std::strtod (s.c_str() + i, &end); if (end == s.c_str() + i) { ok = false; return {}; } i = (size_t) (end - s.c_str()); return Json (v);
    }
    Type t_ = Null; bool b_ = false; double n_ = 0; std::string s_; std::vector<Json> a_; std::map<std::string, Json> o_;
};
} // namespace wr
