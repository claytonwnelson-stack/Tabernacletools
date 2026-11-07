// v0.14 backing tracks UI: Live view transport bar + mixer sheet (Tracks.cpp)
#pragma once
#include <JuceHeader.h>
class CoreEditor;
class TracksBar : public juce::Component, private juce::Timer {
public:
    explicit TracksBar (CoreEditor& e) : ed (e) { startTimerHz (10); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void timerCallback() override { if (isVisible()) repaint(); }
    CoreEditor& ed; std::vector<std::pair<juce::Rectangle<int>, int>> btn_;
};
void showTracksMixer (CoreEditor&, juce::Rectangle<int> anchorScreen);
void showTempoMapMenu (CoreEditor&, juce::Rectangle<int> anchorScreen);   // v0.16
