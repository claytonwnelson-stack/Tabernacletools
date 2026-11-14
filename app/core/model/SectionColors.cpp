#include "SectionColors.h"
#include "Song.h"
#include <cctype>

namespace wr {
const std::vector<PaletteColor>& sectionPalette() {
    static const std::vector<PaletteColor> p = {
        { "orange", "Orange", 0xfff5a524 }, { "pink", "Pink", 0xffff5fae }, { "blue", "Blue", 0xff38bdf8 }, { "purple", "Purple", 0xffa78bfa },
        { "green", "Green", 0xff4ade80 }, { "red", "Red", 0xfff87171 }, { "yellow", "Yellow", 0xfffacc15 }, { "teal", "Teal", 0xff2dd4bf },
        { "lime", "Lime", 0xffa3e635 }, { "indigo", "Indigo", 0xff818cf8 }, { "white", "White", 0xffe8eaf0 }, { "grey", "Grey", 0xff8a93a8 } };
    return p;
}
const std::vector<std::string>& sectionKinds() { static const std::vector<std::string> k = { "verse", "prechorus", "chorus", "bridge", "tag", "intro", "outro", "interlude", "other" }; return k; }
std::string kindDisplayName (const std::string& k) {
    if (k == "prechorus") return "Pre-Chorus"; if (k == "other") return "Other / custom";
    std::string s = k; if (! s.empty()) s[0] = (char) std::toupper ((unsigned char) s[0]); return s;
}
std::string builtinKindColor (const std::string& k) {
    if (k == "verse") return "orange"; if (k == "prechorus") return "pink"; if (k == "chorus" || k == "tag") return "blue"; if (k == "bridge") return "purple"; return "grey";
}
std::string kindColor (const SectionColorMap& d, const std::string& kind) {
    auto it = d.find (kind.empty() ? "other" : kind); if (it != d.end()) { auto n = normalizeColor (it->second); if (! n.empty()) return n; }
    return builtinKindColor (kind);
}
std::string normalizeColor (const std::string& in) {
    std::string s; for (char c : in) if (! std::isspace ((unsigned char) c)) s += (char) std::tolower ((unsigned char) c);
    if (s == "gray") s = "grey";
    for (auto& p : sectionPalette()) if (s == p.name) return s;
    if (s.size() == 4 && s[0] == '#') s = std::string ("#") + s[1] + s[1] + s[2] + s[2] + s[3] + s[3];
    if (s.size() != 7 || s[0] != '#') return {};
    for (size_t i = 1; i < 7; ++i) if (! std::isxdigit ((unsigned char) s[i])) return {};
    for (auto& c : s) c = (char) std::toupper ((unsigned char) c);
    return s;
}
bool colorArgb (const std::string& c, uint32_t& out) {
    auto n = normalizeColor (c); if (n.empty()) return false;
    for (auto& p : sectionPalette()) if (n == p.name) { out = p.argb; return true; }
    out = 0xff000000u | (uint32_t) std::stoul (n.substr (1), nullptr, 16); return true;
}
uint32_t colorArgbOr (const std::string& c, uint32_t fb) { uint32_t v; return colorArgb (c, v) ? v : fb; }
std::string effectiveSectionColor (const Section& s, const SectionColorMap& d) {
    auto o = normalizeColor (s.customColor); return o.empty() ? kindColor (d, s.kind) : o;
}
void applySectionColors (Chart& c, const SectionColorMap& d) { for (auto& s : c.sections) s.color = effectiveSectionColor (s, d); }
void clearSectionColorOverrides (Chart& c) { for (auto& s : c.sections) s.customColor.clear(); }
bool setSectionColorOverride (Chart& c, int i, const std::string& color) {
    if (i < 0 || i >= (int) c.sections.size()) return false;
    if (color.empty()) { c.sections[(size_t) i].customColor.clear(); return true; }
    auto n = normalizeColor (color); if (n.empty()) return false; c.sections[(size_t) i].customColor = n; return true;
}
SectionSwitchTargets sectionSwitchTargets (const Chart& c, int cur, int pass) {
    SectionSwitchTargets t; const int n = (int) c.sections.size(); if (n == 0 || cur < 0 || cur >= n) return t;
    t.prev = pass > 1 ? cur : (cur > 0 ? cur - 1 : -1);
    t.next = pass < c.sections[(size_t) cur].repeat ? cur : (cur + 1 < n ? cur + 1 : -1);
    for (int i = cur + 1; i < n && t.chorus < 0; ++i) if (c.sections[(size_t) i].kind == "chorus") t.chorus = i;
    for (int i = n - 1; i >= 0 && t.chorus < 0; --i) if (c.sections[(size_t) i].kind == "chorus") t.chorus = i;
    return t;
}
} // namespace wr
