#pragma once
#include "HarnessProcessor.h"
#include "CoreEditor.h"
#include "ChartView.h"
#include "TopBar.h"
class SetlistView;

// LIVE (top bar + chart + transport strip) and SETLIST tabs. Everything shown is shared core UI.
class HarnessEditor : public CoreEditor {
public:
    explicit HarnessEditor (HarnessProcessor&);
    ~HarnessEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void showLive() override { show (0); }
    void show (int page);
    HarnessProcessor& proc;
private:
    void timerCallback() override;
    juce::Rectangle<int> tabRect (int i) const { return { 21 + i * 84, 17, 82, 30 }; }
    int page_ = 0, lastVersion_ = -1;
    ChartView chart_; TopBar top_; std::unique_ptr<SetlistView> setlist_;
    juce::Rectangle<int> strip_; std::vector<std::pair<juce::Rectangle<int>, int>> btns_;
};
