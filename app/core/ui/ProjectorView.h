// Projector lyrics output (v0.5): full-screen lyrics-only slides. Runs on an external display (HDMI / USB-C adapter,
// AirPlay) or on this device's own screen (a projector-only follower iPad). Driven by the local LiveSession, which in
// follower mode mirrors the band leader. Tap right/left = next/previous slide, long-press = blank toggle, double-tap = close
// (own-screen mode only).
#pragma once
#include <JuceHeader.h>
#include "model/Projector.h"

class CoreProcessor;

class ProjectorView : public juce::Component, private juce::Timer {
public:
    ProjectorView (CoreProcessor& p, bool ownScreen);
    ~ProjectorView() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    std::function<void()> onClose;
    static void paintSlide (juce::Graphics&, juce::Rectangle<float> area, const wr::Slide&, const wr::ProjectorSettings&, const juce::Image& bg, const juce::Image& logo);
private:
    void timerCallback() override;
    void reloadImages();
    CoreProcessor& proc; bool own_; wr::ProjectorState state_; wr::Slide shown_;
    juce::Image bg_, logo_; std::string bgPath_, logoPath_;
};

// keeps a ProjectorView on the external display while one is connected (iOS: UIScreen connect/disconnect)
class ProjectorOutput {
public:
    explicit ProjectorOutput (CoreProcessor& p) : proc (p) {}
    ~ProjectorOutput();
    bool externalAvailable() const;
    void showExternal (bool on);                       // follows HDMI plug/unplug while on
    void showOnThisScreen (juce::Component& parent);   // projector-only follower: covers the app UI
    bool externalOn() const { return external_ != nullptr; }
    bool showing() const { return external_ != nullptr || local_ != nullptr; }   // v0.15 CCLI log: lyrics are on a screen
private:
    CoreProcessor& proc; std::unique_ptr<ProjectorView> external_, local_;
};
