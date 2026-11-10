// Bearing Live palette. Change the k* values to retheme the app. Song section colours are NOT here;
// they live in SectionColors and must stay independent of this chrome.
#pragma once
#include <JuceHeader.h>
#include "model/SectionColors.h"

namespace wr::theme {
inline constexpr juce::uint32 kBg         = 0xff04122C; // deep navy, main background
inline constexpr juce::uint32 kRaised     = 0xff0A1F3F; // bars, panels, dialogs
inline constexpr juce::uint32 kCard       = 0xff12294D; // cards, buttons, fields that sit up
inline constexpr juce::uint32 kLine       = 0xff1E3A5F; // borders
inline constexpr juce::uint32 kTeal       = 0xff1A7184; // primary accent fill
inline constexpr juce::uint32 kTealBright = 0xff2FA3B5; // accent that has to read as text or a highlight on navy
inline constexpr juce::uint32 kText       = 0xffFFFFFF;
inline constexpr juce::uint32 kText70     = 0xffB3B3B3; // about 70% white
inline constexpr juce::uint32 kMeter      = 0xff4ade80; // level meters and in-tune only
inline constexpr juce::uint32 kMeterHot   = 0xfff5a524; // hot meter segment
inline constexpr juce::uint32 kClip       = 0xfff87171; // clip / error
inline constexpr juce::uint32 kWarn       = 0xfffacc15; // warning

const juce::Colour bg { kBg }, well { kBg }, bar { kRaised }, panel { kRaised },
    card { kCard }, btn { kCard },
    line { kLine }, cardBorder { kLine }, btnBorder { kLine },
    text { kText }, dim { kText70 }, dimmer { 0xff8C8C8C },
    accentFill { kTeal }, accentText { kTealBright }, onAccent { kText },
    // Older call sites used these names for chrome (tabs, selection, drag). They follow the teal accent.
    orange { kTeal }, pink { kTealBright }, blue { kTealBright }, purple { kTeal },
    green { kMeter }, meterHot { kMeterHot }, clip { kClip }, warn { kWarn };

juce::Colour accent (const juce::String& name);       // section-palette name ("orange", "teal"...) or "#RRGGBB"; other -> dim. Not the UI accent.
inline juce::Colour colourOf (const std::string& c) { return juce::Colour (wr::colorArgbOr (c, 0xff8a93a8)); }
inline juce::Colour inkOn (juce::Colour bgc) { return bgc.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff111111) : juce::Colours::white; } // readable text on a filled chip (section colours included)
juce::Font font (float h, bool bold = false);
juce::Font mono (float h, bool bold = false);
void pill (juce::Graphics&, juce::Rectangle<float>, juce::Colour fill, juce::Colour border, float radius = 8.0f);
void led (juce::Graphics&, juce::Point<float> c, bool on);
// Small Bearing Live compass (ring, needle, three level bars). Crisp at about 18-22 pt; no image asset.
inline void paintBearingMark (juce::Graphics& g, juce::Rectangle<float> area) {
    const float d = juce::jmin (area.getWidth(), area.getHeight());
    const auto box = juce::Rectangle<float> (d, d).withCentre (area.getCentre());
    const auto c = box.getCentre();
    const float s = juce::jmax (1.0f, d / 22.0f);
    auto arm = [&] (float ang, float len) {
        return c + juce::Point<float> (std::sin (ang) * len, -std::cos (ang) * len);
    };
    auto perp = [&] (float ang) { return juce::Point<float> (std::cos (ang), std::sin (ang)); };
    auto point = [&] (float ang, float len, float halfW) {
        juce::Path p; auto base = arm (ang, len * 0.22f); auto side = perp (ang) * halfW;
        p.startNewSubPath (arm (ang, len)); p.lineTo (base + side); p.lineTo (base - side); p.closeSubPath(); g.fillPath (p);
    };
    // At button size the rose is a ring plus a north tick. The needle and the three bars have to stay readable.
    g.setColour (juce::Colours::white);
    point (0.0f, d * 0.50f, d * 0.09f);
    g.setColour (accentText);
    g.drawEllipse (box.reduced (d * 0.16f), s * 1.6f);
    const float bw = d * 0.09f, gapb = d * 0.03f;
    const float heights[3] = { d * 0.22f, d * 0.36f, d * 0.28f };
    const juce::Colour cols[3] = { green, meterHot, clip };
    float x = c.x - (3.0f * bw + 2.0f * gapb) * 0.5f;
    const float base = c.y + d * 0.16f;
    for (int i = 0; i < 3; ++i) {
        g.setColour (cols[i]);
        g.fillRoundedRectangle (x, base - heights[i], bw, heights[i], bw * 0.3f);
        x += bw + gapb;
    }
    const float na = 0.85f; // needle toward the upper right, white tail opposite
    auto blade = [&] (float ang, float len, float halfW) {
        juce::Path p; auto side = perp (ang) * halfW;
        p.startNewSubPath (arm (ang, len)); p.lineTo (c + side); p.lineTo (c - side); p.closeSubPath(); g.fillPath (p);
    };
    g.setColour (accentText); blade (na, d * 0.40f, d * 0.07f);
    g.setColour (juce::Colours::white); blade (na + 3.1416f, d * 0.28f, d * 0.05f);
    g.fillEllipse (c.x - d * 0.055f, c.y - d * 0.055f, d * 0.11f, d * 0.11f);
    g.setColour (accentText);
    g.fillEllipse (c.x - d * 0.026f, c.y - d * 0.026f, d * 0.052f, d * 0.052f);
}
void label (juce::Graphics&, const juce::String&, juce::Rectangle<int>, juce::Colour = dimmer, float h = 12.0f, juce::Justification j = juce::Justification::centredLeft);

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
};
}
