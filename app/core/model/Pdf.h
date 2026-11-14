// Tiny dependency-free PDF writer (PDF 1.4, standard Type1 fonts, WinAnsi text). Enough for chord charts.
#pragma once
#include <string>
#include <vector>

namespace wr {
class PdfWriter {
public:
    enum Font { Helvetica = 0, HelveticaBold, Courier, CourierBold, kNumFonts };
    explicit PdfWriter (double pageW = 612, double pageH = 792) : w_ (pageW), h_ (pageH) {}  // US Letter points
    void newPage();
    // y measured from the TOP of the page (baseline). rgb 0..1
    void text (double x, double yTop, Font f, double size, const std::string& utf8, double r = 0, double g = 0, double b = 0);
    void rect (double x, double yTop, double w, double h, double r, double g, double b);       // filled
    static double textWidth (Font f, double size, const std::string& utf8);                     // approximate (exact for Courier)
    int pageCount() const { return (int) pages_.size(); }
    double width() const { return w_; } double height() const { return h_; }
    std::string finish (const std::string& title = {}) const;                                    // complete file bytes
    static std::string toWinAnsi (const std::string& utf8);                                      // best effort; unknown -> '?'
private:
    double w_, h_; std::vector<std::string> pages_;
};
} // namespace wr
