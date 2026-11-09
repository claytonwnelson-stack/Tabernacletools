// Tabernacle Tools v0.10 - Workshop "pedal face" descriptors: colour, the 2-4 main knobs shown big, a one-line description and a
// suggested footswitch action. Pure data (no JUCE) so it is unit-tested. Colours are our own (no copied trade dress).
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace wr {
struct PedalFace { std::string type; uint32_t argb; std::vector<std::string> mainParams; std::string blurb, footswitch; };
const PedalFace* pedalFace (const std::string& type); // nullptr = generic layout
const std::vector<PedalFace>& pedalFaces();
// Chain-tile identity: a colour and a 2-4 letter tag. Uses the face colour when one exists.
struct PedalLook { uint32_t argb; const char* tag; };
PedalLook pedalLook (const std::string& type);
} // namespace wr
