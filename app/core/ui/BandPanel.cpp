#include "BandPanel.h"
#include "CoreProcessor.h"
#include "Theme.h"

using namespace wr; using namespace wr::sync; using namespace wr::theme;

juce::Colour BandPanel::statusColour (Member::Status s) { return s == Member::Green ? green : s == Member::Yellow ? juce::Colour (0xffffd23f) : juce::Colour (0xffff4d4d); }

void BandPanel::paint (juce::Graphics& g) {
    g.fillAll (bg.withAlpha (0.55f));
    auto ms = proc.sync().roster().members(); kick_.clear();
    const int rowH = 46, h = 96 + std::max (1, (int) ms.size()) * rowH;
    box_ = getLocalBounds().withSizeKeepingCentre (std::min (760, getWidth() - 40), std::min (h, getHeight() - 40));
    pill (g, box_.toFloat(), bar, cardBorder, 14);
    auto r = box_.reduced (20, 16); auto head = r.removeFromTop (34);
    g.setColour (juce::Colours::white); g.setFont (font (18, true));
    g.drawText ("BAND - " + juce::String (proc.sync().roster().connectedCount()) + " connected", head, juce::Justification::centredLeft);
    close_ = head.removeFromRight (80); pill (g, close_.toFloat().reduced (0, 3), btn, btnBorder, 8); g.setColour (text); g.setFont (font (13, true)); g.drawText ("CLOSE", close_, juce::Justification::centred);
    g.setColour (dim); g.setFont (font (11)); auto cols = r.removeFromTop (20);
    g.drawText ("DEVICE", cols.withX (r.getX() + 26).withWidth (200), juce::Justification::centredLeft);
    g.drawText ("ROLE", cols.withX (r.getX() + 230).withWidth (80), juce::Justification::centredLeft);
    g.drawText ("PING", cols.withX (r.getX() + 310).withWidth (60), juce::Justification::centredLeft);
    g.drawText ("POWER", cols.withX (r.getX() + 370).withWidth (100), juce::Justification::centredLeft);
    g.drawText ("SHOWING", cols.withX (r.getX() + 470).withWidth (200), juce::Justification::centredLeft);
    if (ms.empty()) { g.setColour (dim); g.setFont (font (14)); g.drawText ("No followers yet - on each iPad: AUDIO / MIDI > Band sync > Join", r.removeFromTop (rowH), juce::Justification::centredLeft); return; }
    for (auto& m : ms) {
        auto row = r.removeFromTop (rowH); g.setColour (line); g.drawHorizontalLine (row.getY(), (float) row.getX(), (float) row.getRight());
        g.setColour (statusColour (m.status)); g.fillEllipse ((float) row.getX() + 4, (float) row.getCentreY() - 7, 14, 14);
        g.setColour (m.connected ? juce::Colours::white : dim); g.setFont (font (15, true));
        g.drawText (juce::String (m.name), row.withX (row.getX() + 26).withWidth (200).withTrimmedBottom (m.reason.empty() ? 0 : 16), juce::Justification::centredLeft, true);
        if (! m.reason.empty()) { g.setColour (statusColour (m.status)); g.setFont (font (11)); g.drawText (juce::String (m.reason), row.withX (row.getX() + 26).withWidth (330).withTrimmedTop (24), juce::Justification::centredLeft, true); }
        g.setColour (text); g.setFont (font (13));
        g.drawText (m.role == "projector" ? "projector" : "player", row.withX (row.getX() + 230).withWidth (80).withTrimmedBottom (m.reason.empty() ? 0 : 16), juce::Justification::centredLeft);
        g.drawText (m.rttMs < 0 || ! m.connected ? juce::String ("-") : juce::String ((int) std::lround (m.rttMs)) + " ms", row.withX (row.getX() + 310).withWidth (60).withTrimmedBottom (m.reason.empty() ? 0 : 16), juce::Justification::centredLeft);
        { auto pr = row.withX (row.getX() + 370).withWidth (96); const auto& h = m.health;   // v0.7 battery / thermal
          juce::String t = h.batteryPct >= 0 ? juce::String (h.batteryPct) + "%" + (h.charging ? " +" : "") : juce::String ("-");
          if (h.thermal >= 1) t << (h.thermal >= 3 ? "  CRIT" : h.thermal == 2 ? "  HOT" : "  warm");
          g.setColour (m.healthSev >= 2 ? clip : m.healthSev == 1 ? warn : text); g.drawText (t, pr.withTrimmedBottom (m.warning().empty() ? 0 : 16), juce::Justification::centredLeft);
          if (! m.warning().empty()) { g.setFont (font (10)); g.drawText (juce::String (m.warning()), pr.withTrimmedTop (24).withWidth (200), juce::Justification::centredLeft, true); g.setFont (font (13)); }
          g.setColour (text); }
        g.drawText (m.connected ? juce::String (m.section.empty() ? "-" : m.section) : juce::String (m.reason), row.withX (row.getX() + 470).withWidth (row.getWidth() - 470 - 96).withTrimmedBottom (m.reason.empty() ? 0 : 16), juce::Justification::centredLeft, true);
        if (m.connected) { juce::Rectangle<int> k (row.getRight() - 88, row.getY() + 8, 88, rowH - 16); pill (g, k.toFloat(), btn, juce::Colour (0xffff4d4d), 8);
            g.setColour (juce::Colour (0xffff8080)); g.setFont (font (12, true)); g.drawText ("REMOVE", k, juce::Justification::centred); kick_.push_back ({ k, m.id }); }
    }
}
void BandPanel::mouseUp (const juce::MouseEvent& e) {
    auto p = e.getPosition();
    if (close_.contains (p) || ! box_.contains (p)) { setVisible (false); return; }
    for (auto& k : kick_) if (k.first.contains (p)) {
        const int id = k.second; juce::String name; for (auto& m : proc.sync().roster().members()) if (m.id == id) name = juce::String (m.name);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Remove " + name + "?", "They stop following until they rejoin from their own iPad.", "Remove", "Cancel", this,
            juce::ModalCallbackFunction::create ([this, id] (int r) { if (r == 1) { proc.sync().kick (id, proc.nowSec()); repaint(); } }));
        return;
    }
}
