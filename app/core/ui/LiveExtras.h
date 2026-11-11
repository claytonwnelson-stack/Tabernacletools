// v0.7 shared Live-screen extras (every app): arrangement editor, practice-track bar, next-song card, service-note banner,
// song menu (arrangement / notes / practice) and the service-notes editor.
#pragma once
#include "CoreEditor.h"

// Full-screen chart editor: sections, chords/lyrics, and tabs pasted or imported per section.
class SongEditor : public juce::Component {
public:
    SongEditor();
    void open (CoreEditor* ed, const std::string& songId, wr::Chart chart, std::function<void (wr::Chart)> onSave);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void loadSel(); void storeBody(); void pushUndo(); void undo(); void save();
    void run (const wr::ChartEdit&); void addBlank(); void chooseKind (const std::string& kind);
    void takeTabText (const juce::String&); void importTabFile();
    int nonTabs (const wr::Section&) const;
    CoreEditor* ed_ = nullptr; std::string songId_; wr::Chart opened_, chart_; std::vector<int> origin_;
    std::vector<wr::Chart> undo_; std::vector<std::vector<int>> undoOrigin_;
    std::function<void (wr::Chart)> onSave_; int sel_ = 0, splitAt_ = 1, dragFrom_ = -1, dragTo_ = -1; bool dragged_ = false;
    juce::TextEditor name_, body_; std::vector<wr::TabPart> pending_; std::unique_ptr<juce::FileChooser> files_;
    juce::Rectangle<int> close_, save_, undoR_, list_, kindArea_, splitDec_, splitInc_, splitGo_, paste_, importB_, tabArea_;
    std::vector<juce::Rectangle<int>> rows_, kinds_, tabRows_, partRows_;
    std::vector<std::pair<juce::Rectangle<int>, int>> actions_;
};

// Full-screen overlay: the play order as chips (drag to reorder, tap for duplicate / remove), written sections to add.
class ArrangementEditor : public juce::Component {
public:
    using Done = std::function<void (std::vector<std::string> tokens, int scope)>;  // scope 0 = song, 1 = this setlist item
    // songId: v0.8 the SECTION COLOURS row edits this song's per-section colours (committed with either SAVE button)
    void open (const wr::Chart& written, std::vector<std::string> tokens, bool setlistScope, Done done, std::string songId = {});
    std::function<void (const std::string& songId, std::vector<std::string> customColors)> onColours;  // set by CoreEditor
    const wr::SectionColorMap* defaults = nullptr;                                                     // global kind colours (display)
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    std::string tokenFor (int section) const;
    int chipAt (juce::Point<int>) const;
    wr::Chart written_; std::vector<std::string> tok_; bool setlistScope_ = false, asWritten_ = false; Done done_;
    juce::Rectangle<int> box_; std::vector<juce::Rectangle<int>> chips_; std::vector<std::pair<juce::Rectangle<int>, int>> palette_, buttons_, swatches_;
    std::string songId_; bool coloursDirty_ = false; juce::Rectangle<int> resetColours_;
    void recolour() { wr::applySectionColors (written_, defaults ? *defaults : wr::SectionColorMap()); }
    int dragFrom_ = -1, dragTo_ = -1; bool dragged_ = false;
};

// Bottom strip while practice mode is on: play / pause, -5 / +5 s, time, speed, loop section, mark section, auto marks.
class PracticeBar : public juce::Component, private juce::Timer {
public:
    explicit PracticeBar (CoreEditor& e) : ed (e) { startTimerHz (10); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void timerCallback() override { if (isVisible()) repaint(); }
    CoreEditor& ed; std::vector<std::pair<juce::Rectangle<int>, int>> btn_;
};

namespace wr::ui {
// v0.8 colour picker: the 12-colour stage palette (ticked = current), "Custom colour..." (full colour wheel) and
// "Use default" (resetLabel, e.g. "Use Chorus default (Blue)"; "" hides it). done gets a palette name, "#RRGGBB" or "" (= default).
void pickSectionColour (juce::Component* anchor, juce::Rectangle<int> anchorLocal, const std::string& current, const juce::String& resetLabel, std::function<void (std::string)> done);
juce::String colourName (const std::string&);   // "Teal", "#FF8800"
void addSectionColourSettings (CoreEditor&, juce::PopupMenu& settings, juce::Component* anchor); // Settings > Section colours
// v0.8 (V08Menus.cpp)
void addFootswitchMenu (CoreEditor&, juce::PopupMenu&, int unit, int sw);       // assign press / long actions per bank, manage banks
void addV08Settings (CoreEditor&, juce::PopupMenu&, juce::Component* anchor);   // stage mode, auto-scroll, recorder, stats, backup
void addFootPedalSettings (CoreEditor&, juce::PopupMenu&);                        // which Chocolate is the guitar unit, which is sections
void paintNextSongCard (juce::Graphics&, juce::Rectangle<float> area, const wr::NextSongInfo&, bool songEnded);
void paintNotesBanner (juce::Graphics&, juce::Rectangle<float> area, const std::vector<wr::ServiceNote>&);
juce::String noteWhenText (const std::string& at);
// add / remove service notes; written = the song's written chart (section choices for "when")
void editServiceNotes (std::vector<wr::ServiceNote> notes, const wr::Chart& written, juce::Component* anchor, std::function<void (std::vector<wr::ServiceNote>)> done);
void showSongMenu (CoreEditor&, juce::Rectangle<int> anchorInEditor);     // Live title tap: arrangement, notes, practice
}
