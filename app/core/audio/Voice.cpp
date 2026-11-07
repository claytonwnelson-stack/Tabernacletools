#include "Voice.h"
#include <cctype>
#include <cstring>
#include <cmath>
#include <sstream>

namespace wr {
std::string numberWord (int n) { static const char* w[] = { "", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve" }; return n >= 1 && n <= 12 ? w[n] : std::string(); }

std::vector<std::string> cueWords (const std::string& in) {
    std::string s; for (char c : in) s += (char) std::tolower ((unsigned char) c);
    // normalise pre/post-chorus spellings and separators
    auto rep = [&] (const std::string& a, const std::string& b) { size_t p = 0; while ((p = s.find (a, p)) != std::string::npos) { s.replace (p, a.size(), b); p += b.size(); } };
    rep ("prechorus", "pre-chorus"); rep ("pre chorus", "pre-chorus"); rep ("pre - chorus", "pre-chorus"); rep ("postchorus", "post-chorus"); rep ("post chorus", "post-chorus");
    rep ("a cappella", "acapella"); rep ("a capella", "acapella"); rep ("acappella", "acapella");
    for (auto& c : s) if (c == '_' || c == '/' || c == ':' || c == '(' || c == ')' || c == ',' || c == '.') c = ' ';
    std::vector<std::string> out; std::istringstream is (s); std::string t;
    while (is >> t) {
        if (t.size() >= 2 && (t[0] == 'x' || t[0] == '*') && std::isdigit ((unsigned char) t[1])) continue;            // "x2" repeat marks
        if (t == "-" || t == "&") continue;
        // split letters/digits: "verse1" -> verse, 1
        std::string word, digits;
        for (char c : t) { if (std::isdigit ((unsigned char) c)) digits += c; else { if (! digits.empty()) { break; } word += c; } }
        while (! word.empty() && word.back() == '-') word.pop_back(); while (! word.empty() && word.front() == '-') word.erase (0, 1);
        if (word == "inst" || word == "instr") word = "instrumental"; if (word == "v") word = "verse"; if (word == "ch") word = "chorus"; if (word == "br") word = "bridge"; if (word == "pc") word = "pre-chorus";
        if (! word.empty()) out.push_back (word);
        if (! digits.empty()) { int n = std::atoi (digits.c_str()); auto nw = numberWord (n); if (! nw.empty()) out.push_back (nw); else for (char d : digits) { auto dw = d == '0' ? std::string ("zero") : numberWord (d - '0'); out.push_back (dw); } }
    }
    return out;
}
std::string spokenName (const std::string& n) {
    auto w = cueWords (n); std::string o; for (size_t i = 0; i < w.size(); ++i) { if (i) o += ' '; o += w[i]; } if (! o.empty()) o[0] = (char) std::toupper ((unsigned char) o[0]); return o;
}

bool decodeWav (const uint8_t* d, size_t n, std::vector<float>& out, double& sr) {
    auto u16 = [&] (size_t p) { return (uint32_t) d[p] | ((uint32_t) d[p + 1] << 8); }; auto u32 = [&] (size_t p) { return u16 (p) | (u16 (p + 2) << 16); };
    if (n < 12 || std::memcmp (d, "RIFF", 4) || std::memcmp (d + 8, "WAVE", 4)) return false;
    int fmt = 0, ch = 0, bits = 0; sr = 0; size_t p = 12;
    while (p + 8 <= n) {
        uint32_t len = u32 (p + 4); size_t body = p + 8; if (body + len > n) len = (uint32_t) (n - body);
        if (! std::memcmp (d + p, "fmt ", 4) && len >= 16) { fmt = (int) u16 (body); ch = (int) u16 (body + 2); sr = (double) u32 (body + 4); bits = (int) u16 (body + 14); if (fmt == 0xFFFE && len >= 26) fmt = (int) u16 (body + 24); }
        else if (! std::memcmp (d + p, "data", 4) && ch > 0 && bits > 0) {
            const int bps = bits / 8, frame = bps * ch; size_t frames = len / (size_t) frame; out.assign (frames, 0.0f);
            for (size_t f = 0; f < frames; ++f) { float acc = 0; for (int c = 0; c < ch; ++c) { const uint8_t* q = d + body + f * (size_t) frame + (size_t) (c * bps); float v = 0;
                    if (fmt == 3 && bits == 32) { float x; std::memcpy (&x, q, 4); v = x; }
                    else if (bits == 16) v = (float) (int16_t) (q[0] | (q[1] << 8)) / 32768.0f;
                    else if (bits == 24) { int32_t x = (q[0] << 8) | (q[1] << 16) | (q[2] << 24); v = (float) x / 2147483648.0f; }
                    else if (bits == 32) { int32_t x; std::memcpy (&x, q, 4); v = (float) x / 2147483648.0f; }
                    else if (bits == 8) v = ((float) q[0] - 128.0f) / 128.0f;
                    acc += v; } out[f] = acc / (float) ch; }
            return sr > 0 && ! out.empty();
        }
        p = body + len + (len & 1);
    }
    return false;
}

void VoiceBank::resample (Src& s, double fs) {
    if (s.sr <= 0 || s.raw.empty()) { s.out.clear(); return; }
    const double ratio = s.sr / fs; const size_t n = (size_t) std::floor ((double) (s.raw.size() - 1) / ratio) + 1; s.out.resize (n);
    for (size_t i = 0; i < n; ++i) { double x = (double) i * ratio; size_t a = (size_t) x; double f = x - (double) a; float y0 = s.raw[a], y1 = a + 1 < s.raw.size() ? s.raw[a + 1] : y0; s.out[i] = (float) (y0 + (y1 - y0) * f); }
}
void VoiceBank::addWord (const std::string& w, std::vector<float> m, double sr) { auto& s = words_[w]; s.raw = std::move (m); s.sr = sr; resample (s, fs_); }
void VoiceBank::addPhrase (const std::string& k, std::vector<float> m, double sr) { auto& s = phrases_[k]; s.raw = std::move (m); s.sr = sr; resample (s, fs_); }
void VoiceBank::prepare (double fs) { fs_ = fs; for (auto& kv : words_) resample (kv.second, fs); for (auto& kv : phrases_) resample (kv.second, fs); }
bool VoiceBank::covers (const std::string& n) const { if (hasPhrase (n)) return true; auto w = cueWords (n); if (w.empty()) return false; for (auto& x : w) if (! hasWord (x)) return false; return true; }
std::vector<float> VoiceBank::phrase (const std::string& n) const {
    auto w = cueWords (n); bool all = ! w.empty(); for (auto& x : w) if (! hasWord (x)) all = false;
    if (! all) { auto it = phrases_.find (n); if (it != phrases_.end()) return it->second.out; }
    std::vector<float> o; const size_t gap = (size_t) (0.045 * fs_);
    for (auto& x : w) { auto it = words_.find (x); if (it == words_.end()) continue; if (! o.empty()) o.insert (o.end(), gap, 0.0f); o.insert (o.end(), it->second.out.begin(), it->second.out.end()); }
    return o;
}
VoiceClip VoiceBank::number (int n) const { auto it = words_.find (numberWord (n)); if (it == words_.end()) return {}; return { it->second.out.data(), (int) it->second.out.size() }; }
} // namespace wr
