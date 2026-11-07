// Tabernacle Tools Core Test - minimal iOS app on the shared core only (no guitar rig): setlist + chart + click + voice cues +
// count-in + band sync (leader / follower, Link, roster) + projector lyrics. Click and cues play on outs 1/2
// (iPad speaker / headphones / interface). Joins the same band session as Tabernacle Tools - Electric Guitar.
#pragma once
#include "CoreProcessor.h"

class HarnessProcessor : public CoreProcessor {
public:
    HarnessProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainOutputChannels() == 2 || l.getMainOutputChannels() == 1; }
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override { return 0.0; }
    wr::ClickEngine& clickEngine() override { return click; }
    void post (const wr::Command& c) override;                    // click always on outs 1/2
    void renderAudio (juce::AudioBuffer<float>&, int n) override;
    juce::String clickOutLabel (int) const override { return {}; } // no output choice (no rig to share outputs with)
    wr::ClickEngine click;
};
