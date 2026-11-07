// Projector lyrics (v0.5): lyrics-only slides for a screen / projector (external display over HDMI, or this device's own
// screen), driven by a LiveSession - the local one, or a follower session mirroring the band leader.
// No chords, no tabs, no comments. Big white text on black (or over a background image).
#pragma once
#include "LiveSession.h"
#include <functional>

namespace wr {

// ProjectorSettings + its JSON live in Song.h (part of AppSettings)

struct Slide {
    enum Kind { Blank, Title, Lyrics } kind = Blank;
    int section = -1, index = 0, count = 0;   // lyric slide i of count within its section
    std::vector<std::string> lines;          // lyric lines (Title: title, artist)
    bool operator== (const Slide& o) const { return kind == o.kind && section == o.section && index == o.index && lines == o.lines; }
};

std::string lyricOnly (const ChartLine&);                       // "[G]Holy [D]is" -> "Holy is"; chord-only lines -> ""
std::vector<std::string> sectionLyrics (const Section&);         // non-empty lyric lines, chords/tabs/comments dropped
std::vector<Slide> sectionSlides (const Section&, int sectionIndex, int linesPerSlide);
std::vector<Slide> songSlides (const Chart&, const ProjectorSettings&); // [title] + every section's slides in order

// What the projector shows now. Rules:
//  * song loaded, not started: title slide (or blank/logo if titles are off)
//  * lyrics once the song starts: PLAY past the count-in, a section change (local or from the leader), or the operator
//  * within a section: slides advance evenly over the section's bars while the bar grid runs; otherwise the operator
//    taps next / previous; a new section always starts on its first slide
//  * song ended (auto-advance ran off the end) or operator "blank": blank / logo slide
class ProjectorState {
public:
    void update (const LiveSession& live, const ProjectorSettings& s);  // call on every LiveSession version change / tick
    Slide current() const { return cur_; }
    void nextSlide(); void prevSlide();       // operator (tap right / left on the projector screen)
    void setBlank (bool b) { blank_ = b; }    // operator blank (long press); next update shows blank / logo
    bool blank() const { return blank_; }
private:
    std::string songId_; int section_ = -2, pass_ = 0, manual_ = 0, lastVersion_ = -1; bool started_ = false, ended_ = false, blank_ = false, wasPlaying_ = false;
    std::vector<Slide> slides_; Slide cur_; int titleCount_ = 0; Chart chart_;
};

// text layout for a slide box (pure; the measure function returns the width of a string at a font size in px)
struct SlideLayout { float fontPx = 0; std::vector<std::string> lines; std::vector<float> baselines; bool shrunk = false; };
using MeasureFn = std::function<float (const std::string&, float fontPx)>;
SlideLayout layoutSlide (const Slide&, float width, float height, const ProjectorSettings&, const MeasureFn&);

} // namespace wr
