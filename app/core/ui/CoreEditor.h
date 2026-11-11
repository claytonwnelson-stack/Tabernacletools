// Shared editor base for every Tabernacle Tools app: look-and-feel, AUDIO/MIDI settings menu (click, cues, count-in,
// MIDI clock, band sync, projector), the band roster panel, join/drop toasts, crash-recovery note, text / image prompts.
// The app adds its pages (Live / Setlist / instrument pages) and instrument settings via the hooks.
#pragma once
#include "CoreProcessor.h"
#include "Theme.h"
#include "Platform.h"
class BandPanel; class ArrangementEditor; class SongEditor; class PracticeBar; class ImportPreviewView;

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
    void openSongEditor (const std::string& songId, std::function<void()> afterSave = {});
    SongEditor& songEditor() { return *songEd_; }
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
    struct SettingsButton : juce::TextButton {
        SettingsButton() { setButtonText ("Settings"); }
        void paintButton (juce::Graphics& g, bool over, bool down) override {
            using namespace wr::theme;
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            pill (g, r, down ? accentFill : (over ? btn.brighter (0.08f) : btn), down ? accentText : btnBorder, 10.0f);
            auto f = font (13.0f, true);
            const float icon = 20.0f, gap = 6.0f;
            juce::GlyphArrangement ga; ga.addLineOfText (f, "Settings", 0, 0);
            const float tw = ga.getBoundingBox (0, -1, true).getWidth();
            const float x = r.getCentreX() - (icon + gap + tw) * 0.5f;
            paintBearingMark (g, { x, r.getCentreY() - icon * 0.5f, icon, icon });
            g.setColour (wr::theme::text); g.setFont (f);
            g.drawText ("Settings", juce::Rectangle<float> (x + icon + gap, r.getY(), tw + 2.0f, r.getHeight()), juce::Justification::centredLeft);
        }
    };
    SettingsButton settingsBtn_;
    wr::theme::LookAndFeel lnf_;
    std::unique_ptr<BandPanel> band_;
    std::unique_ptr<ArrangementEditor> arr_; std::unique_ptr<SongEditor> songEd_; std::unique_ptr<PracticeBar> pbar_; std::unique_ptr<ImportPreviewView> imp_; std::unique_ptr<class HymnBrowserView> hymns_; std::unique_ptr<class TeamPanel> team_; std::unique_ptr<class PaywallView> paywall_;
    std::vector<wr::platform::SharedChart> sharedQueue_; int shareTick_ = 0;
private:
    class SettingsPanel;
    std::unique_ptr<SettingsPanel> settings_;
    void layoutSettingsOverlay();
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
