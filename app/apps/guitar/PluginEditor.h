#pragma once
#include "PluginProcessor.h"
#include "CoreEditor.h"

class LiveView; class SetlistView; class WorkshopView; class BandPanel;
class TabernacleGuitarEditor;

// Full-screen chromatic tuner. Opens from the TUNER button or by holding guitar Chocolate A (~1 s). Rig output is muted while open.
class TunerOverlay : public juce::Component {
public:
    explicit TunerOverlay (TabernacleGuitarEditor& e);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick();
private:
    TabernacleGuitarEditor& ed; float hzSm_ = 0; juce::Rectangle<int> close_, refMinus_, refPlus_;
};

// Top bar (LIVE / SETLIST / WORKSHOP tabs) + one full-size view. Live is split chart | rig; Setlist and Workshop are full screen.
// Shared parts (settings menu, band panel, toasts, chart, top bar, setlist) come from the core UI (CoreEditor).
class TabernacleGuitarEditor : public CoreEditor {
public:
    explicit TabernacleGuitarEditor (TabernacleGuitarProcessor&);
    ~TabernacleGuitarEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    enum class Page { Live, Setlist, Workshop };
    void show (Page);
    void showLive() override { show (Page::Live); }
    Page page() const { return page_; }
    TabernacleGuitarProcessor& proc;
    bool tunerShown = true;
private:
    void timerCallback() override;
    void addAudioSettings (juce::PopupMenu&) override;
    void addInstrumentSettings (juce::PopupMenu&) override;
    void addViewSettings (juce::PopupMenu&) override;
    juce::Rectangle<int> tabRect (int i) const;
    Page page_ = Page::Live;
    std::unique_ptr<LiveView> live_; std::unique_ptr<SetlistView> setlist_; std::unique_ptr<WorkshopView> workshop_;
    std::unique_ptr<TunerOverlay> tuner_;
    int lastVersion_ = -1;
};
