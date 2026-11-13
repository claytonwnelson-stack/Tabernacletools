// v0.14 multitrack backing tracks: processor side (import, load, tick) + the Live view transport bar and mixer sheet.
#include "CoreProcessor.h"
#include "CoreEditor.h"
#include "Tracks.h"
#include "Theme.h"
#include "model/Ccli.h"
#include "Platform.h"
using namespace wr; using namespace wr::theme;

// ------------------------------------------------------------------ import: any readable format -> float WAV at the session rate
// v0.16: streamed in 32k-frame blocks (never the whole file in RAM), resampled with a stateful Lagrange interpolator
static juce::String convertStem (juce::AudioFormatManager& fm, const juce::File& src, const juce::File& dst, double rate, double& lenSec) {
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (src));
    if (! r || r->lengthInSamples <= 0) return "Can't read " + src.getFileName();
    if (r->lengthInSamples > (juce::int64) r->sampleRate * 60 * 20) return src.getFileName() + " is longer than 20 minutes";
    std::vector<mt::FileMarker> marks;   // keep the source's WAV cue markers (section names) if it had any
    if (auto w = mt::WavFileSource::open (src.getFullPathName().toStdString())) for (auto m : w->markers()) marks.push_back (m);
    mt::WavFloatWriter out; if (! out.open (dst.getFullPathName().toStdString(), rate)) return "Can't write " + dst.getFileName();
    const double ratio = r->sampleRate / rate; const bool rs = std::abs (ratio - 1.0) > 1e-6;
    constexpr int kBlock = 32768; juce::AudioBuffer<float> in (2, kBlock); std::vector<float> ol, orr; juce::LagrangeInterpolator li[2];
    std::vector<float> carryL, carryR;   // input not yet consumed by the resampler
    for (juce::int64 pos = 0; pos < r->lengthInSamples; pos += kBlock) {
        const int n = (int) std::min<juce::int64> (kBlock, r->lengthInSamples - pos);
        r->read (&in, 0, n, pos, true, true); if (r->numChannels == 1) in.copyFrom (1, 0, in, 0, 0, n);
        if (! rs) { if (! out.write (in.getReadPointer (0), in.getReadPointer (1), n)) return "Can't write " + dst.getFileName() + " (storage full?)"; continue; }
        carryL.insert (carryL.end(), in.getReadPointer (0), in.getReadPointer (0) + n); carryR.insert (carryR.end(), in.getReadPointer (1), in.getReadPointer (1) + n);
        const int avail = (int) carryL.size(), outN = (int) std::floor ((avail - 4) / ratio); if (outN <= 0) continue;
        ol.resize ((size_t) outN); orr.resize ((size_t) outN);
        const int usedL = li[0].process (ratio, carryL.data(), ol.data(), outN); li[1].process (ratio, carryR.data(), orr.data(), outN);
        if (! out.write (ol.data(), orr.data(), outN)) return "Can't write " + dst.getFileName() + " (storage full?)";
        carryL.erase (carryL.begin(), carryL.begin() + usedL); carryR.erase (carryR.begin(), carryR.begin() + usedL);
    }
    lenSec = out.frames() / rate;
    if (! out.close (marks)) return "Can't write " + dst.getFileName() + " (storage full?)";
    return {};
}
juce::String CoreProcessor::importStems (const juce::Array<juce::File>& picked) {
    auto* s = tracksSong(); if (! s) return "No song loaded";
    if (! allows (Feature::Import)) return "Importing needs a subscription";
    juce::Array<juce::File> files;
    for (auto& f : picked) { if (f.isDirectory()) for (auto& c : f.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.m4a;*.mp3;*.caf;*.aac")) files.add (c); else files.add (f); }
    files.sort();
    if (files.isEmpty()) return "No audio files found";
    if ((int) s->multitrack.stems.size() + files.size() > mt::kMaxStems) return "Up to " + juce::String (mt::kMaxStems) + " stems per song";
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto dir = store.file().getParentDirectory().getChildFile ("Multitracks").getChildFile (juce::File::createLegalFileName (s->id)); dir.createDirectory();
    const double rate = fs_ > 0 ? fs_ : 48000; juce::String errs;
    if (! s->multitrack.stems.empty() && std::abs (s->multitrack.sampleRate - rate) > 0.5) return "This song's stems are at a different rate - remove them and import again";
    for (auto& f : files) {
        auto dst = dir.getNonexistentChildFile (f.getFileNameWithoutExtension(), ".wav"); double len = 0;
        auto e = convertStem (fm, f, dst, rate, len); if (e.isNotEmpty()) { errs << e << "\n"; continue; }
        mt::Stem st; st.id = juce::Uuid().toString().toStdString(); st.file = dst.getFullPathName().toStdString();
        st.name = mt::stemNameFromFile (f.getFileName().toStdString()); st.color = mt::guessStemColor (st.name);
        if (juce::String (st.name).containsIgnoreCase ("click") || juce::String (st.name).containsIgnoreCase ("guide")) st.bus = mt::Bus::Cue;  // in-ears only
        s->multitrack.stems.push_back (st); s->multitrack.lengthSec = std::max (s->multitrack.lengthSec, len);
        if (s->multitrack.sectionBeats.empty() || std::all_of (s->multitrack.sectionBeats.begin(), s->multitrack.sectionBeats.end(), [] (double b) { return b < 0; }))
            if (auto w = mt::WavFileSource::open (st.file)) if (! w->markers().empty()) mt::applyFileMarkers (s->multitrack, s->chart, w->markers());
    }
    auto& m = s->multitrack; m.sampleRate = rate; m.beatsPerBar = live_->beatsPerBar();
    if (m.tempo.pts.empty()) m.tempo = mt::TempoMap::constant (live_->songBpm() > 0 ? live_->songBpm() : 120);
    store.save(); reloadTracks(); return errs.trim();
}
void CoreProcessor::reloadTracks() {
    auto* s = tracksSong(); tracksSongId_ = s ? s->id : std::string(); tracksArrVersion_ = -1;
    const bool inSession = sync_->mode() == BandSync::Mode::Following;
    tracksGate_ = inSession ? "on:" + sync_->sessionId() : "off";
    if (! s) { tracksLive_.unload(); return; }
    Song playable = *s; playable.multitrack.stems.clear();
    for (auto& st : s->multitrack.stems) if (wr::sync::sharedAudioPlayable (st.sharedSession, inSession, sync_->sessionId())) playable.multitrack.stems.push_back (st);
    auto r = tracksLive_.loadSong (playable.multitrack.empty() ? nullptr : &playable, *live_, [] (const std::string& p) -> std::unique_ptr<mt::StemSource> { return mt::WavFileSource::open (p); }, allows (Feature::AudioEngine));
    if (! r.empty()) status = juce::String (r);
}
void CoreProcessor::tracksMixChanged() { if (auto* s = tracksSong()) { tracks.setMix (s->multitrack); store.saveSoon(); } }
void CoreProcessor::tracksArrangementChanged() { tracksLive_.arrangementChanged (tracksSong(), *live_); }
void CoreProcessor::markTracksSectionHere() {
    auto* s = tracksSong(); if (! s || ! tracks.loaded()) return; auto& m = s->multitrack; m.sectionBeats.resize (s->chart.sections.size(), -1.0);
    const auto& c = live_->chart(); const int i = live_->sectionIndex(); if (i < 0 || i >= (int) c.sections.size()) return;
    const int w = c.sections[(size_t) i].source >= 0 ? c.sections[(size_t) i].source : i; if (w >= (int) m.sectionBeats.size()) return;
    m.sectionBeats[(size_t) w] = std::round (tracks.beat()); store.save(); tracksArrangementChanged();
}
juce::String CoreProcessor::tracksWarning() const { return tracks.loaded() ? juce::String (mt::loadWarning (tracksLive_.load (0))) : juce::String(); }
juce::Array<juce::File> CoreProcessor::exportCcliUsage (const std::string& from, const std::string& to, bool pd) {   // v0.15 (lives here to keep CoreProcessor.cpp short)
    ccli::ExportOptions o; o.from = from; o.to = to; o.includePublicDomain = pd;
    auto dir = store.file().getParentDirectory().getChildFile ("Exports"); dir.createDirectory();
    const juce::String base = "CCLI-usage-" + juce::String (from.empty() ? "start" : from) + "-to-" + juce::String (to.empty() ? today() : to);
    auto tot = dir.getChildFile (base + "-per-song.csv"), csv = dir.getChildFile (base + "-detail.csv"), txt = dir.getChildFile (base + ".txt");
    tot.replaceWithText (juce::String::fromUTF8 (ccli::exportTotalsCsv (store.lib, o).c_str()));
    csv.replaceWithText (juce::String::fromUTF8 (ccli::exportCsv (store.lib, o).c_str())); txt.replaceWithText (juce::String::fromUTF8 (ccli::exportSummary (store.lib, o).c_str()));
    return { tot, txt, csv };
}
void CoreProcessor::injectAction (Action a) {
    const double now = nowSec();
    if (a.type == Action::TracksPlay) a.type = Action::Play;
    if (tracksLive_.handle (a, *live_, now, allows (Feature::AudioEngine))) return;
    if (! sync_->interceptAction (a, now)) live_->handle (a, now);
}
void CoreProcessor::tracksTick() {
    auto* s = live_->song(); const std::string id = s ? s->id : std::string();
    const std::string gate = sync_->mode() == BandSync::Mode::Following ? "on:" + sync_->sessionId() : "off";
    if (id != tracksSongId_ || gate != tracksGate_) reloadTracks();
    if (! fullAccess() && tracks.playing()) tracks.stop();                       // lapsed: locks like the rest of the audio
    { const auto arr = live_->arrangement(); std::string key; for (auto& a : arr) key += a + " ";   // arrangement edited (Live or setlist)
      const int h = (int) std::hash<std::string>() (key) & 0x7fffffff;
      if (tracks.loaded() && tracksArrVersion_ >= 0 && h != tracksArrVersion_) tracksArrangementChanged(); tracksArrVersion_ = h; }
    tracksLive_.tick (*live_, nowSec());
    sync_->setTracksTransport (tracks.transport());
}

// ------------------------------------------------------------------ Live view transport bar
void TracksBar::paint (juce::Graphics& g) {
    auto& P = ed.core; auto& t = P.tracks; btn_.clear(); const auto& L = P.live();
    g.fillAll (panel); g.setColour (accentText.withAlpha (0.6f)); g.drawHorizontalLine (0, 0, (float) getWidth());
    auto r = getLocalBounds().reduced (10, 8); int x = r.getX();
    auto button = [&] (const juce::String& s, int id, bool lit = false, int w = 0) { if (w == 0) w = (int) juce::GlyphArrangement::getStringWidth (font (12, true), s) + 22;
        juce::Rectangle<int> b (x, r.getY(), w, r.getHeight()); pill (g, b.toFloat(), lit ? purple.withAlpha (0.25f) : btn, lit ? purple : btnBorder, 8);
        g.setColour (lit ? purple : text); g.setFont (font (12, true)); g.drawText (s, b, juce::Justification::centred); btn_.push_back ({ b, id }); x += w + 6; };
    g.setColour (purple); g.setFont (font (11, true)); g.drawText ("TRACKS", x, r.getY(), 60, r.getHeight(), juce::Justification::centredLeft); x += 62;
    if (L.following()) { const auto lt = P.sync().leaderTracks(); auto* song = L.song();
        juce::String note = lt.loaded ? juce::String ("Leader's tracks ") + (lt.playing ? "playing" : "stopped") + (lt.loop >= 0 ? " - VAMP" : "") : juce::String ("Leader has no backing tracks");
        if (song && ! song->leaderAudioNote.empty()) note << "   " << song->leaderAudioNote;
        const float prog = (float) P.sync().leaderAudioProgress();
        if (prog > 0.f && prog < 1.f) note << "   Getting audio " << (int) (prog * 100.f) << "%";
        if (! L.playing() && song && ! song->leaderAudioNote.empty()) button ("GET AUDIO", 21, false, 96);
        g.setColour (dim); g.setFont (font (12)); g.drawText (note, x, r.getY(), r.getRight() - x, r.getHeight(), juce::Justification::centredLeft, true); return; }
    if (auto* song = L.song()) {
        bool locked = false; for (auto& st : song->multitrack.stems) if (! wr::sync::sharedAudioPlayable (st.sharedSession, false, {})) locked = true;
        if (song->practice.sharedSession.size() && ! wr::sync::sharedAudioPlayable (song->practice.sharedSession, false, {})) locked = true;
        if (locked) { g.setColour (dim); g.setFont (font (11)); g.drawText ("Shared audio plays only while connected to the leader", r.getRight() - 360, r.getY(), 350, r.getHeight(), juce::Justification::centredRight, true); }
    }
    if (! t.loaded()) { button ("IMPORT STEMS (files or folder)...", 20); g.setColour (dim); g.setFont (font (12));
        g.drawText ("Up to 16 stems per song. Main outs or in-ears (Out 2).", x + 6, r.getY(), r.getRight() - x - 10, r.getHeight(), juce::Justification::centredLeft, true); return; }
    button (t.playing() ? "STOP" : "PLAY", 1, t.playing(), 70); button ("< SECTION", 2); button ("SECTION >", 3);
    button (t.loopSection() >= 0 ? "VAMPING - RELEASE" : "LOOP SECTION", 4, t.loopSection() >= 0);
    const auto& c = L.chart(); const int cur = t.current();
    juce::String sec = cur >= 0 && cur < (int) c.sections.size() ? juce::String (c.sections[(size_t) cur].name) : juce::String ("-");
    if (t.pendingJump() >= 0 && t.pendingJump() < (int) c.sections.size()) sec << "  > " << c.sections[(size_t) t.pendingJump()].name;
    g.setColour (text); g.setFont (mono (13, true)); g.drawText (sec + "  bar " + juce::String ((int) t.bar()), x, r.getY(), 220, r.getHeight(), juce::Justification::centredLeft); x += 224;
    button ("MIXER", 5); button ("TEMPO", 7); button ("MARK SECTION HERE", 6);
    auto w = P.tracksWarning(); if (w.isNotEmpty()) { g.setColour (juce::Colour (0xffff4d4d)); g.setFont (font (11, true)); g.drawText (w, x, r.getY(), r.getRight() - x, r.getHeight(), juce::Justification::centredLeft, true); }
}
void TracksBar::mouseUp (const juce::MouseEvent& e) {
    auto& P = ed.core; auto& t = P.tracks;
    for (auto& b : btn_) if (b.first.contains (e.getPosition())) {
        if (b.second != 20 && b.second != 21 && ! ed.requireAccess (Feature::AudioEngine)) return;   // read-only: locked like the rest of Live
        Action a; switch (b.second) {
            case 1: a.type = Action::TracksPlay; P.injectAction (a); break;
            case 2: a.type = Action::PrevSection; P.injectAction (a); break;
            case 3: a.type = Action::TracksNext; P.injectAction (a); break;
            case 4: a.type = Action::TracksLoop; P.injectAction (a); break;
            case 5: showTracksMixer (ed, localAreaToGlobal (b.first)); break;
            case 6: P.markTracksSectionHere(); break;
            case 7: showTempoMapMenu (ed, localAreaToGlobal (b.first)); break;
            case 20: ed.chooseStems(); break;
            case 21: if (! P.live().playing()) P.sync().requestLeaderAudio(); break;
        }
        (void) t; repaint(); return;
    }
}

// ------------------------------------------------------------------ v0.16 tempo map editor (menu)
void showTempoMapMenu (CoreEditor& ed, juce::Rectangle<int> anchor) {
    auto* s = ed.core.tracksSong(); if (! s) return; auto& m = s->multitrack; CoreEditor* e = &ed; const std::string id = s->id;
    auto commit = [e] { e->core.store.save(); e->core.tracksLive().reloadNeeded(); e->core.reloadTracks(); };
    auto edit = [e, id, commit] (std::function<void (mt::MultitrackSet&)> f) { if (auto* so = e->core.store.lib.song (id)) { f (so->multitrack); commit(); } };
    const double beat = std::max (0.0, ed.core.tracks.beat()), sec = m.tempo.secAt (beat);
    juce::PopupMenu pm; pm.addSectionHeader (juce::String (mt::tempoMapText (m.tempo, m.beatsPerBar)));
    pm.addItem ("Tempo change at the playhead (bar " + juce::String ((int) std::floor (beat / std::max (1, m.beatsPerBar)) + 1) + ")...", [e, edit, beat] {
        e->askText ("Tempo change", "BPM from this bar on (section markers stay where they are in the audio)", "", [edit, beat] (juce::String t) { const double b = t.getDoubleValue(); if (b < 20) return;
            edit ([beat, b] (mt::MultitrackSet& x) { mt::editTempoKeepingMarkers (x, [&] (mt::TempoMap& tm) { mt::setTempoAt (tm, std::floor (beat / std::max (1, x.beatsPerBar)) * x.beatsPerBar, b); }); }); }); });
    pm.addItem ("Beat 1 of the song is at the playhead (" + juce::String (sec, 2) + " s)", [edit, sec] { edit ([sec] (mt::MultitrackSet& x) { x.tempo.offsetSec = sec; }); });
    pm.addItem ("First downbeat at... (seconds)", [e, edit, m] { e->askText ("First downbeat", "Seconds into the files where bar 1 starts", juce::String (m.tempo.offsetSec, 3).toStdString(), [edit] (juce::String t) {
        const double v = t.getDoubleValue(); if (v < 0) return; edit ([v] (mt::MultitrackSet& x) { x.tempo.offsetSec = v; }); }); });
    pm.addItem ("Use the song's tempo (" + juce::String (ed.core.live().songBpm(), 1) + " BPM) everywhere", [e, edit] { const double b = e->core.live().songBpm();
        edit ([b] (mt::MultitrackSet& x) { const double off = x.tempo.offsetSec; x.tempo = mt::TempoMap::constant (b, off); }); });
    juce::PopupMenu rm; for (size_t i = 0; i < m.tempo.pts.size(); ++i) { const auto& p = m.tempo.pts[i];
        rm.addItem ("Bar " + juce::String ((int) std::floor (p.beat / std::max (1, m.beatsPerBar)) + 1) + ": " + juce::String (p.bpm, 1) + " BPM", m.tempo.pts.size() > 1, false,
                    [edit, i] { edit ([i] (mt::MultitrackSet& x) { mt::editTempoKeepingMarkers (x, [&] (mt::TempoMap& tm) { mt::removeTempoPoint (tm, i); }); }); }); }
    pm.addSubMenu ("Remove a tempo change", rm);
    pm.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (anchor));
}

// ------------------------------------------------------------------ mixer sheet
class MixerSheet : public juce::Component {
public:
    explicit MixerSheet (CoreEditor& e) : ed (e) { auto* s = ed.core.tracksSong(); n_ = s ? (int) s->multitrack.stems.size() : 0;
        for (int i = 0; i < n_; ++i) { auto* v = vol_.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight)); v->setRange (-60, 6, 0.5); v->setTextValueSuffix (" dB");
            v->setValue (s->multitrack.stems[(size_t) i].volumeDb, juce::dontSendNotification); v->onValueChange = [this, i, v] { edit (i, [v] (mt::Stem& st) { st.volumeDb = (float) v->getValue(); }); }; addAndMakeVisible (v);
            auto* p = pan_.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)); p->setRange (-1, 1, 0.01); p->setDoubleClickReturnValue (true, 0);
            p->setValue (s->multitrack.stems[(size_t) i].pan, juce::dontSendNotification); p->onValueChange = [this, i, p] { edit (i, [p] (mt::Stem& st) { st.pan = (float) p->getValue(); }); }; addAndMakeVisible (p);
            if (! wr::sync::sharedAudioPlayable (s->multitrack.stems[(size_t) i].sharedSession, ed.core.sync().mode() == BandSync::Mode::Following, ed.core.sync().sessionId())) { v->setEnabled (false); p->setEnabled (false); } }
        setSize (620, 44 * std::max (1, n_) + 40); }
    void paint (juce::Graphics& g) override {
        g.fillAll (panel); auto* s = ed.core.tracksSong(); if (! s) return; btn_.clear();
        g.setColour (dim); g.setFont (font (11)); g.drawText ("Mutes are remembered for this song. IN-EARS = Out 2 click/cue bus only.", 10, getHeight() - 30, getWidth() - 20, 24, juce::Justification::centredLeft);
        for (int i = 0; i < n_; ++i) { const auto& st = s->multitrack.stems[(size_t) i]; auto row = juce::Rectangle<int> (0, i * 44, getWidth(), 44).reduced (8, 6);
            g.setColour (accent (juce::String (st.color))); g.fillRoundedRectangle (row.removeFromLeft (6).toFloat(), 3); row.removeFromLeft (6);
            const bool locked = ! wr::sync::sharedAudioPlayable (st.sharedSession, ed.core.sync().mode() == BandSync::Mode::Following, ed.core.sync().sessionId());
            g.setColour (locked ? dim : text); g.setFont (font (13, true));
            g.drawText (locked ? juce::String (st.name) + "  (leader only)" : juce::String (st.name), row.removeFromLeft (160), juce::Justification::centredLeft, true);
            if (locked) continue;
            auto tog = [&] (const juce::String& t, bool on, juce::Colour c, int id) { auto b = row.removeFromLeft (64).reduced (2, 0); pill (g, b.toFloat(), on ? c.withAlpha (0.3f) : btn, on ? c : btnBorder, 6);
                g.setColour (on ? c : text); g.setFont (font (11, true)); g.drawText (t, b, juce::Justification::centred); btn_.push_back ({ b, i * 10 + id }); };
            tog ("MUTE", st.mute, juce::Colour (0xffff4d4d), 0); tog ("SOLO", st.solo, orange, 1); tog (st.bus == mt::Bus::Cue ? "IN-EARS" : "MAIN", st.bus == mt::Bus::Cue, blue, 2); }
    }
    void resized() override { for (int i = 0; i < n_; ++i) { auto row = juce::Rectangle<int> (330, i * 44 + 6, getWidth() - 338, 32); vol_[i]->setBounds (row.removeFromLeft (190)); pan_[i]->setBounds (row.reduced (4, 0)); } }
    void mouseUp (const juce::MouseEvent& e) override {
        for (auto& b : btn_) if (b.first.contains (e.getPosition())) { const int i = b.second / 10, k = b.second % 10;
            edit (i, [k] (mt::Stem& st) { if (k == 0) st.mute = ! st.mute; else if (k == 1) st.solo = ! st.solo; else st.bus = st.bus == mt::Bus::Cue ? mt::Bus::Main : mt::Bus::Cue; }); repaint(); return; }
    }
private:
    template <class F> void edit (int i, F f) { auto* s = ed.core.tracksSong(); if (! s || i >= (int) s->multitrack.stems.size()) return; f (s->multitrack.stems[(size_t) i]); ed.core.tracksMixChanged(); }
    CoreEditor& ed; int n_ = 0; juce::OwnedArray<juce::Slider> vol_, pan_; std::vector<std::pair<juce::Rectangle<int>, int>> btn_;
};
void showTracksMixer (CoreEditor& ed, juce::Rectangle<int> anchorScreen) {
    juce::CallOutBox::launchAsynchronously (std::make_unique<MixerSheet> (ed), anchorScreen, nullptr);
}
