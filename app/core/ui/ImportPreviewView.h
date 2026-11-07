// v0.7 import confirm screen (Paste chart, Share sheet, "Open in", file import): editable title / artist / key / capo,
// the sections found, and what was cleaned up. IMPORT adds the song to the library; CANCEL drops it.
#pragma once
#include <JuceHeader.h>
#include "model/Import.h"

class ImportPreviewView : public juce::Component {
public:
    using Done = std::function<void (wr::Chart)>;
    ImportPreviewView();
    void open (const juce::String& text, const juce::String& fallbackTitle, Done onImport);
    void paint (juce::Graphics&) override;
    void resized() override;
    const wr::ImportPreview& preview() const { return p_; }
private:
    wr::ImportPreview p_; Done done_;
    juce::TextEditor title_, artist_, key_, capo_;
    juce::TextButton import_ { "IMPORT" }, cancel_ { "CANCEL" };
    juce::Rectangle<int> box_;
};
