// v0.10 built-in public-domain hymnal: search, preview and COPY a hymn into your own library (the hymnal itself is read-only).
#pragma once
#include <JuceHeader.h>
#include "model/Hymns.h"

class HymnBrowserView : public juce::Component, private juce::ListBoxModel {
public:
    using Copy = std::function<void (const std::string& hymnId)>;
    HymnBrowserView();
    void open (Copy onCopy);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    int getNumRows() override { return (int) results_.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int row) override;
    void runSearch();
    std::vector<const wr::Hymn*> results_; const wr::Hymn* sel_ = nullptr; juce::String preview_;
    Copy copy_; juce::TextEditor search_; juce::ListBox list_ { "hymns", this };
    juce::TextButton copyBtn_ { "COPY TO MY LIBRARY" }, close_ { "CLOSE" };
    juce::Rectangle<int> box_, previewArea_;
};
