#include "HymnBrowserView.h"
#include "Theme.h"
using namespace wr; using namespace wr::theme;

HymnBrowserView::HymnBrowserView() {
    addAndMakeVisible (search_); search_.setFont (font (15)); search_.setTextToShowWhenEmpty ("Search title, first line, author, tune or lyrics...", dimmer);
    search_.setColour (juce::TextEditor::backgroundColourId, well); search_.onTextChange = [this] { runSearch(); };
    addAndMakeVisible (list_); list_.setRowHeight (44); list_.setColour (juce::ListBox::backgroundColourId, panel);
    addAndMakeVisible (copyBtn_); addAndMakeVisible (close_);
    copyBtn_.setColour (juce::TextButton::buttonColourId, accentFill); copyBtn_.setColour (juce::TextButton::textColourOffId, onAccent);
    close_.onClick = [this] { setVisible (false); copy_ = nullptr; };
    copyBtn_.onClick = [this] { if (sel_ && copy_) { auto id = sel_->id; setVisible (false); auto c = std::move (copy_); c (id); } };
}
void HymnBrowserView::open (Copy onCopy) {
    copy_ = std::move (onCopy); search_.clear(); runSearch(); setVisible (true); toFront (true); resized(); search_.grabKeyboardFocus();
}
void HymnBrowserView::runSearch() {
    results_ = searchHymns (search_.getText().toStdString()); list_.updateContent();
    if (! results_.empty()) list_.selectRow (0); else { sel_ = nullptr; preview_.clear(); copyBtn_.setEnabled (false); }
    repaint();
}
void HymnBrowserView::selectedRowsChanged (int row) {
    sel_ = row >= 0 && row < (int) results_.size() ? results_[(size_t) row] : nullptr; copyBtn_.setEnabled (sel_ != nullptr); preview_.clear();
    if (sel_) { auto c = hymnChart (*sel_); int lines = 0;
        for (auto& s : c.sections) { if (lines > 22) break; preview_ << juce::String (s.name).toUpperCase() << "\n";
            for (auto& it : s.items) if (it.kind == ChartItem::Line) { preview_ << juce::String (it.line.lyric) << "\n"; ++lines; } preview_ << "\n"; } }
    repaint();
}
void HymnBrowserView::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) {
    if (row < 0 || row >= (int) results_.size()) return; auto* hy = results_[(size_t) row];
    if (selected) { g.setColour (orange.withAlpha (0.18f)); g.fillRect (0, 0, w, h); }
    g.setColour (text); g.setFont (font (15, true)); g.drawText (juce::String (hy->title), 12, 4, w - 24, 20, juce::Justification::left, true);
    g.setColour (dim); g.setFont (font (11)); g.drawText (juce::String (hy->tune) + "  -  " + juce::String (hymnChart (*hy).key) + "  -  " + juce::String (hy->textAuthor), 12, 24, w - 24, 16, juce::Justification::left, true);
}
void HymnBrowserView::resized() {
    box_ = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, 980), juce::jmin (getHeight() - 40, 680));
    auto r = box_.reduced (22).withTrimmedTop (40); search_.setBounds (r.removeFromTop (40)); r.removeFromTop (10);
    auto b = r.removeFromBottom (44); copyBtn_.setBounds (b.removeFromRight (220)); b.removeFromRight (10); close_.setBounds (b.removeFromRight (120)); r.removeFromBottom (10);
    list_.setBounds (r.removeFromLeft (r.getWidth() * 2 / 5)); r.removeFromLeft (16); previewArea_ = r;
}
void HymnBrowserView::paint (juce::Graphics& g) {
    g.fillAll (bg.withAlpha (0.8f)); pill (g, box_.toFloat(), panel, line, 14);
    auto r = box_.reduced (22);
    g.setColour (juce::Colours::white); g.setFont (font (20, true)); g.drawText ("Hymnal - " + juce::String ((int) hymnLibrary().size()) + " public-domain hymns", r.removeFromTop (30), juce::Justification::left);
    label (g, "Read-only. Copy a hymn to your library to edit it, change the key, or arrange it.", juce::Rectangle<int> (r.getX(), r.getY() - 30, r.getWidth(), 30), dim, 12, juce::Justification::centredRight);
    if (! sel_) { label (g, results_.empty() ? "No hymns match." : "", previewArea_); return; }
    auto p = previewArea_; pill (g, p.toFloat(), panel, cardBorder, 10); p = p.reduced (14, 10);
    g.setColour (text); g.setFont (font (17, true)); g.drawText (juce::String (sel_->title), p.removeFromTop (24), juce::Justification::left, true);
    g.setColour (dim); g.setFont (font (12));
    g.drawText ("Words: " + juce::String (sel_->textAuthor) + " (" + juce::String (sel_->textYear) + ")", p.removeFromTop (16), juce::Justification::left, true);
    g.drawText ("Tune: " + juce::String (sel_->tune) + " - " + juce::String (sel_->tuneSource) + " (" + juce::String (sel_->tuneYear) + ")", p.removeFromTop (16), juce::Justification::left, true);
    auto c = hymnChart (*sel_); g.drawText ("Key " + juce::String (c.key) + "  -  " + juce::String (c.bpm, 0) + " BPM  -  " + juce::String (c.time), p.removeFromTop (16), juce::Justification::left);
    g.setColour (green.withAlpha (0.9f)); g.setFont (font (10)); g.drawFittedText (juce::String (sel_->pdBasis), p.removeFromTop (30), juce::Justification::topLeft, 2);
    p.removeFromTop (6); g.setColour (text); g.setFont (font (13)); g.drawFittedText (preview_, p, juce::Justification::topLeft, 40);
}
