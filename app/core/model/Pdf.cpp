#include "Pdf.h"
#include <cstdio>
#include <cstdint>
#include <algorithm>

namespace wr {
static std::string num (double v) { char b[32]; std::snprintf (b, sizeof b, "%.2f", v); std::string s = b; while (s.back() == '0') s.pop_back(); if (s.back() == '.') s.pop_back(); return s; }

std::string PdfWriter::toWinAnsi (const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char) s[i]; uint32_t cp; int len = 1;
        if (c < 0x80) cp = c; else if ((c >> 5) == 6 && i + 1 < s.size()) { cp = ((c & 31u) << 6) | (s[i + 1] & 63u); len = 2; }
        else if ((c >> 4) == 14 && i + 2 < s.size()) { cp = ((c & 15u) << 12) | ((s[i + 1] & 63u) << 6) | (s[i + 2] & 63u); len = 3; }
        else if ((c >> 3) == 30 && i + 3 < s.size()) { cp = 0x10000; len = 4; } else { cp = '?'; }
        i += (size_t) len;
        if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) o += (char) cp;
        else switch (cp) { case 0x2018: o += (char) 0x91; break; case 0x2019: o += (char) 0x92; break; case 0x201C: o += (char) 0x93; break; case 0x201D: o += (char) 0x94; break;
                           case 0x2013: o += (char) 0x96; break; case 0x2014: o += (char) 0x97; break; case 0x2026: o += (char) 0x85; break; case 0x266F: o += '#'; break; case 0x266D: o += 'b'; break;
                           case 0x2022: o += (char) 0x95; break; default: o += '?'; }
    }
    return o;
}
static std::string esc (const std::string& s) { std::string o; for (char c : s) { if (c == '(' || c == ')' || c == '\\') o += '\\'; if (c == '\r' || c == '\n') c = ' '; o += c; } return o; }

double PdfWriter::textWidth (Font f, double size, const std::string& s) {
    double n = (double) toWinAnsi (s).size();
    if (f == Courier || f == CourierBold) return n * 0.6 * size;
    return n * (f == HelveticaBold ? 0.58 : 0.53) * size; // average Helvetica advance - good enough for layout
}
void PdfWriter::newPage() { pages_.emplace_back(); }
void PdfWriter::text (double x, double y, Font f, double size, const std::string& s, double r, double g, double b) {
    if (pages_.empty()) newPage();
    pages_.back() += num (r) + " " + num (g) + " " + num (b) + " rg BT /F" + std::to_string ((int) f + 1) + " " + num (size) + " Tf " + num (x) + " " + num (h_ - y) + " Td (" + esc (toWinAnsi (s)) + ") Tj ET\n";
}
void PdfWriter::rect (double x, double y, double w, double hh, double r, double g, double b) {
    if (pages_.empty()) newPage();
    pages_.back() += num (r) + " " + num (g) + " " + num (b) + " rg " + num (x) + " " + num (h_ - y - hh) + " " + num (w) + " " + num (hh) + " re f\n";
}
std::string PdfWriter::finish (const std::string& title) const {
    std::vector<std::string> objs; // 1 catalog, 2 pages, 3..6 fonts, 7 info, then per page: page, content
    const char* fonts[] = { "Helvetica", "Helvetica-Bold", "Courier", "Courier-Bold" };
    const int nPages = std::max (1, (int) pages_.size()), first = 8;
    std::string kids; for (int i = 0; i < nPages; ++i) kids += std::to_string (first + i * 2) + " 0 R ";
    objs.push_back ("<< /Type /Catalog /Pages 2 0 R >>");
    objs.push_back ("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string (nPages) + " >>");
    for (auto* f : fonts) objs.push_back (std::string ("<< /Type /Font /Subtype /Type1 /BaseFont /") + f + " /Encoding /WinAnsiEncoding >>");
    objs.push_back ("<< /Title (" + esc (toWinAnsi (title)) + ") /Producer (Bearing Live) >>");
    for (int i = 0; i < nPages; ++i) {
        const std::string content = i < (int) pages_.size() ? pages_[(size_t) i] : std::string();
        objs.push_back ("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num (w_) + " " + num (h_) + "] /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >> /Contents " + std::to_string (first + i * 2 + 1) + " 0 R >>");
        objs.push_back ("<< /Length " + std::to_string (content.size()) + " >>\nstream\n" + content + "endstream");
    }
    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n"; std::vector<size_t> off;
    for (size_t i = 0; i < objs.size(); ++i) { off.push_back (out.size()); out += std::to_string (i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n"; }
    size_t xref = out.size(); out += "xref\n0 " + std::to_string (objs.size() + 1) + "\n0000000000 65535 f \n";
    for (auto o : off) { char b[24]; std::snprintf (b, sizeof b, "%010zu 00000 n \n", o); out += b; }
    out += "trailer\n<< /Size " + std::to_string (objs.size() + 1) + " /Root 1 0 R /Info 7 0 R >>\nstartxref\n" + std::to_string (xref) + "\n%%EOF\n";
    return out;
}
} // namespace wr
