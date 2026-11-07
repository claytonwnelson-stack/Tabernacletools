// Shared Live top bar (every app): setlist position + title, KEY (transpose / capo), tempo [-] BPM [+] TAP SAVE /
// MIDI CLOCK, BAND n roster indicator (leader), section chips. The app may reserve space at the right (guitar tuner).
#pragma once
#include "CoreEditor.h"

class TopBar {
public:
    explicit TopBar (CoreEditor& e) : ed (e) {}
    void paint (juce::Graphics&, juce::Rectangle<int> area, int rightReserve);
    void click (juce::Point<int>);
private:
    CoreEditor& ed;
    juce::Rectangle<int> titleR_, bandR_, keyR_, bpmR_, bpmMinusR_, bpmPlusR_, tapR_, tempoSaveR_; std::vector<std::pair<juce::Rectangle<int>, int>> chipR_;
};
