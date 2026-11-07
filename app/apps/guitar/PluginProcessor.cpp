#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace wr;

TabernacleGuitarProcessor::TabernacleGuitarProcessor()
    : CoreProcessor (BusesProperties()
          .withInput ("Input", juce::AudioChannelSet::discreteChannels (2), true)      // in1 guitar (Hi-Z), in2 amp loop send
          .withOutput ("Output", juce::AudioChannelSet::discreteChannels (4), true),  // out1 amp input, out2 click, outs 3/4 loop return
          "Tabernacle Tools - Electric Guitar") {
    rig.prepare (48000, 512);
    if (! store.lib.rigs.empty()) loadRig (store.lib.rigs.front());
    coreInit();
}

bool TabernacleGuitarProcessor::isBusesLayoutSupported (const BusesLayout& l) const {
    const int in = l.getMainInputChannels(), out = l.getMainOutputChannels();
    return in >= 1 && in <= 2 && (out == 2 || out == 4);
}

void TabernacleGuitarProcessor::prepareToPlay (double sr, int block) {
    // re-prepare rig (allocates) - safe here: the audio callback is stopped during prepareToPlay
    rig.prepare (sr, std::max (block, 64));
    corePrepare (sr, block);
    for (auto& s : scratch_) s.assign ((size_t) std::max (block, 64) * 4, 0.0f);
    loadRig (rigPreset_); // re-applies current preset + pedal states at the new rate
    applySettings();
}

void TabernacleGuitarProcessor::renderAudio (juce::AudioBuffer<float>& buf, int n) {
    const int nIn = getTotalNumInputChannels();
    // copy inputs first (JUCE buffers are in-place)
    if ((int) scratch_[0].size() < n) { buf.clear(); return; } // host broke its promise about block size
    for (int c = 0; c < 2; ++c) { if (c < nIn) std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + n, scratch_[c].data()); else std::fill (scratch_[c].begin(), scratch_[c].begin() + n, 0.0f); }
    const float* ins[2] = { scratch_[0].data(), scratch_[1].data() };
    float* outs[4] = { nullptr, nullptr, nullptr, nullptr };
    for (int c = 0; c < std::min (4, buf.getNumChannels()); ++c) outs[c] = buf.getWritePointer (c);
    const int usable = prepareOutputs (buf, n);
    rig.process (ins, 2, outs, usable, n);
    if (recorder.recording()) {   // v0.8 rehearsal recorder: dry guitar input (+ the click output channel: Out 2 / Out 3 / Out 1)
        const int co = store.lib.settings.clickOut, cc = co == 2 ? 1 : co == 1 ? 2 : 0;
        recorder.push (ins[0], store.lib.settings.recordClick && cc < usable ? buf.getReadPointer (cc) : nullptr, n); }
    mixPractice (buf, n);   // v0.7 practice track on outs 1/2
}

juce::String TabernacleGuitarProcessor::clickOutLabel (int i) const {
    const char* outs[] = { "Outs 1/2 (2-cable rig output - shares the amp feed)", "Outs 3/4 (2-cable only: in 4-cable these are the amp return)", "Out 2 only (mono, default - the free output in 4-cable)" };
    return i >= 0 && i < 3 ? juce::String (outs[i]) : juce::String();
}

void TabernacleGuitarProcessor::loadRig (const RigPreset& p) {
    rigPreset_ = p; std::vector<std::string> warn;
    auto chain = buildChain (p, &warn); layout_ = chain.get();
    enabled_.clear(); paramMirror_.clear();
    for (int l = 0; l < 2; ++l) for (auto& d : p.lanes[l]) { enabled_[d.uid] = d.enabled; for (auto& kv : d.params) paramMirror_[d.uid][kv.first] = kv.second; }
    rig.setChain (std::move (chain)); reportLatency();
    if (! warn.empty()) status = juce::String (warn.front());
}
void TabernacleGuitarProcessor::applyScene (const Scene& s) {
    if (! layout_) return;
    for (auto& c : sceneCommands (*layout_, s)) rig.post (c);
    for (auto& kv : s.pedals) { if (! enabled_.count (kv.first)) continue; enabled_[kv.first] = kv.second.enabled; for (auto& pv : kv.second.params) paramMirror_[kv.first][pv.first] = pv.second; }
}
void TabernacleGuitarProcessor::setPedal (const std::string& uid, bool on) { if (! layout_ || ! enabled_.count (uid)) return; rig.post (cmdEnable (*layout_, uid, on)); enabled_[uid] = on; }
void TabernacleGuitarProcessor::setTunerOpen (bool open) { rig.post (cmdTuner (true, false)); rig.post (cmdTuner (open, true)); } // tracker on; mute the rig while open
void TabernacleGuitarProcessor::setParam (const std::string& uid, const std::string& param, float v) {
    if (! layout_) return; bool ok = false; auto c = cmdParam (*layout_, uid, param, v, &ok); if (! ok) return;
    rig.post (c); paramMirror_[uid][param] = v;
    for (auto& lane : rigPreset_.lanes) for (auto& d : lane) if (d.uid == uid) d.params[param] = v;
}
float TabernacleGuitarProcessor::paramValue (const std::string& uid, const std::string& param) const {
    auto it = paramMirror_.find (uid); if (it != paramMirror_.end()) { auto p = it->second.find (param); if (p != it->second.end()) return p->second; }
    if (layout_) if (auto* s = const_cast<Chain*> (layout_)->find (uid)) return s->pedal->getParam (param); // default (layout specs are immutable)
    return 0;
}
void TabernacleGuitarProcessor::applyInstrumentSettings() {
    auto& s = store.lib.settings;
    rig.post (cmdRouting ((Routing) s.routing)); rig.post (cmdTrim (Lane::Pre, s.trimPreDb)); rig.post (cmdTrim (Lane::Post, s.trimPostDb));
    rig.post (cmdClick (Command::TailSec, s.rigTailSec));
    rig.post (cmdOversampling (s.ampOversampling, s.autoOversampling));
    reportLatency();
}
void TabernacleGuitarProcessor::reportLatency() {   // v0.12: oversampling delay (a few samples) for hosts with latency compensation
    const int want = (int) std::lround (rig.latencySamples()); if (want != getLatencySamples()) setLatencySamples (want);
}

juce::AudioProcessorEditor* TabernacleGuitarProcessor::createEditor() { return new TabernacleGuitarEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TabernacleGuitarProcessor(); }
