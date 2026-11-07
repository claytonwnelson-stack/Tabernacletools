// v0.8 editable section colours (core, every app).
// A colour is a palette name ("orange", "teal", ...) or "#RRGGBB". Each section kind has a global default (Settings >
// Section colours), and a song can override any section (stored on Section::customColor, ChordPro {x_section_color: Verse 1=red}).
// Section::color is the effective colour the UI draws; LiveSession / leader sync bake it with applySectionColors().
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace wr {
struct Chart; struct Section;

struct PaletteColor { const char* name; const char* label; uint32_t argb; };
const std::vector<PaletteColor>& sectionPalette();                 // 12 stage-readable colours on the dark theme
using SectionColorMap = std::map<std::string, std::string>;        // kind -> colour (only kinds changed from the built-in default)
const std::vector<std::string>& sectionKinds();                    // verse, prechorus, chorus, bridge, tag, intro, outro, interlude, other
std::string kindDisplayName (const std::string& kind);             // "Pre-Chorus"
std::string builtinKindColor (const std::string& kind);            // v0.7 colours: verse orange, prechorus pink, chorus blue, bridge purple, tag blue, else grey
std::string kindColor (const SectionColorMap& defaults, const std::string& kind);
std::string normalizeColor (const std::string&);                   // palette name (any case) or #RGB/#RRGGBB -> "teal" / "#FF8800"; "" if not a colour
bool colorArgb (const std::string& color, uint32_t& argb);         // false = not a colour (callers fall back to grey)
uint32_t colorArgbOr (const std::string& color, uint32_t fallback);
std::string effectiveSectionColor (const Section&, const SectionColorMap& defaults);
void applySectionColors (Chart&, const SectionColorMap& defaults); // sets every section's color = override or kind default
void clearSectionColorOverrides (Chart&);                          // reset a song to the defaults
bool setSectionColorOverride (Chart&, int sectionIndex, const std::string& color); // "" clears; false = bad index / colour
// v0.12 footswitch panels: the section each SECTIONS-unit switch would go to (same rules as LiveSession: repeats first,
// chorus = next chorus after `cur`, else the last chorus). -1 = nothing (first/last section, no chorus).
struct SectionSwitchTargets { int prev = -1, next = -1, chorus = -1; };
SectionSwitchTargets sectionSwitchTargets (const Chart&, int cur, int pass);
} // namespace wr
