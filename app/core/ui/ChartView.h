// Shared chart view (every app): full-song view by default, section view while MIDI section commands arrive,
// auto-return to full view; NEXT banner with the switch to press.
#pragma once
#include "CoreEditor.h"

class ChartView : public juce::Component {
public:
    explicit ChartView (CoreEditor& e) : ed (e) {}
    void paint (juce::Graphics&) override;
    void paintChart (juce::Graphics&);
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override { dragStart_ = scroll_; }
    void liveChanged() { followCurrent_ = true; repaint(); }
private:
    float sectionHeight (const wr::Section&, float w, bool compact) const;
    float drawSection (juce::Graphics&, const wr::Section&, int idx, juce::Rectangle<float> area, bool cur, bool dim, bool compact);
    CoreEditor& ed; float scroll_ = 0, dragStart_ = 0, contentH_ = 0; bool followCurrent_ = true;
    std::vector<std::pair<float, float>> secY_; // top/bottom per section in content coords
};
