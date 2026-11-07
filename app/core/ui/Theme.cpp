#include "Theme.h"
namespace wr::theme {
juce::Colour accent (const juce::String& n) { return juce::Colour (wr::colorArgbOr (n.toStdString(), dim.getARGB())); }
juce::Font font (float h, bool bold) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
juce::Font mono (float h, bool bold) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }
void pill (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour f, juce::Colour b, float rad) { g.setColour (f); g.fillRoundedRectangle (r, rad); g.setColour (b); g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f); }
void led (juce::Graphics& g, juce::Point<float> c, bool on) {
    if (on) { g.setColour (green.withAlpha (0.35f)); g.fillEllipse (c.x - 7, c.y - 7, 14, 14); }
    g.setColour (on ? green : juce::Colour (0xff333333)); g.fillEllipse (c.x - 4.5f, c.y - 4.5f, 9, 9);
}
void label (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Colour c, float h, juce::Justification j) { g.setColour (c); g.setFont (font (h, true)); g.drawText (s, r, j, true); }

LookAndFeel::LookAndFeel() {
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::TextButton::buttonColourId, btn); setColour (juce::TextButton::textColourOffId, text); setColour (juce::TextButton::buttonOnColourId, orange); setColour (juce::TextButton::textColourOnId, juce::Colour (0xff111111));
    setColour (juce::Label::textColourId, text); setColour (juce::TextEditor::backgroundColourId, well); setColour (juce::TextEditor::outlineColourId, cardBorder); setColour (juce::TextEditor::textColourId, text);
    setColour (juce::ComboBox::backgroundColourId, btn); setColour (juce::ComboBox::outlineColourId, btnBorder); setColour (juce::ComboBox::textColourId, text);
    setColour (juce::PopupMenu::backgroundColourId, bar); setColour (juce::PopupMenu::textColourId, text); setColour (juce::PopupMenu::highlightedBackgroundColourId, orange);
    setColour (juce::ListBox::backgroundColourId, panel); setColour (juce::Slider::textBoxTextColourId, dim); setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, cardBorder);
}
void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) {
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4); float d = std::min (r.getWidth(), r.getHeight()); auto c = r.getCentre(); r = juce::Rectangle<float> (d, d).withCentre (c);
    juce::ColourGradient grad (juce::Colour (0xff4a5470), r.getX() + d * 0.4f, r.getY() + d * 0.35f, juce::Colour (0xff1d2230), r.getRight(), r.getBottom(), true);
    g.setGradientFill (grad); g.fillEllipse (r); g.setColour (well); g.drawEllipse (r, 2.0f);
    juce::Path arc; arc.addCentredArc (c.x, c.y, d * 0.5f + 3, d * 0.5f + 3, 0, a0, a0 + pos * (a1 - a0), true);
    g.setColour (s.isEnabled() ? orange : dimmer); g.strokePath (arc, juce::PathStrokeType (2.5f));
    float ang = a0 + pos * (a1 - a0); juce::Point<float> tip (c.x + std::sin (ang) * d * 0.38f, c.y - std::cos (ang) * d * 0.38f);
    g.setColour (juce::Colours::white); g.drawLine (c.x + std::sin (ang) * d * 0.1f, c.y - std::cos (ang) * d * 0.1f, tip.x, tip.y, 2.0f);
}
void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& col, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced (0.5f); auto f = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : col;
    if (down) f = f.brighter (0.15f); else if (over) f = f.brighter (0.05f);
    pill (g, r, f, b.getToggleState() ? f : btnBorder, 10.0f);
}
}
