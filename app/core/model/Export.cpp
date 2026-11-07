#include "Export.h"
#include "Pdf.h"
#include <sstream>
#include <algorithm>

namespace wr {
PartKey partKeyFor (const Library& lib, const Setlist& set, size_t i, const Player* p) {
    PartKey k; if (i >= set.entries.size()) return k; const auto& e = set.entries[i];
    const Song* s = const_cast<Library&> (lib).song (e.songId); if (! s) return k;
    k.soundingKey = ! e.keyOverride.empty() ? e.keyOverride : s->chart.key;
    k.capo = e.capoOverride >= 0 ? e.capoOverride : s->chart.capo;
    if (p) {
        k.capo = p->capo; auto it = p->songs.find (e.songId);
        if (it != p->songs.end()) { if (! it->second.key.empty()) k.soundingKey = it->second.key; if (it->second.capo >= 0) k.capo = it->second.capo; }
    }
    return k;
}
Chart chartForPlayer (const Library& lib, const Setlist& set, size_t i, const Player* p) {
    if (i >= set.entries.size()) return {}; const Song* s = const_cast<Library&> (lib).song (set.entries[i].songId); if (! s) return {};
    auto k = partKeyFor (lib, set, i, p); Chart c = chartForPart (s->chart, k.soundingKey, k.capo); c.capo = k.capo;
    if (set.entries[i].bpmOverride > 0) c.bpm = set.entries[i].bpmOverride;
    return c;
}
std::string safeFileName (const std::string& s) { std::string o; for (char c : s) o += (std::isalnum ((unsigned char) c) || c == ' ' || c == '-' || c == '_' || c == '.') ? c : '-'; while (! o.empty() && o.back() == ' ') o.pop_back(); return o.empty() ? "export" : o; }

std::string setlistToChordPro (const Library& lib, const Setlist& set, const Player* p) {
    std::string o = "# " + set.name + " " + set.date + (p ? " - " + p->name : std::string()) + "\n";
    bool first = true;
    for (size_t i = 0; i < set.entries.size(); ++i) {
        const Song* s = const_cast<Library&> (lib).song (set.entries[i].songId); if (! s) continue;
        Chart c = chartForPlayer (lib, set, i, p);
        // chartForPlayer gives shapes when capo > 0; the writer wants sounding chords + capo, so give it the sounding chart
        Chart sounding = chartForPart (s->chart, partKeyFor (lib, set, i, p).soundingKey, 0); sounding.capo = c.capo; sounding.bpm = c.bpm;
        if (! first) o += "\n{new_song}\n"; first = false;
        o += chartToChordPro (sounding);
    }
    return o;
}
std::vector<Chart> parseChordProSongs (const std::string& text) {
    std::vector<Chart> v; std::istringstream is (text); std::string ln, cur;
    auto flush = [&] { bool any = cur.find_first_not_of (" \t\r\n") != std::string::npos; if (any) v.push_back (parseChart (cur)); cur.clear(); };
    while (std::getline (is, ln)) { std::string t = ln; t.erase (0, t.find_first_not_of (" \t")); if (t.rfind ("{new_song}", 0) == 0 || t.rfind ("{ns}", 0) == 0) flush(); else if (t.rfind ("#", 0) == 0) continue; else cur += ln + "\n"; }
    flush(); return v;
}

// ------------------------------------------------------------------ PDF
static std::string chordLine (const ChartLine& l, std::string& lyricOut) {
    std::string ch; lyricOut = l.lyric; auto chords = l.chords; std::stable_sort (chords.begin(), chords.end(), [] (const ChordAt& a, const ChordAt& b) { return a.pos < b.pos; });
    for (auto& c : chords) { size_t p = std::max ((size_t) std::max (0, c.pos), ch.empty() ? 0 : ch.size() + 1); if (ch.size() < p) ch += std::string (p - ch.size(), ' '); ch += c.chord; }
    return ch;
}
static void rgbFor (const std::string& c, double& r, double& g, double& b) {
    r = 0.35; g = 0.35; b = 0.35;
    if (c == "orange") { r = 0.85; g = 0.45; b = 0.05; } else if (c == "pink") { r = 0.85; g = 0.2; b = 0.55; } else if (c == "blue") { r = 0.15; g = 0.4; b = 0.8; } else if (c == "purple") { r = 0.5; g = 0.3; b = 0.75; }
    else if (c != "grey") { uint32_t v; if (colorArgb (c, v)) { r = ((v >> 16) & 255) / 255.0 * 0.85; g = ((v >> 8) & 255) / 255.0 * 0.85; b = (v & 255) / 255.0 * 0.85; } } // v0.8 (darkened for white paper)
}
static void songToPdf (PdfWriter& pdf, const Chart& c, const std::string& footer, int songNo) {
    const double M = 48, W = pdf.width() - 2 * M, bottom = pdf.height() - 50; double y = 0;
    // monospace size so the widest chord/lyric line fits
    size_t maxLen = 20; for (auto& s : c.sections) for (auto& it : s.items) { if (it.kind == ChartItem::Line) { std::string ly; maxLen = std::max ({ maxLen, chordLine (it.line, ly).size(), ly.size() }); } else if (it.kind == ChartItem::Tab) for (auto& t : it.tab.strings) maxLen = std::max (maxLen, t.size()); }
    const double fs = std::min (11.5, std::max (6.5, W / (0.6 * (double) maxLen))), lh = fs * 1.25;
    int page = 0;
    auto header = [&] (bool cont) {
        pdf.newPage(); ++page; y = M + 18;
        pdf.text (M, y, PdfWriter::HelveticaBold, cont ? 13 : 18, std::to_string (songNo) + ". " + c.title + (cont ? " (cont.)" : ""));
        y += cont ? 18 : 22;
        if (! cont) {
            std::string meta = capoLabel (c.key, c.capo); if (c.bpm > 0) meta += "   |   " + formatBpm (c.bpm) + " BPM"; if (! c.time.empty()) meta += "   |   " + c.time; if (! c.artist.empty()) meta += "   |   " + c.artist;
            pdf.text (M, y, PdfWriter::Helvetica, 10, meta, 0.3, 0.3, 0.3); y += 20;
        }
        pdf.text (M, pdf.height() - 28, PdfWriter::Helvetica, 8, footer + "   -   page " + std::to_string (page), 0.5, 0.5, 0.5);
    };
    header (false);
    auto need = [&] (double h) { if (y + h > bottom) header (true); };
    for (auto& s : c.sections) {
        need (lh * 3); double r, g, b; rgbFor (s.color, r, g, b);
        pdf.rect (M, y - 10, 3, 13, r, g, b);
        pdf.text (M + 8, y, PdfWriter::HelveticaBold, 11, s.name + (s.repeat > 1 ? "  x" + std::to_string (s.repeat) : std::string()) + (s.bars > 0 ? "   (" + std::to_string (s.bars) + " bars)" : std::string()), r, g, b);
        y += lh + 3;
        for (auto& it : s.items) {
            if (it.kind == ChartItem::Comment) { need (lh); pdf.text (M + 8, y, PdfWriter::Helvetica, fs - 1, it.text, 0.35, 0.35, 0.35); y += lh; }
            else if (it.kind == ChartItem::Tab) {
                need (lh * (double) (it.tab.strings.size() + 2)); double tr, tg, tb; rgbFor (it.tab.color, tr, tg, tb);
                pdf.text (M + 8, y, PdfWriter::HelveticaBold, fs - 1, it.tab.title, tr, tg, tb); y += lh;
                for (auto& st : it.tab.strings) { pdf.text (M + 8, y, PdfWriter::Courier, fs, st, tr * 0.6, tg * 0.6, tb * 0.6); y += lh * 0.95; }
                if (! it.tab.note.empty()) { pdf.text (M + 8, y, PdfWriter::Helvetica, fs - 2, it.tab.note, 0.4, 0.4, 0.4); y += lh; }
                y += 3;
            } else {
                std::string ly, ch = chordLine (it.line, ly); bool hasLy = ly.find_first_not_of (' ') != std::string::npos;
                need (lh * ((ch.empty() ? 0 : 1) + (hasLy ? 1 : 0)));
                double hr = 0, hg = 0, hb = 0; if (! it.line.highlight.empty()) rgbFor (it.line.highlight, hr, hg, hb);
                if (! ch.empty()) { pdf.text (M + 8, y, PdfWriter::CourierBold, fs, ch, 0.1, 0.25, 0.65); y += lh * 0.95; }
                if (hasLy) { pdf.text (M + 8, y, PdfWriter::Courier, fs, ly, hr * 0.8, hg * 0.8, hb * 0.8); y += lh; }
            }
        }
        y += 6;
    }
}
std::string setlistToPdf (const Library& lib, const Setlist& set, const Player* p) {
    PdfWriter pdf; const std::string who = p ? p->name + (p->instrument.empty() ? std::string() : " (" + p->instrument + ")") : std::string ("Band chart");
    const std::string footer = set.name + " " + set.date + " - " + who; int n = 0;
    for (size_t i = 0; i < set.entries.size(); ++i) { if (! const_cast<Library&> (lib).song (set.entries[i].songId)) continue; songToPdf (pdf, chartForPlayer (lib, set, i, p), footer, ++n); }
    if (n == 0) { pdf.newPage(); pdf.text (48, 80, PdfWriter::HelveticaBold, 16, set.name + " - empty setlist"); }
    return pdf.finish (footer);
}
std::vector<ExportFile> exportSetlist (const Library& lib, const Setlist& set, const std::vector<const Player*>& players, ExportFormat f) {
    std::vector<ExportFile> out;
    for (auto* p : players) {
        std::string base = safeFileName (set.date + " " + set.name + " - " + (p ? p->name : std::string ("Band")));
        if (f == ExportFormat::ChordPro) out.push_back ({ base + ".cho", "text/plain", setlistToChordPro (lib, set, p) });
        else out.push_back ({ base + ".pdf", "application/pdf", setlistToPdf (lib, set, p) });
    }
    return out;
}
} // namespace wr
