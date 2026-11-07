// Colours and drawing helpers matching mockup/common.css.
#pragma once
#include <JuceHeader.h>
#include "model/SectionColors.h"

namespace wr::theme {
const juce::Colour bg { 0xff0d0f14 }, bar { 0xff151922 }, line { 0xff262c3a }, panel { 0xff141821 }, card { 0xff1a1f2b }, cardBorder { 0xff2a3142 },
    btn { 0xff222838 }, btnBorder { 0xff333b50 }, text { 0xffe8eaf0 }, dim { 0xff8a93a8 }, dimmer { 0xff6b7489 },
    orange { 0xfff5a524 }, pink { 0xffff5fae }, blue { 0xff38bdf8 }, green { 0xff4ade80 }, purple { 0xffa78bfa }, well { 0xff0b0d12 };
juce::Colour accent (const juce::String& name);       // palette name ("orange", "teal"...) or "#RRGGBB"; other -> dim
inline juce::Colour colourOf (const std::string& c) { return juce::Colour (wr::colorArgbOr (c, 0xff8a93a8)); }
inline juce::Colour inkOn (juce::Colour bgc) { return bgc.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff111111) : juce::Colours::white; } // readable text on a filled chip
juce::Font font (float h, bool bold = false);
juce::Font mono (float h, bool bold = false);
void pill (juce::Graphics&, juce::Rectangle<float>, juce::Colour fill, juce::Colour border, float radius = 8.0f);
void led (juce::Graphics&, juce::Point<float> c, bool on);
void label (juce::Graphics&, const juce::String&, juce::Rectangle<int>, juce::Colour = dimmer, float h = 12.0f, juce::Justification j = juce::Justification::centredLeft);

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
};
}
