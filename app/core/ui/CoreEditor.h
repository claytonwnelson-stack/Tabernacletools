// Shared editor base for every Tabernacle Tools app: look-and-feel, AUDIO/MIDI settings menu (click, cues, count-in,
// MIDI clock, band sync, projector), the band roster panel, join/drop toasts, crash-recovery note, text / image prompts.
// The app adds its pages (Live / Setlist / instrument pages) and instrument settings via the hooks.
#pragma once
#include "CoreProcessor.h"
#include "Theme.h"
#include "Platform.h"
class BandPanel; class ArrangementEditor; class PracticeBar; class ImportPreviewView;

class CoreEditor : public juce::AudioProcessorEditor, protected juce::Timer {
public:
    explicit CoreEditor (CoreProcessor&);
    ~CoreEditor() override;
    CoreProcessor& core;
    virtual void showLive() = 0;                                     // SetlistView "Go Live"
    void showBandPanel();
    void paintOverChildren (juce::Graphics&) override;               // join / drop toasts
    void askText (const juce::String& title, const juce::String& msg, const std::string& initial, std::function<void (juce::String)> done);
    void chooseImage (bool logo);
    void openSettings (juce::Component* anchor);
    // v0.7
    ArrangementEditor& arrangementEditor() { return *arr_; }
    void openLiveArrangement();                                      // current song: save to song or to this setlist item
    void choosePracticeTrack();
    int practiceBarHeight() const;
    // import confirm screen; onAdded gets the new song id (e.g. SetlistView appends it to the set). Shared charts
    // arriving from the Share sheet / "Open in" open it automatically (see coreTick).
    void openTeam();   // v0.11 church team (App Store builds only)
    // v0.12 read-only locks: true if allowed; otherwise opens the paywall with the feature's reason and returns false
    bool requireAccess (wr::Feature f);
    void openPaywall (const juce::String& reason = {});
    void openHymnal (std::function<void (juce::String)> onAdded = {}); // v0.10 public-domain hymnal (copy into library)
    void openImportPreview (const juce::String& text, const juce::String& fallbackTitle, std::function<void (juce::String)> onAdded = {});
    std::function<void (juce::String)> onSharedSongAdded;           // app hook (Guitar: append to the open set)                                   // 56 while practice mode is on (apps shrink their content)
protected:
    void coreLayout();                                               // call from resized()
    void coreTick();                                                 // call from timerCallback()
    virtual void addAudioSettings (juce::PopupMenu&) {}              // after "Audio interface..." (e.g. guitar routing)
    virtual void addInstrumentSettings (juce::PopupMenu&) {}         // after auto-advance (e.g. rig tails)
    virtual void addViewSettings (juce::PopupMenu&) {}               // after projector (e.g. tuner)
    juce::TextButton settingsBtn_ { "AUDIO / MIDI" };
    wr::theme::LookAndFeel lnf_;
    std::unique_ptr<BandPanel> band_;
    std::unique_ptr<ArrangementEditor> arr_; std::unique_ptr<PracticeBar> pbar_; std::unique_ptr<ImportPreviewView> imp_; std::unique_ptr<class HymnBrowserView> hymns_; std::unique_ptr<class TeamPanel> team_; std::unique_ptr<class PaywallView> paywall_;
    std::vector<wr::platform::SharedChart> sharedQueue_; int shareTick_ = 0;
private:
    std::unique_ptr<juce::FileChooser> chooser_;
    bool lastPractice_ = false, lastTracks_ = false; std::unique_ptr<class TracksBar> tbar_;
public:
    void chooseStems();                                              // v0.14 backing tracks: files or a folder
    bool tracksBarShown() const { return lastTracks_; }
    bool wantTracksBar = false;
    juce::Array<juce::File> pendingStems_;                           // v0.16 shared via the Share Sheet, waiting for OK                                      // song menu: show the bar to import stems
private:
    juce::String toast_; int toastSev_ = 0; double toastUntil_ = 0; bool noteShown_ = false; int bandTick_ = 0;
};
