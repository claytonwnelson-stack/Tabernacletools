// Renders projector slides to PNG with the same layout the app uses (layoutSlide) - stb_truetype + stb_image_write.
// Usage: wr_projector_png <out.png> [font.ttf]. Also checks the image: white text, centred, inside the safe area, black elsewhere.
#define STB_TRUETYPE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb/stb_truetype.h"
#include "third_party/stb/stb_image_write.h"
#include "model/Projector.h"
#include "model/Chart.h"
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace wr;

int main (int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "projector-sample.png";
    const char* fontPath = argc > 2 ? argv[2] : "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    std::ifstream f (fontPath, std::ios::binary); if (! f) { std::printf ("skip: no font %s\n", fontPath); return 0; }
    std::vector<unsigned char> ttf ((std::istreambuf_iterator<char> (f)), {}); stbtt_fontinfo font; stbtt_InitFont (&font, ttf.data(), 0);
    auto measure = [&] (const std::string& s, float px) { float sc = stbtt_ScaleForPixelHeight (&font, px), w = 0;
        for (size_t i = 0; i < s.size(); ++i) { int adv, lsb; stbtt_GetCodepointHMetrics (&font, (unsigned char) s[i], &adv, &lsb); w += adv * sc;
            if (i + 1 < s.size()) w += sc * stbtt_GetCodepointKernAdvance (&font, (unsigned char) s[i], (unsigned char) s[i + 1]); } return w; };

    // the seed song's chorus, as the projector shows it once the band lands there
    Library lib = seedLibrary(); const Chart& c = lib.songs[0].chart; ProjectorSettings st; int ci = 0;
    for (size_t i = 0; i < c.sections.size(); ++i) if (c.sections[i].kind == "chorus") { ci = (int) i; break; }
    auto slides = sectionSlides (c.sections[(size_t) ci], ci, st.linesPerSlide); if (slides.empty()) { std::printf ("FAIL: no slides\n"); return 1; }
    const Slide& slide = slides[0];
    const int W = 1920, H = 1080; std::vector<unsigned char> img ((size_t) W * H * 3, 0);
    auto lay = layoutSlide (slide, (float) W, (float) H, st, measure);
    int minX = W, maxX = 0, minY = H, maxY = 0;
    for (size_t li = 0; li < lay.lines.size(); ++li) {
        const std::string& line = lay.lines[li]; const float sc = stbtt_ScaleForPixelHeight (&font, lay.fontPx);
        float x = ((float) W - measure (line, lay.fontPx)) * 0.5f; const int base = (int) lay.baselines[li];
        for (size_t i = 0; i < line.size(); ++i) {
            int cp = (unsigned char) line[i], adv, lsb, x0, y0, x1, y1; stbtt_GetCodepointHMetrics (&font, cp, &adv, &lsb);
            stbtt_GetCodepointBitmapBox (&font, cp, sc, sc, &x0, &y0, &x1, &y1); int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) { std::vector<unsigned char> g ((size_t) gw * gh); stbtt_MakeCodepointBitmap (&font, g.data(), gw, gh, gw, sc, sc, cp);
                for (int yy = 0; yy < gh; ++yy) for (int xx = 0; xx < gw; ++xx) { int px = (int) x + x0 + xx, py = base + y0 + yy; unsigned char a = g[(size_t) yy * gw + xx];
                    if (px < 0 || py < 0 || px >= W || py >= H || ! a) continue; for (int k = 0; k < 3; ++k) { auto& d = img[((size_t) py * W + px) * 3 + k]; d = std::max (d, a); }
                    if (a > 128) { minX = std::min (minX, px); maxX = std::max (maxX, px); minY = std::min (minY, py); maxY = std::max (maxY, py); } } }
            x += adv * sc; if (i + 1 < line.size()) x += sc * stbtt_GetCodepointKernAdvance (&font, cp, (unsigned char) line[i + 1]);
        }
    }
    int fails = 0; auto check = [&] (bool ok, const std::string& m) { std::printf ("%s %s\n", ok ? "ok  " : "FAIL", m.c_str()); if (! ok) ++fails; };
    std::string txt; for (auto& l : slide.lines) txt += (txt.empty() ? "" : " / ") + l;
    check (! slide.lines.empty() && txt.find ('[') == std::string::npos, "chorus slide 1: \"" + txt + "\" (no chords)");
    check (lay.fontPx >= 90, "big text: " + std::to_string ((int) lay.fontPx) + " px on 1080p");
    check (minX >= W * 0.05 - 1 && maxX <= W * 0.95 + 1 && minY > 0 && maxY < H, "text inside the 90% safe area");
    check (std::abs ((minX + maxX) * 0.5 - W * 0.5) < W * 0.02 && std::abs ((minY + maxY) * 0.5 - H * 0.5) < H * 0.06, "centred");
    bool gray = true; for (size_t i = 0; i < img.size(); i += 3) if (img[i] != img[i + 1] || img[i] != img[i + 2]) gray = false;
    check (gray && img[0] == 0 && img[(size_t) (W * H - 1) * 3] == 0, "white on black");
    check (stbi_write_png (out, W, H, 3, img.data(), W * 3) != 0, std::string ("wrote ") + out);
    return fails ? 1 : 0;
}
