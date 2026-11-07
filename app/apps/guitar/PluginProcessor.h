// Tabernacle Tools - Electric Guitar - JUCE AudioProcessor (iOS standalone app + AUv3). CoreProcessor (setlists, charts, click, cues,
// band sync, projector) + the guitar layer: two-lane pedal rig, 4-cable routing, tails, scenes, tuner.
#pragma once
#include "CoreProcessor.h"
#include "dsp/RigBuilder.h"

class TabernacleGuitarProcessor : public CoreProcessor {
public:
    TabernacleGuitarProcessor();
    ~TabernacleGuitarProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override { return 8.0; }

    // ---- CoreProcessor instrument layer
    wr::ClickEngine& clickEngine() override { return rig.click; }
    void post (const wr::Command& c) override { rig.post (c); }   // rig forwards click types to rig.click
    void renderAudio (juce::AudioBuffer<float>&, int n) override;
    void applyInstrumentSettings() override;
    void instrumentTick() override { rig.collectGarbage(); reportLatency(); }   // also follows auto-reduced oversampling
    void reportLatency();
    juce::String clickOutLabel (int i) const override;

    // ---- RigSink (message thread): the rig parts
    void loadRig (const wr::RigPreset&) override;
    void applyScene (const wr::Scene&) override;
    void setPedal (const std::string& uid, bool on) override;
    void setTunerOpen (bool open) override;

    // ---- message-thread API for the guitar UI
    wr::RigEngine rig;
    const wr::Chain* layout() const { return layout_; }        // chain most recently built (immutable layout + param specs)
    const wr::RigPreset& currentRig() const { return rigPreset_; }
    void rebuildRig (const wr::RigPreset& p) { loadRig (p); }   // Workshop structural edit
    void setParam (const std::string& uid, const std::string& param, float v);
    bool pedalOn (const std::string& uid) const { auto it = enabled_.find (uid); return it != enabled_.end() && it->second; }
    std::string pedalType (const std::string& uid) const { if (layout_) if (auto* s = const_cast<wr::Chain*> (layout_)->find (uid)) return s->pedal->type; return {}; }
    float paramValue (const std::string& uid, const std::string& param) const;

private:
    const wr::Chain* layout_ = nullptr; wr::RigPreset rigPreset_;
    std::map<std::string, bool> enabled_;
    std::map<std::string, std::map<std::string, float>> paramMirror_;
    std::vector<float> scratch_[2];
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabernacleGuitarProcessor)
};
