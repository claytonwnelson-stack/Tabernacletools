#include "HarnessProcessor.h"
#include "HarnessEditor.h"
using namespace wr;

HarnessProcessor::HarnessProcessor()
    : CoreProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true), "Tabernacle Tools Core Test") {
    click.prepare (48000, 512);
    coreInit();
}
void HarnessProcessor::prepareToPlay (double sr, int block) {
    click.prepare (sr, std::max (block, 64)); // audio is stopped here
    corePrepare (sr, block);
    applySettings();
}
void HarnessProcessor::post (const Command& c) {
    if (c.type == Command::ClickOut) { click.post (cmdClick (Command::ClickOut, 0)); return; } // outs 1/2
    if (ClickEngine::owns (c.type) || c.type == Command::SetBpm) click.post (c);
}
void HarnessProcessor::renderAudio (juce::AudioBuffer<float>& buf, int n) {
    float* outs[2] = { nullptr, nullptr };
    for (int c = 0; c < std::min (2, buf.getNumChannels()); ++c) { outs[c] = buf.getWritePointer (c); buf.clear (c, 0, n); }
    const int usable = prepareOutputs (buf, n);
    if (usable == 1) outs[1] = nullptr;
    click.process (outs, usable, n);
    mixPractice (buf, n);
}
juce::AudioProcessorEditor* HarnessProcessor::createEditor() { return new HarnessEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HarnessProcessor(); }
