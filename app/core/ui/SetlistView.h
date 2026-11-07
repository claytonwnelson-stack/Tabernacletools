#pragma once
#include "CoreEditor.h"

// Full-screen setlist builder: song library (search, import), the set for a date (drag to reorder, per-set key),
// save by date, Go Live; section-controller mapping with MIDI learn.
class SetlistView : public juce::Component {
public:
    explicit SetlistView (CoreEditor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void paintTopBar (juce::Graphics&, juce::Rectangle<int>);
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void importChart();
    void pasteChart();
    void saveSet();
    void loadSet (const std::string& date);
    void entryMenu (int idx);
    std::vector<int> filteredSongs() const;
    CoreEditor& ed;
    wr::Setlist working_;
    juce::TextEditor search_, date_, name_;
    void exportMenu(); void playersMenu(); void backupsMenu();
    juce::TextButton export_ { "Share..." }, players_ { "Players..." }, backups_ { "Backups..." };
    juce::TextButton save_ { "Save Set" }, goLive_ { "Go Live  >" }, import_ { "+  Import file" }, paste_ { "Paste chart" }, hymnal_ { "Hymnal" }, sets_ { "Saved sets..." };
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Rectangle<int> libArea_, setArea_, ctlArea_;
    std::vector<std::pair<juce::Rectangle<int>, int>> libRows_, setRows_, learnBtns_;
    int dragFrom_ = -1, dragTo_ = -1; juce::String savedMsg_;
};
