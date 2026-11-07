#include "ImportPreviewView.h"
#include "Theme.h"
using namespace wr; using namespace wr::theme;

ImportPreviewView::ImportPreviewView() {
    for (auto* t : { &title_, &artist_, &key_, &capo_ }) { addAndMakeVisible (*t); t->setFont (font (15)); t->setColour (juce::TextEditor::backgroundColourId, well); }
    title_.setTextToShowWhenEmpty ("Title", dimmer); artist_.setTextToShowWhenEmpty ("Artist", dimmer); key_.setTextToShowWhenEmpty ("Key", dimmer); capo_.setTextToShowWhenEmpty ("0", dimmer);
    capo_.setInputRestrictions (2, "0123456789");
    addAndMakeVisible (import_); addAndMakeVisible (cancel_);
    import_.setColour (juce::TextButton::buttonColourId, orange); import_.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff111111));
    cancel_.onClick = [this] { setVisible (false); done_ = nullptr; };
    import_.onClick = [this] {
        if (! p_.ok()) return;
        auto c = p_.chart; c.title = title_.getText().trim().toStdString(); c.artist = artist_.getText().trim().toStdString();
        auto k = key_.getText().trim().toStdString(); if (! k.empty()) c.key = k;
        c.capo = juce::jlimit (0, 11, capo_.getText().getIntValue());
        if (c.title.empty()) c.title = "Imported song";
        setVisible (false); if (auto d = std::move (done_)) d (c); };
}

void ImportPreviewView::open (const juce::String& text, const juce::String& fallbackTitle, Done onImport) {
    if (text.startsWith ("[unsupported]")) { p_ = {}; p_.notes.push_back ((text.substring (13) + " isn't a chart format Tabernacle Tools reads (use text, ChordPro or a text PDF; photo/OCR import is on the to-do list).").toStdString()); }
    else p_ = previewImport (text.toStdString(), fallbackTitle.toStdString());
    done_ = std::move (onImport);
    title_.setText (juce::String (p_.chart.title), false); artist_.setText (juce::String (p_.chart.artist), false);
    key_.setText (juce::String (p_.chart.key), false); capo_.setText (juce::String (p_.chart.capo), false);
    import_.setEnabled (p_.ok());
    setVisible (true); toFront (true); resized(); repaint();
}

void ImportPreviewView::resized() {
    box_ = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, 760), juce::jmin (getHeight() - 40, 600));
    auto r = box_.reduced (22).withTrimmedTop (40);
    auto row = r.removeFromTop (36); title_.setBounds (row.removeFromLeft (row.getWidth() / 2 - 6)); row.removeFromLeft (12); artist_.setBounds (row);
    r.removeFromTop (10); row = r.removeFromTop (36); key_.setBounds (row.removeFromLeft (110)); row.removeFromLeft (110); capo_.setBounds (row.removeFromLeft (70));
    auto b = box_.reduced (22).removeFromBottom (44); import_.setBounds (b.removeFromRight (160)); b.removeFromRight (10); cancel_.setBounds (b.removeFromRight (130));
}

void ImportPreviewView::paint (juce::Graphics& g) {
    g.fillAll (juce::Colour (0xcc000000));
    pill (g, box_.toFloat(), bg, juce::Colour (0xff3a4050), 14);
    auto r = box_.reduced (22);
    g.setColour (juce::Colours::white); g.setFont (font (20, true)); g.drawText (p_.urlOnly ? "Only a link was shared" : "Import chart - check before adding", r.removeFromTop (30), juce::Justification::left);
    r.removeFromTop (10 + 36 + 10);
    auto kr = r.removeFromTop (36);
    g.setColour (dim); g.setFont (font (13));
    g.drawText (p_.keyGuessed ? "KEY (guessed)" : "KEY", kr.getX() + 118, kr.getY(), 100, 36, juce::Justification::left);   // between key and capo fields
    g.drawText ("CAPO", kr.getX() + 290, kr.getY(), 60, 36, juce::Justification::left);
    if (p_.keyGuessed) { g.setColour (orange); g.drawText ("from the chords - fix it if wrong", kr.getX() + 360, kr.getY(), 300, 36, juce::Justification::left); }
    r.removeFromTop (14); r.removeFromBottom (56);
    g.setColour (dim); g.setFont (font (12, true)); g.drawText ("SECTIONS (" + juce::String ((int) p_.chart.sections.size()) + ")  -  " + juce::String (p_.chordCount) + " chords, " + juce::String (p_.lyricLines) + " lyric lines", r.removeFromTop (20), juce::Justification::left);
    float x = (float) r.getX(), y = (float) r.getY() + 4;
    for (auto& s : p_.chart.sections) {
        juce::String t = juce::String (s.label.empty() ? s.name : s.label + "  " + s.name); g.setFont (font (14, true));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 22;   // JUCE 8
        if (x + w > (float) r.getRight()) { x = (float) r.getX(); y += 36; }
        if (y + 30 > (float) r.getY() + r.getHeight() * 0.55f) break;
        pill (g, { x, y, w, 28 }, well, juce::Colour (0xff3a4050), 8); g.setColour (juce::Colours::white); g.drawText (t, juce::Rectangle<float> (x, y, w, 28), juce::Justification::centred);
        x += w + 8;
    }
    auto nr = r.withTop ((int) (r.getY() + r.getHeight() * 0.6f));
    g.setFont (font (13)); g.setColour (dim);
    juce::StringArray lines; for (auto& n : p_.notes) lines.add ("- " + juce::String (n)); for (auto& w : p_.chart.warnings) lines.add ("- " + juce::String (w));
    if (! p_.ok() && ! p_.urlOnly) lines.add ("- No chords were recognised. Select the chord chart text (chords above lyrics, or ChordPro) and share or paste again.");
    g.drawFittedText (lines.joinIntoString ("\n"), nr, juce::Justification::topLeft, 8);
}
