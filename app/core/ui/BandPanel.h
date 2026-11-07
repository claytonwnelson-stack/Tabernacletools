// Leader-side band roster panel (v0.5): opened from the BAND n indicator on the Live screen.
// One row per follower: status dot (green in sync / yellow lagging / red dropped), device name, role, ping ms,
// what they are showing (current section, "> next" while a cue is queued), why they are yellow/red, and REMOVE.
#pragma once
#include <JuceHeader.h>
#include "sync/Roster.h"

class CoreProcessor;

class BandPanel : public juce::Component {
public:
    explicit BandPanel (CoreProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh() { repaint(); }
    static juce::Colour statusColour (wr::sync::Member::Status);
private:
    CoreProcessor& proc; juce::Rectangle<int> box_, close_; std::vector<std::pair<juce::Rectangle<int>, int>> kick_;
};
