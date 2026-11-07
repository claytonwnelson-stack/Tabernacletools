#pragma once
#include "PluginEditor.h"
#include "ChartView.h"
#include "TopBar.h"

// Rig (right): pedal tiles with LEDs + the two Chocolate Plus units with cues and next-switch highlight.
class RigPanel : public juce::Component {
public:
    explicit RigPanel (TabernacleGuitarEditor& e) : ed (e) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    TabernacleGuitarEditor& ed;
    std::vector<std::pair<juce::Rectangle<float>, std::string>> tiles_;
    std::vector<std::pair<juce::Rectangle<float>, wr::Action::Type>> switches_;
    std::vector<std::pair<juce::Rectangle<float>, int>> ctl_; // 0 click on/off, 1 click mute, 2 tuner, 3 play/stop
};

class LiveView : public juce::Component {
public:
    explicit LiveView (TabernacleGuitarEditor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintTopBar (juce::Graphics&, juce::Rectangle<int>);
    void topBarClick (juce::Point<int> p) { top_.click (p); }
    void liveChanged() { chart_.liveChanged(); rig_.repaint(); }
    void tick();
private:
    TabernacleGuitarEditor& ed; ChartView chart_; RigPanel rig_;
    TopBar top_;
    float tunerHzSm_ = 0; int meterTick_ = 0;
};
