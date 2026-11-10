#pragma once
#include "PluginEditor.h"

// Full-screen rig workshop: pedal library -> two lanes (BEFORE AMP / AFTER AMP) with drag & drop,
// a zoomed stompbox for the selected pedal, output trims, save as scene / assign to song.
class WorkshopView : public juce::Component, public juce::DragAndDropContainer, public juce::DragAndDropTarget {
public:
    explicit WorkshopView (TabernacleGuitarEditor&);
    ~WorkshopView() override;
    void refresh();
    void select (const std::string& uid);
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
    struct FootLnF : public juce::LookAndFeel_V4 {
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override {}
    };
    struct TempoHost : public juce::Component {
        std::unique_ptr<juce::ComponentTraverser> createKeyboardFocusTraverser() override;
    };
    void visibilityChanged() override;
    void rebuild();
    void buildEditor();
    void saveScene();
    void assignMenu();
    int dropIndex (int lane, juce::Point<int> p) const;
    void layoutHeaderTempo();
    juce::Rectangle<int> headerTempoBounds() const;
    void toggleFavorite (const std::string& type);
    void rememberRecent (const std::string& type);
    void addFromLibrary (const std::string& type);
    void toggleCategory (int cat);
    bool categoryOpen (int cat) const;
    void loadIrFile();
    void setSongTempo (double bpm);
    TabernacleGuitarEditor& ed;
    wr::RigPreset work_; std::string selected_; int dropLane_ = -1, dropIdx_ = -1;
    juce::Rectangle<int> libArea_, libList_, laneArea_[2], ampBar_, edArea_, laneLeft_[2], laneRight_[2], pedalRect_;
    int laneScroll_[2] {}, laneMaxScroll_[2] {}, scrollLane_ = -1, scrollGrab_ = 0;
    int libScroll_ = 0, libMaxScroll_ = 0, libScrollGrab_ = 0; bool libScrolling_ = false;
    int libPress_ = -1, libHeaderPress_ = -1;
    std::vector<int> openCats_ { 0, 1 };   // at most two category sections open
    std::vector<std::pair<juce::Rectangle<int>, std::string>> libItems_;
    std::vector<std::pair<juce::Rectangle<int>, int>> libHeaders_;
    std::vector<std::tuple<juce::Rectangle<int>, int, int>> tiles_;
    juce::OwnedArray<juce::Slider> knobs_; juce::OwnedArray<juce::Label> knobLabels_; juce::OwnedArray<juce::ComboBox> choices_;
    juce::OwnedArray<juce::TextButton> choiceBtns_;
    std::vector<int> cellKind_, pillAt_, pillN_;   // 0 slider, 1 combo, 2 choice pills
    FootLnF footLnF_;
    juce::TextButton onOff_ { "ON" }, remove_ { "Remove" }, reset_ { "Reset" }, saveScene_ { "Save as Scene" }, assign_ { "Assign to Song  v" }, saveRig_ { "Save Rig" }, backups_ { "Backups..." };
    juce::Slider trimPre_, trimPost_; juce::Label trimPreL_, trimPostL_;
    juce::TextEditor search_;
    juce::TextButton realAmp_ { "Using my own amp" }, loadIr_ { "Load IR file..." }, removeIr_ { "Remove IR" };
    std::unique_ptr<juce::FileChooser> irChooser_;
    std::vector<std::pair<int, juce::String>> groups_;
    juce::Rectangle<int> faceArea_;
    TempoHost tempoHost_;
    juce::Label tempoL_; juce::TextEditor tempo_; juce::TextButton tempoMinus_ { "-" }, tempoPlus_ { "+" };
public:
    void refreshTempo();
    juce::String msg_;
};
