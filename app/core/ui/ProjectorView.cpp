#include "ProjectorView.h"
#include "CoreProcessor.h"
#include "Platform.h"

using namespace wr;

ProjectorView::ProjectorView (CoreProcessor& p, bool own) : proc (p), own_ (own) { setOpaque (true); startTimerHz (30); reloadImages(); }
ProjectorView::~ProjectorView() { stopTimer(); }

void ProjectorView::reloadImages() {
    const auto& st = proc.store.lib.settings.projector;
    if (st.background != bgPath_) { bgPath_ = st.background; bg_ = bgPath_.empty() ? juce::Image() : juce::ImageFileFormat::loadFrom (juce::File (bgPath_)); }
    if (st.logo != logoPath_) { logoPath_ = st.logo; logo_ = logoPath_.empty() ? juce::Image() : juce::ImageFileFormat::loadFrom (juce::File (logoPath_)); }
}
void ProjectorView::timerCallback() {
    reloadImages(); state_.update (proc.live(), proc.store.lib.settings.projector);
    if (! (state_.current() == shown_)) { shown_ = state_.current(); repaint(); }
}
void ProjectorView::paintSlide (juce::Graphics& g, juce::Rectangle<float> r, const Slide& s, const ProjectorSettings& st, const juce::Image& bg, const juce::Image& logo) {
    g.setColour (juce::Colours::black); g.fillRect (r);
    if (s.kind == Slide::Blank) {
        if (logo.isValid()) g.drawImage (logo, r.reduced (r.getWidth() * 0.3f, r.getHeight() * 0.3f), juce::RectanglePlacement::centred);
        return;                                                          // blank / logo between songs: no background either
    }
    if (bg.isValid()) { g.drawImage (bg, r, juce::RectanglePlacement::fillDestination); g.setColour (juce::Colours::black.withAlpha (st.backgroundDim)); g.fillRect (r); }
    auto font = [] (float px) { return juce::Font (juce::FontOptions (px, juce::Font::bold)); };
    MeasureFn measure = [&] (const std::string& t, float px) { return juce::GlyphArrangement::getStringWidth (font (px), juce::String::fromUTF8 (t.c_str())); };
    auto lay = layoutSlide (s, r.getWidth(), r.getHeight(), st, measure);
    g.setColour (juce::Colours::white);
    for (size_t i = 0; i < lay.lines.size(); ++i) {
        g.setFont (font (lay.fontPx));
        auto line = juce::String::fromUTF8 (lay.lines[i].c_str());
        // soft shadow keeps text readable over busy backgrounds
        if (bg.isValid()) { g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawSingleLineText (line, (int) (r.getCentreX() + 3), (int) (r.getY() + lay.baselines[i] + 3), juce::Justification::horizontallyCentred); g.setColour (juce::Colours::white); }
        g.drawSingleLineText (line, (int) r.getCentreX(), (int) (r.getY() + lay.baselines[i]), juce::Justification::horizontallyCentred);
    }
}
void ProjectorView::paint (juce::Graphics& g) {
    paintSlide (g, getLocalBounds().toFloat(), shown_, proc.store.lib.settings.projector, bg_, logo_);
    if (proc.store.lib.settings.projector.showNotes) {   // v0.7 service notes strip (off by default)
        auto notes = proc.live().activeNotes(); if (notes.empty()) return; juce::String t; for (auto& n : notes) { if (t.isNotEmpty()) t << "   -   "; t << juce::String (n.text); }
        auto r = getLocalBounds().toFloat(); auto strip = r.removeFromBottom (r.getHeight() * 0.09f);
        g.setColour (juce::Colours::black.withAlpha (0.75f)); g.fillRect (strip); g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (strip.getHeight() * 0.5f))); g.drawText (t, strip.reduced (strip.getHeight() * 0.4f, 0), juce::Justification::centred, true);
    }
}
void ProjectorView::mouseUp (const juce::MouseEvent& e) {
    if (e.getLengthOfMousePress() > 600) { state_.setBlank (! state_.blank()); return; }
    if (e.x > getWidth() / 2) state_.nextSlide(); else state_.prevSlide();
}
void ProjectorView::mouseDoubleClick (const juce::MouseEvent&) { if (own_ && onClose) onClose(); }

ProjectorOutput::~ProjectorOutput() { showExternal (false); }
bool ProjectorOutput::externalAvailable() const { return platform::externalDisplayCount() > 0; }
void ProjectorOutput::showExternal (bool on) {
    if (! on) { platform::setExternalDisplayContent (nullptr); external_.reset(); return; }
    if (! external_) external_ = std::make_unique<ProjectorView> (proc, false);
    platform::setExternalDisplayContent (external_.get());   // re-attaches when a display is plugged in later
}
void ProjectorOutput::showOnThisScreen (juce::Component& parent) {
    proc.sync().setRole ("projector");   // shown as "projector" in the leader's roster
    local_ = std::make_unique<ProjectorView> (proc, true); parent.addAndMakeVisible (*local_); local_->setBounds (parent.getLocalBounds());
    local_->onClose = [this] { juce::MessageManager::callAsync ([this] { local_.reset(); proc.sync().setRole ("player"); }); };
}
