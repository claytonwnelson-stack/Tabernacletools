#pragma once
#include "PluginEditor.h"

// Full-screen rig workshop: pedal library -> two lanes (BEFORE AMP / AFTER AMP) with drag & drop, knob editor,
// output trims, save as scene / assign to song section / assign to guitar switch C/D.
class WorkshopView : public juce::Component, public juce::DragAndDropContainer, public juce::DragAndDropTarget {
public:
    explicit WorkshopView (TabernacleGuitarEditor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void paintTopBar (juce::Graphics&, juce::Rectangle<int>);
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::unique_ptr<juce::ComponentTraverser> createKeyboardFocusTraverser() override;
    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override { dropLane_ = -1; repaint(); }
    void itemDropped (const SourceDetails&) override;
private:
    void select (const std::string& uid);
    void rebuild();                                   // push working preset to the audio engine
    void buildEditor();
    void saveScene();
    void assignMenu();
    int dropIndex (int lane, juce::Point<int> p) const;
    TabernacleGuitarEditor& ed;
    wr::RigPreset work_; std::string selected_; int dropLane_ = -1, dropIdx_ = -1;
    juce::Rectangle<int> libArea_, laneArea_[2], ampBar_, edArea_, laneLeft_[2], laneRight_[2];
    int laneScroll_[2] {}, laneMaxScroll_[2] {}, scrollLane_ = -1, scrollGrab_ = 0;
    std::vector<std::pair<juce::Rectangle<int>, std::string>> libItems_;       // type
    std::vector<std::tuple<juce::Rectangle<int>, int, int>> tiles_;             // lane, index
    juce::OwnedArray<juce::Slider> knobs_; juce::OwnedArray<juce::Label> knobLabels_; juce::OwnedArray<juce::ComboBox> choices_;
    juce::TextButton onOff_ { "ON" }, remove_ { "Remove" }, reset_ { "Reset" }, saveScene_ { "Save as Scene" }, assign_ { "Assign to Song  v" }, saveRig_ { "Save Rig" }, backups_ { "Backups..." };
    juce::Slider trimPre_, trimPost_; juce::Label trimPreL_, trimPostL_;
    juce::TextEditor search_;
    // v0.10 amp section (head / cab / mic / output, or bypass for a real amp) + pedal-face layout
    juce::TextButton realAmp_ { "Using my own amp" }, loadIr_ { "Load IR file..." }, removeIr_ { "Remove IR" };
    std::unique_ptr<juce::FileChooser> irChooser_;
    std::vector<std::pair<int, juce::String>> groups_;   // (first knob index, heading) for the editor grid
    juce::Rectangle<int> faceArea_;
    void loadIrFile();
    // song tempo editor (current song): numeric field + 1-BPM steps, 30..300, saved to the song ({tempo:} on export)
    juce::Label tempoL_; juce::TextEditor tempo_; juce::TextButton tempoMinus_ { "-" }, tempoPlus_ { "+" };
    void setSongTempo (double bpm);
public:
    void refreshTempo();
    juce::String msg_;
};
